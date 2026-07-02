#pragma once

namespace pipeline_contract {

struct TargetTrackerConfig {
    float reticle_speed_px_per_sec = 1500.0f;
    float max_projection_age_ms = 50.0f;
    float velocity_lowpass_alpha = 0.35f;
    float max_target_velocity_px_per_sec = 1200.0f;
    float weak_observation_velocity_decay = 0.70f;
};

}  // namespace pipeline_contract
