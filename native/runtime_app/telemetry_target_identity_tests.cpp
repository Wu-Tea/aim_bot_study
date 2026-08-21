#include "telemetry_target_identity.h"
#include "test_support/native_test_registry.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool value, int line) {
    if (!value) throw std::runtime_error(
        "target identity assertion failed at line " + std::to_string(line));
}
#define REQUIRE(value) require((value), __LINE__)

runtime_app::TargetIdentityObservation live(
    std::uint64_t frame_id,
    float x1,
    float y1,
    float x2,
    float y2) {
    runtime_app::TargetIdentityObservation value;
    value.frame_id = frame_id;
    value.frame_width = 640;
    value.frame_height = 512;
    value.live = true;
    value.x1 = x1;
    value.y1 = y1;
    value.x2 = x2;
    value.y2 = y2;
    value.target_x = (x1 + x2) * 0.5f;
    value.target_y = y1 + (y2 - y1) * 0.4f;
    return value;
}

void test_stable_live_target_retains_identity() {
    runtime_app::TelemetryTargetIdentity identity;
    const auto first = identity.observe(live(1, 100, 100, 180, 260));
    const auto same = identity.observe(live(2, 103, 101, 183, 261));
    REQUIRE(first.track_id != 0);
    REQUIRE(first.event == runtime_app::TargetEventKind::Created);
    REQUIRE(same.track_id == first.track_id);
    REQUIRE(same.quality == runtime_app::TargetIdentityQuality::StrongGeometricMatch);
    REQUIRE(same.model_eligible());
}

void test_explicit_switch_issues_new_identity() {
    runtime_app::TelemetryTargetIdentity identity;
    const auto first = identity.observe(live(1, 100, 100, 180, 260));
    auto switched = live(2, 400, 100, 480, 260);
    switched.explicit_switch = true;
    const auto result = identity.observe(switched);
    REQUIRE(result.track_id != first.track_id);
    REQUIRE(result.previous_track_id == first.track_id);
    REQUIRE(result.event == runtime_app::TargetEventKind::Switched);
}

void test_incompatible_reacquisition_does_not_reuse_identity() {
    runtime_app::TelemetryTargetIdentity identity;
    const auto first = identity.observe(live(1, 100, 100, 180, 260));
    runtime_app::TargetIdentityObservation missing;
    missing.frame_id = 2;
    missing.frame_width = 640;
    missing.frame_height = 512;
    identity.observe(missing);
    const auto reacquired = identity.observe(live(3, 420, 120, 500, 280));
    REQUIRE(reacquired.track_id != first.track_id);
    REQUIRE(reacquired.event == runtime_app::TargetEventKind::Reacquired);
}

void test_ambiguous_crossing_is_not_model_eligible() {
    runtime_app::TelemetryTargetIdentity identity;
    identity.observe(live(1, 100, 100, 180, 260));
    auto crossing = live(2, 105, 100, 185, 260);
    crossing.association_ambiguous = true;
    const auto result = identity.observe(crossing);
    REQUIRE(result.quality == runtime_app::TargetIdentityQuality::Ambiguous);
    REQUIRE(!result.model_eligible());
}

} // namespace

void register_telemetry_target_identity_tests(native_test::Registry& registry) {
    registry.add_case("FeatureTelemetryAndDiagnostics", "stable_live_target_retains_identity", test_stable_live_target_retains_identity);
    registry.add_case("FeatureTelemetryAndDiagnostics", "explicit_switch_issues_new_identity", test_explicit_switch_issues_new_identity);
    registry.add_case("FeatureTelemetryAndDiagnostics", "incompatible_reacquisition_does_not_reuse_identity", test_incompatible_reacquisition_does_not_reuse_identity);
    registry.add_case("FeatureTelemetryAndDiagnostics", "ambiguous_crossing_not_model_eligible", test_ambiguous_crossing_is_not_model_eligible);
}
