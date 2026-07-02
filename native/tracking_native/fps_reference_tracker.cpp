#include "fps_reference_tracker.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace tracking_native {

namespace {

constexpr float kFallbackBoxWidth = 60.0f;
constexpr float kFallbackBoxHeight = 140.0f;
constexpr double kMinScreenSizePx = 64.0;

bool nearly_equal(float lhs, float rhs) {
    return std::fabs(lhs - rhs) <= 0.5f;
}

float positive_or(float value, float fallback) {
    return value > 0.0f ? value : fallback;
}

std::string normalized(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return result;
}

bool is_weak_tier(std::string_view tier) {
    const std::string value = normalized(tier);
    return value == "associated_weak" ||
        value == "weak" ||
        value == "weak_association" ||
        value == "weak_observed" ||
        value == "cue_hold" ||
        value == "projected" ||
        value == "predicted";
}

fps::TargetTier fps_tier_from_string(std::string_view tier) {
    const std::string value = normalized(tier);
    if (value == "head") {
        return fps::TargetTier::Head;
    }
    if (value == "body" || value == "associated_weak" || value == "weak" ||
        value == "weak_association" || value == "weak_observed") {
        return fps::TargetTier::Body;
    }
    if (value == "cue_hold" || value == "projected" || value == "predicted") {
        return fps::TargetTier::Low;
    }
    if (value == "none" || value == "lost" || value.empty()) {
        return fps::TargetTier::Unknown;
    }
    return fps::TargetTier::UpperBody;
}

fps::TargetClass fps_class_from_id(int class_id) {
    switch (class_id) {
    case 2:
        return fps::TargetClass::Head;
    case 1:
        return fps::TargetClass::Body;
    default:
        return fps::TargetClass::Player;
    }
}

common_native::AssistAuthority common_assist(fps::AssistAuthority authority) {
    switch (authority) {
    case fps::AssistAuthority::AimObserved:
        return common_native::AssistAuthority::AimObserved;
    case fps::AssistAuthority::AimCoast:
        return common_native::AssistAuthority::AimCoast;
    case fps::AssistAuthority::None:
    default:
        return common_native::AssistAuthority::None;
    }
}

common_native::FireAuthority common_fire(fps::FireAuthority authority) {
    return authority == fps::FireAuthority::ObservedOnly
        ? common_native::FireAuthority::ObservedOnly
        : common_native::FireAuthority::None;
}

TrackerSnapshotSource snapshot_source(fps::TrackLife life, bool predicted_only) {
    if (!predicted_only) {
        return TrackerSnapshotSource::Observed;
    }
    if (life == fps::TrackLife::Coasting) {
        return TrackerSnapshotSource::Coast;
    }
    return TrackerSnapshotSource::Projected;
}

fps::TrackerConfig make_fps_config(
    const pipeline_contract::TargetTrackerConfig& config,
    float screen_width,
    float screen_height) {
    fps::TrackerConfig fps_config;
    const double width = std::max<double>(kMinScreenSizePx, screen_width);
    const double height = std::max<double>(kMinScreenSizePx, screen_height);
    fps_config.projection.screenSizePx = {width, height};
    fps_config.projection.baseFocalPx = {width * 0.5, height * 0.5};

    const double speed = std::max(0.0f, config.reticle_speed_px_per_sec);
    fps_config.stick.deadzone = 0.0;
    fps_config.stick.antiDeadzone = 0.0;
    fps_config.stick.responseExponent = 1.0;
    fps_config.stick.hipfireRateTrackUnitsPerSec = {
        speed / std::max(1.0, fps_config.projection.baseFocalPx.x),
        speed / std::max(1.0, fps_config.projection.baseFocalPx.y)};
    fps_config.stick.adsRateMultiplier = 1.0;
    fps_config.stick.zoomRatePower = 0.0;

    fps_config.confirmHits = 2;
    fps_config.maxCoastSec =
        std::max(0.0f, config.max_projection_age_ms) / 1000.0;
    fps_config.missDecayTauSec = std::max(0.030, fps_config.maxCoastSec * 0.55);
    fps_config.spawnMinConfidence = 0.20;
    fps_config.minAssistConfidence = 0.12;
    fps_config.minFireConfidence = 0.45;
    fps_config.fireMaxCaptureAgeSec = 0.030;
    fps_config.association.maxPixelDistance = std::max(
        120.0,
        std::min(320.0, static_cast<double>(
            std::max(0.0f, config.max_target_velocity_px_per_sec)) *
            std::max(0.030, fps_config.maxCoastSec) + 80.0));
    return fps_config;
}

}  // namespace

FpsReferenceTracker::FpsReferenceTracker(pipeline_contract::TargetTrackerConfig config)
    : config_(config) {
    rebuild_tracker(screen_width_, screen_height_);
}

void FpsReferenceTracker::reset() {
    rebuild_tracker(screen_width_, screen_height_);
    preferred_track_id_ = fps::kInvalidTrackId;
}

void FpsReferenceTracker::rebuild_tracker(float screen_width, float screen_height) {
    screen_width_ = positive_or(screen_width, 640.0f);
    screen_height_ = positive_or(screen_height, 512.0f);
    tracker_ = std::make_unique<fps::TargetTracker>(
        make_fps_config(config_, screen_width_, screen_height_));
}

void FpsReferenceTracker::ensure_tracker_for_observation(
    const TrackerObservation& observation) {
    if (observation.screen_center_px.x <= 0.0f || observation.screen_center_px.y <= 0.0f) {
        return;
    }
    const float observed_width = observation.screen_center_px.x * 2.0f;
    const float observed_height = observation.screen_center_px.y * 2.0f;
    if (!tracker_ ||
        !nearly_equal(observed_width, screen_width_) ||
        !nearly_equal(observed_height, screen_height_)) {
        rebuild_tracker(observed_width, observed_height);
        preferred_track_id_ = fps::kInvalidTrackId;
    }
}

void FpsReferenceTracker::ingest(const TrackerObservation& observation) {
    ensure_tracker_for_observation(observation);
    if (!tracker_) {
        return;
    }
    tracker_->ingestVisionFrame(make_vision_frame(observation));
}

void FpsReferenceTracker::push_control_sample(const TrackerControlSample& sample) {
    if (!tracker_ || sample.apply_time.value <= 0.0) {
        return;
    }
    fps::ControlSample control;
    control.sendTime = sample.apply_time.value;
    control.applyTime = sample.apply_time.value;
    control.finalRightStick = {
        sample.sticks.final_output.x,
        sample.sticks.final_output.y};
    control.mode.ads = true;
    control.mode.zoom = 1.0;
    control.mode.sensitivity = 1.0;
    tracker_->pushFinalControlSample(control);
}

fps::VisionFrame FpsReferenceTracker::make_vision_frame(
    const TrackerObservation& observation) {
    fps::VisionFrame frame;
    frame.frameSeq = observation.frame_id != 0 ? observation.frame_id : synthetic_frame_id_++;
    frame.captureTime = observation.capture_time.value;
    frame.readyTime = observation.ready_time.value > 0.0
        ? observation.ready_time.value
        : observation.capture_time.value;
    frame.roi.originPx = {0.0, 0.0};
    frame.roi.sizePx = {screen_width_, screen_height_};
    frame.mode.ads = true;
    frame.mode.zoom = 1.0;
    frame.mode.sensitivity = 1.0;

    // Raw detector boxes are only target candidates after the selector/controller
    // grants assist authority. A no-target frame with boxes is a processed miss,
    // not permission for tracker-only aim assist.
    frame.detections.reserve(
        observation.has_target
            ? (observation.detections.empty() ? 1u : observation.detections.size())
            : 0u);
    std::uint64_t fallback_id_base = frame.frameSeq << 32u;
    if (observation.has_target) {
        for (std::size_t index = 0; index < observation.detections.size(); ++index) {
            const TrackerDetection& detection = observation.detections[index];
            if (detection.is_friendly || detection.body_box_px.w <= 1.0f ||
                detection.body_box_px.h <= 1.0f || detection.confidence <= 0.0f) {
                continue;
            }
            frame.detections.push_back(make_detection(
                detection,
                fallback_id_base | static_cast<std::uint64_t>(index + 1u)));
        }
    }

    if (frame.detections.empty() && observation.has_target) {
        frame.detections.push_back(make_selected_target_detection(
            observation,
            fallback_id_base | 1u));
    }

    return frame;
}

fps::Detection FpsReferenceTracker::make_detection(
    const TrackerDetection& detection,
    std::uint64_t fallback_id) const {
    fps::Detection converted;
    converted.id = detection.id != 0 ? detection.id : fallback_id;
    converted.cls = fps_class_from_id(detection.class_id);
    converted.tier = fps_tier_from_string(detection.target_tier);
    converted.confidence = std::max(0.0f, std::min(1.0f, detection.confidence));
    if (is_weak_tier(detection.target_tier)) {
        converted.confidence = std::min(converted.confidence, 0.30);
    }
    converted.bodyBoxPx = {
        detection.body_box_px.x,
        detection.body_box_px.y,
        detection.body_box_px.x + detection.body_box_px.w,
        detection.body_box_px.y + detection.body_box_px.h};
    converted.bodyCenterPx = converted.bodyBoxPx.center();
    if (detection.has_aim_point) {
        converted.aimPointPx = {detection.aim_point_px.x, detection.aim_point_px.y};
    } else {
        converted.aimPointPx = {
            converted.bodyCenterPx.x,
            converted.bodyBoxPx.y0 + (converted.bodyBoxPx.height() * 0.40)};
    }
    converted.validAimPoint = true;
    return converted;
}

fps::Detection FpsReferenceTracker::make_selected_target_detection(
    const TrackerObservation& observation,
    std::uint64_t fallback_id) const {
    const float screen_center_x = positive_or(observation.screen_center_px.x, screen_width_ * 0.5f);
    const float screen_center_y = positive_or(observation.screen_center_px.y, screen_height_ * 0.5f);
    const float aim_x = screen_center_x + observation.aim_error_px.x;
    const float aim_y = screen_center_y + observation.aim_error_px.y;

    TrackerDetection detection;
    detection.id = fallback_id;
    detection.confidence = is_weak_tier(observation.target_tier) ? 0.30f : 0.90f;
    detection.target_tier = observation.target_tier;
    detection.class_id = 0;
    detection.has_aim_point = true;
    detection.aim_point_px = {aim_x, aim_y};
    if (observation.has_body_box && observation.body_box_px.w > 1.0f &&
        observation.body_box_px.h > 1.0f) {
        detection.body_box_px = observation.body_box_px;
    } else {
        detection.body_box_px = {
            aim_x - (kFallbackBoxWidth * 0.5f),
            aim_y - (kFallbackBoxHeight * 0.40f),
            kFallbackBoxWidth,
            kFallbackBoxHeight};
    }
    return make_detection(detection, fallback_id);
}

TrackerSnapshot FpsReferenceTracker::query(const TrackerQuery& query) const {
    TrackerSnapshot snapshot;
    if (!tracker_ || query.query_time.value <= 0.0) {
        return snapshot;
    }

    fps::SelectionRequest request;
    request.preferredTrackId = preferred_track_id_;
    const fps::TrackerOutput output = tracker_->query(query.query_time.value, request);
    if (!output.hasSelection) {
        preferred_track_id_ = fps::kInvalidTrackId;
        return snapshot;
    }

    const fps::TrackSnapshot& selected = output.selected;
    const common_native::AssistAuthority assist = common_assist(selected.assistAuthority);
    if (assist == common_native::AssistAuthority::None) {
        return snapshot;
    }

    preferred_track_id_ = selected.id;
    snapshot.has_target = true;
    snapshot.source = snapshot_source(selected.life, selected.predictedOnly);
    snapshot.aim_error_px = {
        static_cast<float>(selected.aimErrorPx.x),
        static_cast<float>(selected.aimErrorPx.y)};
    snapshot.body_box_px = {
        static_cast<float>(selected.predictedBoxPx.x0),
        static_cast<float>(selected.predictedBoxPx.y0),
        static_cast<float>(selected.predictedBoxPx.width()),
        static_cast<float>(selected.predictedBoxPx.height())};
    snapshot.has_body_box = snapshot.body_box_px.w > 1.0f && snapshot.body_box_px.h > 1.0f;
    snapshot.projection_age_ms = selected.obsAgeMs;
    if (std::isfinite(selected.obsAgeMs)) {
        snapshot.observed_at = {query.query_time.value - (selected.obsAgeMs / 1000.0)};
    }
    snapshot.assist_authority = assist;
    snapshot.fire_authority = common_fire(selected.fireAuthority);
    return snapshot;
}

std::vector<TrackerDebugTrack> FpsReferenceTracker::debug_tracks() const {
    if (!tracker_) {
        return {};
    }
    std::vector<TrackerDebugTrack> tracks;
    for (const fps::TrackDebugInfo& info : tracker_->debugTracks()) {
        TrackerDebugTrack track;
        track.aim_error_px = {
            static_cast<float>(info.compensatedPosition.x),
            static_cast<float>(info.compensatedPosition.y)};
        track.velocity_px_per_sec = {
            static_cast<float>(info.compensatedVelocity.x),
            static_cast<float>(info.compensatedVelocity.y)};
        tracks.push_back(track);
    }
    return tracks;
}

}  // namespace tracking_native
