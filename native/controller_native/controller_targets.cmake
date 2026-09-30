# Included by native/CMakeLists.txt; source paths are relative to native/.

# The production controller has one current target-plan path.  Historical
# trackers, predictive bridges, causal-memory controllers, intent fusers and
# shadow gates are intentionally absent from this source set.
add_library(controller_native_core STATIC
    tracking_native/tracker_authority.cpp
    controller_native/runtime_config.cpp
    controller_native/auto_fire_gate.cpp
    controller_native/output_diagnostics.cpp
    controller_native/output_composer.cpp
    controller_native/native_gamepad_controller.cpp
    controller_native/target_geometry.cpp
    controller_native/target_state_reducers.cpp
    controller_native/ads_lifecycle_reducer.cpp
    controller_native/ads_reacquisition_reducer.cpp
    controller_native/recoil_reducer.cpp
    controller_native/intent_filter.cpp
    controller_native/assist_control_state_machine.cpp
    controller_native/operation_intent.cpp
    controller_native/target_coordinator.cpp
    controller_native/ads_acquisition_controller.cpp
    controller_native/bodylock_follow_controller.cpp
    controller_native/bodylock_target_motion_observer.cpp
    controller_native/response_model_aim_solver.cpp
    controller_native/aim_response_estimator.cpp
    controller_native/ads_response_estimator.cpp
    controller_native/aim_dynamics_shaper.cpp)
target_include_directories(controller_native_core PUBLIC
    ${PROJECT_SOURCE_DIR}/vision_native/include
    ${PROJECT_SOURCE_DIR})
target_compile_definitions(controller_native_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(controller_native_core PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
target_link_libraries(controller_native_core PUBLIC windowsapp)

# Retired file-backed recoil tools are isolated from the production link graph.
add_library(recoil_profile_tools STATIC
    controller_native/recoil_profile.cpp
    controller_native/recoil_calibration.cpp
    controller_native/recoil_compensation.cpp
    recoil_native/recoil_compensation.cpp
    recoil_native/recoil_visual_model.cpp)
cod_native_defaults(recoil_profile_tools)
