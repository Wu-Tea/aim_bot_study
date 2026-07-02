#pragma once

#include "tracker_backend.h"
#include "tracker_contract.h"

#include <string>

namespace tracking_native {

class LegacyProjectionTracker final : public TrackerBackend {
public:
    explicit LegacyProjectionTracker(
        pipeline_contract::TargetTrackerConfig config = {});

    void reset() override;
    void ingest(const TrackerObservation& observation) override;
    void push_control_sample(const TrackerControlSample& sample) override;
    TrackerSnapshot query(const TrackerQuery& query) const override;
    std::vector<TrackerDebugTrack> debug_tracks() const override;

private:
    float clamp_velocity(float value) const;
    void decay_velocity_for_weak_observation();

    pipeline_contract::TargetTrackerConfig config_;
    common_native::Vec2f observed_error_px_;
    common_native::Vec2f target_velocity_px_per_sec_;
    common_native::Vec2f camera_motion_since_observation_px_;
    double observed_at_seconds_ = 0.0;
    std::string target_tier_ = "none";
    common_native::Vec2f last_aim_error_px_;
    common_native::Box2f last_body_box_px_;
    bool last_has_body_box_ = false;
};

}  // namespace tracking_native
