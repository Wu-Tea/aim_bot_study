#pragma once

#include "tracker_backend.h"

#include <fps_tracker/target_tracker.hpp>

#include <memory>

namespace tracking_native {

class FpsReferenceTracker final : public TrackerBackend {
public:
    explicit FpsReferenceTracker(
        pipeline_contract::TargetTrackerConfig config = {});

    void reset() override;
    void ingest(const TrackerObservation& observation) override;
    void ingest_batch(const pipeline_contract::TrackObservationBatch& batch) override;
    void push_control_sample(const TrackerControlSample& sample) override;
    TrackerSnapshot query(const TrackerQuery& query) const override;
    std::vector<pipeline_contract::TrackEstimate> estimates(
        common_native::TimeSeconds query_time) const override;
    std::vector<TrackerDebugTrack> debug_tracks() const override;

private:
    void rebuild_tracker(float screen_width, float screen_height);
    void ensure_tracker_for_observation(const TrackerObservation& observation);
    void ensure_tracker_for_screen_center(common_native::Vec2f screen_center_px);
    fps::VisionFrame make_vision_frame(const TrackerObservation& observation);
    fps::VisionFrame make_vision_frame(
        const pipeline_contract::TrackObservationBatch& batch);
    fps::Detection make_detection(
        const TrackerDetection& detection,
        std::uint64_t fallback_id) const;
    fps::Detection make_detection(
        const pipeline_contract::TrackObservationDetection& detection,
        std::uint64_t fallback_id) const;
    fps::Detection make_selected_target_detection(
        const TrackerObservation& observation,
        std::uint64_t fallback_id) const;

    pipeline_contract::TargetTrackerConfig config_;
    std::unique_ptr<fps::TargetTracker> tracker_;
    float screen_width_ = 640.0f;
    float screen_height_ = 512.0f;
    std::uint64_t synthetic_frame_id_ = 1;
    mutable fps::TrackId preferred_track_id_ = fps::kInvalidTrackId;
};

}  // namespace tracking_native
