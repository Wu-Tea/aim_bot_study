#include "legacy_projection_tracker.h"

namespace tracking_native {

LegacyProjectionTracker::LegacyProjectionTracker(
    controller_native::NativeTargetTrackerConfig config)
    : inner_(config) {}

void LegacyProjectionTracker::reset() {
    inner_.reset();
    last_aim_error_px_ = {};
    last_body_box_px_ = {};
    last_has_body_box_ = false;
}

void LegacyProjectionTracker::ingest(const TrackerObservation& observation) {
    controller_native::NativeTargetTrackerObservation native_observation;
    native_observation.has_target = observation.has_target;
    native_observation.dx = observation.aim_error_px.x;
    native_observation.dy = observation.aim_error_px.y;
    native_observation.target_tier = observation.target_tier;
    native_observation.observed_at_seconds = observation.capture_time.value;
    inner_.update_observation(native_observation);

    if (!observation.has_target) {
        last_aim_error_px_ = {};
        last_body_box_px_ = {};
        last_has_body_box_ = false;
        return;
    }

    last_aim_error_px_ = observation.aim_error_px;
    last_body_box_px_ = observation.body_box_px;
    last_has_body_box_ = observation.has_body_box;
}

void LegacyProjectionTracker::push_control_sample(const TrackerControlSample& sample) {
    inner_.record_output(
        sample.sticks.final_output.x,
        sample.sticks.final_output.y,
        sample.dt.value);
}

TrackerSnapshot LegacyProjectionTracker::query(const TrackerQuery& query) const {
    TrackerSnapshot snapshot;
    const auto projection = inner_.project(query.query_time.value);
    if (!projection) {
        return snapshot;
    }

    snapshot.has_target = true;
    snapshot.source = TrackerSnapshotSource::Projected;
    snapshot.aim_error_px = {projection->dx, projection->dy};
    snapshot.observed_at = {projection->observed_at_seconds};
    snapshot.projection_age_ms =
        (query.query_time.value - projection->observed_at_seconds) * 1000.0;
    snapshot.assist_authority = common_native::AssistAuthority::AimCoast;
    snapshot.fire_authority = common_native::FireAuthority::None;

    if (last_has_body_box_) {
        const float shift_x = projection->dx - last_aim_error_px_.x;
        const float shift_y = projection->dy - last_aim_error_px_.y;
        snapshot.has_body_box = true;
        snapshot.body_box_px = {
            last_body_box_px_.x + shift_x,
            last_body_box_px_.y + shift_y,
            last_body_box_px_.w,
            last_body_box_px_.h};
    }

    return snapshot;
}

std::vector<TrackerDebugTrack> LegacyProjectionTracker::debug_tracks() const {
    return {};
}

}  // namespace tracking_native
