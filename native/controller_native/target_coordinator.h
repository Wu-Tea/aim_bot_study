#pragma once

#include "pipeline_contract/intent_state.h"
#include "pipeline_contract/target_plan.h"
#include "pipeline_contract/vision_observation.h"

#include <cstdint>
namespace controller_native {

struct TargetCoordinatorConfig {
    float max_observation_age_ms = 50.0f;
    float association_radius_px = 80.0f;
    float settle_radius_px = 8.0f;
    std::uint32_t settle_frames = 5;
    float ads_nominal_acquisition_ms = 135.0f;
    float ads_max_acquisition_ms = 220.0f;
    // Response-demand normalization only; Vision owns target admission.
    float ads_activation_radius_px = 135.0f;
    float bodylock_activation_radius_px = 150.0f;
    float bodylock_exit_radius_px = 48.0f;
    float handoff_prediction_seconds = 0.020f;
    float handoff_max_closing_velocity_px_per_sec = 320.0f;
    // During fire, cap the influence of unmodelled camera motion. The rejected
    // tail is never accumulated or released into a later controller tick.
    float bodylock_max_target_acceleration_px_per_second2 = 3000.0f;
    float fire_innovation_limit_px = 3.5f;
    // A fast mean-reverting observation state absorbs camera/gun-kick
    // transients so they cannot become persistent target velocity.
    bool firing_disturbance_observer_enabled = true;
    float jump_fall_velocity_px_per_second = 100.0f;
    float max_authority = 1.0f;
    float desired_point_traversal_ms = 180.0f;
    float desired_point_boundary_exit_ms = 50.0f;
    float enemy_cue_expected_radius_px = 70.0f;
    float unconfirmed_enemy_search_authority_scale = 0.24f;
    float unconfirmed_enemy_near_authority_scale = 0.08f;
    float confirmed_enemy_cue_loss_authority_scale = 0.45f;
    float unchecked_enemy_authority_scale = 0.35f;
    bool visual_authority_enabled = true;
};

struct TargetControlFeedback {
    pipeline_contract::Vec2f previous_delivered_stick{};
    float aim_response_px_per_stick_second = 500.0f;
    float aim_response_confidence = 0.0f;
    bool firing_recently = false;
};

class TargetCoordinator {
public:
    explicit TargetCoordinator(TargetCoordinatorConfig config = {});

    pipeline_contract::TargetPlan update(
        const pipeline_contract::VisionObservationBatch& observations,
        const pipeline_contract::IntentState& intent,
        double now_seconds,
        const TargetControlFeedback& feedback = {}) noexcept;

    void begin_ads_epoch(std::uint64_t epoch, double now_seconds) noexcept;
    void set_firing_disturbance_observer_enabled_for_benchmark(
        bool enabled) noexcept;
    void reset() noexcept;

private:
    void reset_target_owned_state_for_replacement() noexcept;
    void adopt_candidate_geometry(
        const pipeline_contract::VisionCandidate& candidate,
        bool cue_continuation,
        bool reset_desired_point) noexcept;
    void update_desired_point_from_manual(
        const pipeline_contract::IntentState& intent,
        bool firing_recently,
        float dt_seconds) noexcept;
    const pipeline_contract::VisionCandidate* choose_candidate(
        const pipeline_contract::VisionObservationBatch& observations,
        pipeline_contract::Vec2f association_anchor) const noexcept;
    bool is_effective_selector_replacement(
        const pipeline_contract::VisionObservationBatch& observations,
        const pipeline_contract::VisionCandidate& candidate) const noexcept;
    pipeline_contract::TargetPlan no_target_plan(
        double now_seconds,
        std::uint64_t source_frame_id = 0,
        std::uint32_t candidate_count = 0,
        std::uint64_t preferred_source_id = 0) noexcept;

    TargetCoordinatorConfig config_{};
    pipeline_contract::Vec2f
        previous_firing_velocity_innovation_{};
    bool firing_velocity_observer_active_ = false;
    pipeline_contract::TargetPlan latest_{};
    pipeline_contract::Vec2f source_position_{};
    pipeline_contract::Vec2f position_{};
    common_native::Box2f aim_region_{};
    pipeline_contract::Vec2f desired_point_normalized_{};
    pipeline_contract::AimRegionSource aim_region_source_ =
        pipeline_contract::AimRegionSource::None;
    pipeline_contract::DesiredPointSource desired_point_source_ =
        pipeline_contract::DesiredPointSource::None;
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
    double last_update_seconds_ = 0.0;
    double acquisition_started_seconds_ = 0.0;
    double acquisition_completed_seconds_ = 0.0;
    double ads_epoch_started_seconds_ = 0.0;
    float last_observed_reliability_ = 0.0f;
    bool enemy_cue_current_ = false;
    bool enemy_identity_confirmed_ = false;
    bool enemy_cue_checked_ = false;
    float last_observed_normalized_size_ = 0.0f;
    pipeline_contract::Vec2f last_observed_target_size_px_{};
    std::uint32_t settled_frames_ = 0;
    std::uint32_t observed_frames_ = 0;
    float manual_boundary_seconds_x_ = 0.0f;
    float manual_boundary_seconds_y_ = 0.0f;
    bool has_target_ = false;
    bool has_aim_region_ = false;
    bool user_desired_point_active_ = false;
    bool manual_correction_x_ = false;
    bool manual_correction_y_ = false;
    bool manual_boundary_x_ = false;
    bool manual_boundary_y_ = false;
    bool manual_exit_requested_ = false;
    bool has_observation_capture_time_ = false;
    bool has_processed_capture_ = false;
    bool has_processed_capture_time_ = false;
    bool fire_requested_ = false;
    bool observed_fire_eligible_ = false;
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
    bool ads_target_switch_seen_ = false;
    std::uint64_t selector_target_generation_ = 0;
    pipeline_contract::ControlMode control_mode_ = pipeline_contract::ControlMode::Manual;
    float frame_width_px_ = 480.0f;
    float frame_height_px_ = 416.0f;
};

}  // namespace controller_native
