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
option(NATIVE_TEST_ENABLE_OFFLINE_BENCHMARKS
    "Build and register AimLab, PID, scenario, score, and trace tests"
    OFF)
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
    "${TensorRT_LIB_DIR}/nvinfer_plugin_10.lib")
foreach(_lib IN LISTS TensorRT_LIBS)
    if(NOT EXISTS "${_lib}")
        message(FATAL_ERROR "Required TensorRT library missing: ${_lib}")
    endif()
endforeach()

enable_testing()

function(cod_native_defaults target)
    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../controller_native)
    target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(${target} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
endfunction()

# Explicit hardware gate: requires a CUDA-capable D3D11 adapter, never ViGEm
# or a live game. Not part of portable/CPU-only CTest runs.
add_executable(cod_native_cuda_d3d11_interop_tests EXCLUDE_FROM_ALL
    src/cuda_d3d11_interop_tests.cpp)
cod_native_defaults(cod_native_cuda_d3d11_interop_tests)
target_link_libraries(cod_native_cuda_d3d11_interop_tests PRIVATE CUDA::cudart d3d11 dxgi)

function(cod_native_add_offline_test target test_name)
    add_executable(${target} ${ARGN})
    cod_native_defaults(${target})
    if(NATIVE_TEST_ENABLE_OFFLINE_BENCHMARKS)
        add_test(NAME ${test_name} COMMAND ${target})
    else()
        set_target_properties(${target} PROPERTIES EXCLUDE_FROM_ALL TRUE)
    endif()
endfunction()

add_library(vision_native_core STATIC
    src/build_family.cpp
    src/color_readback.cpp
    src/dxgi_capture.cpp
    src/qpc_steady_clock.cpp
    src/present_interval_contract.cpp
    src/engine_io.cpp
    src/preprocess.cu
    src/resize_contract.cpp
    src/target_selector.cpp
    src/tensorrt_engine.cpp
    src/tensorrt_inspector.cpp
    src/vision_engine.cpp)
target_include_directories(vision_native_core PUBLIC
    include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    PRIVATE "${TensorRT_INCLUDE_DIR}")
target_compile_definitions(vision_native_core PUBLIC VISION_BUILD_FAMILY_MODERN=1)
target_compile_definitions(vision_native_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
set_target_properties(vision_native_core PROPERTIES
    CUDA_SEPARABLE_COMPILATION OFF
    CUDA_ARCHITECTURES "${CMAKE_CUDA_ARCHITECTURES}")
target_link_libraries(vision_native_core PUBLIC
    CUDA::cudart
    ${TensorRT_LIBS}
    d3d11
    dxgi)

add_library(fusion_shared STATIC
    ../shared_fusion/fusion_channel.cpp
    ../shared_fusion/fusion_channel.h)
target_include_directories(fusion_shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_options(fusion_shared PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

# The production controller has one current target-plan path.  Historical
# trackers, predictive bridges, causal-memory controllers, intent fusers and
# shadow gates are intentionally absent from this source set.
add_library(controller_native_core STATIC
    ../tracking_native/tracker_authority.cpp
    ../controller_native/runtime_config.cpp
    ../controller_native/auto_fire_gate.cpp
    ../controller_native/output_diagnostics.cpp
    ../controller_native/output_composer.cpp
    ../controller_native/native_gamepad_controller.cpp
    ../controller_native/target_geometry.cpp
    ../controller_native/target_state_reducers.cpp
    ../controller_native/ads_lifecycle_reducer.cpp
    ../controller_native/ads_reacquisition_reducer.cpp
    ../controller_native/recoil_reducer.cpp
    ../controller_native/intent_filter.cpp
    ../controller_native/operation_intent.cpp
    ../controller_native/target_coordinator.cpp
    ../controller_native/ads_acquisition_controller.cpp
    ../controller_native/bodylock_follow_controller.cpp
    ../controller_native/bodylock_target_motion_observer.cpp
    ../controller_native/response_model_aim_solver.cpp
    ../controller_native/aim_response_estimator.cpp
    ../controller_native/ads_response_estimator.cpp
    ../controller_native/aim_dynamics_shaper.cpp)
target_include_directories(controller_native_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(controller_native_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(controller_native_core PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
target_link_libraries(controller_native_core PUBLIC windowsapp)

# Retired file-backed recoil tools are isolated from the production link graph.
add_library(recoil_profile_tools STATIC
    ../controller_native/recoil_profile.cpp
    ../controller_native/recoil_calibration.cpp
    ../controller_native/recoil_compensation.cpp
    ../recoil_native/recoil_compensation.cpp
    ../recoil_native/recoil_visual_model.cpp)
cod_native_defaults(recoil_profile_tools)

execute_process(
    COMMAND git rev-parse HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../..
    OUTPUT_VARIABLE COD_BUILD_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(NOT COD_BUILD_COMMIT)
    set(COD_BUILD_COMMIT "unknown")
endif()

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../control_contract/architecture.json"
    COD_CONTROL_ARCHITECTURE_CONTRACT)
string(SHA256 COD_CONTROL_CONTRACT_HASH
    "${COD_CONTROL_ARCHITECTURE_CONTRACT}")

add_executable(cod_native_runtime
    ../runtime_app/main.cpp
    ../runtime_app/runtime_provenance.cpp
    ../runtime_app/runtime_loop.cpp
    ../runtime_app/runtime_timing.cpp
    ../runtime_app/vision_service.cpp
    ../runtime_app/viewport_controller.cpp
    ../runtime_app/fusion_channel_publisher.cpp
    ../runtime_app/perf_logger.cpp
    ../runtime_app/log_session_manager.cpp
    ../runtime_app/runtime_telemetry.cpp
    ../runtime_app/telemetry_collectors.cpp
    ../runtime_app/telemetry_target_identity.cpp
    ../runtime_app/telemetry_event_sampler.cpp
    ../runtime_app/ads_visual_transition.cpp
    ../runtime_app/ads_transition_collector.cpp
    ../runtime_app/downward_diagnostics.cpp
    ../runtime_app/native_replay_writer.cpp
    ../runtime_app/native_replay_runner.cpp
    ../replay_native/replay_reader.cpp
    ../replay_native/replay_metrics.cpp
    ../runtime_app/vision_controller_adapter.cpp
    ../controller_native/virtual_gamepad.cpp
    ../controller_native/io_recovery_policy.cpp
    ../controller_native/xinput_reader.cpp
    ../controller_native/sdl_gamepad_reader.cpp)
cod_native_defaults(cod_native_runtime)
target_compile_definitions(cod_native_runtime PRIVATE
    COD_BUILD_COMMIT="${COD_BUILD_COMMIT}"
    COD_CONTROL_CONTRACT_HASH="${COD_CONTROL_CONTRACT_HASH}"
    COD_CONTROL_ARCHITECTURE_VERSION=5
    COD_CONTROL_EVENT_SCHEMA_VERSION=2
    $<$<BOOL:${NATIVE_ENABLE_VIGEM}>:NATIVE_ENABLE_VIGEM>)
target_link_libraries(cod_native_runtime PRIVATE
    vision_native_core
    controller_native_core
    fusion_shared
    d3d11 dxgi xinput gdi32 winmm windowsapp advapi32)

if(ViGEmClient_DLL AND EXISTS "${ViGEmClient_DLL}")
    add_custom_command(TARGET cod_native_runtime POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${ViGEmClient_DLL}"
            "$<TARGET_FILE_DIR:cod_native_runtime>/ViGEmClient.dll"
        VERBATIM)
else()
    message(WARNING "ViGEmClient.dll not found; runtime will use its configured fallback.")
endif()
if(SDL2_DLL AND EXISTS "${SDL2_DLL}")
    add_custom_command(TARGET cod_native_runtime POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${SDL2_DLL}"
            "$<TARGET_FILE_DIR:cod_native_runtime>/SDL2.dll"
        VERBATIM)
endif()

add_executable(vision_native_smoke src/trt_smoke_main.cpp)
target_link_libraries(vision_native_smoke PRIVATE vision_native_core)
add_executable(vision_native_debug src/vision_debug_main.cpp)
target_link_libraries(vision_native_debug PRIVATE vision_native_core)
add_executable(vision_native_bgra_benchmark EXCLUDE_FROM_ALL
    src/bgra_benchmark.cpp)
target_link_libraries(vision_native_bgra_benchmark PRIVATE vision_native_core)

add_executable(vision_native_color_readback_benchmark EXCLUDE_FROM_ALL
    src/color_readback_benchmark.cpp src/color_readback.cpp)
cod_native_defaults(vision_native_color_readback_benchmark)
target_include_directories(vision_native_color_readback_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(vision_native_color_readback_benchmark PRIVATE CUDA::cudart)

add_executable(cod_native_scheduler_benchmark EXCLUDE_FROM_ALL
    ../runtime_app/scheduler_benchmark.cpp
    ../runtime_app/runtime_timing.cpp)
cod_native_defaults(cod_native_scheduler_benchmark)
target_link_libraries(cod_native_scheduler_benchmark PRIVATE winmm)

# Native product tests are grouped by ownership into two physical runners.
# Each source contributes named cases through the shared test registry below.
add_executable(cod_native_base_tests
    ../test_support/base_tests_main.cpp
    ../controller_native/ads_bodylock_september_incident_tests.cpp
    ../controller_native/runtime_config_tests.cpp
    ../pipeline_contract/target_plan_contract_tests.cpp
    ../pipeline_contract/committed_capture_observation_tests.cpp
    ../controller_native/input_edge_reducer_tests.cpp
    ../controller_native/aim_scope_reducer_tests.cpp
    ../controller_native/intent_filter_tests.cpp
    ../controller_native/operation_intent_tests.cpp
    ../controller_native/target_state_reducers_tests.cpp
    ../controller_native/ads_lifecycle_reducer_tests.cpp
    ../controller_native/ads_reacquisition_reducer_tests.cpp
    ../controller_native/control_pipeline_primitives_tests.cpp
    ../controller_native/target_pipeline_integration_tests.cpp
    ../controller_native/target_coordinator_tests.cpp
    ../controller_native/ads_acquisition_controller_tests.cpp
    ../controller_native/bodylock_follow_controller_tests.cpp
    ../controller_native/bodylock_target_motion_observer_tests.cpp
    ../controller_native/aim_dynamics_shaper_tests.cpp
    ../controller_native/aim_response_estimator_tests.cpp
    ../controller_native/response_model_aim_solver_tests.cpp
    ../controller_native/target_geometry_tests.cpp
    src/build_family_tests.cpp
    src/resize_contract_tests.cpp
    src/target_selector_tests.cpp
    src/color_readback_tests.cpp
    ../controller_native/io_recovery_policy_tests.cpp
    ../controller_native/io_recovery_policy.cpp
    ../controller_native/controller_protocol_tests.cpp
    ../runtime_app/vision_service_tests.cpp
    ../runtime_app/vision_service.cpp
    ../runtime_app/runtime_timing_tests.cpp
    ../runtime_app/runtime_timing.cpp
    ../controller_native/ads_snap_deadline_release_incident_regression.cpp
    ../controller_native/acquisition_gesture_purpose_incident_regression.cpp
    ../controller_native/ads_center_cross_late_exit_incident_regression.cpp
    ../controller_native/ads_completion_geometry_continuity_incident_regression.cpp
    ../controller_native/ads_incomplete_acquisition_safety_incident_regression.cpp
    ../controller_native/ads_close_acquisition_pacing_incident_regression.cpp
    ../controller_native/ads_initial_scope_existing_target_incident_regression.cpp
    ../controller_native/ads_scope_ready_authority_incident_regression.cpp
    ../controller_native/ads_single_press_token_incident_regression.cpp
    ../controller_native/ads_snap_core_contract_incident_regression.cpp
    ../controller_native/ads_cross_epoch_pickup_revalidation_incident_regression.cpp
    ../controller_native/bodylock_high_frequency_incident_regression.cpp
    ../controller_native/bodylock_position_motion_conflict_incident_regression.cpp
    ../controller_native/bodylock_pov_fire_cue_continuity_incident_regression.cpp
    ../controller_native/bodylock_response_coordinate_incident_regression.cpp
    ../controller_native/bodylock_target_direction_latency_incident_regression.cpp
    ../controller_native/bodylock_target_motion_total_incident_regression.cpp
    ../controller_native/close_cue_hold_authority_incident_regression.cpp
    ../controller_native/controller_product_contract_incident_regression.cpp
    ../controller_native/far_selected_person_ads_admission_incident_regression.cpp
    ../controller_native/manual_ai_arbitration_continuity_incident_regression.cpp
    ../controller_native/manual_residual_authority_incident_regression.cpp
    ../controller_native/neutral_drift_arbitration_incident_regression.cpp
    ../controller_native/vertical_correction_release_incident_regression.cpp
    src/ads_dynamic_pickup_roi_incident_regression.cpp
    ../runtime_app/vision_controller_adapter.cpp)
cod_native_defaults(cod_native_base_tests)
target_include_directories(
    cod_native_base_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(cod_native_base_tests PRIVATE
    controller_native_core vision_native_core CUDA::cudart
    gdi32 winmm windowsapp)
foreach(_suite IN ITEMS
    BaseContracts
    BaseVisionSelection
    BaseRuntimeFreshness
    BaseAds
    BaseBodyLock
    BaseEndToEnd)
    add_test(
        NAME ${_suite}
        COMMAND cod_native_base_tests
            --suite ${_suite}
            --artifacts
            "${CMAKE_CURRENT_BINARY_DIR}/native-test-artifacts/base/${_suite}")
    set_tests_properties(${_suite} PROPERTIES
        LABELS "base;${_suite}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../..")
endforeach()

add_executable(cod_native_functional_tests
    ../test_support/functional_tests_main.cpp
    ../runtime_app/viewport_controller_tests.cpp
    ../runtime_app/viewport_controller.cpp
    ../controller_native/recoil_reducer_tests.cpp
    ../controller_native/recoil_contract_tests.cpp
    ../controller_native/weapon_recognizer_tests.cpp
    ../controller_native/weapon_recognizer.cpp
    ../controller_native/auto_fire_gate_tests.cpp
    ../runtime_app/runtime_telemetry_tests.cpp
    ../runtime_app/runtime_telemetry.cpp
    ../runtime_app/log_session_manager_tests.cpp
    ../runtime_app/log_session_manager.cpp
    ../controller_native/benchmark_metrics_tests.cpp
    ../runtime_app/perf_logger.cpp
    ../replay_native/replay_metrics.cpp)
target_sources(cod_native_functional_tests PRIVATE
    ../runtime_app/person_detection_gesture_tests.cpp
    ../runtime_app/runtime_telemetry_production_shape_tests.cpp
    ../runtime_app/telemetry_target_identity_tests.cpp
    ../runtime_app/telemetry_target_identity.cpp
    ../runtime_app/telemetry_event_sampler_tests.cpp
    ../runtime_app/telemetry_event_sampler.cpp
    ../runtime_app/runtime_provenance_tests.cpp
    ../runtime_app/runtime_provenance.cpp
    ../runtime_app/ads_visual_transition_tests.cpp
    ../runtime_app/ads_visual_transition.cpp
    ../runtime_app/ads_transition_collector_tests.cpp
    ../runtime_app/ads_transition_collector.cpp
    ../runtime_app/telemetry_collectors_tests.cpp
    ../runtime_app/telemetry_collectors.cpp)
cod_native_defaults(cod_native_functional_tests)
target_link_libraries(cod_native_functional_tests PRIVATE
    controller_native_core recoil_profile_tools advapi32)
foreach(_suite IN ITEMS
    FeatureDynamicViewport
    FeatureRecoilAndWeapon
    FeatureAutoFireAndMarker
    FeatureTelemetryAndDiagnostics)
    add_test(
        NAME ${_suite}
        COMMAND cod_native_functional_tests
            --suite ${_suite}
            --artifacts
            "${CMAKE_CURRENT_BINARY_DIR}/native-test-artifacts/functional/${_suite}")
    set_tests_properties(${_suite} PROPERTIES
        LABELS "functional;${_suite}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../..")
endforeach()

# Keep this incident standalone so its frozen RED executable and current GREEN
# candidate can be built and run explicitly without duplicating a long closed-
# loop matrix inside every normal BaseAds CTest invocation.
add_executable(cod_native_ads_near_target_slowdown_incident_regression
    EXCLUDE_FROM_ALL
    ../controller_native/ads_near_target_slowdown_incident_regression.cpp)
cod_native_defaults(cod_native_ads_near_target_slowdown_incident_regression)
target_link_libraries(
    cod_native_ads_near_target_slowdown_incident_regression PRIVATE
    controller_native_core)


cod_native_add_offline_test(cod_native_aimlab_benchmark_tests NativeAimlabBenchmarkTests
    ../controller_native/aimlab_benchmark_tests.cpp
    ../controller_native/aimlab_benchmark.cpp)
target_link_libraries(cod_native_aimlab_benchmark_tests PRIVATE vision_native_core)
cod_native_add_offline_test(cod_native_sustained_aimlab_scenario_tests NativeSustainedAimlabScenarioTests
    ../controller_native/sustained_aimlab_scenario_tests.cpp
    ../controller_native/sustained_aimlab_scenario.cpp)
cod_native_add_offline_test(cod_native_sustained_aimlab_score_tests NativeSustainedAimlabScoreTests
    ../controller_native/sustained_aimlab_score_tests.cpp
    ../controller_native/sustained_aimlab_score.cpp)
cod_native_add_offline_test(cod_native_sustained_aimlab_simulator_tests NativeSustainedAimlabSimulatorTests
    ../controller_native/sustained_aimlab_simulator_tests.cpp
    ../controller_native/sustained_aimlab_simulator.cpp
    ../controller_native/sustained_aimlab_score.cpp
    ../controller_native/sustained_aimlab_scenario.cpp)
cod_native_add_offline_test(cod_native_sustained_aimlab_trace_tests NativeSustainedAimlabTraceTests
    ../controller_native/sustained_aimlab_trace_tests.cpp
    ../controller_native/sustained_aimlab_trace.cpp
    ../controller_native/sustained_aimlab_scenario.cpp)
cod_native_add_offline_test(cod_native_benchmark_physical_input_tests NativeBenchmarkPhysicalInputTests
    ../controller_native/native_benchmark_physical_input_tests.cpp)
cod_native_add_offline_test(cod_native_benchmark_controller_adapter_tests NativeBenchmarkControllerAdapterTests
    ../controller_native/native_benchmark_controller_adapter_tests.cpp
    ../controller_native/native_benchmark_controller_adapter.cpp)
target_link_libraries(cod_native_benchmark_controller_adapter_tests PRIVATE
    controller_native_core vision_native_core gdi32 windowsapp)
cod_native_add_offline_test(cod_native_pid_benchmark_controller_tests NativePidBenchmarkControllerTests
    ../controller_native/pid_benchmark_controller_tests.cpp)
add_executable(cod_native_aimlab_benchmark
    ../controller_native/cod_native_aimlab_benchmark.cpp
    ../controller_native/aimlab_benchmark.cpp)
cod_native_defaults(cod_native_aimlab_benchmark)
target_link_libraries(cod_native_aimlab_benchmark PRIVATE vision_native_core)

add_executable(cod_native_sustained_aimlab_benchmark
    ../controller_native/cod_native_sustained_aimlab_benchmark.cpp
    ../controller_native/native_benchmark_controller_adapter.cpp
    ../controller_native/sustained_aimlab_trace.cpp
    ../controller_native/sustained_aimlab_simulator.cpp
    ../controller_native/sustained_aimlab_score.cpp
    ../controller_native/sustained_aimlab_scenario.cpp)
cod_native_defaults(cod_native_sustained_aimlab_benchmark)
target_link_libraries(cod_native_sustained_aimlab_benchmark PRIVATE
    controller_native_core vision_native_core gdi32 windowsapp)

if(NATIVE_TEST_ENABLE_OFFLINE_BENCHMARKS)
    add_test(NAME NativeSustainedAimlabLeftStrafeOff
        COMMAND cod_native_sustained_aimlab_benchmark
            --config ${CMAKE_CURRENT_SOURCE_DIR}/../../config.native.example.toml
            --duration-ms 1575 --seed 2026072301
            --profile pure --cohort ads --smoke --left-strafe off)
    add_test(NAME NativeSustainedAimlabLeftStrafeBoth
        COMMAND cod_native_sustained_aimlab_benchmark
            --config ${CMAKE_CURRENT_SOURCE_DIR}/../../config.native.example.toml
            --duration-ms 1575 --seed 2026072301
            --profile pure --cohort ads --smoke --left-strafe both)
    add_test(NAME NativeSustainedAimlabBodyLockFreshLtPerTarget
        COMMAND cod_native_sustained_aimlab_benchmark
            --config ${CMAKE_CURRENT_SOURCE_DIR}/../../config.native.example.toml
            --duration-ms 6000 --seed 2026082703
            --profile pure --cohort bodylock --vision-hz 100
            --smoke --left-strafe off)
    add_test(NAME NativeSustainedAimlabEightKhzCounterfactual
        COMMAND cod_native_sustained_aimlab_benchmark
            --config ${CMAKE_CURRENT_SOURCE_DIR}/../../config.native.example.toml
            --duration-ms 1575 --seed 2026082703
            --controller-tick-hz 8000 --vision-hz 200
            --profile pure --cohort ads --target-motion stationary
            --smoke --left-strafe off)
    add_test(NAME NativeSustainedAimlabLeftStrafeRejectsInvalid
        COMMAND cod_native_sustained_aimlab_benchmark
            --config ${CMAKE_CURRENT_SOURCE_DIR}/../../config.native.example.toml
            --duration-ms 100 --seed 2026072301
            --profile pure --cohort ads --left-strafe invalid)
    set_tests_properties(
        NativeSustainedAimlabLeftStrafeRejectsInvalid
        PROPERTIES WILL_FAIL TRUE)
    add_test(NAME NativeRuntimeRejectsRetiredAiCadenceCli
        COMMAND cod_native_runtime
            --config ${CMAKE_CURRENT_SOURCE_DIR}/../../config.native.example.toml
            --dump-effective-config --ai-proposal-mode fixed)
    add_test(NAME NativeSustainedAimlabRejectsRetiredAiCadence
        COMMAND cod_native_sustained_aimlab_benchmark
            --config ${CMAKE_CURRENT_SOURCE_DIR}/../../config.native.example.toml
            --duration-ms 100 --seed 2026072301
            --profile pure --cohort ads
            --ai-proposal-mode fixed --ai-proposal-hz 250)
    set_tests_properties(
        NativeRuntimeRejectsRetiredAiCadenceCli
        NativeSustainedAimlabRejectsRetiredAiCadence
        PROPERTIES WILL_FAIL TRUE)
else()
    set_target_properties(
        cod_native_aimlab_benchmark
        cod_native_sustained_aimlab_benchmark
        PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()

add_library(fusion_overlay_core STATIC
    ../overlay_canvas/fusion_overlay_contract.cpp
    ../overlay_canvas/fusion_overlay_contract.h
    ../overlay_canvas/overlay_window_policy.h)
cod_native_defaults(fusion_overlay_core)
target_include_directories(fusion_overlay_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../overlay_canvas)

add_library(fusion_capture_support STATIC
    src/dxgi_capture.cpp
    src/qpc_steady_clock.cpp)
cod_native_defaults(fusion_capture_support)
target_include_directories(fusion_capture_support PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(fusion_capture_support PUBLIC d3d11 dxgi)

add_executable(fusion_overlay_contract_tests
    ../overlay_canvas/fusion_overlay_contract_tests.cpp)
cod_native_defaults(fusion_overlay_contract_tests)
target_link_libraries(fusion_overlay_contract_tests PRIVATE fusion_overlay_core)
add_test(NAME FusionOverlayContracts COMMAND fusion_overlay_contract_tests)
set_tests_properties(FusionOverlayContracts PROPERTIES LABELS "fusion;contracts")

add_executable(fusion_canvas
    ../overlay_canvas/fusion_canvas.cpp
    ../overlay_canvas/capture_isolation_guard.cpp
    ../overlay_canvas/capture_isolation_guard.h)
cod_native_defaults(fusion_canvas)
target_link_libraries(fusion_canvas PRIVATE
    fusion_shared fusion_overlay_core fusion_capture_support
    d3d11 d2d1 dxgi dcomp dwrite Shcore dwmapi)

add_executable(fusion_channel_cycle_publisher EXCLUDE_FROM_ALL
    ../overlay_canvas/fusion_channel_cycle_publisher.cpp
    ../runtime_app/fusion_channel_publisher.cpp)
cod_native_defaults(fusion_channel_cycle_publisher)
target_link_libraries(fusion_channel_cycle_publisher PRIVATE fusion_shared)

pybind11_add_module(vision_native_cpp src/vision_native_module.cpp)
set_target_properties(vision_native_cpp PROPERTIES OUTPUT_NAME "vision_native_cpp")
target_link_libraries(vision_native_cpp PRIVATE vision_native_core)

include(../mouse_native/mouse_targets.cmake)

set(_runtime_dlls nvinfer_10.dll nvinfer_plugin_10.dll)
foreach(_target vision_native_smoke vision_native_debug vision_native_bgra_benchmark cod_native_runtime cod_native_mouse_runtime vision_native_cpp)
    foreach(_dll IN LISTS _runtime_dlls)
        add_custom_command(TARGET ${_target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${TensorRT_BIN_DIR}/${_dll}"
                "$<TARGET_FILE_DIR:${_target}>/${_dll}"
            VERBATIM)
    endforeach()
endforeach()
