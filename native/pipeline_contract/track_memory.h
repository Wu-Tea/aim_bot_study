#pragma once

#include "../common_native/screen_geometry.h"
#include "../common_native/time_types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace pipeline_contract {

enum class TrackEstimateSource : std::uint8_t {
    Observed = 0,
    Projected = 1,
};

enum class TrackLifecycle : std::uint8_t {
    Tentative = 0,
    Confirmed = 1,
    Coasting = 2,
    Lost = 3,
};

struct TrackObservationDetection {
    std::uint64_t observation_id = 0;
    common_native::Box2f body_box_px;
    common_native::Vec2f aim_point_px;
    bool has_aim_point = false;
    float confidence = 0.0f;
    int class_id = 0;
    std::string evidence_tier = "observed_strong";
    bool is_friendly = false;
};

struct TrackObservationBatch {
    std::uint64_t frame_id = 0;
    common_native::TimeSeconds captured_at;
    common_native::TimeSeconds ready_at;
    common_native::Vec2f screen_center_px;
    std::vector<TrackObservationDetection> detections;
};

struct TrackEstimate {
    std::uint64_t track_id = 0;
    std::uint64_t backing_observation_id = 0;
    std::uint64_t backing_frame_id = 0;
    TrackEstimateSource source = TrackEstimateSource::Projected;
    TrackLifecycle lifecycle = TrackLifecycle::Lost;
    common_native::Vec2f aim_error_px;
    common_native::Box2f body_box_px;
    bool has_body_box = false;
    common_native::Vec2f velocity_model_units_per_sec;
    float position_sigma = 0.0f;
    float ambiguity = 0.0f;
    float association_quality = 0.0f;
    common_native::TimeSeconds last_observed_at;
    common_native::TimeSeconds query_time;
    double observation_age_ms = 0.0;
};

enum class SelectedTrackReason : std::uint8_t {
    None = 0,
    VisionSelector = 1,
    Continuity = 2,
};

struct SelectedTrackRef {
    bool has_selection = false;
    std::uint64_t selected_observation_id = 0;
    std::uint64_t track_id = 0;
    std::uint64_t backing_frame_id = 0;
    float confidence = 0.0f;
    SelectedTrackReason reason = SelectedTrackReason::None;
};

}  // namespace pipeline_contract
