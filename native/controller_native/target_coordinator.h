#pragma once

#include "control_response_estimator.h"
#include "pipeline_contract/intent_state.h"
#include "pipeline_contract/target_plan.h"
#include "pipeline_contract/vision_observation.h"

#include <cstdint>

namespace controller_native {

struct TargetCoordinatorConfig {
    float hold_ms = 180.0f;
    float association_radius_px = 80.0f;
    float max_reacquire_innovation_px = 18.0f;
    float settle_radius_px = 8.0f;
    std::uint32_t settle_frames = 5;
    float ads_max_acquisition_ms = 180.0f;
    float bodylock_activation_radius_px = 150.0f;
    float bodylock_exit_radius_px = 48.0f;
    float handoff_prediction_seconds = 0.020f;
    float handoff_max_closing_velocity_px_per_sec = 320.0f;
    // Velocity response at the historical ~91Hz Vision cadence. The applied
    // gain is converted from this reference to each actual capture interval.
    float motion_velocity_alpha = 0.2f;
    float motion_velocity_reference_interval_seconds = 0.011f;
    float jump_fall_velocity_px_per_second = 100.0f;
    float player_jump_acceleration_model_ms = 700.0f;
    float max_authority = 1.0f;
};

float motion_velocity_alpha_for_interval(
    float reference_alpha,
    float reference_interval_seconds,
    float observation_interval_seconds) noexcept;

struct TargetControlFeedback {
    pipeline_contract::Vec2f previous_delivered_stick{};
    float aim_response_px_per_stick_second = 500.0f;
    float aim_response_confidence = 0.0f;
    float player_jump_action_age_ms = -1.0f;
    // Benchmark headroom oracle: exact player-motion contribution that has
    // already occurred since the preceding controller tick. Production never
    // sets this; a later causal observer would have to estimate both values.
    bool has_player_motion_oracle = false;
    bool has_player_motion_rate_oracle = false;
    pipeline_contract::Vec2f player_error_delta_px{};
    pipeline_contract::Vec2f player_error_rate_px_per_sec{};
};

class TargetCoordinator {
public:
    explicit TargetCoordinator(TargetCoordinatorConfig config = {});

    pipeline_contract::TargetPlan update(
        const pipeline_contract::VisionObservationBatch& observations,
        const pipeline_contract::IntentState& intent,
        double now_seconds,
        const TargetControlFeedback& feedback = {}) noexcept;

    bool observe_control_response(const ControlResponseSample& sample) noexcept;
    void begin_ads_epoch(std::uint64_t epoch, double now_seconds) noexcept;
    void set_motion_velocity_alpha_for_benchmark(float alpha) noexcept;
    void reset() noexcept;

private:
    const pipeline_contract::VisionCandidate* choose_candidate(
        const pipeline_contract::VisionObservationBatch& observations,
        pipeline_contract::Vec2f predicted) const noexcept;
    pipeline_contract::TargetPlan no_target_plan() noexcept;
    void fill_horizon(pipeline_contract::TargetPlan& plan) const noexcept;

    TargetCoordinatorConfig config_{};
    ControlResponseEstimator response_estimator_{};
    pipeline_contract::TargetPlan latest_{};
    pipeline_contract::Vec2f position_{};
    pipeline_contract::Vec2f velocity_{};
    pipeline_contract::Vec2f acceleration_{};
    std::uint64_t source_id_ = 0;
    std::uint64_t target_id_ = 0;
    std::uint64_t next_target_id_ = 1;
    std::uint64_t generation_ = 0;
    std::uint64_t source_frame_id_ = 0;
    double last_observed_seconds_ = 0.0;
    double last_observation_capture_seconds_ = 0.0;
    double last_update_seconds_ = 0.0;
    double acquisition_started_seconds_ = 0.0;
    double ads_epoch_started_seconds_ = 0.0;
    float last_observed_reliability_ = 0.0f;
    float last_observed_normalized_size_ = 0.0f;
    std::uint32_t settled_frames_ = 0;
    std::uint32_t observed_frames_ = 0;
    bool has_target_ = false;
    bool has_observation_capture_time_ = false;
    bool fire_requested_ = false;
    bool observed_fire_eligible_ = false;
    bool was_missing_ = false;
    bool ads_epoch_active_ = false;
    bool ads_snap_consumed_ = false;
    pipeline_contract::ControlMode control_mode_ = pipeline_contract::ControlMode::Manual;
};

}  // namespace controller_native
