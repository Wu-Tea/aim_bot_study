# Compare the shipped executable's public configuration output with its owner.
# This path exits before model loading, physical input, or virtual output.
file(READ "${ARCHITECTURE}" _architecture)
string(JSON _expected GET "${_architecture}" version)
execute_process(
    COMMAND "${RUNTIME}" --config "${CONFIG}" --dump-effective-config
    RESULT_VARIABLE _status OUTPUT_VARIABLE _output ERROR_VARIABLE _error
    TIMEOUT 15)
if(NOT _status EQUAL 0)
    message(FATAL_ERROR "Configuration inspection failed: ${_status}; ${_error}")
endif()
if(NOT _output MATCHES "# effective-config complete")
    message(FATAL_ERROR "Configuration output was incomplete")
endif()
string(REGEX MATCH "runtime.provenance.control_architecture_version=([0-9]+)" _match "${_output}")
set(_actual "${CMAKE_MATCH_1}")
if(NOT _actual STREQUAL _expected)
    message(FATAL_ERROR "Runtime architecture version ${_actual} differs from architecture.json ${_expected}")
endif()
message(STATUS "Runtime architecture version ${_actual} matches architecture.json")

# Legacy settings are accepted, but inspection must describe the live policy.
file(READ "${CONFIG}" _legacy)
string(REPLACE "gpu_service_enabled = true" "gpu_service_enabled = false" _legacy "${_legacy}")
string(REPLACE "max_observation_age_ms = 50" "max_observation_age_ms = 250" _legacy "${_legacy}")
string(REPLACE "max_source_age_ms = 50" "max_source_age_ms = 250" _legacy "${_legacy}")
set(_fixture "${CMAKE_CURRENT_BINARY_DIR}/runtime-inspection-legacy.toml")
file(WRITE "${_fixture}" "${_legacy}")
execute_process(COMMAND "${RUNTIME}" --config "${_fixture}" --dump-effective-config
    RESULT_VARIABLE _status OUTPUT_VARIABLE _output ERROR_VARIABLE _error TIMEOUT 15)
if(NOT _status EQUAL 0 OR NOT _output MATCHES "# effective-config complete")
    message(FATAL_ERROR "Legacy configuration inspection failed: ${_status}; ${_error}")
endif()
foreach(_expected_line IN ITEMS
    "runtime.vision.gpu_service_enabled=1 source=live-policy"
    "gamepad.tracker.max_observation_age_ms=40 source=live-policy"
    "gamepad.auto_fire.max_source_age_ms=40 source=live-policy")
    string(FIND "${_output}" "${_expected_line}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR "Effective configuration does not report live policy: ${_expected_line}")
    endif()
endforeach()
