#include "vision_snapshot_adapter.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

namespace controller_native {

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

std::string tracker_tier_for_detection(const vision_native::Detection& detection) {
    if (detection.conf < 0.40f && detection.color_bonus <= 0.0f) {
        return "associated_weak";
    }
    return "observed_strong";
}

}  // namespace

ControllerVisionSnapshot adapt_vision_result(
    const vision_native::VisionResult& result) {
    ControllerVisionSnapshot snapshot;
    snapshot.frame_updated = result.frame_updated;
    if (!result.frame_updated) {
        return snapshot;
    }

    snapshot.frame_id = result.frame_id;
    snapshot.capture_time_seconds = ns_to_seconds(
        result.captured_at_ns != 0 ? result.captured_at_ns : result.result_at_ns);
    snapshot.ready_time_seconds = ns_to_seconds(
        result.result_at_ns != 0 ? result.result_at_ns : result.captured_at_ns);

    snapshot.tracker_detections.reserve(result.detections.size());
    for (std::size_t index = 0; index < result.detections.size(); ++index) {
        const vision_native::Detection& detection = result.detections[index];
        const float width = std::max(0.0f, detection.x2 - detection.x1);
        const float height = std::max(0.0f, detection.y2 - detection.y1);
        if (width <= 1.0f || height <= 1.0f) {
            continue;
        }

        tracking_native::TrackerDetection tracker_detection;
        tracker_detection.id = tracker_detection_id(result.frame_id, index);
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

    NativeControllerVisionState state;
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

}  // namespace controller_native
