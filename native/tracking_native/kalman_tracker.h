#pragma once

#include "ego_motion_buffer.h"
#include "tracker_backend.h"

namespace tracking_native {

class KalmanTracker final : public TrackerBackend {
public:
    explicit KalmanTracker(pipeline_contract::TargetTrackerConfig config = {});

    void reset() override;
    void ingest(const TrackerObservation& observation) override;
    void push_control_sample(const TrackerControlSample& sample) override;
    TrackerSnapshot query(const TrackerQuery& query) const override;
    std::vector<TrackerDebugTrack> debug_tracks() const override;

private:
    float clamp_velocity(float value) const;
    void decay_velocity_for_weak_observation();

    pipeline_contract::TargetTrackerConfig config_;
    EgoMotionBuffer ego_motion_;
    common_native::Vec2f observed_error_px_;
    common_native::Vec2f velocity_px_per_sec_;
    common_native::Box2f body_box_px_;
    bool has_body_box_ = false;
    double observed_at_seconds_ = 0.0;
    std::string target_tier_ = "none";
};

}  // namespace tracking_native
