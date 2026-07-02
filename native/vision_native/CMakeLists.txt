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
    src/dxgi_capture.cpp
    src/engine_io.cpp
    src/preprocess.cu
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
    CUDA_ARCHITECTURES 89
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

add_executable(cod_native_runtime
    ../runtime_app/main.cpp
    ../runtime_app/runtime_loop.cpp
    ../runtime_app/fusion_channel_publisher.cpp
    ../runtime_app/perf_logger.cpp
    ../runtime_app/aim_perf_file_logger.cpp
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
    ../controller_native/ads_state_tracker.cpp
    ../controller_native/auto_fire_gate.cpp
    ../controller_native/target_tracker.cpp
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
    ../controller_native/native_gamepad_controller.cpp
    ../controller_native/virtual_gamepad.cpp
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
    windowsapp
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
    ../controller_native/controller_behavior_tests.cpp
    ../runtime_app/aim_perf_file_logger.cpp
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
    ../controller_native/ads_state_tracker.cpp
    ../controller_native/auto_fire_gate.cpp
    ../controller_native/target_tracker.cpp
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
    ../controller_native/native_gamepad_controller.cpp
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
)

target_link_libraries(cod_native_controller_tests PRIVATE
    gdi32
    windowsapp
)

add_executable(cod_native_controller_protocol_tests
    ../controller_native/controller_protocol_tests.cpp
    ../runtime_app/vision_controller_adapter.cpp
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

add_executable(cod_native_target_snapshot_tests
    ../controller_native/target_snapshot_provider_tests.cpp
    ../controller_native/target_snapshot_provider.cpp
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

add_executable(cod_native_ads_state_tests
    ../controller_native/ads_state_tracker_tests.cpp
    ../controller_native/ads_state_tracker.cpp
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

add_executable(cod_native_gamepad_benchmark
    ../controller_native/cod_native_gamepad_benchmark.cpp
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
    ../controller_native/auto_fire_gate.cpp
    ../controller_native/target_tracker.cpp
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
    ../controller_native/native_gamepad_controller.cpp
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
