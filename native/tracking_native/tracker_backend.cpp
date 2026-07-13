#include "tracker_backend.h"

#include "fps_reference_tracker.h"
#include "kalman_tracker.h"
#include "legacy_projection_tracker.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

namespace tracking_native {

namespace {

std::string normalized_backend_name(std::string_view value) {
    std::string normalized(value);
    std::transform(
        normalized.begin(),
        normalized.end(),
        normalized.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return normalized;
}

}  // namespace

void TrackerBackend::ingest_batch(
    const pipeline_contract::TrackObservationBatch& batch) {
    TrackerObservation observation;
    observation.frame_id = batch.frame_id;
    observation.capture_time = batch.captured_at;
    observation.ready_time = batch.ready_at;
    observation.screen_center_px = batch.screen_center_px;
    observation.detections.reserve(batch.detections.size());

    const pipeline_contract::TrackObservationDetection* selected = nullptr;
    for (const pipeline_contract::TrackObservationDetection& source : batch.detections) {
        TrackerDetection detection;
        detection.id = source.observation_id;
        detection.body_box_px = source.body_box_px;
        detection.aim_point_px = source.aim_point_px;
        detection.has_aim_point = source.has_aim_point;
        detection.confidence = source.confidence;
        detection.class_id = source.class_id;
        detection.target_tier = source.evidence_tier;
        detection.is_friendly = source.is_friendly;
        observation.detections.push_back(detection);
        if (selected == nullptr && !source.is_friendly && source.confidence > 0.0f &&
            source.body_box_px.w > 1.0f && source.body_box_px.h > 1.0f) {
            selected = &source;
        }
    }

    // Legacy single-target adapters retain their old behavior through this
    // bridge. The production fps backend overrides this method and ingests the
    // complete candidate batch as estimator input.
    if (selected != nullptr) {
        observation.has_target = true;
        observation.body_box_px = selected->body_box_px;
        observation.has_body_box = true;
        observation.target_tier = selected->evidence_tier;
        common_native::Vec2f aim_point = selected->aim_point_px;
        if (!selected->has_aim_point) {
            aim_point = {
                selected->body_box_px.x + selected->body_box_px.w * 0.5f,
                selected->body_box_px.y + selected->body_box_px.h * 0.40f};
        }
        observation.aim_error_px = {
            aim_point.x - batch.screen_center_px.x,
            aim_point.y - batch.screen_center_px.y};
    }
    ingest(observation);
}

std::vector<pipeline_contract::TrackEstimate> TrackerBackend::estimates(
    common_native::TimeSeconds query_time) const {
    const TrackerSnapshot snapshot = query({query_time});
    if (!snapshot.has_target) {
        return {};
    }

    pipeline_contract::TrackEstimate estimate;
    estimate.track_id = 1;
    estimate.source = snapshot.source == TrackerSnapshotSource::Observed
        ? pipeline_contract::TrackEstimateSource::Observed
        : pipeline_contract::TrackEstimateSource::Projected;
    estimate.lifecycle = snapshot.source == TrackerSnapshotSource::Coast
        ? pipeline_contract::TrackLifecycle::Coasting
        : pipeline_contract::TrackLifecycle::Confirmed;
    estimate.aim_error_px = snapshot.aim_error_px;
    estimate.body_box_px = snapshot.body_box_px;
    estimate.has_body_box = snapshot.has_body_box;
    estimate.confidence = 1.0f;
    estimate.last_observed_at = snapshot.observed_at;
    estimate.query_time = query_time;
    estimate.observation_age_ms = snapshot.projection_age_ms;
    return {estimate};
}

TrackerBackendKind parse_tracker_backend_kind(std::string_view value) {
    const std::string normalized = normalized_backend_name(value);
    if (normalized.empty() || normalized == "fps_reference") {
        return TrackerBackendKind::FpsReference;
    }
    if (normalized == "legacy_projection") {
        return TrackerBackendKind::LegacyProjection;
    }
    if (normalized == "kalman_experimental") {
        return TrackerBackendKind::KalmanExperimental;
    }
    throw std::runtime_error("unknown tracker_backend: " + std::string(value));
}

std::string_view tracker_backend_kind_name(TrackerBackendKind kind) {
    switch (kind) {
    case TrackerBackendKind::FpsReference:
        return "fps_reference";
    case TrackerBackendKind::LegacyProjection:
        return "legacy_projection";
    case TrackerBackendKind::KalmanExperimental:
        return "kalman_experimental";
    }
    return "legacy_projection";
}

std::unique_ptr<TrackerBackend> create_tracker_backend(
    TrackerBackendKind kind,
    pipeline_contract::TargetTrackerConfig config) {
    switch (kind) {
    case TrackerBackendKind::FpsReference:
        return std::make_unique<FpsReferenceTracker>(config);
    case TrackerBackendKind::LegacyProjection:
        return std::make_unique<LegacyProjectionTracker>(config);
    case TrackerBackendKind::KalmanExperimental:
        return std::make_unique<KalmanTracker>(config);
    }
    return std::make_unique<FpsReferenceTracker>(config);
}

}  // namespace tracking_native
