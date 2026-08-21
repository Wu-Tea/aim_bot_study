#include "test_support/native_test_registry.h"

void register_viewport_controller_tests(native_test::Registry&);
void register_recoil_reducer_tests(native_test::Registry&);
void register_recoil_contract_tests(native_test::Registry&);
void register_weapon_recognizer_tests(native_test::Registry&);
void register_auto_fire_gate_tests(native_test::Registry&);
void register_runtime_telemetry_tests(native_test::Registry&);
void register_log_session_manager_tests(native_test::Registry&);
void register_benchmark_metrics_tests(native_test::Registry&);
void register_person_detection_gesture_tests(native_test::Registry&);
void register_runtime_telemetry_production_shape_tests(native_test::Registry&);
void register_telemetry_target_identity_tests(native_test::Registry&);
void register_telemetry_event_sampler_tests(native_test::Registry&);
void register_runtime_provenance_tests(native_test::Registry&);
void register_ads_visual_transition_tests(native_test::Registry&);
void register_ads_transition_collector_tests(native_test::Registry&);
void register_telemetry_collectors_tests(native_test::Registry&);

int main(int argc, char** argv) {
    native_test::Registry registry;
    register_viewport_controller_tests(registry);
    register_recoil_reducer_tests(registry);
    register_recoil_contract_tests(registry);
    register_weapon_recognizer_tests(registry);
    register_auto_fire_gate_tests(registry);
    register_runtime_telemetry_tests(registry);
    register_log_session_manager_tests(registry);
    register_benchmark_metrics_tests(registry);
    register_person_detection_gesture_tests(registry);
    register_runtime_telemetry_production_shape_tests(registry);
    register_telemetry_target_identity_tests(registry);
    register_telemetry_event_sampler_tests(registry);
    register_runtime_provenance_tests(registry);
    register_ads_visual_transition_tests(registry);
    register_ads_transition_collector_tests(registry);
    register_telemetry_collectors_tests(registry);
    return native_test::run(registry, argc, argv, "NativeFunctionalTests");
}
