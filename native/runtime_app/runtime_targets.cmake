# Included by native/CMakeLists.txt; source paths are relative to native/.

execute_process(
    COMMAND git rev-parse HEAD
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/..
    OUTPUT_VARIABLE COD_BUILD_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(NOT COD_BUILD_COMMIT)
    set(COD_BUILD_COMMIT "unknown")
endif()

add_executable(cod_native_runtime
    runtime_app/main.cpp
    runtime_app/runtime_loop.cpp
    runtime_app/runtime_loop_diagnostics.cpp
    runtime_app/runtime_reload_policy.cpp
    runtime_app/runtime_control_bridge.cpp
    runtime_app/runtime_timing.cpp
    runtime_app/vision_service.cpp
    runtime_app/viewport_controller.cpp
    runtime_app/fusion_channel_publisher.cpp
    runtime_app/perf_logger.cpp
    runtime_app/log_session_manager.cpp
    runtime_app/runtime_telemetry.cpp
    runtime_app/telemetry_collectors.cpp
    runtime_app/telemetry_target_identity.cpp
    runtime_app/telemetry_event_sampler.cpp
    runtime_app/ads_visual_transition.cpp
    runtime_app/ads_transition_collector.cpp
    runtime_app/downward_diagnostics.cpp
    runtime_app/native_replay_writer.cpp
    runtime_app/native_replay_runner.cpp
    replay_native/replay_reader.cpp
    replay_native/replay_metrics.cpp
    runtime_app/vision_controller_adapter.cpp
    controller_native/virtual_gamepad.cpp
    controller_native/io_recovery_policy.cpp
    controller_native/xinput_reader.cpp
    controller_native/sdl_gamepad_reader.cpp)
cod_native_defaults(cod_native_runtime)
target_compile_definitions(cod_native_runtime PRIVATE
    COD_BUILD_COMMIT="${COD_BUILD_COMMIT}"
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
