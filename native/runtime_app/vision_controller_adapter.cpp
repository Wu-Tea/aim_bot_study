#include "vision_controller_adapter.h"

#include "controller_native/target_geometry.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

namespace runtime_app {

namespace {

double ns_to_seconds(std::uint64_t ns) {
    return static_cast<double>(ns) / 1'000'000'000.0;
}

std::string safe_c_string(const char* value, const char* fallback) {
    if (value == nullptr || value[0] == '\0') {
        return std::string(fallback);
    }
    return std::string(value);
}

std::uint64_t tracker_detection_id(std::uint64_t frame_id, std::size_t index) {
    return ((frame_id & 0xffffffffull) << 32ull) |
        static_cast<std::uint64_t>(index + 1u);
}

bool detection_has_enemy_evidence(const vision_native::Detection& detection) {
    return detection.has_cue_point || detection.color_bonus > 0.0f;
}

bool detection_is_wide_low(const vision_native::Detection& detection) {
    const float width = std::max(0.0f, detection.x2 - detection.x1);
    const float height = std::max(0.0f, detection.y2 - detection.y1);
    if (width <= 0.0f) {
        return false;
    }
    return (height / width) < 0.65f;
}

bool detection_is_color_checked_wide_low_without_enemy_evidence(
    const vision_native::Detection& detection) {
    return detection.color_classified
        && detection_is_wide_low(detection)
        && !detection_has_enemy_evidence(detection);
}

std::string tracker_tier_for_detection(const vision_native::Detection& detection) {
    if (detection_is_color_checked_wide_low_without_enemy_evidence(detection)) {
        return "associated_weak";
    }
    if (detection.conf < 0.40f && detection.color_bonus <= 0.0f) {
        return "associated_weak";
    }
    return "observed_strong";
}

common_native::TargetAuthorityState suggested_candidate_authority_state(
    const vision_native::Detection& detection) {
    if (detection.is_friendly) {
        return common_native::TargetAuthorityState::Reject;
    }
    if (tracker_tier_for_detection(detection) == "observed_strong") {
        return common_native::TargetAuthorityState::StrongAssist;
    }
    return common_native::TargetAuthorityState::WeakAssist;
}

pipeline_contract::VisionCandidateSnapshot candidate_snapshot_from_detection(
    const vision_native::Detection& detection,
    std::uint64_t candidate_id,
    float width,
    float height) {
    pipeline_contract::VisionCandidateSnapshot candidate;
    candidate.id = candidate_id;
    candidate.valid = true;
    candidate.body_box_px = {detection.x1, detection.y1, width, height};
    candidate.aim_point_px = {
        (detection.x1 + detection.x2) * 0.5f,
        detection.y1 + (height * 0.40f)};
    candidate.has_aim_point = true;
    candidate.confidence =
        std::max(0.0f, std::min(1.0f, detection.conf + detection.color_bonus));
    candidate.class_id = detection.class_id;
    candidate.is_friendly = detection.is_friendly;
    candidate.color_classified = detection.color_classified;
    candidate.color_bonus = detection.color_bonus;
    candidate.has_cue_point = detection.has_cue_point;
    candidate.cue_point_px = {detection.cue_x, detection.cue_y};
    candidate.cue_score = detection.cue_score;
    candidate.has_motion_anchor = detection.has_motion_anchor;
    candidate.motion_anchor_px = {
        detection.motion_anchor_x, detection.motion_anchor_y};
    candidate.motion_anchor_score = detection.motion_anchor_score;
    candidate.suggested_authority_state = suggested_candidate_authority_state(detection);
    return candidate;
}

}  // namespace

controller_native::ControllerVisionSnapshot adapt_vision_result(
    const vision_native::VisionResult& result) {
    controller_native::ControllerVisionSnapshot snapshot;
    snapshot.frame_updated = result.frame_updated;
    if (!result.frame_updated) {
        return snapshot;
    }

    snapshot.frame_id = result.frame_id;
    snapshot.selector_identity_protocol = result.selector_identity_protocol;
    snapshot.user_intent = result.user_aim_intent;
    snapshot.capture_time_seconds = ns_to_seconds(
        result.captured_at_ns != 0 ? result.captured_at_ns : result.result_at_ns);
    snapshot.ready_time_seconds = ns_to_seconds(
        result.result_at_ns != 0 ? result.result_at_ns : result.captured_at_ns);
    if (result.has_selected_detection &&
        result.selected_detection_index < result.detections.size()) {
        snapshot.selected_observation_id = tracker_detection_id(
            result.frame_id,
            result.selected_detection_index);
    }

    snapshot.candidates.reserve(result.detections.size());
    snapshot.tracker_detections.reserve(result.detections.size());
    for (std::size_t index = 0; index < result.detections.size(); ++index) {
        const vision_native::Detection& detection = result.detections[index];
        const float width = std::max(0.0f, detection.x2 - detection.x1);
        const float height = std::max(0.0f, detection.y2 - detection.y1);
        if (width <= 1.0f || height <= 1.0f) {
            continue;
        }

        const std::uint64_t candidate_id = tracker_detection_id(result.frame_id, index);
        snapshot.candidates.push_back(candidate_snapshot_from_detection(
            detection,
            candidate_id,
            width,
            height));

        tracking_native::TrackerDetection tracker_detection;
        tracker_detection.id = candidate_id;
        tracker_detection.body_box_px = {detection.x1, detection.y1, width, height};
        tracker_detection.aim_point_px = {
            (detection.x1 + detection.x2) * 0.5f,
            detection.y1 + (height * 0.40f)};
        tracker_detection.has_aim_point = true;
        tracker_detection.confidence =
            std::max(0.0f, std::min(1.0f, detection.conf + detection.color_bonus));
        tracker_detection.class_id = detection.class_id;
        tracker_detection.target_tier = tracker_tier_for_detection(detection);
        tracker_detection.is_friendly = detection.is_friendly;
        snapshot.tracker_detections.push_back(std::move(tracker_detection));
    }

    controller_native::NativeControllerVisionState state;
    state.has_target = result.has_target;
    state.auto_fire_requested = result.auto_fire;
    state.dx = result.dx;
    state.dy = result.dy;
    state.target_x = result.target_x;
    state.target_y = result.target_y;
    state.screen_center_x = result.screen_center_x;
    state.screen_center_y = result.screen_center_y;
    state.has_body_box = result.has_body_box;
    state.body_x1 = result.body_x1;
    state.body_y1 = result.body_y1;
    state.body_x2 = result.body_x2;
    state.body_y2 = result.body_y2;
    state.aim_authority = result.aim_authority;
    state.fire_authority = result.fire_authority;
    state.target_tier = safe_c_string(result.target_tier, "none");
    state.observed_at_seconds = ns_to_seconds(
        result.result_at_ns != 0 ? result.result_at_ns : result.captured_at_ns);
    snapshot.state = std::move(state);
    return snapshot;
}

pipeline_contract::CommittedCaptureObservation
adapt_committed_capture_observation(
    const vision_native::VisionResult& result,
    const pipeline_contract::TargetPlan& committed_plan,
    float aim_height_ratio,
    std::uint64_t ads_epoch) {
    pipeline_contract::CommittedCaptureObservation committed;
    if (!result.frame_updated || result.frame_id == 0 ||
        result.frame_id != committed_plan.source_frame_id ||
        committed_plan.source_observation_id == 0 ||
        committed_plan.target_id == 0 || result.captured_at_ns == 0 ||
        result.result_at_ns < result.captured_at_ns) {
        return committed;
    }

    const vision_native::Detection* selected = nullptr;
    std::uint16_t eligible_count = 0;
    for (std::size_t index = 0; index < result.detections.size(); ++index) {
        const auto& detection = result.detections[index];
        const float width = std::max(0.0f, detection.x2 - detection.x1);
        const float height = std::max(0.0f, detection.y2 - detection.y1);
        if (width <= 1.0f || height <= 1.0f || detection.is_friendly) {
            continue;
        }
        const float confidence = std::clamp(
            detection.conf + detection.color_bonus, 0.0f, 1.0f);
        if (confidence <= 0.0f) continue;
        if (eligible_count != UINT16_MAX) ++eligible_count;
        if (tracker_detection_id(result.frame_id, index) ==
            committed_plan.source_observation_id) {
            selected = &detection;
        }
    }
    if (selected == nullptr || eligible_count == 0) return committed;

    const float width = std::max(0.0f, selected->x2 - selected->x1);
    const float height = std::max(0.0f, selected->y2 - selected->y1);
    const float center_x = result.screen_center_x;
    const float center_y = result.screen_center_y;
    if (!std::isfinite(center_x) || !std::isfinite(center_y) ||
        center_x <= 0.0f || center_y <= 0.0f) {
        return committed;
    }
    const common_native::Box2f body_box{
        selected->x1, selected->y1, width, height};
    const common_native::Vec2f raw_aim{
        (selected->x1 + selected->x2) * 0.5f,
        selected->y1 + height * 0.40f};
    const auto geometry = controller_native::resolve_target_geometry(
        {raw_aim, body_box, true}, {aim_height_ratio});
    const float frame_height = center_y * 2.0f;
    const float normalized_size = std::clamp(
        height / std::max(1.0f, frame_height), 0.0f, 1.0f);
    const float confidence = std::clamp(
        selected->conf + selected->color_bonus, 0.0f, 1.0f);
    const float size_weight = std::clamp(
        normalized_size / 0.12f, 0.2f, 1.0f);

    committed.source_frame_id = result.frame_id;
    committed.source_observation_id = committed_plan.source_observation_id;
    committed.persistent_target_id = committed_plan.target_id;
    committed.viewport_sequence = result.viewport_sequence;
    committed.viewport_source_frame_id = result.frame_id;
    committed.captured_at_ns = result.captured_at_ns;
    committed.result_at_ns = result.result_at_ns;
    committed.stable_error_px = {
        geometry.aim_px.x - center_x,
        geometry.aim_px.y - center_y};
    committed.stable_body_size_px = {width, height};
    committed.raw_body_box_px = body_box;
    committed.motion_anchor_px = {
        selected->motion_anchor_x, selected->motion_anchor_y};
    committed.motion_anchor_score = std::clamp(
        selected->motion_anchor_score, 0.0f, 1.0f);
    committed.has_motion_anchor = selected->has_motion_anchor;
    committed.viewport_offset_px = {
        static_cast<float>(result.viewport_left),
        static_cast<float>(result.viewport_top)};
    committed.target_acceleration_px_per_sec2 = {};
    committed.reliability = confidence * size_weight;
    committed.normalized_size = normalized_size;
    committed.lifecycle = committed_plan.lifecycle;
    committed.motion = committed_plan.motion;
    committed.mode = committed_plan.mode;
    committed.ads_epoch = ads_epoch;
    committed.eligible_candidate_count = eligible_count;
    committed.fresh_observed = true;
    committed.strong_observation =
        tracker_tier_for_detection(*selected) == "observed_strong";
    committed.stable_coordinates_valid = geometry.geometry_resolved;
    committed.reused_or_projected = false;
    return committed;
}

}  // namespace runtime_app
