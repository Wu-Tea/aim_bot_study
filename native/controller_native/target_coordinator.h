#pragma once

#include "control_response_estimator.h"
#include "target_geometry.h"
#include "pipeline_contract/intent_state.h"
#include "pipeline_contract/target_plan.h"
#include "pipeline_contract/vision_observation.h"

#include <cstdint>
namespace controller_native {

struct TargetCoordinatorConfig {
    float hold_ms = 180.0f;
    float max_observation_age_ms = 50.0f;
    // Identity/association stays on hold_ms. These parameters only retire
    // stale Coasting actuation authority after a brief Vision gap.
    float coast_full_authority_grace_ms = 12.5f;
    float coast_actuation_release_ms = 65.0f;
    float association_radius_px = 80.0f;
    float max_reacquire_innovation_px = 18.0f;
    float settle_radius_px = 8.0f;
    std::uint32_t settle_frames = 5;
    float ads_nominal_acquisition_ms = 135.0f;
    float ads_max_acquisition_ms = 220.0f;
    float ads_activation_radius_px = 135.0f;
    float bodylock_activation_radius_px = 150.0f;
    float bodylock_exit_radius_px = 48.0f;
    float handoff_prediction_seconds = 0.020f;
    float handoff_max_closing_velocity_px_per_sec = 320.0f;
    // Velocity response at the historical ~91Hz Vision cadence. The applied
    // gain is converted from this reference to each actual capture interval.
    float motion_velocity_alpha = 0.2f;
    float motion_velocity_reference_interval_seconds = 0.011f;
    // During fire, cap the influence of unmodelled camera motion. The rejected
    // tail is never accumulated or released into a later controller tick.
    float bodylock_max_target_acceleration_px_per_second2 = 3000.0f;
    float fire_innovation_limit_px = 3.5f;
    bool firing_body_geometry_stabilizer_enabled = true;
    // A fast mean-reverting observation state absorbs camera/gun-kick
    // transients so they cannot become persistent target velocity.
    bool firing_disturbance_observer_enabled = true;
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
    // VectorIntentFuser is the sole manual/AI arbitration owner. This is
    // consumed on the next controller tick; the coordinator does not infer
    // escape from raw stick magnitude.
    bool fusion_manual_escape = false;
    bool firing_recently = false;
    float player_jump_action_age_ms = -1.0f;
    float player_slide_action_age_ms = -1.0f;
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
    void set_firing_body_geometry_stabilizer_enabled_for_benchmark(
        bool enabled) noexcept;
    void set_firing_disturbance_observer_enabled_for_benchmark(
        bool enabled) noexcept;
    void set_causal_player_motion_enabled_for_benchmark(
        bool state_enabled,
        bool forecast_enabled) noexcept;
    void reset() noexcept;

private:
    enum class PlayerMotionEvent : unsigned char {
        None,
        Jump,
        Slide,
    };

    struct PlayerMotionEstimate {
        float realized_delta_y_px = 0.0f;
        float forecast_y_px = 0.0f;
        float confidence = 0.0f;
        float unit_offset = 0.0f;
        PlayerMotionEvent event = PlayerMotionEvent::None;
    };

    PlayerMotionEstimate update_player_motion_estimate(
        const TargetControlFeedback& feedback,
        float manual_camera_ownership) noexcept;
    void learn_player_motion_amplitude(
        PlayerMotionEvent event,
        float unit_offset,
        float innovation_y_px,
        float reliability,
        bool reacquiring,
        float manual_camera_ownership) noexcept;
    void reset_target_owned_state_for_replacement() noexcept;
    const pipeline_contract::VisionCandidate* choose_candidate(
        const pipeline_contract::VisionObservationBatch& observations,
        pipeline_contract::Vec2f predicted) const noexcept;
    bool is_effective_selector_replacement(
        const pipeline_contract::VisionObservationBatch& observations,
        const pipeline_contract::VisionCandidate& candidate) const noexcept;
    pipeline_contract::TargetPlan no_target_plan(
        double now_seconds,
        std::uint64_t source_frame_id = 0,
        std::uint32_t candidate_count = 0,
        std::uint64_t preferred_source_id = 0) noexcept;
    void fill_horizon(pipeline_contract::TargetPlan& plan) const noexcept;

    TargetCoordinatorConfig config_{};
    ControlResponseEstimator response_estimator_{};
    StableBodyAimTracker stable_body_aim_tracker_{};
    pipeline_contract::Vec2f
        previous_firing_velocity_innovation_{};
    bool firing_velocity_observer_active_ = false;
    pipeline_contract::TargetPlan latest_{};
    pipeline_contract::Vec2f position_{};
    pipeline_contract::Vec2f velocity_{};
    pipeline_contract::Vec2f acceleration_{};
    std::uint64_t source_id_ = 0;
    std::uint64_t target_id_ = 0;
    std::uint64_t next_target_id_ = 1;
    std::uint64_t generation_ = 0;
    std::uint64_t source_frame_id_ = 0;
    std::uint64_t last_processed_frame_id_ = 0;
    double last_observed_seconds_ = 0.0;
    double last_observation_capture_seconds_ = 0.0;
    double last_processed_capture_seconds_ = 0.0;
    double last_unique_observation_seconds_ = 0.0;
    double last_update_seconds_ = 0.0;
    double acquisition_started_seconds_ = 0.0;
    double acquisition_completed_seconds_ = 0.0;
    double ads_epoch_started_seconds_ = 0.0;
    float last_observed_reliability_ = 0.0f;
    float last_observed_normalized_size_ = 0.0f;
    pipeline_contract::Vec2f last_observed_target_size_px_{};
    std::uint32_t settled_frames_ = 0;
    std::uint32_t observed_frames_ = 0;
    bool has_target_ = false;
    bool has_observation_capture_time_ = false;
    bool has_processed_capture_ = false;
    bool has_processed_capture_time_ = false;
    bool has_unique_observation_time_ = false;
    bool fire_requested_ = false;
    bool observed_fire_eligible_ = false;
    bool was_missing_ = false;
    bool cue_continuation_active_ = false;
    bool ads_epoch_active_ = false;
    bool ads_snap_consumed_ = false;
    bool ads_target_admitted_ = false;
    std::uint64_t physical_ads_epoch_ = 0;
    std::uint64_t target_acquisition_id_ = 0;
    std::uint64_t next_target_acquisition_id_ = 1;
    pipeline_contract::AdsAcquisitionState ads_acquisition_state_ =
        pipeline_contract::AdsAcquisitionState::Idle;
    pipeline_contract::AdsDecisionReason ads_decision_reason_ =
        pipeline_contract::AdsDecisionReason::None;
    bool source_decision_available_ = false;
    pipeline_contract::SourceDecisionOutcome source_decision_outcome_ =
        pipeline_contract::SourceDecisionOutcome::NoDecision;
    pipeline_contract::AdsDecisionReason source_decision_reason_ =
        pipeline_contract::AdsDecisionReason::None;
    pipeline_contract::AdsDecisionReason acquisition_terminal_reason_ =
        pipeline_contract::AdsDecisionReason::None;
    std::uint64_t ads_acquisition_begin_ns_ = 0;
    std::uint64_t ads_acquisition_complete_ns_ = 0;
    bool ads_center_cross_seen_ = false;
    bool ads_moving_away_seen_ = false;
    bool ads_target_switch_seen_ = false;
    std::uint64_t selector_target_generation_ = 0;
    pipeline_contract::ControlMode control_mode_ = pipeline_contract::ControlMode::Manual;
    PlayerMotionEvent active_player_motion_event_ =
        PlayerMotionEvent::None;
    float previous_player_motion_offset_y_px_ = 0.0f;
    float previous_observed_player_motion_unit_offset_ = 0.0f;
    float jump_effective_amplitude_px_ = 28.0f;
    float slide_effective_amplitude_px_ = 36.0f;
    std::uint32_t jump_motion_learning_samples_ = 0;
    std::uint32_t slide_motion_learning_samples_ = 0;
    bool causal_player_motion_state_enabled_ = false;
    bool causal_player_motion_forecast_enabled_ = true;
    float frame_width_px_ = 480.0f;
    float frame_height_px_ = 416.0f;
};

}  // namespace controller_native
