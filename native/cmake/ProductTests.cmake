# Included by native/CMakeLists.txt; source paths are relative to native/.

# Native product tests are grouped by ownership into two physical runners.
# Each source contributes named cases through the shared test registry below.
add_executable(cod_native_base_tests
    controller_native/state_machine_replay_tests.cpp
    controller_native/state_machine_contract_tests.cpp
    test_support/base_tests_main.cpp
    controller_native/ads_bodylock_september_incident_tests.cpp
    controller_native/runtime_config_tests.cpp
    pipeline_contract/target_plan_contract_tests.cpp
    pipeline_contract/committed_capture_observation_tests.cpp
    controller_native/input_edge_reducer_tests.cpp
    controller_native/aim_scope_reducer_tests.cpp
    controller_native/intent_filter_tests.cpp
    controller_native/operation_intent_tests.cpp
    controller_native/target_state_reducers_tests.cpp
    controller_native/ads_lifecycle_reducer_tests.cpp
    controller_native/ads_reacquisition_reducer_tests.cpp
    controller_native/control_pipeline_primitives_tests.cpp
    controller_native/target_pipeline_integration_tests.cpp
    controller_native/target_coordinator_tests.cpp
    controller_native/ads_acquisition_controller_tests.cpp
    controller_native/bodylock_follow_controller_tests.cpp
    controller_native/bodylock_target_motion_observer_tests.cpp
    controller_native/aim_dynamics_shaper_tests.cpp
    controller_native/aim_response_estimator_tests.cpp
    controller_native/startup_response_prior_tests.cpp
    controller_native/touchpad_fire_tests.cpp
    controller_native/response_model_aim_solver_tests.cpp
    controller_native/target_geometry_tests.cpp
    vision_native/src/build_family_tests.cpp
    vision_native/src/resize_contract_tests.cpp
    vision_native/src/target_selector_tests.cpp
    vision_native/src/marker_loss_acquisition_incident_tests.cpp
    vision_native/src/visible_selection_contract_tests.cpp
    vision_native/src/visible_selection_pipeline_tests.cpp
    vision_native/src/color_readback_tests.cpp
    controller_native/io_recovery_policy_tests.cpp
    controller_native/io_recovery_policy.cpp
    controller_native/controller_protocol_tests.cpp
    controller_native/ds4_output_report_tests.cpp
    controller_native/sdl_gamepad_reader_tests.cpp
    controller_native/sdl_gamepad_reader.cpp
    runtime_app/vision_service_tests.cpp
    runtime_app/vision_service.cpp
    runtime_app/runtime_timing_tests.cpp
    runtime_app/runtime_timing.cpp
    controller_native/ads_snap_deadline_release_incident_regression.cpp
    controller_native/acquisition_gesture_purpose_incident_regression.cpp
    controller_native/ads_center_cross_late_exit_incident_regression.cpp
    controller_native/ads_completion_geometry_continuity_incident_regression.cpp
    controller_native/ads_incomplete_acquisition_safety_incident_regression.cpp
    controller_native/ads_close_acquisition_pacing_incident_regression.cpp
    controller_native/ads_initial_scope_existing_target_incident_regression.cpp
    controller_native/ads_scope_ready_authority_incident_regression.cpp
    controller_native/ads_single_press_token_incident_regression.cpp
    controller_native/ads_snap_core_contract_incident_regression.cpp
    controller_native/ads_cross_epoch_pickup_revalidation_incident_regression.cpp
    controller_native/bodylock_high_frequency_incident_regression.cpp
    controller_native/bodylock_position_motion_conflict_incident_regression.cpp
    controller_native/bodylock_pov_fire_cue_continuity_incident_regression.cpp
    controller_native/bodylock_response_coordinate_incident_regression.cpp
    controller_native/bodylock_target_direction_latency_incident_regression.cpp
    controller_native/bodylock_target_motion_total_incident_regression.cpp
    controller_native/close_cue_hold_authority_incident_regression.cpp
    controller_native/controller_product_contract_incident_regression.cpp
    controller_native/far_selected_person_ads_admission_incident_regression.cpp
    controller_native/manual_ai_arbitration_continuity_incident_regression.cpp
    controller_native/manual_residual_authority_incident_regression.cpp
    controller_native/neutral_drift_arbitration_incident_regression.cpp
    controller_native/vertical_correction_release_incident_regression.cpp
    vision_native/src/ads_dynamic_pickup_roi_incident_regression.cpp
    runtime_app/vision_controller_adapter.cpp)
cod_native_defaults(cod_native_base_tests)
if(SDL2_DLL AND EXISTS "${SDL2_DLL}")
    add_custom_command(TARGET cod_native_base_tests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${SDL2_DLL}" "$<TARGET_FILE_DIR:cod_native_base_tests>"
        VERBATIM)
endif()
target_include_directories(
    cod_native_base_tests PRIVATE ${PROJECT_SOURCE_DIR}/vision_native/src)
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
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/..")
endforeach()

add_executable(cod_native_functional_tests
    test_support/functional_tests_main.cpp
    runtime_app/viewport_controller_tests.cpp
    runtime_app/viewport_controller.cpp
    controller_native/recoil_reducer_tests.cpp
    controller_native/recoil_contract_tests.cpp
    controller_native/weapon_recognizer_tests.cpp
    controller_native/weapon_recognizer.cpp
    controller_native/auto_fire_gate_tests.cpp
    runtime_app/runtime_telemetry_tests.cpp
    runtime_app/runtime_telemetry.cpp
    runtime_app/log_session_manager_tests.cpp
    runtime_app/log_session_manager.cpp
    controller_native/benchmark_metrics_tests.cpp
    runtime_app/perf_logger.cpp
    replay_native/replay_metrics.cpp)
target_sources(cod_native_functional_tests PRIVATE
    runtime_app/person_detection_gesture_tests.cpp
    runtime_app/runtime_telemetry_production_shape_tests.cpp
    runtime_app/telemetry_target_identity_tests.cpp
    runtime_app/telemetry_target_identity.cpp
    runtime_app/telemetry_event_sampler_tests.cpp
    runtime_app/telemetry_event_sampler.cpp
    runtime_app/runtime_provenance_tests.cpp
    runtime_app/runtime_provenance.cpp
    runtime_app/ads_visual_transition_tests.cpp
    runtime_app/ads_visual_transition.cpp
    runtime_app/ads_transition_collector_tests.cpp
    runtime_app/ads_transition_collector.cpp
    runtime_app/telemetry_collectors_tests.cpp
    runtime_app/telemetry_collectors.cpp)
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
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/..")
endforeach()

# Keep this incident standalone so its frozen RED executable and current GREEN
# candidate can be built and run explicitly without duplicating a long closed-
# loop matrix inside every normal BaseAds CTest invocation.
add_executable(cod_native_ads_near_target_slowdown_incident_regression
    EXCLUDE_FROM_ALL
    controller_native/ads_near_target_slowdown_incident_regression.cpp)
cod_native_defaults(cod_native_ads_near_target_slowdown_incident_regression)
target_link_libraries(
    cod_native_ads_near_target_slowdown_incident_regression PRIVATE
    controller_native_core)
