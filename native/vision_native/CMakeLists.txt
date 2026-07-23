cmake_minimum_required(VERSION 3.26)

project(vision_native LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
    set(CMAKE_CUDA_ARCHITECTURES 89)
endif()

option(NATIVE_ENABLE_VIGEM "Enable ViGEmClient output for cod_native_runtime" OFF)
set(ViGEmClient_DLL "" CACHE FILEPATH "Path to ViGEmClient.dll copied beside cod_native_runtime")
set(SDL2_DLL "" CACHE FILEPATH "Path to SDL2.dll copied beside cod_native_runtime")

set(TensorRT_ROOT "" CACHE PATH "Path to the TensorRT Windows SDK root")

if(NOT TensorRT_ROOT)
    if(DEFINED ENV{TensorRT_ROOT})
        set(TensorRT_ROOT "$ENV{TensorRT_ROOT}")
    elseif(DEFINED ENV{TENSORRT_ROOT})
        set(TensorRT_ROOT "$ENV{TENSORRT_ROOT}")
    else()
        set(TensorRT_ROOT "D:/env/TensorRT-10.15.1.29")
    endif()
endif()

if(NOT ViGEmClient_DLL)
    if(DEFINED ENV{VIGEM_CLIENT_DLL})
        set(ViGEmClient_DLL "$ENV{VIGEM_CLIENT_DLL}" CACHE FILEPATH "Path to ViGEmClient.dll" FORCE)
    else()
        set(_default_vigem_client_dll "D:/env/python/Lib/site-packages/vgamepad/win/vigem/client/x64/ViGEmClient.dll")
        if(EXISTS "${_default_vigem_client_dll}")
            set(ViGEmClient_DLL "${_default_vigem_client_dll}" CACHE FILEPATH "Path to ViGEmClient.dll" FORCE)
        endif()
    endif()
endif()

if(NOT SDL2_DLL)
    if(DEFINED ENV{SDL2_DLL})
        set(SDL2_DLL "$ENV{SDL2_DLL}" CACHE FILEPATH "Path to SDL2.dll" FORCE)
    else()
        set(_default_sdl2_dll "D:/env/python/Lib/site-packages/pygame/SDL2.dll")
        if(EXISTS "${_default_sdl2_dll}")
            set(SDL2_DLL "${_default_sdl2_dll}" CACHE FILEPATH "Path to SDL2.dll" FORCE)
        endif()
    endif()
endif()

file(TO_CMAKE_PATH "${TensorRT_ROOT}" TensorRT_ROOT)
set(TensorRT_INCLUDE_DIR "${TensorRT_ROOT}/include")
set(TensorRT_LIB_DIR "${TensorRT_ROOT}/lib")
set(TensorRT_BIN_DIR "${TensorRT_ROOT}/bin")

if(NOT EXISTS "${TensorRT_INCLUDE_DIR}/NvInfer.h")
    message(FATAL_ERROR "NvInfer.h not found. Pass -DTensorRT_ROOT=<TensorRT SDK root>.")
endif()

find_package(CUDAToolkit REQUIRED)
find_package(Python COMPONENTS Interpreter Development.Module REQUIRED)
find_package(pybind11 CONFIG REQUIRED)

set(TensorRT_LIBS
    "${TensorRT_LIB_DIR}/nvinfer_10.lib"
    "${TensorRT_LIB_DIR}/nvinfer_plugin_10.lib"
)

foreach(_lib IN LISTS TensorRT_LIBS)
    if(NOT EXISTS "${_lib}")
        message(FATAL_ERROR "Required TensorRT library missing: ${_lib}")
    endif()
endforeach()

add_library(vision_native_core STATIC
    src/aim_enhancement.cpp
    src/build_family.cpp
    src/color_readback.cpp
    src/dxgi_capture.cpp
    src/engine_io.cpp
    src/preprocess.cu
    src/resize_contract.cpp
    src/target_selector.cpp
    src/tensorrt_engine.cpp
    src/tensorrt_inspector.cpp
    src/vision_engine.cpp
)

# Fusion shared-memory ABI library
add_library(fusion_shared STATIC
    ../shared_fusion/fusion_channel.cpp
    ../shared_fusion/fusion_channel.h
)

target_include_directories(fusion_shared
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_options(fusion_shared PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

target_include_directories(vision_native_core
    PUBLIC
        include
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    PRIVATE
        "${TensorRT_INCLUDE_DIR}"
)

target_compile_definitions(vision_native_core
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

set_target_properties(vision_native_core PROPERTIES
    CUDA_SEPARABLE_COMPILATION OFF
    CUDA_ARCHITECTURES "${CMAKE_CUDA_ARCHITECTURES}"
)
target_compile_definitions(vision_native_core PUBLIC VISION_BUILD_FAMILY_MODERN=1)

add_executable(vision_native_build_family_tests
    src/build_family_tests.cpp
    src/build_family.cpp
)
target_include_directories(vision_native_build_family_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(vision_native_resize_contract_tests
    src/resize_contract_tests.cpp
    src/resize_contract.cpp
)
target_include_directories(vision_native_resize_contract_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(vision_native_core
    PUBLIC
        CUDA::cudart
        ${TensorRT_LIBS}
        d3d11
        dxgi
)

add_executable(vision_native_smoke
    src/trt_smoke_main.cpp
)

target_link_libraries(vision_native_smoke PRIVATE vision_native_core)

add_executable(vision_native_debug
    src/vision_debug_main.cpp
)

target_link_libraries(vision_native_debug PRIVATE vision_native_core)

add_executable(vision_native_bgra_benchmark
    src/bgra_benchmark.cpp
)

target_link_libraries(vision_native_bgra_benchmark PRIVATE vision_native_core)

add_executable(cod_native_target_selector_tests
    src/target_selector_tests.cpp
)

target_include_directories(cod_native_target_selector_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_definitions(cod_native_target_selector_tests
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

target_link_libraries(cod_native_target_selector_tests PRIVATE vision_native_core)

# Native runtime source layout: native/runtime_app and native/controller_native.
set(FPS_TRACKER_PACKAGE_SOURCES
    ../fps_2d_tracker_package/src/association.cpp
    ../fps_2d_tracker_package/src/ego_motion_buffer.cpp
    ../fps_2d_tracker_package/src/kalman_cv2d.cpp
    ../fps_2d_tracker_package/src/projection_model.cpp
    ../fps_2d_tracker_package/src/recoil_visual_model.cpp
    ../fps_2d_tracker_package/src/stick_projector.cpp
    ../fps_2d_tracker_package/src/target_tracker.cpp
)

execute_process(
    COMMAND git rev-parse HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../..
    OUTPUT_VARIABLE COD_BUILD_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(NOT COD_BUILD_COMMIT)
    set(COD_BUILD_COMMIT "unknown")
endif()

add_executable(cod_native_runtime
    ../runtime_app/main.cpp
    ../runtime_app/runtime_provenance.cpp
    ../runtime_app/runtime_loop.cpp
    ../runtime_app/runtime_timing.cpp
    ../runtime_app/vision_service.cpp
    ../runtime_app/viewport_controller.cpp
    ../runtime_app/fusion_channel_publisher.cpp
    ../runtime_app/perf_logger.cpp
    ../runtime_app/aim_perf_file_logger.cpp
    ../runtime_app/log_session_manager.cpp
    ../runtime_app/runtime_telemetry.cpp
    ../runtime_app/telemetry_collectors.cpp
    ../runtime_app/telemetry_target_identity.cpp
    ../runtime_app/telemetry_event_sampler.cpp
    ../runtime_app/control_response_window.cpp
    ../control_learning/causal_online_response_learner.cpp
    ../control_learning/pending_motion_model.cpp
    ../control_learning/short_horizon_rollout.cpp
    ../runtime_app/ads_visual_transition.cpp
    ../runtime_app/ads_transition_collector.cpp
    ../runtime_app/downward_diagnostics.cpp
    ../runtime_app/native_replay_writer.cpp
    ../runtime_app/native_replay_runner.cpp
    ../replay_native/replay_reader.cpp
    ../replay_native/replay_metrics.cpp
    ../tracking_native/association.cpp
    ../tracking_native/ego_motion_buffer.cpp
    ../tracking_native/projection_model.cpp
    ../tracking_native/tracker_authority.cpp
    ../tracking_native/kalman_tracker.cpp
    ../tracking_native/fps_reference_tracker.cpp
    ../tracking_native/tracker_backend.cpp
    ../tracking_native/legacy_projection_tracker.cpp
    ${FPS_TRACKER_PACKAGE_SOURCES}
    ../controller_native/runtime_config.cpp
    ../controller_native/auto_fire_gate.cpp
    ../controller_native/recoil_profile.cpp
    ../controller_native/recoil_calibration.cpp
    ../controller_native/recoil_compensation.cpp
    ../recoil_native/recoil_compensation.cpp
    ../recoil_native/recoil_visual_model.cpp
    ../controller_native/weapon_recognizer.cpp
    ../controller_native/output_mixer.cpp
    ../controller_native/controller_pipeline.cpp
    ../runtime_app/vision_controller_adapter.cpp
    ../controller_native/native_gamepad_controller.cpp
    ../controller_native/virtual_gamepad.cpp
    ../controller_native/io_recovery_policy.cpp
    ../controller_native/xinput_reader.cpp
    ../controller_native/sdl_gamepad_reader.cpp
)

target_include_directories(cod_native_runtime PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../fps_2d_tracker_package/include
)

target_compile_definitions(cod_native_runtime
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        COD_BUILD_COMMIT="${COD_BUILD_COMMIT}"
        $<$<BOOL:${NATIVE_ENABLE_VIGEM}>:NATIVE_ENABLE_VIGEM>
)

target_compile_options(cod_native_runtime PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

target_link_libraries(cod_native_runtime PRIVATE
    vision_native_core
    fusion_shared
    d3d11
    dxgi
    xinput
    gdi32
    winmm
    windowsapp
    advapi32
)

if(ViGEmClient_DLL AND EXISTS "${ViGEmClient_DLL}")
    add_custom_command(TARGET cod_native_runtime POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${ViGEmClient_DLL}"
            "$<TARGET_FILE_DIR:cod_native_runtime>/ViGEmClient.dll"
        VERBATIM
    )
else()
    message(WARNING "ViGEmClient.dll not found; cod_native_runtime will fall back to logging output unless VIGEM_CLIENT_DLL is set at runtime.")
endif()

if(SDL2_DLL AND EXISTS "${SDL2_DLL}")
    add_custom_command(TARGET cod_native_runtime POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${SDL2_DLL}"
            "$<TARGET_FILE_DIR:cod_native_runtime>/SDL2.dll"
        VERBATIM
    )
else()
    message(WARNING "SDL2.dll not found; cod_native_runtime will fall back to XInput unless SDL2_DLL is set at runtime.")
endif()

add_executable(cod_native_controller_tests
    ../controller_native/target_pipeline_integration_tests.cpp
    ../tracking_native/tracker_authority.cpp
    ../controller_native/auto_fire_gate.cpp
    ../controller_native/recoil_profile.cpp
    ../controller_native/recoil_calibration.cpp
    ../controller_native/recoil_compensation.cpp
    ../recoil_native/recoil_compensation.cpp
    ../recoil_native/recoil_visual_model.cpp
    ../controller_native/weapon_recognizer.cpp
    ../controller_native/output_mixer.cpp
    ../controller_native/controller_pipeline.cpp
    ../controller_native/native_gamepad_controller.cpp
)

add_executable(cod_native_io_recovery_policy_tests
    ../controller_native/io_recovery_policy_tests.cpp
    ../controller_native/io_recovery_policy.cpp
)

add_executable(cod_native_live_failure_benchmark
    ../controller_native/native_live_failure_benchmark.cpp
    ../tracking_native/association.cpp
    ../tracking_native/ego_motion_buffer.cpp
    ../tracking_native/projection_model.cpp
    ../tracking_native/tracker_authority.cpp
    ../tracking_native/kalman_tracker.cpp
    ../tracking_native/fps_reference_tracker.cpp
    ../tracking_native/tracker_backend.cpp
    ../tracking_native/legacy_projection_tracker.cpp
    ${FPS_TRACKER_PACKAGE_SOURCES}
    ../controller_native/runtime_config.cpp
    ../controller_native/ads_state_tracker.cpp
    ../controller_native/ads_completion_gate.cpp
    ../controller_native/auto_fire_gate.cpp
    ../controller_native/target_tracker.cpp
    ../controller_native/ads_carry_brake_policy.cpp
    ../controller_native/ai_aim.cpp
    ../controller_native/body_lock_short_plan_policy.cpp
    ../controller_native/bodylock_policy.cpp
    ../controller_native/aim_assist_dynamics.cpp
    ../controller_native/recoil_profile.cpp
    ../controller_native/recoil_calibration.cpp
    ../controller_native/recoil_compensation.cpp
    ../recoil_native/recoil_compensation.cpp
    ../recoil_native/recoil_visual_model.cpp
    ../controller_native/weapon_recognizer.cpp
    ../controller_native/output_mixer.cpp
    ../controller_native/output_validation_policy.cpp
    ../controller_native/controller_pipeline.cpp
    ../runtime_app/vision_controller_adapter.cpp
    ../controller_native/target_snapshot_provider.cpp
    ../controller_native/assist_authority_policy.cpp
    ../controller_native/bodylock_lifecycle.cpp
    ../controller_native/native_gamepad_controller.cpp
    ../controller_native/io_recovery_policy.cpp
)

target_include_directories(cod_native_live_failure_benchmark PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../fps_2d_tracker_package/include
)

target_compile_definitions(cod_native_live_failure_benchmark
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

target_link_libraries(cod_native_live_failure_benchmark PRIVATE
    gdi32
    windowsapp
)

target_include_directories(cod_native_io_recovery_policy_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_definitions(cod_native_io_recovery_policy_tests
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

target_include_directories(cod_native_controller_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../fps_2d_tracker_package/include
)

target_compile_definitions(cod_native_controller_tests
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        COD_BENCHMARK_MIX_OVERRIDE
)

target_link_libraries(cod_native_controller_tests PRIVATE
    gdi32
    windowsapp
)

add_executable(cod_native_controller_protocol_tests
    ../controller_native/controller_protocol_tests.cpp
    ../runtime_app/vision_controller_adapter.cpp
    ../controller_native/target_geometry.cpp
)

target_include_directories(cod_native_controller_protocol_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_definitions(cod_native_controller_protocol_tests
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

add_executable(cod_native_vision_service_tests
    ../runtime_app/vision_service_tests.cpp
    ../runtime_app/vision_service.cpp
    ../runtime_app/runtime_timing.cpp
)

target_include_directories(cod_native_vision_service_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_definitions(cod_native_vision_service_tests
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

target_link_libraries(cod_native_vision_service_tests PRIVATE
    winmm
)

add_executable(cod_native_viewport_controller_tests
    ../runtime_app/viewport_controller_tests.cpp
    ../runtime_app/viewport_controller.cpp
)

target_include_directories(cod_native_viewport_controller_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_definitions(cod_native_viewport_controller_tests
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

add_executable(cod_native_runtime_timing_tests
    ../runtime_app/runtime_timing_tests.cpp
    ../runtime_app/runtime_timing.cpp
)

add_executable(cod_native_scheduler_benchmark
    ../runtime_app/scheduler_benchmark.cpp
    ../runtime_app/runtime_timing.cpp
)
target_include_directories(cod_native_scheduler_benchmark PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_compile_definitions(cod_native_scheduler_benchmark PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(cod_native_scheduler_benchmark PRIVATE winmm)

add_executable(vision_native_color_readback_tests
    src/color_readback_tests.cpp
    src/color_readback.cpp
)
target_include_directories(vision_native_color_readback_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(vision_native_color_readback_tests PRIVATE CUDA::cudart)

add_executable(vision_native_color_readback_benchmark
    src/color_readback_benchmark.cpp
    src/color_readback.cpp
)
target_include_directories(vision_native_color_readback_benchmark PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(vision_native_color_readback_benchmark PRIVATE CUDA::cudart)

add_executable(cod_native_runtime_telemetry_tests
    ../runtime_app/runtime_telemetry_tests.cpp
    ../runtime_app/runtime_telemetry.cpp
)

add_executable(cod_native_telemetry_target_identity_tests
    ../runtime_app/telemetry_target_identity_tests.cpp
    ../runtime_app/telemetry_target_identity.cpp
)
target_include_directories(cod_native_telemetry_target_identity_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

add_executable(cod_native_telemetry_event_sampler_tests
    ../runtime_app/telemetry_event_sampler_tests.cpp
    ../runtime_app/telemetry_event_sampler.cpp
)
target_include_directories(cod_native_telemetry_event_sampler_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

add_executable(cod_native_control_response_window_tests
    ../runtime_app/control_response_window_tests.cpp
    ../runtime_app/control_response_window.cpp
)

add_executable(cod_native_runtime_provenance_tests
    ../runtime_app/runtime_provenance_tests.cpp
    ../runtime_app/runtime_provenance.cpp
)
target_include_directories(cod_native_runtime_provenance_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_compile_definitions(cod_native_runtime_provenance_tests PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)
target_link_libraries(cod_native_runtime_provenance_tests PRIVATE advapi32)
target_include_directories(cod_native_control_response_window_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

add_executable(cod_native_ads_visual_transition_tests
    ../runtime_app/ads_visual_transition_tests.cpp
    ../runtime_app/ads_visual_transition.cpp
)
target_include_directories(cod_native_ads_visual_transition_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

add_executable(cod_native_ads_transition_collector_tests
    ../runtime_app/ads_transition_collector_tests.cpp
    ../runtime_app/ads_transition_collector.cpp
    ../runtime_app/ads_visual_transition.cpp
)
target_include_directories(cod_native_ads_transition_collector_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

add_executable(cod_native_telemetry_collectors_tests
    ../runtime_app/telemetry_collectors_tests.cpp
    ../runtime_app/telemetry_collectors.cpp
    ../runtime_app/runtime_telemetry.cpp
    ../runtime_app/telemetry_target_identity.cpp
    ../runtime_app/telemetry_event_sampler.cpp
    ../runtime_app/control_response_window.cpp
    ../runtime_app/ads_visual_transition.cpp
    ../runtime_app/ads_transition_collector.cpp
)
target_include_directories(cod_native_telemetry_collectors_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

add_executable(cod_native_telemetry_benchmark
    ../runtime_app/telemetry_benchmark.cpp
    ../runtime_app/runtime_telemetry.cpp
)
target_include_directories(cod_native_telemetry_benchmark PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_compile_definitions(cod_native_telemetry_benchmark PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

target_include_directories(cod_native_runtime_telemetry_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_definitions(cod_native_runtime_telemetry_tests PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_include_directories(cod_native_runtime_timing_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_definitions(cod_native_runtime_timing_tests
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

target_link_libraries(cod_native_runtime_timing_tests PRIVATE
    winmm
)

add_executable(cod_native_runtime_config_tests
    ../controller_native/runtime_config_tests.cpp
    ../controller_native/runtime_config.cpp
    ../tracking_native/association.cpp
    ../tracking_native/ego_motion_buffer.cpp
    ../tracking_native/projection_model.cpp
    ../tracking_native/tracker_authority.cpp
    ../tracking_native/kalman_tracker.cpp
    ../tracking_native/fps_reference_tracker.cpp
    ../tracking_native/tracker_backend.cpp
    ../tracking_native/legacy_projection_tracker.cpp
    ${FPS_TRACKER_PACKAGE_SOURCES}
)

add_executable(cod_native_ads_completion_gate_tests
    ../controller_native/ads_completion_gate_tests.cpp
    ../controller_native/ads_completion_gate.cpp
)

target_include_directories(cod_native_ads_completion_gate_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_include_directories(cod_native_runtime_config_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../fps_2d_tracker_package/include
)

target_compile_definitions(cod_native_runtime_config_tests
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

add_executable(cod_native_target_snapshot_tests
    ../controller_native/target_snapshot_provider_tests.cpp
    ../controller_native/target_snapshot_provider.cpp
    ../controller_native/target_geometry.cpp
    ../controller_native/assist_authority_policy.cpp
    ../runtime_app/vision_controller_adapter.cpp
    ../tracking_native/association.cpp
    ../tracking_native/ego_motion_buffer.cpp
    ../tracking_native/projection_model.cpp
    ../tracking_native/tracker_authority.cpp
    ../tracking_native/kalman_tracker.cpp
    ../tracking_native/fps_reference_tracker.cpp
    ../tracking_native/tracker_backend.cpp
    ../tracking_native/legacy_projection_tracker.cpp
    ${FPS_TRACKER_PACKAGE_SOURCES}
)

target_include_directories(cod_native_target_snapshot_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../fps_2d_tracker_package/include
)

target_compile_definitions(cod_native_target_snapshot_tests
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

add_executable(cod_native_track_memory_tests
    ../tracking_native/track_memory_service_tests.cpp
    ../tracking_native/track_memory_service.cpp
    ../tracking_native/association.cpp
    ../tracking_native/ego_motion_buffer.cpp
    ../tracking_native/projection_model.cpp
    ../tracking_native/tracker_authority.cpp
    ../tracking_native/kalman_tracker.cpp
    ../tracking_native/fps_reference_tracker.cpp
    ../tracking_native/tracker_backend.cpp
    ../tracking_native/legacy_projection_tracker.cpp
    ${FPS_TRACKER_PACKAGE_SOURCES}
)

add_executable(cod_native_assist_authority_policy_tests
    ../controller_native/assist_authority_policy_tests.cpp
    ../controller_native/assist_authority_policy.cpp
)

add_executable(cod_native_bodylock_lifecycle_tests
    ../controller_native/bodylock_lifecycle_tests.cpp
    ../controller_native/bodylock_lifecycle.cpp
)

target_include_directories(cod_native_bodylock_lifecycle_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_definitions(cod_native_bodylock_lifecycle_tests
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

target_include_directories(cod_native_assist_authority_policy_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_definitions(cod_native_assist_authority_policy_tests
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

target_include_directories(cod_native_track_memory_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../fps_2d_tracker_package/include
)

target_compile_definitions(cod_native_track_memory_tests
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

add_executable(cod_native_ads_state_tests
    ../controller_native/ads_state_tracker_tests.cpp
    ../controller_native/ads_state_tracker.cpp
)

add_executable(cod_native_target_plan_contract_tests
    ../pipeline_contract/target_plan_contract_tests.cpp
)

add_executable(cod_native_intent_filter_tests
    ../controller_native/intent_filter_tests.cpp
    ../controller_native/intent_filter.cpp
)

add_executable(cod_native_control_response_estimator_tests
    ../controller_native/control_response_estimator_tests.cpp
    ../controller_native/control_response_estimator.cpp
)

add_executable(cod_native_target_coordinator_tests
    ../controller_native/target_coordinator_tests.cpp
    ../controller_native/target_coordinator.cpp
    ../controller_native/control_response_estimator.cpp
)

add_executable(cod_native_target_plan_publisher_tests
    ../controller_native/target_plan_publisher_tests.cpp
)

add_executable(cod_native_ads_acquisition_controller_tests
    ../controller_native/ads_acquisition_controller_tests.cpp
    ../controller_native/ads_acquisition_controller.cpp
    ../controller_native/response_model_aim_solver.cpp
)

add_executable(cod_native_bodylock_follow_controller_tests
    ../controller_native/bodylock_follow_controller_tests.cpp
    ../controller_native/bodylock_follow_controller.cpp
    ../controller_native/response_model_aim_solver.cpp
)

add_executable(cod_native_aim_dynamics_shaper_tests
    ../controller_native/aim_dynamics_shaper_tests.cpp
    ../controller_native/aim_dynamics_shaper.cpp
)

add_executable(cod_native_axis_intent_arbiter_tests
    ../controller_native/axis_intent_arbiter_tests.cpp
    ../controller_native/axis_intent_arbiter.cpp
)

add_executable(cod_native_vector_intent_fuser_tests
    ../controller_native/vector_intent_fuser_tests.cpp
    ../controller_native/vector_intent_fuser.cpp
)

add_executable(cod_native_log_session_manager_tests
    ../runtime_app/log_session_manager_tests.cpp
    ../runtime_app/log_session_manager.cpp
)

target_include_directories(cod_native_log_session_manager_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../runtime_app
)

target_compile_definitions(cod_native_log_session_manager_tests
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

target_include_directories(cod_native_aim_dynamics_shaper_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../controller_native
)

target_compile_definitions(cod_native_aim_dynamics_shaper_tests
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

target_include_directories(cod_native_bodylock_follow_controller_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../controller_native
)

target_compile_definitions(cod_native_bodylock_follow_controller_tests
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

target_include_directories(cod_native_ads_acquisition_controller_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../controller_native
)

target_compile_definitions(cod_native_ads_acquisition_controller_tests
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

target_include_directories(cod_native_target_plan_publisher_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../controller_native
)

target_compile_definitions(cod_native_target_plan_publisher_tests
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

target_include_directories(cod_native_target_coordinator_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../controller_native
)

target_compile_definitions(cod_native_target_coordinator_tests
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

target_include_directories(cod_native_control_response_estimator_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../controller_native
)

target_compile_definitions(cod_native_control_response_estimator_tests
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

target_include_directories(cod_native_intent_filter_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../controller_native
)

target_compile_definitions(cod_native_intent_filter_tests
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

target_include_directories(cod_native_target_plan_contract_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_definitions(cod_native_target_plan_contract_tests
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

target_include_directories(cod_native_ads_state_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_definitions(cod_native_ads_state_tests
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

add_executable(cod_native_auto_fire_tests
    ../controller_native/auto_fire_gate_tests.cpp
    ../controller_native/auto_fire_gate.cpp
    ../tracking_native/tracker_authority.cpp
)

target_include_directories(cod_native_auto_fire_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_definitions(cod_native_auto_fire_tests
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

add_executable(cod_native_output_validation_tests
    ../controller_native/output_validation_policy_tests.cpp
    ../controller_native/output_validation_policy.cpp
    ../tracking_native/tracker_authority.cpp
)

target_include_directories(cod_native_output_validation_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_definitions(cod_native_output_validation_tests
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

add_executable(cod_native_body_lock_plan_tests
    ../controller_native/body_lock_short_plan_policy_tests.cpp
    ../controller_native/body_lock_short_plan_policy.cpp
)

target_include_directories(cod_native_body_lock_plan_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_definitions(cod_native_body_lock_plan_tests
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

add_executable(cod_native_recoil_contract_tests
    ../controller_native/recoil_contract_tests.cpp
    ../controller_native/recoil_profile.cpp
    ../controller_native/recoil_calibration.cpp
    ../controller_native/recoil_compensation.cpp
    ../recoil_native/recoil_visual_model.cpp
)

target_include_directories(cod_native_recoil_contract_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_definitions(cod_native_recoil_contract_tests
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

target_link_libraries(cod_native_recoil_contract_tests PRIVATE
    windowsapp
)

add_executable(cod_native_weapon_recognizer_tests
    ../controller_native/weapon_recognizer_tests.cpp
    ../controller_native/weapon_recognizer.cpp
    ../controller_native/recoil_profile.cpp
)

target_include_directories(cod_native_weapon_recognizer_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_definitions(cod_native_weapon_recognizer_tests
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

target_link_libraries(cod_native_weapon_recognizer_tests PRIVATE
    windowsapp
)

add_executable(cod_native_benchmark_metrics_tests
    ../controller_native/benchmark_metrics_tests.cpp
    ../tracking_native/tracker_authority.cpp
    ../runtime_app/aim_perf_file_logger.cpp
    ../runtime_app/perf_logger.cpp
    ../replay_native/replay_metrics.cpp
)

target_include_directories(cod_native_axis_intent_arbiter_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../controller_native
)

target_compile_definitions(cod_native_axis_intent_arbiter_tests
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

add_executable(cod_native_partial_occlusion_benchmark_tests
    ../controller_native/partial_occlusion_benchmark_tests.cpp
    ../controller_native/partial_occlusion_benchmark.cpp
)

add_executable(cod_native_partial_occlusion_benchmark
    ../controller_native/cod_native_partial_occlusion_benchmark.cpp
    ../controller_native/partial_occlusion_benchmark.cpp
    ../tracking_native/association.cpp
    ../tracking_native/ego_motion_buffer.cpp
    ../tracking_native/projection_model.cpp
    ../tracking_native/tracker_authority.cpp
    ../tracking_native/kalman_tracker.cpp
    ../tracking_native/fps_reference_tracker.cpp
    ../tracking_native/tracker_backend.cpp
    ../tracking_native/legacy_projection_tracker.cpp
    ${FPS_TRACKER_PACKAGE_SOURCES}
    ../controller_native/runtime_config.cpp
    ../controller_native/ads_state_tracker.cpp
    ../controller_native/ads_completion_gate.cpp
    ../controller_native/auto_fire_gate.cpp
    ../controller_native/target_tracker.cpp
    ../controller_native/ads_carry_brake_policy.cpp
    ../controller_native/ai_aim.cpp
    ../controller_native/body_lock_short_plan_policy.cpp
    ../controller_native/bodylock_policy.cpp
    ../controller_native/aim_assist_dynamics.cpp
    ../controller_native/recoil_profile.cpp
    ../controller_native/recoil_calibration.cpp
    ../controller_native/recoil_compensation.cpp
    ../recoil_native/recoil_compensation.cpp
    ../recoil_native/recoil_visual_model.cpp
    ../controller_native/output_mixer.cpp
    ../controller_native/output_validation_policy.cpp
    ../controller_native/controller_pipeline.cpp
    ../runtime_app/vision_controller_adapter.cpp
    ../controller_native/target_snapshot_provider.cpp
    ../controller_native/assist_authority_policy.cpp
    ../controller_native/bodylock_lifecycle.cpp
    ../controller_native/native_gamepad_controller.cpp
)

target_include_directories(cod_native_partial_occlusion_benchmark PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../fps_2d_tracker_package/include
)

target_compile_definitions(cod_native_partial_occlusion_benchmark PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_link_libraries(cod_native_partial_occlusion_benchmark PRIVATE
    vision_native_core
    gdi32
    windowsapp
)

target_include_directories(cod_native_partial_occlusion_benchmark_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_definitions(cod_native_partial_occlusion_benchmark_tests PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

add_test(
    NAME NativePartialOcclusionBenchmarkTests
    COMMAND cod_native_partial_occlusion_benchmark_tests)

target_include_directories(cod_native_benchmark_metrics_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_definitions(cod_native_benchmark_metrics_tests
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

add_executable(cod_native_gamepad_benchmark
    ../controller_native/cod_native_gamepad_benchmark.cpp
    ../replay_native/replay_metrics.cpp
    ../tracking_native/association.cpp
    ../tracking_native/ego_motion_buffer.cpp
    ../tracking_native/projection_model.cpp
    ../tracking_native/tracker_authority.cpp
    ../tracking_native/kalman_tracker.cpp
    ../tracking_native/fps_reference_tracker.cpp
    ../tracking_native/tracker_backend.cpp
    ../tracking_native/legacy_projection_tracker.cpp
    ${FPS_TRACKER_PACKAGE_SOURCES}
    ../controller_native/runtime_config.cpp
    ../controller_native/ads_state_tracker.cpp
    ../controller_native/ads_completion_gate.cpp
    ../controller_native/auto_fire_gate.cpp
    ../controller_native/target_tracker.cpp
    ../controller_native/ads_carry_brake_policy.cpp
    ../controller_native/ai_aim.cpp
    ../controller_native/body_lock_short_plan_policy.cpp
    ../controller_native/bodylock_policy.cpp
    ../controller_native/aim_assist_dynamics.cpp
    ../controller_native/recoil_profile.cpp
    ../controller_native/recoil_calibration.cpp
    ../controller_native/recoil_compensation.cpp
    ../recoil_native/recoil_compensation.cpp
    ../recoil_native/recoil_visual_model.cpp
    ../controller_native/output_mixer.cpp
    ../controller_native/output_validation_policy.cpp
    ../controller_native/controller_pipeline.cpp
    ../runtime_app/vision_controller_adapter.cpp
    ../controller_native/target_snapshot_provider.cpp
    ../controller_native/assist_authority_policy.cpp
    ../controller_native/bodylock_lifecycle.cpp
    ../controller_native/native_gamepad_controller.cpp
)

add_executable(cod_native_target_geometry_tests
    ../controller_native/target_geometry_tests.cpp
    ../controller_native/target_geometry.cpp
)
target_include_directories(cod_native_target_geometry_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_include_directories(cod_native_gamepad_benchmark PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../fps_2d_tracker_package/include
)

target_compile_definitions(cod_native_gamepad_benchmark
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

target_link_libraries(cod_native_gamepad_benchmark PRIVATE
    vision_native_core
    gdi32
    windowsapp
)

add_executable(cod_native_vlock_defect_tests
    ../controller_native/vertical_bodylock_defect_benchmark_tests.cpp
    ../controller_native/vertical_bodylock_defect_benchmark.cpp
    ../tracking_native/association.cpp
    ../tracking_native/ego_motion_buffer.cpp
    ../tracking_native/projection_model.cpp
    ../tracking_native/tracker_authority.cpp
    ../tracking_native/kalman_tracker.cpp
    ../tracking_native/fps_reference_tracker.cpp
    ../tracking_native/tracker_backend.cpp
    ../tracking_native/legacy_projection_tracker.cpp
    ${FPS_TRACKER_PACKAGE_SOURCES}
    ../controller_native/runtime_config.cpp
    ../controller_native/ads_state_tracker.cpp
    ../controller_native/ads_completion_gate.cpp
    ../controller_native/auto_fire_gate.cpp
    ../controller_native/target_tracker.cpp
    ../controller_native/ads_carry_brake_policy.cpp
    ../controller_native/ai_aim.cpp
    ../controller_native/body_lock_short_plan_policy.cpp
    ../controller_native/bodylock_policy.cpp
    ../controller_native/aim_assist_dynamics.cpp
    ../controller_native/recoil_profile.cpp
    ../controller_native/recoil_calibration.cpp
    ../controller_native/recoil_compensation.cpp
    ../recoil_native/recoil_compensation.cpp
    ../recoil_native/recoil_visual_model.cpp
    ../controller_native/output_mixer.cpp
    ../controller_native/output_validation_policy.cpp
    ../controller_native/controller_pipeline.cpp
    ../controller_native/target_snapshot_provider.cpp
    ../controller_native/assist_authority_policy.cpp
    ../controller_native/bodylock_lifecycle.cpp
    ../controller_native/native_gamepad_controller.cpp
    ../controller_native/intent_filter.cpp
    ../controller_native/control_response_estimator.cpp
    ../controller_native/target_coordinator.cpp
    ../controller_native/ads_acquisition_controller.cpp
    ../controller_native/bodylock_follow_controller.cpp
    ../controller_native/response_model_aim_solver.cpp
    ../controller_native/aim_response_estimator.cpp
    ../controller_native/aim_dynamics_shaper.cpp
)
target_include_directories(cod_native_vlock_defect_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../fps_2d_tracker_package/include
)
target_compile_definitions(cod_native_vlock_defect_tests PRIVATE
    NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(cod_native_vlock_defect_tests PRIVATE
    vision_native_core gdi32 windowsapp)

add_executable(cod_native_lstick_benchmark
    ../controller_native/cod_native_left_stick_motion_benchmark.cpp
    ../controller_native/left_stick_motion_defect_benchmark.cpp
    ../tracking_native/association.cpp
    ../tracking_native/ego_motion_buffer.cpp
    ../tracking_native/projection_model.cpp
    ../tracking_native/tracker_authority.cpp
    ../tracking_native/kalman_tracker.cpp
    ../tracking_native/fps_reference_tracker.cpp
    ../tracking_native/tracker_backend.cpp
    ../tracking_native/legacy_projection_tracker.cpp
    ${FPS_TRACKER_PACKAGE_SOURCES}
    ../controller_native/runtime_config.cpp
    ../controller_native/ads_state_tracker.cpp
    ../controller_native/ads_completion_gate.cpp
    ../controller_native/auto_fire_gate.cpp
    ../controller_native/target_tracker.cpp
    ../controller_native/ads_carry_brake_policy.cpp
    ../controller_native/ai_aim.cpp
    ../controller_native/body_lock_short_plan_policy.cpp
    ../controller_native/bodylock_policy.cpp
    ../controller_native/aim_assist_dynamics.cpp
    ../controller_native/recoil_profile.cpp
    ../controller_native/recoil_calibration.cpp
    ../controller_native/recoil_compensation.cpp
    ../recoil_native/recoil_compensation.cpp
    ../recoil_native/recoil_visual_model.cpp
    ../controller_native/output_mixer.cpp
    ../controller_native/output_validation_policy.cpp
    ../controller_native/controller_pipeline.cpp
    ../controller_native/target_snapshot_provider.cpp
    ../controller_native/assist_authority_policy.cpp
    ../controller_native/bodylock_lifecycle.cpp
    ../controller_native/native_gamepad_controller.cpp
)

target_include_directories(cod_native_lstick_benchmark PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../fps_2d_tracker_package/include
)

target_compile_definitions(cod_native_lstick_benchmark PRIVATE
    NOMINMAX WIN32_LEAN_AND_MEAN)

target_link_libraries(cod_native_lstick_benchmark PRIVATE
    vision_native_core gdi32 windowsapp)

set_target_properties(cod_native_lstick_benchmark PROPERTIES
    OUTPUT_NAME cod_native_left_stick_motion_benchmark)

add_executable(cod_native_lstick_benchmark_tests
    ../controller_native/left_stick_motion_defect_benchmark_tests.cpp
    ../controller_native/left_stick_motion_defect_benchmark.cpp
    ../tracking_native/association.cpp
    ../tracking_native/ego_motion_buffer.cpp
    ../tracking_native/projection_model.cpp
    ../tracking_native/tracker_authority.cpp
    ../tracking_native/kalman_tracker.cpp
    ../tracking_native/fps_reference_tracker.cpp
    ../tracking_native/tracker_backend.cpp
    ../tracking_native/legacy_projection_tracker.cpp
    ${FPS_TRACKER_PACKAGE_SOURCES}
    ../controller_native/runtime_config.cpp
    ../controller_native/ads_state_tracker.cpp
    ../controller_native/ads_completion_gate.cpp
    ../controller_native/auto_fire_gate.cpp
    ../controller_native/target_tracker.cpp
    ../controller_native/ads_carry_brake_policy.cpp
    ../controller_native/ai_aim.cpp
    ../controller_native/body_lock_short_plan_policy.cpp
    ../controller_native/bodylock_policy.cpp
    ../controller_native/aim_assist_dynamics.cpp
    ../controller_native/recoil_profile.cpp
    ../controller_native/recoil_calibration.cpp
    ../controller_native/recoil_compensation.cpp
    ../recoil_native/recoil_compensation.cpp
    ../recoil_native/recoil_visual_model.cpp
    ../controller_native/output_mixer.cpp
    ../controller_native/output_validation_policy.cpp
    ../controller_native/controller_pipeline.cpp
    ../controller_native/target_snapshot_provider.cpp
    ../controller_native/assist_authority_policy.cpp
    ../controller_native/bodylock_lifecycle.cpp
    ../controller_native/native_gamepad_controller.cpp
)

target_include_directories(cod_native_lstick_benchmark_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../fps_2d_tracker_package/include
)

foreach(_controller_target IN ITEMS
    cod_native_runtime
    cod_native_controller_tests
    cod_native_live_failure_benchmark
    cod_native_gamepad_benchmark
    cod_native_partial_occlusion_benchmark
    cod_native_vlock_defect_tests
    cod_native_lstick_benchmark
    cod_native_lstick_benchmark_tests)
    target_sources(${_controller_target} PRIVATE
        ../controller_native/target_geometry.cpp)
endforeach()

target_compile_definitions(cod_native_lstick_benchmark_tests PRIVATE
    NOMINMAX WIN32_LEAN_AND_MEAN)

target_link_libraries(cod_native_lstick_benchmark_tests PRIVATE
    vision_native_core gdi32 windowsapp)

enable_testing()
add_test(
    NAME VisionResizeContractTests
    COMMAND vision_native_resize_contract_tests)
add_test(
    NAME RuntimeConfigTests
    COMMAND cod_native_runtime_config_tests)
add_test(
    NAME ViewportControllerTests
    COMMAND cod_native_viewport_controller_tests)
add_test(
    NAME VisionServiceTests
    COMMAND cod_native_vision_service_tests)
set_tests_properties(RuntimeConfigTests PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../..")
add_test(
    NAME NativeLeftStickMotionBenchmarkTests
    COMMAND cod_native_lstick_benchmark_tests)

add_executable(cod_native_aimlab_benchmark_tests
    ../controller_native/aimlab_benchmark_tests.cpp
    ../controller_native/aimlab_benchmark.cpp
)

add_executable(cod_native_sustained_aimlab_scenario_tests
    ../controller_native/sustained_aimlab_scenario_tests.cpp
    ../controller_native/sustained_aimlab_scenario.cpp
)

add_executable(cod_native_aim_response_estimator_tests
    ../controller_native/aim_response_estimator_tests.cpp
    ../controller_native/aim_response_estimator.cpp
)

target_include_directories(cod_native_aim_response_estimator_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

add_test(
    NAME NativeAimResponseEstimatorTests
    COMMAND cod_native_aim_response_estimator_tests)

add_executable(cod_native_response_model_aim_solver_tests
    ../controller_native/response_model_aim_solver_tests.cpp
    ../controller_native/response_model_aim_solver.cpp
)

target_include_directories(cod_native_response_model_aim_solver_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

add_test(
    NAME NativeResponseModelAimSolverTests
    COMMAND cod_native_response_model_aim_solver_tests)

add_executable(cod_native_sustained_aimlab_score_tests
    ../controller_native/sustained_aimlab_score_tests.cpp
    ../controller_native/sustained_aimlab_score.cpp
)

add_executable(cod_native_sustained_aimlab_simulator_tests
    ../controller_native/sustained_aimlab_simulator_tests.cpp
    ../controller_native/sustained_aimlab_simulator.cpp
    ../controller_native/sustained_aimlab_score.cpp
    ../controller_native/sustained_aimlab_scenario.cpp
)

add_executable(cod_native_benchmark_physical_input_tests
    ../controller_native/native_benchmark_physical_input_tests.cpp
)

target_include_directories(cod_native_benchmark_physical_input_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

add_test(
    NAME NativeBenchmarkPhysicalInputTests
    COMMAND cod_native_benchmark_physical_input_tests)

target_include_directories(cod_native_vector_intent_fuser_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../controller_native
)

target_compile_definitions(cod_native_vector_intent_fuser_tests
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

add_executable(cod_native_sustained_aimlab_trace_tests
    ../controller_native/sustained_aimlab_trace_tests.cpp
    ../controller_native/sustained_aimlab_trace.cpp
    ../controller_native/sustained_aimlab_scenario.cpp
)

add_executable(cod_native_sustained_aimlab_counterfactual_tests
    ../controller_native/sustained_aimlab_counterfactual_tests.cpp
    ../controller_native/sustained_aimlab_counterfactual.cpp
    ../controller_native/sustained_aimlab_trace.cpp
    ../controller_native/sustained_aimlab_simulator.cpp
    ../controller_native/sustained_aimlab_score.cpp
    ../controller_native/sustained_aimlab_scenario.cpp
)

add_executable(cod_native_control_history_tests
    ../control_learning/control_history_tests.cpp
)

add_executable(cod_native_causal_response_benchmark_tests
    ../controller_native/causal_response_synthetic_benchmark_tests.cpp
    ../controller_native/causal_response_synthetic_benchmark.cpp
    ../control_learning/short_horizon_rollout.cpp
)
target_include_directories(cod_native_causal_response_benchmark_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

add_executable(cod_native_causal_response_benchmark
    ../controller_native/cod_native_causal_response_benchmark.cpp
    ../controller_native/causal_response_synthetic_benchmark.cpp
    ../control_learning/short_horizon_rollout.cpp
)
add_executable(cod_native_robust_ew_rls_tests
    ../control_learning/robust_ew_rls_tests.cpp
)
add_executable(cod_native_causal_online_response_learner_tests
    ../control_learning/causal_online_response_learner_tests.cpp
    ../control_learning/causal_online_response_learner.cpp
)
add_executable(cod_native_pending_motion_model_tests
    ../control_learning/pending_motion_model_tests.cpp
    ../control_learning/pending_motion_model.cpp
)
add_executable(cod_native_short_horizon_rollout_tests
    ../control_learning/short_horizon_rollout_tests.cpp
    ../control_learning/short_horizon_rollout.cpp
)
add_executable(cod_native_causal_response_microbenchmark
    ../control_learning/causal_response_microbenchmark.cpp
    ../control_learning/causal_online_response_learner.cpp
)
target_include_directories(cod_native_causal_response_microbenchmark PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_include_directories(cod_native_short_horizon_rollout_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_include_directories(cod_native_pending_motion_model_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_include_directories(cod_native_causal_online_response_learner_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_include_directories(cod_native_robust_ew_rls_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_include_directories(cod_native_causal_response_benchmark PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_include_directories(cod_native_control_history_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
add_test(NAME ControlHistoryTests COMMAND cod_native_control_history_tests)
add_test(
    NAME ControlResponseWindowTests
    COMMAND cod_native_control_response_window_tests)
add_test(NAME RuntimeTelemetryTests COMMAND cod_native_runtime_telemetry_tests)
add_test(
    NAME CausalResponseSyntheticBenchmarkTests
    COMMAND cod_native_causal_response_benchmark_tests)
add_test(NAME RobustEwRlsTests COMMAND cod_native_robust_ew_rls_tests)
add_test(NAME CausalOnlineResponseLearnerTests
    COMMAND cod_native_causal_online_response_learner_tests)
add_test(NAME PendingMotionModelTests COMMAND cod_native_pending_motion_model_tests)
add_test(NAME ShortHorizonRolloutTests COMMAND cod_native_short_horizon_rollout_tests)
add_test(NAME RuntimeProvenanceTests COMMAND cod_native_runtime_provenance_tests)
add_test(NAME TelemetryCollectorsTests COMMAND cod_native_telemetry_collectors_tests)
add_test(NAME LogSessionManagerTests COMMAND cod_native_log_session_manager_tests)

add_executable(cod_native_committed_capture_observation_tests
    ../pipeline_contract/committed_capture_observation_tests.cpp
    ../runtime_app/vision_controller_adapter.cpp
    ../controller_native/target_geometry.cpp
)

target_include_directories(cod_native_committed_capture_observation_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

add_test(
    NAME CommittedCaptureObservationTests
    COMMAND cod_native_committed_capture_observation_tests)

add_executable(cod_native_blind_window_benchmark_tests
    ../controller_native/blind_window_benchmark_tests.cpp
    ../controller_native/blind_window_baseline.cpp
    ../controller_native/blind_window_benchmark.cpp
    ../controller_native/blind_window_fixtures.cpp
    ../controller_native/blind_window_metrics.cpp
)

target_include_directories(cod_native_blind_window_benchmark_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

add_test(
    NAME NativeBlindWindowBenchmarkTests
    COMMAND cod_native_blind_window_benchmark_tests)

add_executable(cod_native_blind_window_benchmark
    ../controller_native/cod_native_blind_window_benchmark.cpp
    ../controller_native/blind_window_native_adapter.cpp
    ../controller_native/native_benchmark_controller_adapter.cpp
    ../controller_native/blind_window_baseline.cpp
    ../controller_native/blind_window_benchmark.cpp
    ../controller_native/blind_window_fixtures.cpp
    ../controller_native/blind_window_metrics.cpp
    ../tracking_native/tracker_authority.cpp
    ../tracking_native/tracker_backend.cpp
    ../tracking_native/fps_reference_tracker.cpp
    ../tracking_native/kalman_tracker.cpp
    ../tracking_native/legacy_projection_tracker.cpp
    ../tracking_native/association.cpp
    ../tracking_native/ego_motion_buffer.cpp
    ../tracking_native/projection_model.cpp
    ${FPS_TRACKER_PACKAGE_SOURCES}
    ../controller_native/runtime_config.cpp
    ../controller_native/auto_fire_gate.cpp
    ../controller_native/recoil_profile.cpp
    ../controller_native/recoil_calibration.cpp
    ../controller_native/recoil_compensation.cpp
    ../recoil_native/recoil_compensation.cpp
    ../recoil_native/recoil_visual_model.cpp
    ../controller_native/weapon_recognizer.cpp
    ../controller_native/output_mixer.cpp
    ../controller_native/controller_pipeline.cpp
    ../controller_native/native_gamepad_controller.cpp
    ../controller_native/target_geometry.cpp
)

target_include_directories(cod_native_blind_window_benchmark PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../fps_2d_tracker_package/include
)

target_compile_definitions(cod_native_blind_window_benchmark PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    COD_BENCHMARK_MIX_OVERRIDE
)

target_link_libraries(cod_native_blind_window_benchmark PRIVATE
    vision_native_core
    gdi32
    windowsapp
)

target_include_directories(cod_native_sustained_aimlab_scenario_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_include_directories(cod_native_sustained_aimlab_score_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_include_directories(cod_native_sustained_aimlab_simulator_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_include_directories(cod_native_sustained_aimlab_trace_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_include_directories(cod_native_sustained_aimlab_counterfactual_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

add_test(
    NAME NativeSustainedAimlabScenarioTests
    COMMAND cod_native_sustained_aimlab_scenario_tests)

add_test(
    NAME NativeSustainedAimlabScoreTests
    COMMAND cod_native_sustained_aimlab_score_tests)

add_test(
    NAME NativeSustainedAimlabSimulatorTests
    COMMAND cod_native_sustained_aimlab_simulator_tests)

add_executable(cod_native_sustained_aimlab_learning_tests
    ../controller_native/sustained_aimlab_learning_tests.cpp
    ../controller_native/sustained_aimlab_learning.cpp
    ../controller_native/sustained_aimlab_simulator.cpp
    ../controller_native/sustained_aimlab_score.cpp
    ../controller_native/sustained_aimlab_scenario.cpp
    ../control_learning/causal_online_response_learner.cpp
    ../control_learning/pending_motion_model.cpp
    ../control_learning/short_horizon_rollout.cpp
)
target_include_directories(cod_native_sustained_aimlab_learning_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
add_test(
    NAME NativeSustainedAimlabLearningTests
    COMMAND cod_native_sustained_aimlab_learning_tests)

add_test(
    NAME NativeSustainedAimlabTraceTests
    COMMAND cod_native_sustained_aimlab_trace_tests)

add_test(
    NAME NativeSustainedAimlabCounterfactualTests
    COMMAND cod_native_sustained_aimlab_counterfactual_tests)

target_include_directories(cod_native_aimlab_benchmark_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_definitions(cod_native_aimlab_benchmark_tests
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

target_link_libraries(cod_native_aimlab_benchmark_tests PRIVATE
    vision_native_core
)

add_executable(cod_native_aimlab_benchmark
    ../controller_native/cod_native_aimlab_benchmark.cpp
    ../controller_native/aimlab_benchmark.cpp
)

add_executable(cod_native_sustained_aimlab_benchmark
    ../controller_native/cod_native_sustained_aimlab_benchmark.cpp
    ../controller_native/native_benchmark_controller_adapter.cpp
    ../controller_native/sustained_aimlab_counterfactual.cpp
    ../controller_native/sustained_aimlab_trace.cpp
    ../controller_native/sustained_aimlab_simulator.cpp
    ../controller_native/sustained_aimlab_learning.cpp
    ../controller_native/sustained_aimlab_score.cpp
    ../controller_native/sustained_aimlab_scenario.cpp
    ../tracking_native/tracker_authority.cpp
    ../tracking_native/tracker_backend.cpp
    ../tracking_native/fps_reference_tracker.cpp
    ../tracking_native/kalman_tracker.cpp
    ../tracking_native/legacy_projection_tracker.cpp
    ../tracking_native/association.cpp
    ../tracking_native/ego_motion_buffer.cpp
    ../tracking_native/projection_model.cpp
    ${FPS_TRACKER_PACKAGE_SOURCES}
    ../controller_native/runtime_config.cpp
    ../controller_native/auto_fire_gate.cpp
    ../controller_native/recoil_profile.cpp
    ../controller_native/recoil_calibration.cpp
    ../controller_native/recoil_compensation.cpp
    ../recoil_native/recoil_compensation.cpp
    ../recoil_native/recoil_visual_model.cpp
    ../controller_native/weapon_recognizer.cpp
    ../controller_native/output_mixer.cpp
    ../controller_native/controller_pipeline.cpp
    ../controller_native/native_gamepad_controller.cpp
    ../controller_native/target_geometry.cpp
    ../control_learning/causal_online_response_learner.cpp
    ../control_learning/pending_motion_model.cpp
    ../control_learning/short_horizon_rollout.cpp
)

target_include_directories(cod_native_sustained_aimlab_benchmark PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../fps_2d_tracker_package/include
)

target_compile_definitions(cod_native_sustained_aimlab_benchmark PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    COD_BENCHMARK_MIX_OVERRIDE
)

target_link_libraries(cod_native_sustained_aimlab_benchmark PRIVATE
    vision_native_core
    gdi32
    windowsapp
)

set(COD_TARGET_PLAN_RUNTIME_SOURCES
    ../controller_native/intent_filter.cpp
    ../controller_native/control_response_estimator.cpp
    ../controller_native/target_coordinator.cpp
    ../controller_native/ads_acquisition_controller.cpp
    ../controller_native/bodylock_follow_controller.cpp
    ../controller_native/response_model_aim_solver.cpp
    ../controller_native/aim_response_estimator.cpp
    ../controller_native/aim_dynamics_shaper.cpp
    ../controller_native/axis_intent_arbiter.cpp
    ../controller_native/vector_intent_fuser.cpp
)

foreach(COD_TARGET
    cod_native_runtime
    cod_native_controller_tests
    cod_native_live_failure_benchmark
    cod_native_gamepad_benchmark
    cod_native_partial_occlusion_benchmark
    cod_native_vlock_defect_tests
    cod_native_lstick_benchmark
    cod_native_lstick_benchmark_tests
    cod_native_sustained_aimlab_benchmark
    cod_native_blind_window_benchmark
)
    if(TARGET ${COD_TARGET})
        target_sources(${COD_TARGET} PRIVATE ${COD_TARGET_PLAN_RUNTIME_SOURCES})
    endif()
endforeach()

target_include_directories(cod_native_aimlab_benchmark PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_definitions(cod_native_aimlab_benchmark
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

target_link_libraries(cod_native_aimlab_benchmark PRIVATE
    vision_native_core
)

# Fusion visual overlay canvas (separate process, D3D11 + D2D1 + DirectComposition)
add_executable(fusion_canvas
    ../overlay_canvas/fusion_canvas.cpp
)

target_include_directories(fusion_canvas PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_definitions(fusion_canvas
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

target_compile_options(fusion_canvas PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

target_link_libraries(fusion_canvas PRIVATE
    fusion_shared
    d3d11
    d2d1
    dxgi
    dcomp
    dwrite
    Shcore
)

pybind11_add_module(vision_native_cpp
    src/vision_native_module.cpp
)

set_target_properties(vision_native_cpp PROPERTIES
    OUTPUT_NAME "vision_native_cpp"
)

target_link_libraries(vision_native_cpp PRIVATE vision_native_core)

set(_runtime_dlls
    nvinfer_10.dll
    nvinfer_plugin_10.dll
)

foreach(_target vision_native_smoke vision_native_debug vision_native_bgra_benchmark cod_native_runtime vision_native_cpp)
    foreach(_dll IN LISTS _runtime_dlls)
        add_custom_command(TARGET ${_target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${TensorRT_BIN_DIR}/${_dll}"
                "$<TARGET_FILE_DIR:${_target}>/${_dll}"
            VERBATIM
        )
    endforeach()
endforeach()
