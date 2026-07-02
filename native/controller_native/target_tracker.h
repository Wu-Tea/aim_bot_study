#pragma once

#include "../pipeline_contract/tracker_config.h"

#include <optional>
#include <string>

namespace controller_native {

using NativeTargetTrackerConfig = pipeline_contract::TargetTrackerConfig;

struct NativeTargetTrackerObservation {
    bool has_target = false;
    float dx = 0.0f;
    float dy = 0.0f;
    std::string target_tier = "none";
    double observed_at_seconds = 0.0;
};

struct NativeTargetProjection {
    float dx = 0.0f;
    float dy = 0.0f;
    double observed_at_seconds = 0.0;
};

class NativeGamepadTargetTracker {
public:
    explicit NativeGamepadTargetTracker(NativeTargetTrackerConfig config = {});

    void reset();
    void update_observation(const NativeTargetTrackerObservation& observation);
    void record_output(float right_x, float right_y, double dt_seconds);
    std::optional<NativeTargetProjection> project(double timestamp_seconds) const;

private:
    float clamp_velocity(float value) const;
    void decay_velocity_for_weak_observation();

    NativeTargetTrackerConfig config_;
    float observed_dx_ = 0.0f;
    float observed_dy_ = 0.0f;
    double observed_at_seconds_ = 0.0;
    std::string target_tier_ = "none";
    float target_velocity_x_ = 0.0f;
    float target_velocity_y_ = 0.0f;
    float camera_dx_since_observation_ = 0.0f;
    float camera_dy_since_observation_ = 0.0f;
};

}  // namespace controller_native
