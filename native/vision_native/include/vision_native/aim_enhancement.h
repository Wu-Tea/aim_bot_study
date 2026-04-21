#pragma once

#include "vision_native/types.h"

#include <optional>

namespace vision_native {

struct AimSlowZone {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

class AimEnhancementPipeline {
public:
    void reset();

    VisionResult process(
        const VisionResult& target,
        double timestamp_seconds,
        const std::optional<AimSlowZone>& slow_zone = std::nullopt);

private:
    struct TargetSnapshot {
        float target_x = 0.0f;
        float target_y = 0.0f;
        float dx = 0.0f;
        float dy = 0.0f;
    };

    struct AimState {
        const VisionResult& target;
        double dt = 0.0;
        float motion_x = 0.0f;
        float motion_y = 0.0f;
        float velocity_x = 0.0f;
        float velocity_y = 0.0f;
        std::optional<float> previous_dx;
        std::optional<float> previous_dy;
        float output_dx = 0.0f;
        float output_dy = 0.0f;
        std::optional<AimSlowZone> slow_zone;
    };

    static bool is_predicted(const VisionResult& target);
    static float clamp_axis_lead(float current_error, float lead, float max_lead);
    static bool is_crosshair_in_slow_zone(const AimState& state);
    static float damping_scale_for_distance(float distance);
    static bool is_converging(const AimState& state);

    void apply_lead(AimState& state);
    void apply_catchup(AimState& state);
    void apply_near_target_damping(AimState& state);
    std::pair<int, float> update_axis_bonus(
        float current_error,
        const std::optional<float>& previous_error,
        int growth_frames,
        float bonus) const;
    std::pair<int, float> update_axis_streak(float motion, int streak, float last_sign) const;

    std::optional<TargetSnapshot> previous_target_;
    std::optional<double> previous_timestamp_;
    float velocity_x_ = 0.0f;
    float velocity_y_ = 0.0f;
    int lead_x_streak_ = 0;
    int lead_y_streak_ = 0;
    float lead_x_sign_ = 0.0f;
    float lead_y_sign_ = 0.0f;
    int catchup_x_growth_frames_ = 0;
    int catchup_y_growth_frames_ = 0;
    float catchup_x_bonus_ = 0.0f;
    float catchup_y_bonus_ = 0.0f;
};

} // namespace vision_native
