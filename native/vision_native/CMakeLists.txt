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

function(cod_native_add_test target test_name)
    add_executable(${target} ${ARGN})
    cod_native_defaults(${target})
    add_test(NAME ${test_name} COMMAND ${target})
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
    ../controller_native/recoil_profile.cpp
    ../controller_native/recoil_calibration.cpp
    ../controller_native/recoil_compensation.cpp
    ../recoil_native/recoil_compensation.cpp
    ../recoil_native/recoil_visual_model.cpp
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
    ../controller_native/aim_dynamics_shaper.cpp)
target_include_directories(controller_native_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(controller_native_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(controller_native_core PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
target_link_libraries(controller_native_core PUBLIC windowsapp)

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
    ../controller_native/weapon_recognizer.cpp
    ../controller_native/virtual_gamepad.cpp
    ../controller_native/io_recovery_policy.cpp
    ../controller_native/xinput_reader.cpp
    ../controller_native/sdl_gamepad_reader.cpp)
cod_native_defaults(cod_native_runtime)
target_compile_definitions(cod_native_runtime PRIVATE
    COD_BUILD_COMMIT="${COD_BUILD_COMMIT}"
    COD_CONTROL_CONTRACT_HASH="${COD_CONTROL_CONTRACT_HASH}"
    COD_CONTROL_ARCHITECTURE_VERSION=3
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
add_executable(vision_native_bgra_benchmark src/bgra_benchmark.cpp)
target_link_libraries(vision_native_bgra_benchmark PRIVATE vision_native_core)

cod_native_add_test(vision_native_build_family_tests VisionBuildFamilyTests
    src/build_family_tests.cpp src/build_family.cpp)
cod_native_add_test(vision_native_resize_contract_tests VisionResizeContractTests
    src/resize_contract_tests.cpp src/resize_contract.cpp)
cod_native_add_test(cod_native_target_selector_tests NativeTargetSelectorTests
    src/target_selector_tests.cpp)
target_link_libraries(cod_native_target_selector_tests PRIVATE vision_native_core)
cod_native_add_test(vision_native_color_readback_tests VisionColorReadbackTests
    src/color_readback_tests.cpp src/color_readback.cpp)
target_include_directories(vision_native_color_readback_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(vision_native_color_readback_tests PRIVATE CUDA::cudart)
add_executable(vision_native_color_readback_benchmark
    src/color_readback_benchmark.cpp src/color_readback.cpp)
cod_native_defaults(vision_native_color_readback_benchmark)
target_include_directories(vision_native_color_readback_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(vision_native_color_readback_benchmark PRIVATE CUDA::cudart)

cod_native_add_test(cod_native_io_recovery_policy_tests NativeIoRecoveryPolicyTests
    ../controller_native/io_recovery_policy_tests.cpp
    ../controller_native/io_recovery_policy.cpp)
cod_native_add_test(cod_native_controller_protocol_tests NativeControllerProtocolTests
    ../controller_native/controller_protocol_tests.cpp
    ../runtime_app/vision_controller_adapter.cpp)
target_link_libraries(cod_native_controller_protocol_tests PRIVATE controller_native_core)
cod_native_add_test(cod_native_vision_service_tests VisionServiceTests
    ../runtime_app/vision_service_tests.cpp
    ../runtime_app/vision_service.cpp
    ../runtime_app/runtime_timing.cpp)
target_link_libraries(cod_native_vision_service_tests PRIVATE winmm)
cod_native_add_test(cod_native_viewport_controller_tests ViewportControllerTests
    ../runtime_app/viewport_controller_tests.cpp
    ../runtime_app/viewport_controller.cpp)
cod_native_add_test(cod_native_runtime_timing_tests RuntimeTimingTests
    ../runtime_app/runtime_timing_tests.cpp
    ../runtime_app/runtime_timing.cpp)
target_link_libraries(cod_native_runtime_timing_tests PRIVATE winmm)
cod_native_add_test(cod_native_person_detection_gesture_tests PersonDetectionGestureTests
    ../runtime_app/person_detection_gesture_tests.cpp)
add_executable(cod_native_scheduler_benchmark
    ../runtime_app/scheduler_benchmark.cpp
    ../runtime_app/runtime_timing.cpp)
cod_native_defaults(cod_native_scheduler_benchmark)
target_link_libraries(cod_native_scheduler_benchmark PRIVATE winmm)

cod_native_add_test(cod_native_runtime_telemetry_tests RuntimeTelemetryTests
    ../runtime_app/runtime_telemetry_tests.cpp
    ../runtime_app/runtime_telemetry.cpp)
cod_native_add_test(cod_native_runtime_telemetry_production_shape_tests RuntimeTelemetryProductionShapeTests
    ../runtime_app/runtime_telemetry_production_shape_tests.cpp
    ../runtime_app/runtime_telemetry.cpp)
cod_native_add_test(cod_native_telemetry_target_identity_tests TelemetryTargetIdentityTests
    ../runtime_app/telemetry_target_identity_tests.cpp
    ../runtime_app/telemetry_target_identity.cpp)
cod_native_add_test(cod_native_telemetry_event_sampler_tests TelemetryEventSamplerTests
    ../runtime_app/telemetry_event_sampler_tests.cpp
    ../runtime_app/telemetry_event_sampler.cpp)
cod_native_add_test(cod_native_runtime_provenance_tests RuntimeProvenanceTests
    ../runtime_app/runtime_provenance_tests.cpp
    ../runtime_app/runtime_provenance.cpp)
target_link_libraries(cod_native_runtime_provenance_tests PRIVATE advapi32)
cod_native_add_test(cod_native_ads_visual_transition_tests AdsVisualTransitionTests
    ../runtime_app/ads_visual_transition_tests.cpp
    ../runtime_app/ads_visual_transition.cpp)
cod_native_add_test(cod_native_ads_transition_collector_tests AdsTransitionCollectorTests
    ../runtime_app/ads_transition_collector_tests.cpp
    ../runtime_app/ads_transition_collector.cpp
    ../runtime_app/ads_visual_transition.cpp)
cod_native_add_test(cod_native_telemetry_collectors_tests TelemetryCollectorsTests
    ../runtime_app/telemetry_collectors_tests.cpp
    ../runtime_app/telemetry_collectors.cpp
    ../runtime_app/runtime_telemetry.cpp
    ../runtime_app/telemetry_target_identity.cpp
    ../runtime_app/telemetry_event_sampler.cpp
    ../runtime_app/ads_visual_transition.cpp
    ../runtime_app/ads_transition_collector.cpp)
cod_native_add_test(cod_native_log_session_manager_tests LogSessionManagerTests
    ../runtime_app/log_session_manager_tests.cpp
    ../runtime_app/log_session_manager.cpp)

cod_native_add_test(cod_native_runtime_config_tests RuntimeConfigTests
    ../controller_native/runtime_config_tests.cpp)
target_link_libraries(cod_native_runtime_config_tests PRIVATE controller_native_core)
set_tests_properties(RuntimeConfigTests PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../..")
cod_native_add_test(cod_native_target_plan_contract_tests NativeTargetPlanContractTests
    ../pipeline_contract/target_plan_contract_tests.cpp)
cod_native_add_test(cod_native_committed_capture_observation_tests CommittedCaptureObservationTests
    ../pipeline_contract/committed_capture_observation_tests.cpp
    ../runtime_app/vision_controller_adapter.cpp)
target_link_libraries(cod_native_committed_capture_observation_tests PRIVATE controller_native_core)

foreach(_unit IN ITEMS
    input_edge_reducer
    aim_scope_reducer
    intent_filter
    operation_intent
    target_coordinator
    ads_lifecycle_reducer
    ads_reacquisition_reducer
    ads_acquisition_controller
    bodylock_follow_controller
    bodylock_target_motion_observer
    aim_dynamics_shaper
    aim_response_estimator
    response_model_aim_solver
    target_geometry
    target_state_reducers
    recoil_reducer
    auto_fire_gate)
    string(REPLACE "_" "" _unit_compact "${_unit}")
    set(_target "cod_native_${_unit}_tests")
    add_executable(${_target} ../controller_native/${_unit}_tests.cpp)
    cod_native_defaults(${_target})
    target_link_libraries(${_target} PRIVATE controller_native_core)
    add_test(NAME "Native${_unit_compact}Tests" COMMAND ${_target})
endforeach()

# Product pacing owner for the close-range ADS response inherited from the
# retired Direct experiment. Close acquisition must remain faster than the
# nominal mid/far path without becoming an abrupt near-instant transfer.
add_executable(cod_native_ads_close_acquisition_pacing_incident_regression
    ../controller_native/ads_close_acquisition_pacing_incident_regression.cpp)
cod_native_defaults(
    cod_native_ads_close_acquisition_pacing_incident_regression)
target_link_libraries(
    cod_native_ads_close_acquisition_pacing_incident_regression PRIVATE
    controller_native_core)
add_test(
    NAME NativeAdsCloseAcquisitionPacingIncidentRegression
    COMMAND cod_native_ads_close_acquisition_pacing_incident_regression
        --output
        "${CMAKE_CURRENT_BINARY_DIR}/ads_close_acquisition_pacing_incident_ctest.json")
set_tests_properties(
    NativeAdsCloseAcquisitionPacingIncidentRegression
    PROPERTIES LABELS "controller;incident-regression")

cod_native_add_test(
    cod_native_control_pipeline_primitives_tests
    ControlPipelinePrimitivesTests
    ../controller_native/control_pipeline_primitives_tests.cpp)
target_link_libraries(
    cod_native_control_pipeline_primitives_tests PRIVATE controller_native_core)

cod_native_add_test(cod_native_recoil_contract_tests NativeRecoilContractTests
    ../controller_native/recoil_contract_tests.cpp)
target_link_libraries(cod_native_recoil_contract_tests PRIVATE controller_native_core)
cod_native_add_test(cod_native_weapon_recognizer_tests NativeWeaponRecognizerTests
    ../controller_native/weapon_recognizer_tests.cpp
    ../controller_native/weapon_recognizer.cpp)
target_link_libraries(cod_native_weapon_recognizer_tests PRIVATE controller_native_core)
cod_native_add_test(cod_native_benchmark_metrics_tests NativeBenchmarkMetricsTests
    ../controller_native/benchmark_metrics_tests.cpp
    ../runtime_app/perf_logger.cpp
    ../replay_native/replay_metrics.cpp)
target_link_libraries(cod_native_benchmark_metrics_tests PRIVATE controller_native_core)

cod_native_add_test(cod_native_controller_tests NativeControllerIntegrationTests
    ../controller_native/target_pipeline_integration_tests.cpp
    ../runtime_app/vision_controller_adapter.cpp)
target_link_libraries(cod_native_controller_tests PRIVATE
    controller_native_core vision_native_core gdi32 windowsapp)

# Fixed Phase-0 incident fixture: preserve the original symptom oracles as a
# permanent regression gate now that the production runtime is GREEN.
add_executable(cod_native_vertical_correction_release_incident_regression
    ../controller_native/vertical_correction_release_incident_regression.cpp)
cod_native_defaults(cod_native_vertical_correction_release_incident_regression)
target_link_libraries(
    cod_native_vertical_correction_release_incident_regression PRIVATE
    controller_native_core)
add_test(
    NAME NativeVerticalCorrectionReleaseIncidentRegression
    COMMAND cod_native_vertical_correction_release_incident_regression
        --output
        "${CMAKE_CURRENT_BINARY_DIR}/vertical_correction_release_incident_ctest.json")
set_tests_properties(
    NativeVerticalCorrectionReleaseIncidentRegression
    PROPERTIES LABELS "controller;incident-regression")

# Live schema-15 incident fixture: same-direction micro input must retain
# target assistance and move D continuously. Opposing output is mode-specific:
# target-first in ADS, cooperative manual authority in BodyLock.
add_executable(cod_native_manual_residual_authority_incident_regression
    ../controller_native/manual_residual_authority_incident_regression.cpp)
cod_native_defaults(cod_native_manual_residual_authority_incident_regression)
target_link_libraries(
    cod_native_manual_residual_authority_incident_regression PRIVATE
    controller_native_core)
add_test(
    NAME NativeManualResidualAuthorityIncidentRegression
    COMMAND cod_native_manual_residual_authority_incident_regression
        --output
        "${CMAKE_CURRENT_BINARY_DIR}/manual_residual_authority_incident_ctest.json")
set_tests_properties(
    NativeManualResidualAuthorityIncidentRegression
    PROPERTIES LABELS "controller;incident-regression")

# Live schema-18 incident fixture: a dominant-axis positioning gesture exposes
# its small cross-axis bias on the first sample. Arbitration must classify that
# conflict immediately and must not wait for filtered manual to hit zero before
# flipping the final axis. The explicit single-axis escape counterfactual keeps
# intentional manual handover behavior distinct from cross-axis coupling.
add_executable(cod_native_manual_ai_arbitration_continuity_incident_regression
    ../controller_native/manual_ai_arbitration_continuity_incident_regression.cpp)
cod_native_defaults(cod_native_manual_ai_arbitration_continuity_incident_regression)
target_link_libraries(
    cod_native_manual_ai_arbitration_continuity_incident_regression PRIVATE
    controller_native_core)
add_test(
    NAME NativeManualAiArbitrationContinuityIncidentRegression
    COMMAND cod_native_manual_ai_arbitration_continuity_incident_regression
        --output
        "${CMAKE_CURRENT_BINARY_DIR}/manual_ai_arbitration_continuity_incident_ctest.json")
set_tests_properties(
    NativeManualAiArbitrationContinuityIncidentRegression
    PROPERTIES LABELS "controller;incident-regression")

# Live schema-18 incident fixture: a stable physical right-stick offset that
# the production IntentFilter has classified as neutral must not veto a
# material opposing AI correction. A deliberate large movement in the same
# direction remains a counterfactual manual-ownership gate.
add_executable(cod_native_neutral_drift_arbitration_incident_regression
    ../controller_native/neutral_drift_arbitration_incident_regression.cpp)
cod_native_defaults(cod_native_neutral_drift_arbitration_incident_regression)
target_link_libraries(
    cod_native_neutral_drift_arbitration_incident_regression PRIVATE
    controller_native_core)
add_test(
    NAME NativeNeutralDriftArbitrationIncidentRegression
    COMMAND cod_native_neutral_drift_arbitration_incident_regression
        --output
        "${CMAKE_CURRENT_BINARY_DIR}/neutral_drift_arbitration_incident_ctest.json")
set_tests_properties(
    NativeNeutralDriftArbitrationIncidentRegression
    PROPERTIES LABELS "controller;incident-regression")

# Live schema-18 incident fixture: a brief close-target person-box dropout
# retains the same target through current marker evidence, but the BodyLock
# correction fell to the generic cue-hold fraction. The fixed fixture keeps
# far/small cue continuation conservative while close X/Y authority is stable.
add_executable(cod_native_close_cue_hold_authority_incident_regression
    ../controller_native/close_cue_hold_authority_incident_regression.cpp)
cod_native_defaults(cod_native_close_cue_hold_authority_incident_regression)
target_link_libraries(
    cod_native_close_cue_hold_authority_incident_regression PRIVATE
    controller_native_core)
add_test(
    NAME NativeCloseCueHoldAuthorityIncidentRegression
    COMMAND cod_native_close_cue_hold_authority_incident_regression
        --output
        "${CMAKE_CURRENT_BINARY_DIR}/close_cue_hold_authority_incident_ctest.json")
set_tests_properties(
    NativeCloseCueHoldAuthorityIncidentRegression
    PROPERTIES LABELS "controller;incident-regression")

# Live schema-15 incident fixture: a right-stick gesture that began before
# target ownership must not rewrite D on the first acquired target sample.
add_executable(cod_native_acquisition_gesture_purpose_incident_regression
    ../controller_native/acquisition_gesture_purpose_incident_regression.cpp)
cod_native_defaults(cod_native_acquisition_gesture_purpose_incident_regression)
target_link_libraries(
    cod_native_acquisition_gesture_purpose_incident_regression PRIVATE
    controller_native_core)
add_test(
    NAME NativeAcquisitionGesturePurposeIncidentRegression
    COMMAND cod_native_acquisition_gesture_purpose_incident_regression
        --output
        "${CMAKE_CURRENT_BINARY_DIR}/acquisition_gesture_purpose_incident_ctest.json")
set_tests_properties(
    NativeAcquisitionGesturePurposeIncidentRegression
    PROPERTIES LABELS "controller;incident-regression")

# Live schema-18 ADS core incident: a gesture that predates target ownership
# must not dilute the accepted target proposal, and watchdog/crossing evidence
# must not masquerade as a settled handoff to BodyLock. Brief same-generation
# source gaps pause output and resume the same acquisition within the age gate.
add_executable(cod_native_ads_snap_core_contract_incident_regression
    ../controller_native/ads_snap_core_contract_incident_regression.cpp)
cod_native_defaults(cod_native_ads_snap_core_contract_incident_regression)
target_link_libraries(
    cod_native_ads_snap_core_contract_incident_regression PRIVATE
    controller_native_core)
add_test(
    NAME NativeAdsSnapCoreContractIncidentRegression
    COMMAND cod_native_ads_snap_core_contract_incident_regression
        --output
        "${CMAKE_CURRENT_BINARY_DIR}/ads_snap_core_contract_incident_ctest.json")
set_tests_properties(
    NativeAdsSnapCoreContractIncidentRegression
    PROPERTIES LABELS "controller;incident-regression")

# Live schema-18 incident owner: an initial physical LT edge must begin ADS
# acquisition even when Vision selected the person one frame before the edge.
add_executable(cod_native_ads_initial_scope_existing_target_incident_regression
    ../controller_native/ads_initial_scope_existing_target_incident_regression.cpp)
cod_native_defaults(
    cod_native_ads_initial_scope_existing_target_incident_regression)
target_link_libraries(
    cod_native_ads_initial_scope_existing_target_incident_regression PRIVATE
    controller_native_core)
add_test(
    NAME NativeAdsInitialScopeExistingTargetIncidentRegression
    COMMAND cod_native_ads_initial_scope_existing_target_incident_regression
        --output
        "${CMAKE_CURRENT_BINARY_DIR}/ads_initial_scope_existing_target_incident_ctest.json")
set_tests_properties(
    NativeAdsInitialScopeExistingTargetIncidentRegression
    PROPERTIES LABELS "controller;incident-regression")

# Incident fixture: a person already selected by Vision must not be rejected
# a second time solely because its aim point begins far from the crosshair.
add_executable(cod_native_far_selected_person_ads_admission_incident_regression
    ../controller_native/far_selected_person_ads_admission_incident_regression.cpp)
cod_native_defaults(cod_native_far_selected_person_ads_admission_incident_regression)
target_link_libraries(
    cod_native_far_selected_person_ads_admission_incident_regression PRIVATE
    controller_native_core)
add_test(
    NAME NativeFarSelectedPersonAdsAdmissionIncidentRegression
    COMMAND cod_native_far_selected_person_ads_admission_incident_regression
        --output
        "${CMAKE_CURRENT_BINARY_DIR}/far_selected_person_ads_admission_ctest.json")
set_tests_properties(
    NativeFarSelectedPersonAdsAdmissionIncidentRegression
    PROPERTIES LABELS "controller;incident-regression")

# Native incident contract derived from the 2026-08-12 gameplay/video report:
# ADS authority is evidence-invariant and target-first after admission,
# downward fire input stays independent, and BodyLock remains cooperative.
add_executable(cod_native_controller_product_contract_incident_regression
    ../controller_native/controller_product_contract_incident_regression.cpp)
cod_native_defaults(cod_native_controller_product_contract_incident_regression)
target_link_libraries(
    cod_native_controller_product_contract_incident_regression PRIVATE
    controller_native_core)
add_test(
    NAME NativeControllerProductContractIncidentRegression
    COMMAND cod_native_controller_product_contract_incident_regression
        --output
        "${CMAKE_CURRENT_BINARY_DIR}/controller_product_contract_incident_ctest.json")
set_tests_properties(
    NativeControllerProductContractIncidentRegression
    PROPERTIES LABELS "controller;incident-regression")

# Regression owner for the 2026-08-12 Apex stable-target 14-15 Hz BodyLock
# incident. Its primary symptom oracles were frozen while RED; the delay
# scenarios protect the causal fix from one-capture over-fitting.
add_executable(cod_native_bodylock_high_frequency_incident_regression
    ../controller_native/bodylock_high_frequency_incident_regression.cpp)
cod_native_defaults(cod_native_bodylock_high_frequency_incident_regression)
target_link_libraries(
    cod_native_bodylock_high_frequency_incident_regression PRIVATE
    controller_native_core)
add_test(
    NAME NativeBodylockHighFrequencyIncidentRegression
    COMMAND cod_native_bodylock_high_frequency_incident_regression
        --output
        "${CMAKE_CURRENT_BINARY_DIR}/bodylock_high_frequency_incident_ctest.json")
set_tests_properties(
    NativeBodylockHighFrequencyIncidentRegression
    PROPERTIES LABELS "controller;incident-regression")

# Permanent owners for the user-confirmed 2026-08-17 hybrid ADS contract.
add_executable(cod_native_ads_single_press_token_incident_regression
    ../controller_native/ads_single_press_token_incident_regression.cpp)
cod_native_defaults(cod_native_ads_single_press_token_incident_regression)
target_link_libraries(
    cod_native_ads_single_press_token_incident_regression PRIVATE
    controller_native_core)
add_test(
    NAME NativeAdsSinglePressTokenIncidentRegression
    COMMAND cod_native_ads_single_press_token_incident_regression
        --output
        "${CMAKE_CURRENT_BINARY_DIR}/ads_single_press_token_incident_ctest.json")
set_tests_properties(
    NativeAdsSinglePressTokenIncidentRegression
    PROPERTIES LABELS "controller;incident-regression")

# Permanent owner for the 2026-08-17 ADS late-exit incident. A meaningful radial
# center crossing must consume the one-per-LT snap and hand off to BodyLock;
# same-direction, orthogonal, and deadzone motion remain in ADS.
add_executable(cod_native_ads_center_cross_late_exit_incident_regression
    ../controller_native/ads_center_cross_late_exit_incident_regression.cpp)
cod_native_defaults(
    cod_native_ads_center_cross_late_exit_incident_regression)
target_link_libraries(
    cod_native_ads_center_cross_late_exit_incident_regression PRIVATE
    controller_native_core)
add_test(
    NAME NativeAdsCenterCrossLateExitIncidentRegression
    COMMAND cod_native_ads_center_cross_late_exit_incident_regression
        --output
        "${CMAKE_CURRENT_BINARY_DIR}/ads_center_cross_late_exit_incident_ctest.json")
set_tests_properties(
    NativeAdsCenterCrossLateExitIncidentRegression
    PROPERTIES LABELS "controller;incident-regression")

# Schema-18 BodyLock axis-conflict incident: an orthogonal same-direction term
# must not hide a position/motion reversal on the affected axis.
add_executable(cod_native_bodylock_position_motion_conflict_incident_regression
    ../controller_native/bodylock_position_motion_conflict_incident_regression.cpp)
cod_native_defaults(
    cod_native_bodylock_position_motion_conflict_incident_regression)
target_link_libraries(
    cod_native_bodylock_position_motion_conflict_incident_regression PRIVATE
    controller_native_core)
add_test(
    NAME NativeBodylockPositionMotionConflictIncidentRegression
    COMMAND cod_native_bodylock_position_motion_conflict_incident_regression
        --output
        "${CMAKE_CURRENT_BINARY_DIR}/bodylock_position_motion_conflict_incident_ctest.json")
set_tests_properties(
    NativeBodylockPositionMotionConflictIncidentRegression
    PROPERTIES LABELS "controller;incident-regression")

# The target solver has already changed to the current BodyLock direction, but
# final arbitration must not retain stale opposing manual work for 14-27 ms.
add_executable(cod_native_bodylock_target_direction_latency_incident_regression
    ../controller_native/bodylock_target_direction_latency_incident_regression.cpp)
cod_native_defaults(
    cod_native_bodylock_target_direction_latency_incident_regression)
target_link_libraries(
    cod_native_bodylock_target_direction_latency_incident_regression PRIVATE
    controller_native_core)
add_test(
    NAME NativeBodylockTargetDirectionLatencyIncidentRegression
    COMMAND cod_native_bodylock_target_direction_latency_incident_regression
        --output
        "${CMAKE_CURRENT_BINARY_DIR}/bodylock_target_direction_latency_incident_ctest.json")
set_tests_properties(
    NativeBodylockTargetDirectionLatencyIncidentRegression
    PROPERTIES LABELS "controller;incident-regression")

# Frozen production-path owner for the 2026-08-18 sustained-horizontal
# BodyLock incident. It entered as a known RED and is now a permanent gate for
# response, error growth, recovery overshoot and target-generation reset.
add_executable(cod_native_bodylock_target_motion_total_incident_regression
    ../controller_native/bodylock_target_motion_total_incident_regression.cpp)
cod_native_defaults(
    cod_native_bodylock_target_motion_total_incident_regression)
target_link_libraries(
    cod_native_bodylock_target_motion_total_incident_regression PRIVATE
    controller_native_core)
add_test(
    NAME NativeBodylockTargetMotionTotalIncidentRegression
    COMMAND cod_native_bodylock_target_motion_total_incident_regression
        --output
        "${CMAKE_CURRENT_BINARY_DIR}/bodylock_target_motion_total_incident_ctest.json")
set_tests_properties(
    NativeBodylockTargetMotionTotalIncidentRegression
    PROPERTIES LABELS "controller;incident-regression")

cod_native_add_test(cod_native_aimlab_benchmark_tests NativeAimlabBenchmarkTests
    ../controller_native/aimlab_benchmark_tests.cpp
    ../controller_native/aimlab_benchmark.cpp)
target_link_libraries(cod_native_aimlab_benchmark_tests PRIVATE vision_native_core)
cod_native_add_test(cod_native_sustained_aimlab_scenario_tests NativeSustainedAimlabScenarioTests
    ../controller_native/sustained_aimlab_scenario_tests.cpp
    ../controller_native/sustained_aimlab_scenario.cpp)
cod_native_add_test(cod_native_sustained_aimlab_score_tests NativeSustainedAimlabScoreTests
    ../controller_native/sustained_aimlab_score_tests.cpp
    ../controller_native/sustained_aimlab_score.cpp)
cod_native_add_test(cod_native_sustained_aimlab_simulator_tests NativeSustainedAimlabSimulatorTests
    ../controller_native/sustained_aimlab_simulator_tests.cpp
    ../controller_native/sustained_aimlab_simulator.cpp
    ../controller_native/sustained_aimlab_score.cpp
    ../controller_native/sustained_aimlab_scenario.cpp)
cod_native_add_test(cod_native_sustained_aimlab_trace_tests NativeSustainedAimlabTraceTests
    ../controller_native/sustained_aimlab_trace_tests.cpp
    ../controller_native/sustained_aimlab_trace.cpp
    ../controller_native/sustained_aimlab_scenario.cpp)
cod_native_add_test(cod_native_benchmark_physical_input_tests NativeBenchmarkPhysicalInputTests
    ../controller_native/native_benchmark_physical_input_tests.cpp)
cod_native_add_test(cod_native_pid_benchmark_controller_tests NativePidBenchmarkControllerTests
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

add_test(NAME NativeSustainedAimlabLeftStrafeOff
    COMMAND cod_native_sustained_aimlab_benchmark
        --config ${CMAKE_CURRENT_SOURCE_DIR}/../../config.native.example.toml
        --duration-ms 1500 --seed 2026072301
        --profile pure --cohort ads --smoke --left-strafe off)
add_test(NAME NativeSustainedAimlabLeftStrafeBoth
    COMMAND cod_native_sustained_aimlab_benchmark
        --config ${CMAKE_CURRENT_SOURCE_DIR}/../../config.native.example.toml
        --duration-ms 1500 --seed 2026072301
        --profile pure --cohort ads --smoke --left-strafe both)
add_test(NAME NativeSustainedAimlabLeftStrafeRejectsInvalid
    COMMAND cod_native_sustained_aimlab_benchmark
        --config ${CMAKE_CURRENT_SOURCE_DIR}/../../config.native.example.toml
        --duration-ms 100 --seed 2026072301
        --profile pure --cohort ads --left-strafe invalid)
set_tests_properties(NativeSustainedAimlabLeftStrafeRejectsInvalid PROPERTIES WILL_FAIL TRUE)

add_executable(fusion_canvas ../overlay_canvas/fusion_canvas.cpp)
cod_native_defaults(fusion_canvas)
target_link_libraries(fusion_canvas PRIVATE
    fusion_shared d3d11 d2d1 dxgi dcomp dwrite Shcore)

pybind11_add_module(vision_native_cpp src/vision_native_module.cpp)
set_target_properties(vision_native_cpp PROPERTIES OUTPUT_NAME "vision_native_cpp")
target_link_libraries(vision_native_cpp PRIVATE vision_native_core)

set(_runtime_dlls nvinfer_10.dll nvinfer_plugin_10.dll)
foreach(_target vision_native_smoke vision_native_debug vision_native_bgra_benchmark cod_native_runtime vision_native_cpp)
    foreach(_dll IN LISTS _runtime_dlls)
        add_custom_command(TARGET ${_target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${TensorRT_BIN_DIR}/${_dll}"
                "$<TARGET_FILE_DIR:${_target}>/${_dll}"
            VERBATIM)
    endforeach()
endforeach()
