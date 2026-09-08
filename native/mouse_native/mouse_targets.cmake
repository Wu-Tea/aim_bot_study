# Included from vision_native: shares the current controller and Vision libraries.
add_library(mouse_native_contract STATIC ../mouse_native/mouse_relay_contract.cpp)
cod_native_defaults(mouse_native_contract)
add_library(mouse_native_control STATIC
    ../mouse_native/mouse_rate_adapter.cpp
    ../mouse_native/mouse_actuator_adapter.cpp
    ../mouse_native/mouse_sensitivity_calibrator.cpp
    ../mouse_native/mouse_controller_facade.cpp
    ../mouse_native/mouse_diagnostics.cpp
    ../mouse_native/mouse_controller_runtime_core.cpp
    ../mouse_native/mouse_relay_client.cpp
    ../mouse_native/mouse_controller_session.cpp
    ../mouse_native/mouse_emergency_exit.cpp
    ../mouse_native/mouse_win32_debug_transport.cpp
    ../mouse_native/mouse_interception_transport.cpp
    ../mouse_native/mouse_virtual_hid_transport.cpp
    ../mouse_native/mouse_runtime_supervisor.cpp
    ../mouse_link/fakerinput_output.cpp)
cod_native_defaults(mouse_native_control)
target_link_libraries(mouse_native_control PUBLIC controller_native_core setupapi user32 hid cfgmgr32)

foreach(_mouse_test relay_contract adapter sensitivity_calibrator controller_facade
        emergency_exit controller_runtime_core relay_client auto_fire_button tuning interception_transport manual_judgment
        virtual_hid_transport virtual_session runtime_supervisor bodylock_deadzone bodylock_escape recoil diagnostics bodylock_curve target_point target_point_boundaries)
    set(_target cod_native_mouse_${_mouse_test}_tests)
    add_executable(${_target} ../mouse_native/mouse_${_mouse_test}_tests.cpp)
    cod_native_defaults(${_target})
    target_link_libraries(${_target} PRIVATE mouse_native_control mouse_native_contract)
    add_test(NAME Mouse_${_mouse_test} COMMAND ${_target})
    set_tests_properties(Mouse_${_mouse_test} PROPERTIES LABELS "mouse;contract")
endforeach()
add_executable(cod_native_mouse_bodylock_curve_reference EXCLUDE_FROM_ALL ../mouse_native/mouse_bodylock_curve_tests.cpp)
cod_native_defaults(cod_native_mouse_bodylock_curve_reference)
target_compile_definitions(cod_native_mouse_bodylock_curve_reference PRIVATE MOUSE_BODYLOCK_CURVE_REFERENCE=1)
target_link_libraries(cod_native_mouse_bodylock_curve_reference PRIVATE mouse_native_control)
set_tests_properties(Mouse_virtual_session PROPERTIES RESOURCE_LOCK mouse_runtime_hotkeys)
set_tests_properties(Mouse_runtime_supervisor PROPERTIES TIMEOUT 40)
add_test(NAME Mouse_virtual_hid_source_leak_red COMMAND cod_native_mouse_virtual_hid_transport_tests --leak-fixture)
set_tests_properties(Mouse_virtual_hid_source_leak_red PROPERTIES WILL_FAIL TRUE LABELS "mouse;contract;negative-control")
add_test(NAME Mouse_virtual_session_source_leak_red COMMAND cod_native_mouse_virtual_session_tests --inject-source-leak)
set_tests_properties(Mouse_virtual_session_source_leak_red PROPERTIES WILL_FAIL TRUE LABELS "mouse;contract;negative-control" RESOURCE_LOCK mouse_runtime_hotkeys)
add_test(NAME Mouse_interception_no_exclusion_red COMMAND cod_native_mouse_interception_transport_tests --leak-fixture)
set_tests_properties(Mouse_interception_no_exclusion_red PROPERTIES WILL_FAIL TRUE LABELS "mouse;contract;negative-control")
target_compile_definitions(cod_native_mouse_relay_contract_tests PRIVATE
    MOUSE_RELAY_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../mouse_native/fixtures/transparent_relay_v1.csv")

# Explicit counterfactual: supported 1/1 settings preserve the previous adapter.
# Same production controller and same quantitative oracles; expected to be RED.
add_executable(cod_native_mouse_tuning_legacy_reference EXCLUDE_FROM_ALL
    ../mouse_native/mouse_tuning_tests.cpp)
cod_native_defaults(cod_native_mouse_tuning_legacy_reference)
target_compile_definitions(cod_native_mouse_tuning_legacy_reference PRIVATE MOUSE_TUNING_LEGACY_REFERENCE=1)
target_link_libraries(cod_native_mouse_tuning_legacy_reference PRIVATE mouse_native_control)

# Explicit desktop probe moves the cursor; keep it out of unattended CTest.
add_executable(cod_native_mouse_win32_debug_transport_tests EXCLUDE_FROM_ALL
    ../mouse_native/mouse_win32_debug_transport_tests.cpp
    ../runtime_app/runtime_timing.cpp)
cod_native_defaults(cod_native_mouse_win32_debug_transport_tests)
target_link_libraries(cod_native_mouse_win32_debug_transport_tests PRIVATE mouse_native_control winmm)

add_executable(cod_native_mouse_runtime
    ../runtime_app/mouse_runtime_main.cpp
    ../runtime_app/runtime_provenance.cpp
    ../runtime_app/runtime_timing.cpp
    ../runtime_app/vision_service.cpp
    ../runtime_app/vision_controller_adapter.cpp)
cod_native_defaults(cod_native_mouse_runtime)
target_link_libraries(cod_native_mouse_runtime PRIVATE
    mouse_native_control vision_native_core winmm windowsapp advapi32)

# Reuse the pinned mouse_link dependency; absence is a runtime diagnostic, not
# permission to substitute SendInput. Never install a system driver at build.
set(_mouse_interception_dll "${CMAKE_CURRENT_SOURCE_DIR}/../../artifacts/mouse_link/deps/Interception/library/x64/interception.dll")
if(EXISTS "${_mouse_interception_dll}")
    file(SHA256 "${_mouse_interception_dll}" _mouse_interception_sha)
    if(NOT _mouse_interception_sha STREQUAL "ab88164c11b1b48488772d4c3bfaa4509d5b0ae9dbc5a691dc4f96f0260443c8")
        message(FATAL_ERROR "Interception DLL differs from pinned v1.0.1")
    endif()
    add_custom_command(TARGET cod_native_mouse_runtime POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_mouse_interception_dll}"
        "$<TARGET_FILE_DIR:cod_native_mouse_runtime>/interception.dll")
endif()
