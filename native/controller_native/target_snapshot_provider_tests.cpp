#include "target_snapshot_provider.h"

#include <stdexcept>

namespace {

void require_true(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_near(float actual, float expected, float tolerance, const char* message) {
    const float delta = actual > expected ? actual - expected : expected - actual;
    if (delta > tolerance) {
        throw std::runtime_error(message);
    }
}

controller_native::ControllerVisionSnapshot snapshot_with_target(float dx, double now_seconds) {
    controller_native::ControllerVisionSnapshot snapshot;
    snapshot.frame_updated = true;
    snapshot.frame_id = 10;
    snapshot.capture_time_seconds = now_seconds;
    snapshot.ready_time_seconds = now_seconds;
    snapshot.state.has_target = true;
    snapshot.state.aim_authority = true;
    snapshot.state.fire_authority = true;
    snapshot.state.dx = dx;
    snapshot.state.dy = 0.0f;
    snapshot.state.screen_center_x = 320.0f;
    snapshot.state.screen_center_y = 256.0f;
    snapshot.state.target_tier = "strong";
    snapshot.state.observed_at_seconds = now_seconds;
    return snapshot;
}

void test_provider_preserves_latest_target_on_no_update_snapshot() {
    controller_native::GamepadAiAimConfig ai_config;
    ai_config.target_max_age_ms = 0.0f;
    ai_config.target_projection_max_age_ms = 50.0f;
    controller_native::TargetSnapshotProvider provider(
        ai_config,
        tracking_native::TrackerBackendKind::LegacyProjection);

    provider.submit_vision_snapshot(snapshot_with_target(42.0f, 10.0), 10.0, false);
    controller_native::NativeControllerVisionState state =
        provider.vision_state_for_frame(10.0, false);
    require_true(state.has_target, "provider should expose submitted target");
    require_near(state.dx, 42.0f, 0.001f, "provider should preserve submitted dx");

    controller_native::ControllerVisionSnapshot no_update;
    no_update.frame_updated = false;
    no_update.state.has_target = false;
    provider.submit_vision_snapshot(no_update, 10.010, false);

    state = provider.vision_state_for_frame(10.010, false);
    require_true(state.has_target, "provider should preserve target across no-update snapshot");
    require_near(state.dx, 42.0f, 0.001f, "provider should preserve dx across no-update snapshot");
}

}  // namespace

int main() {
    test_provider_preserves_latest_target_on_no_update_snapshot();
    return 0;
}
