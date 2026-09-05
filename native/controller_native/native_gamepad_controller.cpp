#include "native_gamepad_controller.h"
#include "output_composer.h"
#include "../tracking_native/tracker_authority.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <utility>

namespace controller_native {
namespace {

constexpr float kCueHoldReducedForceMaxTargetHeightRatio = 0.12f;

float cue_hold_bodylock_force_scale(
    const GamepadAiAimConfig& config,
    float normalized_target_height) noexcept {
    const float reduced_scale = std::clamp(
        config.cue_hold_body_lock_force_scale, 0.0f, 1.0f);
    const float full_force_height = std::max(
        kCueHoldReducedForceMaxTargetHeightRatio + 0.001f,
        std::clamp(
            config.cue_hold_full_force_min_target_height_ratio,
            0.0f,
            1.0f));
    const float finite_target_height = std::isfinite(normalized_target_height)
        ? std::clamp(normalized_target_height, 0.0f, 1.0f)
        : 0.0f;
    float close_weight = std::clamp(
        (finite_target_height -
         kCueHoldReducedForceMaxTargetHeightRatio) /
            (full_force_height -
             kCueHoldReducedForceMaxTargetHeightRatio),
        0.0f,
        1.0f);
    close_weight = close_weight * close_weight *
        (3.0f - 2.0f * close_weight);
    return reduced_scale + (1.0f - reduced_scale) * close_weight;
}

double current_seconds() {
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

float clamp_unit(float value) noexcept {
    return std::clamp(value, -1.0f, 1.0f);
}

const char* mode_name(pipeline_contract::ControlMode mode) noexcept {
    switch (mode) {
    case pipeline_contract::ControlMode::AdsAcquire: return "ads_snap";
    case pipeline_contract::ControlMode::BodyLockFollow: return "body_lock";
    case pipeline_contract::ControlMode::Manual: return "manual";
    }
    return "manual";
}

const char* lifecycle_name(pipeline_contract::TargetLifecycle lifecycle) noexcept {
    switch (lifecycle) {
    case pipeline_contract::TargetLifecycle::Observed: return "observed";
    case pipeline_contract::TargetLifecycle::CueContinuation: return "cue_continuation";
    case pipeline_contract::TargetLifecycle::None: return "inactive";
    }
    return "inactive";
}

const char* aim_region_source_name(
    pipeline_contract::AimRegionSource source) noexcept {
    switch (source) {
    case pipeline_contract::AimRegionSource::VisionGeometry:
        return "vision_geometry";
    case pipeline_contract::AimRegionSource::BodyBoxFallback:
        return "body_box_fallback";
    case pipeline_contract::AimRegionSource::CueTranslated:
        return "cue_translated";
    case pipeline_contract::AimRegionSource::None:
        return "none";
    }
    return "none";
}

const char* desired_point_source_name(
    pipeline_contract::DesiredPointSource source) noexcept {
    switch (source) {
    case pipeline_contract::DesiredPointSource::VisionDefault:
        return "vision_default";
    case pipeline_contract::DesiredPointSource::UserCorrected:
        return "user_corrected";
    case pipeline_contract::DesiredPointSource::CueCarried:
        return "cue_carried";
    case pipeline_contract::DesiredPointSource::None:
        return "none";
    }
    return "none";
}

TargetCoordinatorConfig coordinator_config(const GamepadRuntimeConfig& config) {
    TargetCoordinatorConfig result{};
    result.max_observation_age_ms = std::max(
        1.0f, config.tracker.max_observation_age_ms);
    result.settle_radius_px = std::max(1.0f, config.ai_aim.ads_completion_radius_px);
    result.settle_frames = static_cast<std::uint32_t>(
        std::max(1, config.ai_aim.ads_completion_fresh_frames));
    result.ads_nominal_acquisition_ms = std::max(
        1.0f, static_cast<float>(config.ai_aim.ads_snap_window_ms));
    result.ads_target_wait_ms = std::max(
        0.0f, config.ai_aim.ads_target_wait_ms);
    result.ads_extension_budget_ms = std::max(0.0f, config.ai_aim.ads_extension_budget_ms);
    result.ads_activation_radius_px = std::max(
        result.settle_radius_px, config.ai_aim.ads_activation_radius_px);
    result.ads_pickup_base_radius_px = std::max(
        0.0f, config.ai_aim.ads_pickup_base_radius_px);
    result.bodylock_activation_radius_px = std::max(
        result.settle_radius_px, config.ai_aim.body_lock_activation_box_px);
    result.bodylock_exit_radius_px = std::max(
        result.settle_radius_px * 2.0f,
        config.ai_aim.body_lock_box_tolerance_px * 3.0f);
    result.desired_point_traversal_ms =
        config.ai_aim.desired_point_traversal_ms;
    result.desired_point_boundary_exit_ms =
        config.ai_aim.desired_point_boundary_exit_ms;
    result.visual_authority_enabled =
        config.ai_aim.visual_authority_enabled;
    return result;
}

std::uint64_t seconds_to_ns(double seconds) noexcept {
    if (!std::isfinite(seconds) || seconds <= 0.0) return 0;
    return static_cast<std::uint64_t>(seconds * 1'000'000'000.0);
}

AdsAcquisitionControllerConfig ads_config(const GamepadRuntimeConfig& config) {
    AdsAcquisitionControllerConfig result{};
    result.max_force_x = config.ai_aim.ads_snap_max_ai_force;
    result.max_force_y = config.ai_aim.ads_snap_max_ai_force_y;
    result.arrival_horizon_seconds = std::clamp(
        static_cast<float>(config.ai_aim.ads_snap_window_ms) / 1000.0f,
        0.060f,
        0.350f);
    result.response_curve = config.aim_response_curve;
    return result;
}

BodylockFollowControllerConfig bodylock_config(const GamepadRuntimeConfig& config) {
    BodylockFollowControllerConfig result{};
    result.max_force_x = config.ai_aim.body_lock_max_ai_force;
    result.max_force_y = config.ai_aim.body_lock_max_ai_force_y;
    result.feedback_range_x_px = std::max(
        18.0f, config.ai_aim.body_lock_box_tolerance_px * 1.5f);
    result.feedback_range_y_px = result.feedback_range_x_px;
    result.feedforward_gain = 0.72f;
    result.response_curve = config.aim_response_curve;
    return result;
}

AimResponseEstimatorConfig ads_response_estimator_config() {
    AimResponseEstimatorConfig result{};
    // ADS needs accumulated evidence, but should approach a measured
    // slowdown conservatively before it is allowed to replace the established
    // response estimate used by the rest of the controller.
    result.scale_alpha = 0.04f;
    result.confidence_alpha = 0.02f;
    return result;
}

AssistControlStateMachineConfig assist_control_config(
    const GamepadRuntimeConfig& config) {
    AssistControlStateMachineConfig result{};
    result.capture_settle_radius_px = std::max(
        2.0f, config.ai_aim.ads_completion_radius_px);
    result.capture_settle_fresh_frames = 2;
    result.capture_timeout_ms = std::clamp(
        static_cast<float>(config.ai_aim.ads_snap_window_ms),
        60.0f,
        135.0f);
    return result;
}

}  // namespace

NativeGamepadController::NativeGamepadController(
    GamepadRuntimeConfig config,
    const double* injected_clock_seconds)
    : config_(config),
      target_coordinator_(coordinator_config(config)),
      ads_response_estimator_(ads_response_estimator_config()),
      ads_controller_(ads_config(config)),
      ads_reacquisition_reducer_({
          config.ai_aim.body_lock_activation_box_px,
          8}),
      bodylock_controller_(bodylock_config(config)),
      assist_control_state_machine_(assist_control_config(config)),
      recoil_(config_.recoil),
      auto_fire_gate_(config_.auto_fire, config_.ai_aim),
      injected_clock_seconds_(injected_clock_seconds) {
    // The incident fit is strongest at 8.25-9.0 ms on both axes. Keep this a
    // bounded configuration of plant identification, not an actuation delay.
    aim_response_effect_delay_seconds_ =
        static_cast<double>(std::clamp(
            config_.ai_aim.aim_response_effect_delay_ms,
            0.0f,
            30.0f)) /
        1000.0;
}

void NativeGamepadController::reset() {
    intent_filter_.reset();
    operation_intent_classifier_.reset();
    last_operation_intent_ = {};
    target_coordinator_.reset();
    aim_response_estimator_.reset();
    ads_response_estimator_.reset();
    bodylock_target_motion_observer_.reset();
    nonfiring_pov_motion_snapshot_ = {};
    nonfiring_pov_error_snapshot_ = {};
    nonfiring_pov_left_stick_ = {};
    nonfiring_pov_motion_target_id_ = 0;
    nonfiring_pov_motion_generation_ = 0;
    nonfiring_pov_motion_ads_epoch_ = 0;
    nonfiring_pov_motion_seconds_ = -1.0;
    ads_reacquisition_reducer_.reset();
    dynamics_shaper_.reset();
    assist_control_state_machine_.reset();
    recoil_.reset();
    input_edge_reducer_.reset();
    aim_scope_reducer_.reset();
    auto_fire_gate_.reset();
    pending_snapshot_ = {};
    has_pending_snapshot_ = false;
    physical_aiming_ = false;
    aiming_ = false;
    last_firing_activity_seconds_ = -1.0;
    ads_epoch_ = 0;
    last_tick_seconds_ = 0.0;
    aim_response_command_history_ = {};
    aim_response_history_begin_ = 0;
    aim_response_history_count_ = 0;
    last_aim_response_frame_id_ = 0;
    last_aim_response_target_id_ = 0;
    last_ads_response_epoch_ = 0;
    last_aim_response_capture_seconds_ = 0.0;
    last_aim_response_source_error_px_ = {};
    has_last_aim_response_observation_ = false;
    sampled_physical_ = {};
    sampled_intent_ = {};
    last_tick_preparation_ = {};
    sampled_dt_seconds_ = 0.001f;
    has_sampled_input_ = false;
    next_controller_tick_id_ = 1;
    next_command_sequence_ = 1;
    composed_output_pending_ = false;
    pending_auto_fire_active_ = false;
    last_pipeline_traces_.clear();
    last_acquisition_trace_ = {};
    acquisition_trace_target_id_ = 0;
    last_output_components_ = {};
    last_frame_vision_state_ = {};
    last_target_plan_ = {};
    last_ai_aim_mode_ = "manual";
}

void NativeGamepadController::submit_vision_snapshot(const ControllerVisionSnapshot& snapshot) {
    pending_snapshot_ = snapshot;
    has_pending_snapshot_ = true;
}

pipeline_contract::VisionObservationBatch NativeGamepadController::observation_batch_from(
    const ControllerVisionSnapshot& snapshot) const noexcept {
    pipeline_contract::VisionObservationBatch batch{};
    batch.frame_id = snapshot.frame_id;
    batch.preferred_source_id = snapshot.selected_observation_id;
    batch.source_time_seconds = snapshot.capture_time_seconds;
    batch.publish_time_seconds = snapshot.ready_time_seconds;
    batch.frame_width_px = snapshot.state.screen_center_x > 0.0f
        ? snapshot.state.screen_center_x * 2.0f : 480.0f;
    batch.frame_height_px = snapshot.state.screen_center_y > 0.0f
        ? snapshot.state.screen_center_y * 2.0f : 416.0f;
    batch.capture_fresh = snapshot.frame_updated || snapshot.state.fresh_observation;
    batch.selector_identity_protocol = snapshot.selector_identity_protocol;
    batch.selector_target_generation = snapshot.selector_target_generation;
    batch.selector_target_changed = snapshot.selector_target_changed;
    batch.selector_enemy_cue_current = snapshot.enemy_cue_current;
    batch.selector_enemy_identity_confirmed = snapshot.enemy_identity_confirmed;
    batch.selector_cue_continuation =
        snapshot.selector_identity_protocol &&
        snapshot.selector_target_generation != 0 &&
        !snapshot.selector_target_changed &&
        snapshot.selected_observation_id == 0 &&
        snapshot.state.has_target && snapshot.state.aim_authority &&
        tracking_native::is_cue_hold_observation(
            snapshot.state.target_tier);
    batch.fire_requested = snapshot.state.auto_fire_requested;
    batch.observed_fire_eligible = snapshot.state.fire_authority &&
        (snapshot.state.target_tier == "strong" ||
         snapshot.state.target_tier == "observed_strong");
    batch.rejected_friendly_count = snapshot.rejected_friendly_count;
    batch.rejected_low_reliability_count = snapshot.rejected_low_reliability_count;
    const auto limit = std::min<std::size_t>(
        snapshot.candidates.size(), pipeline_contract::kMaxVisionCandidates);
    for (std::size_t index = 0; index < limit; ++index) {
        const auto& source = snapshot.candidates[index];
        if (!source.valid || source.is_friendly) continue;
        auto& destination = batch.candidates[batch.count++];
        destination.source_id = source.id;
        const float width = std::max(0.0f, source.body_box_px.w);
        const float height = std::max(0.0f, source.body_box_px.h);
        destination.aim_px = {
            source.aim_point_px.x, source.aim_point_px.y};
        destination.has_aim_point = source.has_aim_point;
        destination.box_size_px = {width, height};
        destination.body_box_px = source.body_box_px;
        destination.has_body_box = width > 0.0f && height > 0.0f;
        destination.aim_region_px = source.aim_region_px;
        destination.aim_region_source = source.aim_region_source;
        destination.has_aim_region = source.has_aim_region &&
            source.aim_region_px.w > 1.0f &&
            source.aim_region_px.h > 1.0f;
        if (!destination.has_aim_region && destination.has_body_box &&
            destination.has_aim_point) {
            // Explicit compatibility boundary for fixtures/legacy producers:
            // use the observed body box as conservative R, but never recompute
            // the selector's anatomical point from a second height ratio.
            destination.aim_region_px = source.body_box_px;
            destination.aim_region_source =
                pipeline_contract::AimRegionSource::BodyBoxFallback;
            destination.has_aim_region = true;
        }
        destination.motion_anchor_px = {
            source.motion_anchor_px.x, source.motion_anchor_px.y};
        destination.motion_anchor_score =
            std::clamp(source.motion_anchor_score, 0.0f, 1.0f);
        destination.has_motion_anchor =
            source.has_motion_anchor &&
            source.motion_anchor_score >= 0.20f;
        destination.confidence = std::clamp(source.confidence, 0.0f, 1.0f);
        destination.cue_confidence = std::clamp(source.cue_score, 0.0f, 1.0f);
        if (source.id == snapshot.selected_observation_id) {
            batch.selector_enemy_cue_checked = source.color_classified;
        }
        destination.normalized_size = std::clamp(
            height / std::max(1.0f, batch.frame_height_px), 0.0f, 1.0f);
        // Target size is not visibility: a small but crisp enemy in an optic
        // may be more trustworthy than a large false person. Detector/geometry
        // confidence remains the base; selector enemy evidence caps authority
        // later in TargetCoordinator.
        destination.reliability = destination.confidence;
        destination.body_cue = source.has_cue_point;
    }
    // The selector owns target identity, D and R. The only target-shaped state
    // that may arrive without a person candidate is an explicit same-generation
    // cue continuation. Any other has_target/candidate mismatch must fail closed
    // in TargetCoordinator instead of creating a second controller-side target
    // protocol from the flattened legacy state.
    if (batch.count == 0 && batch.selector_cue_continuation) {
        auto& destination = batch.candidates[0];
        batch.count = 1;
        destination.source_id = snapshot.state.selected_track_id != 0
            ? snapshot.state.selected_track_id
            : snapshot.state.selected_observation_id != 0
                ? snapshot.state.selected_observation_id
                : snapshot.selected_observation_id;
        const float center_x = snapshot.state.screen_center_x > 0.0f
            ? snapshot.state.screen_center_x : batch.frame_width_px * 0.5f;
        const float center_y = snapshot.state.screen_center_y > 0.0f
            ? snapshot.state.screen_center_y : batch.frame_height_px * 0.5f;
        const bool has_absolute_aim =
            snapshot.state.target_x != 0.0f || snapshot.state.target_y != 0.0f;
        const common_native::Vec2f vision_aim = has_absolute_aim
            ? common_native::Vec2f{snapshot.state.target_x, snapshot.state.target_y}
            : common_native::Vec2f{
                center_x + snapshot.state.dx,
                center_y + snapshot.state.dy};
        const float height = std::max(0.0f, snapshot.state.body_y2 - snapshot.state.body_y1);
        const common_native::Box2f body_box{
            snapshot.state.body_x1,
            snapshot.state.body_y1,
            std::max(0.0f, snapshot.state.body_x2 - snapshot.state.body_x1),
            height};
        destination.aim_px = {vision_aim.x, vision_aim.y};
        destination.has_aim_point = true;
        destination.box_size_px = {
            body_box.w, height};
        destination.body_box_px = body_box;
        destination.has_body_box = snapshot.state.has_body_box;
        const common_native::Box2f state_region{
            snapshot.state.aim_region_x1,
            snapshot.state.aim_region_y1,
            std::max(
                0.0f,
                snapshot.state.aim_region_x2 - snapshot.state.aim_region_x1),
            std::max(
                0.0f,
                snapshot.state.aim_region_y2 - snapshot.state.aim_region_y1)};
        destination.aim_region_px = snapshot.state.has_aim_region
            ? state_region : body_box;
        destination.aim_region_source =
            pipeline_contract::AimRegionSource::CueTranslated;
        destination.has_aim_region =
            destination.aim_region_px.w > 1.0f &&
            destination.aim_region_px.h > 1.0f;
        destination.normalized_size = std::clamp(
            height / std::max(1.0f, batch.frame_height_px), 0.0f, 1.0f);
        destination.confidence = 1.0f;
        destination.cue_confidence = 1.0f;
        destination.reliability = destination.confidence * (height > 0.0f
            ? std::clamp(
                destination.normalized_size /
                    kCueHoldReducedForceMaxTargetHeightRatio,
                0.35f,
                1.0f)
            : 1.0f);
        destination.body_cue = snapshot.state.has_body_box;
    }
    return batch;
}

NativeControllerVisionState NativeGamepadController::vision_state_from_plan(
    const pipeline_contract::TargetPlan& plan,
    double now_seconds,
    bool capture_fresh,
    pipeline_contract::Vec2f observed_error_px) const {
    NativeControllerVisionState state{};
    state.vision_sequence = plan.source_frame_id;
    state.selected_track_id = plan.target_id;
    // Observation and persistent target identities are different join keys.
    // The controller sample must carry the source observation chosen by the
    // plan, never the long-lived target identity.
    state.selected_observation_id = plan.source_observation_id;
    state.has_target = plan.lifecycle != pipeline_contract::TargetLifecycle::None;
    state.current_observed_target_present = !plan.cue_continuation &&
        (plan.lifecycle == pipeline_contract::TargetLifecycle::Observed ||
         plan.fire_authority);
    state.fresh_observation =
        capture_fresh && state.current_observed_target_present;
    state.aim_authority = plan.aim_authority > 0.0f;
    state.fire_authority = !plan.cue_continuation && plan.fire_authority;
    state.auto_fire_requested = !plan.cue_continuation && plan.fire_requested;
    // D is desired geometry, while source_aim_px is observation geometry.
    // plan.error_px is the D-to-reticle control request; it must never be
    // mistaken for a fresh source observation.
    state.dx = observed_error_px.x;
    state.dy = observed_error_px.y;
    state.target_x = plan.aim_px.x;
    state.target_y = plan.aim_px.y;
    state.screen_center_x = plan.aim_px.x - observed_error_px.x;
    state.screen_center_y = plan.aim_px.y - observed_error_px.y;
    state.has_aim_region = plan.has_aim_region;
    state.aim_region_x1 = plan.aim_region_px.x;
    state.aim_region_y1 = plan.aim_region_px.y;
    state.aim_region_x2 = plan.aim_region_px.x + plan.aim_region_px.w;
    state.aim_region_y2 = plan.aim_region_px.y + plan.aim_region_px.h;
    state.observed_at_seconds = now_seconds - plan.observation_age_ms / 1000.0;
    state.target_tier = plan.cue_continuation
        ? "cue_hold"
        : state.current_observed_target_present ? "observed_strong" : "none";
    return state;
}

const NativeControlTickPreparation& NativeGamepadController::begin_tick(
    const PhysicalGamepadState& physical,
    std::uint64_t tick_id) {
    const std::uint64_t resolved_tick_id = tick_id != 0
        ? tick_id
        : next_controller_tick_id_++;
    if (resolved_tick_id >= next_controller_tick_id_) {
        next_controller_tick_id_ = resolved_tick_id + 1;
    }
    const double now = now_seconds();
    const float dt = last_tick_seconds_ > 0.0
        ? static_cast<float>(std::clamp(now - last_tick_seconds_, 0.0001, 0.05))
        : 0.001f;
    last_tick_seconds_ = now;
    const InputEdgeSnapshot input_edges = input_edge_reducer_.sample(
        physical,
        config_.rb_counts_as_aiming,
        &next_command_sequence_,
        config_.ai_aim.ads_scope_ready_trigger);
    const AimScopeSnapshot scope = aim_scope_reducer_.reduce(
        input_edges,
        config_.auto_fire.manual_fire_activates_ai_aim);
    physical_aiming_ = scope.physical_ads_ready;
    aiming_ = scope.physical_ads_ready || scope.manual_fire_active;
    const auto reacquisition = ads_reacquisition_reducer_.on_input(
        scope,
        last_target_plan_,
        input_edges.cause_event);
    const bool acquisition_rearm = reacquisition.begin_ads_epoch;
    if (aiming_ && acquisition_rearm) {
        target_coordinator_.begin_ads_epoch(++ads_epoch_, now);
        assist_control_state_machine_.reset();
        auto_fire_gate_.reset_readiness();
    }
    sampled_physical_ = physical;
    sampled_dt_seconds_ = dt;
    sampled_intent_ = intent_filter_.update(
        {physical.left_x, physical.left_y},
        {physical.right_x, physical.right_y},
        aiming_, manual_fire_pressed(physical), now,
        last_target_plan_.target_id != 0,
        last_output_components_.handover_requested);
    has_sampled_input_ = true;
    last_tick_preparation_.tick_id = resolved_tick_id;
    last_tick_preparation_.now_seconds = now;
    last_tick_preparation_.dt_seconds = dt;
    last_tick_preparation_.scope = scope;
    last_tick_preparation_.intent = sampled_intent_;
    last_tick_preparation_.input_cause = input_edges.cause_event;
    last_tick_preparation_.acquisition_rearmed = acquisition_rearm;
    return last_tick_preparation_;
}

GamepadOutputState NativeGamepadController::build_output(
    const PhysicalGamepadState& physical) {
    (void)begin_tick(physical);
    return build_output_from_sampled_input();
}

GamepadOutputState NativeGamepadController::build_output_from_sampled_input() {
    ControlFrame frame = resolve_control_frame();
    OutputComposer composer;
    if (composer.compose(frame) != OutputComposeStatus::Ok ||
        composer.finalized_output() == nullptr) {
        return {};
    }
    const GamepadOutputState output = *composer.finalized_output();
    observe_composed_output(output);
    return output;
}

ControlFrame NativeGamepadController::resolve_control_frame() {
    if (!has_sampled_input_) return {};
    const PhysicalGamepadState physical = sampled_physical_;
    const pipeline_contract::IntentState intent = sampled_intent_;
    // Input edges retain begin_tick's sample time. Observation freshness and
    // control decisions happen after capture/mailbox delivery on this tick.
    const double now = now_seconds();
    const float dt = sampled_dt_seconds_;
    has_sampled_input_ = false;

    const auto controller_tick = pipeline_contract::ControllerTickId::from(
        last_tick_preparation_.tick_id);
    const auto sample_sequence = pipeline_contract::EventSequence::from(
        next_command_sequence_++);
    ControlFrame frame = ControlFrame::begin(
        physical, controller_tick, sample_sequence);

    last_pipeline_traces_.clear();
    NativeControllerOutputComponents components{};
    components.physical_stick = {physical.right_x, physical.right_y};
    components.manual_stick = {physical.right_x, physical.right_y};
    components.target_final_stick = components.manual_stick;
    components.final_stick = components.manual_stick;
    components.fire_button = manual_fire_pressed(physical);
    components.filtered_manual_stick = {
        intent.filtered_right.x,
        intent.filtered_right.y};
    components.manual_confidence = intent.right_confidence;

    pipeline_contract::VisionObservationBatch observations{};
    if (has_pending_snapshot_) {
        observations = observation_batch_from(pending_snapshot_);
        has_pending_snapshot_ = false;
    } else {
        observations.frame_width_px = last_frame_vision_state_.screen_center_x > 0.0f
            ? last_frame_vision_state_.screen_center_x * 2.0f : 480.0f;
        observations.frame_height_px = last_frame_vision_state_.screen_center_y > 0.0f
            ? last_frame_vision_state_.screen_center_y * 2.0f : 416.0f;
    }
    const auto fresh_reacquisition =
        ads_reacquisition_reducer_.on_fresh_observation(
            observations,
            last_target_plan_,
            last_tick_preparation_.scope.physical_ads_ready);
    if (fresh_reacquisition.begin_ads_epoch &&
        last_tick_preparation_.scope.physical_ads_ready) {
        target_coordinator_.begin_ads_epoch(++ads_epoch_, now);
        assist_control_state_machine_.reset();
        auto_fire_gate_.reset_readiness();
        last_tick_preparation_.acquisition_rearmed = true;
    }
    TargetControlFeedback control_feedback{};
    control_feedback.previous_delivered_stick = {
        last_output_components_.before_recoil_stick.x,
        last_output_components_.before_recoil_stick.y};
    const auto aim_response_before_update = aim_response_estimator_.estimate();
    control_feedback.aim_response_px_per_stick_second =
        aim_response_before_update.scale_px_per_stick_second;
    // estimate() already blends toward its safe fallback while confidence is low.
    control_feedback.aim_response_confidence = aim_response_before_update.confidence;
    constexpr double kFiringDisturbanceWindowSeconds = 0.075;
    control_feedback.firing_recently =
        manual_fire_pressed(physical) ||
        (last_firing_activity_seconds_ >= 0.0 &&
         now - last_firing_activity_seconds_ <=
             kFiringDisturbanceWindowSeconds);
    auto plan = target_coordinator_.update(
        observations, intent, now, control_feedback);
    const float aim_response_zone_weight = aim_response_slow_zone_weight(
        plan.error_px, plan.ads_target_size_px);
    if (plan.target_id != 0) {
        const auto localized_response = aim_response_estimator_.estimate(
            aim_response_zone_weight);
        plan.response_scale = std::max(
            50.0f, localized_response.scale_px_per_stick_second);
        plan.response_confidence = localized_response.confidence;
        if (plan.mode == pipeline_contract::ControlMode::AdsAcquire) {
            const auto ads_response = ads_response_estimator_.estimate(
                aim_response_zone_weight);
            // Do not replace a mature legacy estimate with an ADS model that
            // has only primed its history. Eight accepted multi-anchor fits
            // and 0.35 confidence require repeated, mutually consistent
            // evidence rather than one apparent slowdown transition.
            constexpr std::uint32_t kMinimumAdsResponseSamples = 8;
            constexpr float kMinimumAdsResponseConfidence = 0.35f;
            if (ads_response.accepted_samples >=
                    kMinimumAdsResponseSamples &&
                ads_response.confidence >=
                    kMinimumAdsResponseConfidence) {
                plan.response_scale = std::max(
                    50.0f, ads_response.scale_px_per_stick_second);
                plan.response_confidence = ads_response.confidence;
            }
        }
    }
    if (plan.cue_continuation) {
        // A same-generation cue may continue ADS identity, but must not create
        // an arrival-time gain step. Only conservative BodyLock continuation
        // is evidence-scaled after the ADS acquisition lifecycle has ended.
        if (plan.mode == pipeline_contract::ControlMode::BodyLockFollow) {
            plan.aim_authority *= cue_hold_bodylock_force_scale(
                config_.ai_aim,
                plan.normalized_size);
            plan.visual_authority = plan.aim_authority;
        }
        plan.fire_authority = false;
        plan.fire_requested = false;
        plan.fire_suppression = pipeline_contract::FireSuppressionReason::AimOnly;
    }
    const pipeline_contract::Vec2f screen_center{
        plan.aim_px.x - plan.error_px.x,
        plan.aim_px.y - plan.error_px.y};
    const pipeline_contract::Vec2f source_error_px{
        plan.source_aim_px.x - screen_center.x,
        plan.source_aim_px.y - screen_center.y};
    components.observed_error_px = {source_error_px.x, source_error_px.y};
    components.control_error_px = {plan.error_px.x, plan.error_px.y};
    components.source_aim_px = {plan.source_aim_px.x, plan.source_aim_px.y};
    components.desired_aim_px = {plan.aim_px.x, plan.aim_px.y};
    components.desired_point_normalized = {
        plan.desired_point_normalized.x,
        plan.desired_point_normalized.y};
    components.aim_region_px = plan.aim_region_px;
    components.has_aim_region = plan.has_aim_region;
    components.visual_authority = plan.visual_authority;
    components.enemy_cue_current = plan.enemy_cue_current;
    components.enemy_identity_confirmed = plan.enemy_identity_confirmed;
    components.enemy_cue_checked = plan.enemy_cue_checked;
    components.aim_region_source = aim_region_source_name(
        plan.aim_region_source);
    components.desired_point_source = desired_point_source_name(
        plan.desired_point_source);
    const std::uint64_t plan_decision_ns = seconds_to_ns(now);

    if (plan.target_acquisition_id != acquisition_trace_target_id_) {
        last_acquisition_trace_ = {};
        acquisition_trace_target_id_ = plan.target_acquisition_id;
    }
    last_acquisition_trace_.valid = observations.capture_fresh &&
        observations.frame_id != 0 && plan.source_frame_id == observations.frame_id;
    last_acquisition_trace_.source_frame_id = plan.source_frame_id;
    last_acquisition_trace_.source_observation_id = plan.source_observation_id;
    last_acquisition_trace_.persistent_target_id = plan.target_id;
    last_acquisition_trace_.physical_ads_epoch = plan.physical_ads_epoch;
    last_acquisition_trace_.target_acquisition_id = plan.target_acquisition_id;
    last_acquisition_trace_.plan_decision_ns = plan_decision_ns;
    last_acquisition_trace_.final_output_ready_ns = 0;
    last_acquisition_trace_.plan_admitted = plan.ads_plan_admitted;
    last_acquisition_trace_.acquisition_active = plan.ads_acquisition_active;
    last_acquisition_trace_.acquisition_exists = plan.ads_acquisition_exists;
    last_acquisition_trace_.controller_tick_ns = seconds_to_ns(now);
    last_acquisition_trace_.acquisition_state = plan.ads_acquisition_state;
    last_acquisition_trace_.source_decision_available =
        plan.source_decision_available;
    last_acquisition_trace_.source_decision_outcome =
        plan.source_decision_outcome;
    last_acquisition_trace_.source_decision_reason =
        plan.source_decision_reason;
    last_acquisition_trace_.acquisition_terminal_reason =
        plan.acquisition_terminal_reason;
    last_acquisition_trace_.decision_reason = plan.ads_decision_reason;
    last_acquisition_trace_.candidate_count = plan.ads_candidate_count;
    last_acquisition_trace_.preferred_source_id = plan.ads_preferred_source_id;
    last_acquisition_trace_.selected_source_id = plan.ads_selected_source_id;
    last_acquisition_trace_.effective_activation_radius_px =
        plan.ads_activation_radius_px;
    last_acquisition_trace_.raw_error_px = plan.ads_raw_error_px;
    last_acquisition_trace_.target_size_px = plan.ads_target_size_px;
    last_acquisition_trace_.ads_acquisition_begin_ns = plan.ads_acquisition_begin_ns;
    last_acquisition_trace_.ads_acquisition_complete_ns =
        plan.ads_acquisition_complete_ns;
    last_acquisition_trace_.selector_target_generation =
        plan.selector_target_generation;
    last_acquisition_trace_.selector_target_changed =
        plan.selector_target_changed;
    const bool new_observed_frame = observations.count > 0 &&
        !plan.cue_continuation &&
        observations.frame_id != 0 &&
        observations.frame_id != last_aim_response_frame_id_;
    if (new_observed_frame) {
        const double capture_seconds = observations.source_time_seconds;
        const bool same_response_target =
            has_last_aim_response_observation_ &&
            plan.target_id != 0 &&
            plan.target_id == last_aim_response_target_id_;
        if (!same_response_target) {
            aim_response_estimator_.begin_target(plan.target_id);
            ads_response_estimator_.begin_target(plan.target_id);
            bodylock_target_motion_observer_.begin_target(plan.target_id);
        } else {
            if (plan.mode == pipeline_contract::ControlMode::AdsAcquire &&
                plan.physical_ads_epoch != 0 &&
                plan.physical_ads_epoch != last_ads_response_epoch_) {
                // Preserve learned weapon response, but never regress a new
                // physical ADS event against anchors from an older token or
                // an intervening BodyLock interval.
                ads_response_estimator_.begin_target(plan.target_id);
            }
            const double interval_seconds =
                capture_seconds - last_aim_response_capture_seconds_;
            pipeline_contract::Vec2f average_stick{};
            bool manual_ambiguous = false;
            const bool command_window_available =
                std::isfinite(interval_seconds) && interval_seconds > 0.0 &&
                average_aim_response_command(
                    last_aim_response_capture_seconds_ -
                        aim_response_effect_delay_seconds_,
                    capture_seconds - aim_response_effect_delay_seconds_,
                    &average_stick,
                    &manual_ambiguous);
            if (command_window_available) {
                // Use the unfiltered source-position delta here. The
                // coordinator's BodyLock velocity deliberately limits target
                // acceleration; feeding that limited value into plant
                // identification aliases a fast camera into a slow one.
                const pipeline_contract::Vec2f observed_error_rate{
                    static_cast<float>(
                        (source_error_px.x -
                         last_aim_response_source_error_px_.x) /
                        interval_seconds),
                    static_cast<float>(
                        (source_error_px.y -
                         last_aim_response_source_error_px_.y) /
                        interval_seconds),
                };
                aim_response_estimator_.update({
                    average_stick,
                    observed_error_rate,
                    plan.target_id,
                    static_cast<float>(interval_seconds),
                    plan.reliability,
                    // A target-only acceleration signal is not available at
                    // this boundary. Manual/fire ambiguity is rejected by the
                    // causally aligned command ledger instead.
                    0.0f,
                    aim_response_zone_weight,
                    plan.lifecycle ==
                        pipeline_contract::TargetLifecycle::Observed,
                    manual_ambiguous,
                });
                if (plan.mode == pipeline_contract::ControlMode::AdsAcquire) {
                    ads_response_estimator_.update({
                        average_stick,
                        observed_error_rate,
                        plan.target_id,
                        static_cast<float>(interval_seconds),
                        plan.reliability,
                        0.0f,
                        aim_response_zone_weight,
                        plan.lifecycle ==
                            pipeline_contract::TargetLifecycle::Observed,
                        manual_ambiguous,
                    });
                }
                bodylock_target_motion_observer_.update({
                    plan.target_id,
                    capture_seconds,
                    static_cast<float>(interval_seconds),
                    observed_error_rate,
                    average_stick,
                    plan.response_scale,
                    plan.reliability,
                    plan.direct_person_observation,
                });
            }
        }
        last_aim_response_frame_id_ = observations.frame_id;
        last_aim_response_target_id_ = plan.target_id;
        if (plan.mode == pipeline_contract::ControlMode::AdsAcquire &&
            plan.physical_ads_epoch != 0) {
            last_ads_response_epoch_ = plan.physical_ads_epoch;
        }
        last_aim_response_capture_seconds_ = capture_seconds;
        last_aim_response_source_error_px_ = source_error_px;
        has_last_aim_response_observation_ = plan.target_id != 0 &&
            std::isfinite(capture_seconds) &&
            pipeline_contract::finite(source_error_px);
    }
    if (plan.target_id == 0 ||
        plan.lifecycle == pipeline_contract::TargetLifecycle::None) {
        bodylock_target_motion_observer_.reset();
        nonfiring_pov_motion_snapshot_ = {};
        nonfiring_pov_error_snapshot_ = {};
        nonfiring_pov_motion_target_id_ = 0;
        nonfiring_pov_motion_generation_ = 0;
        nonfiring_pov_motion_ads_epoch_ = 0;
        nonfiring_pov_motion_seconds_ = -1.0;
    } else {
        auto target_motion = bodylock_target_motion_observer_.estimate(
            plan.target_id, now);
        const pipeline_contract::Vec2f current_left{
            physical.left_x, physical.left_y};
        // Lateral left-stick motion is direct evidence for horizontal POV
        // translation. It does not establish vertical target motion: that may
        // instead be jump/slide geometry, recoil, or target animation.
        const bool current_lateral_pov_motion =
            std::fabs(current_left.x) >= 0.15f;
        const float lateral_input_change = std::fabs(
            current_left.x - nonfiring_pov_left_stick_.x);
        if (nonfiring_pov_motion_snapshot_.valid &&
            (!current_lateral_pov_motion || lateral_input_change > 0.12f)) {
            nonfiring_pov_motion_snapshot_ = {};
            nonfiring_pov_error_snapshot_ = {};
            nonfiring_pov_motion_target_id_ = 0;
            nonfiring_pov_motion_generation_ = 0;
            nonfiring_pov_motion_ads_epoch_ = 0;
            nonfiring_pov_motion_seconds_ = -1.0;
        }
        if (!control_feedback.firing_recently &&
            current_lateral_pov_motion &&
            new_observed_frame &&
            plan.selector_target_generation != 0 &&
            plan.lifecycle == pipeline_contract::TargetLifecycle::Observed &&
            target_motion.valid) {
            // Freeze the last non-firing estimate together with the physical
            // POV input that produced it. Firing observations continue down
            // the ordinary visible path, but cannot become the evidence used
            // to bridge a smoke/kick gap.
            nonfiring_pov_motion_snapshot_ = target_motion;
            nonfiring_pov_error_snapshot_ = plan.error_px;
            nonfiring_pov_left_stick_ = current_left;
            nonfiring_pov_motion_target_id_ = plan.target_id;
            nonfiring_pov_motion_generation_ =
                plan.selector_target_generation;
            nonfiring_pov_motion_ads_epoch_ = plan.physical_ads_epoch;
            nonfiring_pov_motion_seconds_ = now;
        }
        bool cue_motion_eligible = false;
        if (plan.lifecycle ==
                pipeline_contract::TargetLifecycle::CueContinuation) {
            // Do not turn a valid pre-fire observation into a longer-lived
            // motion model. This matches BodylockTargetMotionObserver's
            // existing 55 ms maximum hold horizon and remains well inside the
            // separate 180 ms identity-only cue lifetime.
            constexpr double kMaximumFiringPovSnapshotAgeSeconds =
                static_cast<double>(
                    BodylockTargetMotionObserverConfig{}.
                        maximum_hold_seconds);
            const bool snapshot_moves_horizontal_error_outward =
                nonfiring_pov_motion_snapshot_.target_motion_stick.x *
                    nonfiring_pov_error_snapshot_.x > 0.0f;
            // The coordinator exposes the selector generation on source
            // ticks. Zero between source ticks means "not published this
            // tick", not a new generation; any published mismatch still
            // revokes the snapshot immediately.
            const bool generation_matches_or_not_published =
                plan.selector_target_generation == 0 ||
                nonfiring_pov_motion_generation_ ==
                    plan.selector_target_generation;
            cue_motion_eligible = control_feedback.firing_recently &&
                current_lateral_pov_motion &&
                nonfiring_pov_motion_snapshot_.valid &&
                nonfiring_pov_motion_target_id_ == plan.target_id &&
                nonfiring_pov_motion_generation_ != 0 &&
                generation_matches_or_not_published &&
                nonfiring_pov_motion_ads_epoch_ != 0 &&
                nonfiring_pov_motion_ads_epoch_ == plan.physical_ads_epoch &&
                nonfiring_pov_motion_seconds_ >= 0.0 &&
                now >= nonfiring_pov_motion_seconds_ &&
                now - nonfiring_pov_motion_seconds_ <=
                    kMaximumFiringPovSnapshotAgeSeconds &&
                snapshot_moves_horizontal_error_outward;
            if (cue_motion_eligible) {
                target_motion = nonfiring_pov_motion_snapshot_;
                // Only the lateral component is owned by the evidence above.
                target_motion.target_motion_stick.y = 0.0f;
                target_motion.aligned_delivered_stick.y = 0.0f;
            } else {
                target_motion = {};
            }
        }
        // A cue never trains motion. It may consume only the bounded clean
        // snapshot above, under matching same-target and current-POV evidence.
        // The existing cue authority scale remains the force ceiling.
        plan.bodylock_target_motion_valid = target_motion.valid &&
            (plan.lifecycle == pipeline_contract::TargetLifecycle::Observed ||
             cue_motion_eligible);
        plan.bodylock_target_motion_confidence = target_motion.confidence;
        plan.bodylock_aligned_delivered_stick =
            target_motion.aligned_delivered_stick;
        if (target_motion.valid) {
            const float response = std::max(50.0f, plan.response_scale);
            plan.bodylock_target_motion_px_per_sec = {
                target_motion.target_motion_stick.x * response,
                -target_motion.target_motion_stick.y * response,
            };
        }
    }
    last_target_plan_ = plan;
    last_frame_vision_state_ = vision_state_from_plan(
        plan, now, observations.capture_fresh, plan.error_px);
    last_ai_aim_mode_ = mode_name(plan.mode);

    const bool target_authoritative =
        plan.target_id != 0 && plan.aim_authority > 0.0f &&
        plan.mode != pipeline_contract::ControlMode::Manual;
    pipeline_contract::Vec2f requested{};
    pipeline_contract::Vec2f shaped{};
    BodylockFollowControllerOutput bodylock_diagnostics{};
    auto assist_generation_intent = intent;
    assist_generation_intent.filtered_right = {};
    assist_generation_intent.right_x.confidence = 0.0f;
    assist_generation_intent.right_y.confidence = 0.0f;
    assist_generation_intent.right_confidence = 0.0f;
    if (plan.mode == pipeline_contract::ControlMode::AdsAcquire) {
        requested = ads_controller_.compute(
            plan, assist_generation_intent, dt);
    } else if (
        plan.mode == pipeline_contract::ControlMode::BodyLockFollow) {
        bodylock_diagnostics = bodylock_controller_.compute_detailed(
            plan,
            assist_generation_intent,
            dt);
        requested = bodylock_diagnostics.stick;
    }
    shaped = dynamics_shaper_.shape(
        requested,
        assist_generation_intent,
        plan,
        dt,
        {});
    components.bodylock_error_rate_px_per_sec = {
        bodylock_diagnostics.error_rate_px_per_sec.x,
        bodylock_diagnostics.error_rate_px_per_sec.y};
    components.bodylock_position_stick = {
        bodylock_diagnostics.position_stick.x,
        bodylock_diagnostics.position_stick.y};
    components.bodylock_motion_stick = {
        bodylock_diagnostics.motion_stick.x,
        bodylock_diagnostics.motion_stick.y};
    components.bodylock_effective_motion_stick = {
        bodylock_diagnostics.effective_motion_stick.x,
        bodylock_diagnostics.effective_motion_stick.y};
    components.bodylock_radial_motion_bound =
        bodylock_diagnostics.radial_motion_bound_applied;
    switch (bodylock_diagnostics.constraint_reason) {
    case ResponseModelConstraintReason::PositionRadialMotionBound:
        components.bodylock_constraint_reason = "position_radial_motion_bound";
        break;
    case ResponseModelConstraintReason::None:
        components.bodylock_constraint_reason = "none";
        break;
    }
    last_acquisition_trace_.requested_ai = requested;
    last_acquisition_trace_.shaped_ai = shaped;
    const auto has_material_vector = [](pipeline_contract::Vec2f value) {
        return std::isfinite(value.x) && std::isfinite(value.y) &&
            std::hypot(value.x, value.y) > 1.0e-4f;
    };
    if (plan.target_acquisition_id != 0 &&
        plan.mode != pipeline_contract::ControlMode::Manual) {
        if (!last_acquisition_trace_.has_first_requested_ai &&
            has_material_vector(requested)) {
            last_acquisition_trace_.has_first_requested_ai = true;
            last_acquisition_trace_.first_requested_ai_ns = seconds_to_ns(now);
            last_acquisition_trace_.first_requested_ai = requested;
        }
        if (!last_acquisition_trace_.has_first_shaped_ai &&
            has_material_vector(shaped)) {
            last_acquisition_trace_.has_first_shaped_ai = true;
            last_acquisition_trace_.first_shaped_ai_ns = seconds_to_ns(now);
            last_acquisition_trace_.first_shaped_ai = shaped;
        }
    }
    components.requested_assist_stick = {requested.x, requested.y};
    components.shaped_assist_stick = {shaped.x, shaped.y};
    components.ai_aim_stick = components.shaped_assist_stick;
    bool target_manual_passthrough_x = false;
    bool target_manual_passthrough_y = false;
    bool target_manual_correction_x = false;
    bool target_manual_correction_y = false;
    AssistControlPhase assist_control_phase = AssistControlPhase::Manual;
    bool assist_handover_requested = false;
    bool assist_handover_braking = false;
    pipeline_contract::Vec2f pre_recoil_stick{
        physical.right_x, physical.right_y};
    // Mode-specific solvers publish one AI-only desired total. Final
    // manual/AI ownership is decided exactly once here. In particular, ADS is
    // target-first rather than manual+AI addition; BodyLock remains the
    // cooperative mode. Recoil is composed independently after this command.
    AssistControlStateMachineInput control_input;
    control_input.aiming = aiming_;
    control_input.target_authoritative = target_authoritative;
    control_input.fresh_observation = observations.capture_fresh &&
        plan.lifecycle == pipeline_contract::TargetLifecycle::Observed;
    control_input.cue_continuation = plan.cue_continuation;
    control_input.target_id = plan.target_id;
    control_input.selector_target_generation =
        plan.selector_target_generation;
    control_input.now_seconds = now;
    control_input.target_error_px = plan.error_px;
    control_input.mode = plan.mode;
    control_input.ads_acquisition_state = plan.ads_acquisition_state;
    control_input.visual_authority = plan.visual_authority;
    // Preserve the firing/downward invariant across short fire-pulse and
    // controller ordering gaps as well as on the physical fire tick.
    control_input.firing = control_feedback.firing_recently;
    control_input.manual_stick = {
        physical.right_x, physical.right_y};
    control_input.centered_manual_stick = {
        physical.right_x - intent.right_x.neutral_bias,
        physical.right_y - intent.right_y.neutral_bias};
    control_input.centered_manual_available = true;
    control_input.filtered_manual_stick = intent.filtered_right;
    control_input.manual_axis_activity = {
        intent.right_x.activity, intent.right_y.activity};
    control_input.ai_stick = {shaped.x, shaped.y};
    control_input.manual_correction_x = plan.manual_correction_x;
    control_input.manual_correction_y = plan.manual_correction_y;
    control_input.manual_exit_requested = plan.manual_exit_requested;
    control_input.carried_acquisition_gesture =
        plan.mode == pipeline_contract::ControlMode::BodyLockFollow &&
        plan.ads_acquisition_exists &&
        intent.right_purpose ==
            pipeline_contract::UserAimIntentPurpose::AcquireTarget;
    const auto decision = assist_control_state_machine_.update(control_input);
    pre_recoil_stick = {
        clamp_unit(decision.stick.x),
        clamp_unit(decision.stick.y)};
    assist_control_phase = decision.phase;
    assist_handover_requested = decision.handover_requested;
    assist_handover_braking = decision.handover_braking;
    target_manual_passthrough_x = decision.manual_passthrough_x;
    target_manual_passthrough_y = decision.manual_passthrough_y;
    target_manual_correction_x = decision.manual_correction_x;
    target_manual_correction_y = decision.manual_correction_y;
    {
        // Operation-pattern model (§4.5): classify what the user is doing this
        // tick. The classifier is context-conditioned: the same stick push is
        // a recoil pull while firing, a flick during onset toward a new target,
        // a lead when the crosshair rides ahead of a fast target, and simply
        // unreliable when it matches no known template.
        const float target_speed = std::hypot(
            plan.velocity_px_per_sec.x, plan.velocity_px_per_sec.y);
        OperationIntentInput operation_input;
        operation_input.aiming = physical_aiming_;
        operation_input.firing = control_feedback.firing_recently;
        operation_input.target_owned = plan.target_id != 0;
        operation_input.manual_correction =
            plan.manual_correction_x || plan.manual_correction_y;
        operation_input.filtered_right_x = intent.filtered_right.x;
        operation_input.filtered_right_y = intent.filtered_right.y;
        operation_input.right_confidence = intent.right_confidence;
        operation_input.right_phase = intent.right_phase;
        operation_input.right_purpose = intent.right_purpose;
        operation_input.error_px = std::hypot(plan.error_px.x, plan.error_px.y);
        operation_input.target_velocity_px_per_sec = target_speed;
        if (target_speed > 1.0f) {
            operation_input.error_along_motion_px =
                (plan.error_px.x * plan.velocity_px_per_sec.x +
                 plan.error_px.y * plan.velocity_px_per_sec.y) /
                target_speed;
        }
        last_operation_intent_ =
            operation_intent_classifier_.classify(operation_input);
    }
    components.operation_class =
        operation_class_name(last_operation_intent_.operation_class);
    components.operation_confidence =
        last_operation_intent_.class_confidence;
    components.direction_trust = last_operation_intent_.direction_trust;
    components.recoil_pull_strength =
        last_operation_intent_.recoil_pull_strength;
    last_acquisition_trace_.fused_output = pre_recoil_stick;
    if (plan.target_acquisition_id != 0 &&
        plan.mode != pipeline_contract::ControlMode::Manual &&
        !last_acquisition_trace_.has_first_fused_output &&
        has_material_vector(last_acquisition_trace_.fused_output)) {
        last_acquisition_trace_.has_first_fused_output = true;
        last_acquisition_trace_.first_fused_output_ns = seconds_to_ns(now);
        last_acquisition_trace_.first_fused_output =
            last_acquisition_trace_.fused_output;
    }
    components.target_final_stick = {
        pre_recoil_stick.x, pre_recoil_stick.y};
    components.ai_correction_stick = {
        components.target_final_stick.x - components.manual_stick.x,
        components.target_final_stick.y - components.manual_stick.y};
    components.manual_authority_mode = assist_handover_requested ||
            assist_control_phase == AssistControlPhase::HandoverSeek
        ? "handover_seek_manual"
        : assist_handover_braking
            ? "capture_ai_brake"
        : plan.mode == pipeline_contract::ControlMode::AdsAcquire &&
                plan.ads_acquisition_state ==
                    pipeline_contract::AdsAcquisitionState::AcquiringManualSafe
            ? "ads_manual_safe_pursuit"
        : plan.mode == pipeline_contract::ControlMode::AdsAcquire &&
                plan.ads_acquisition_state ==
                    pipeline_contract::AdsAcquisitionState::AcquiringExtended
            ? "ads_extension_cooperative"
        : target_manual_correction_x || target_manual_correction_y
            ? "target_valid_point_correction"
        : target_authoritative &&
                intent.right_purpose ==
                    pipeline_contract::UserAimIntentPurpose::AcquireTarget &&
                std::hypot(
                    intent.filtered_right.x,
                    intent.filtered_right.y) > 0.0f
            ? "target_acquisition_gesture_cooperative"
        : target_authoritative
            ? (target_manual_passthrough_x && target_manual_passthrough_y
            ? "target_present_ai_idle_manual_passthrough"
            : (target_manual_passthrough_x || target_manual_passthrough_y)
                ? "target_present_axis_split"
                : plan.ads_candidate_count > 1
            ? "multi_target_selector_handover"
            : "single_target_authoritative")
        : "no_target_passthrough";
    components.assist_control_phase =
        assist_control_phase_name(assist_control_phase);
    components.manual_passthrough_x = target_manual_passthrough_x;
    components.manual_passthrough_y = target_manual_passthrough_y;
    components.manual_correction_x = target_manual_correction_x;
    components.manual_correction_y = target_manual_correction_y;
    components.manual_boundary_x = plan.manual_boundary_x;
    components.manual_boundary_y = plan.manual_boundary_y;
    components.manual_exit_requested = plan.manual_exit_requested;
    components.handover_requested = assist_handover_requested;
    components.handover_braking = assist_handover_braking;
    components.aim_mode = last_ai_aim_mode_;
    components.assist_authority = plan.aim_authority > 0.0f
        ? (plan.cue_continuation ? "continuity" : "full")
        : "reject";
    components.assist_authority_reason = plan.cue_continuation
        ? "cue_only" : "target_plan";
    components.bodylock_lifecycle = lifecycle_name(plan.lifecycle);
    components.assist_limit_reason = "single_dynamics_shaper";
    record_stage_trace(
        "target_plan_aim",
        physical.right_y,
        pre_recoil_stick.y,
        false,
        false);

    AutoFireGateInput fire_input{};
    fire_input.vision_state = last_frame_vision_state_;
    // A fire-triggered hip-fire search grants aim-assist ownership only. It
    // must not turn on synthetic AutoFire as though physical LT were held.
    fire_input.aiming = physical_aiming_;
    fire_input.ads_min_elapsed = true;
    fire_input.manual_fire_pressed = manual_fire_pressed(physical);
    fire_input.now_seconds = now;
    fire_input.manual_right_x = physical.right_x;
    fire_input.manual_right_y = physical.right_y;
    fire_input.output_right_x = pre_recoil_stick.x;
    fire_input.output_right_y = pre_recoil_stick.y;
    fire_input.settle_dx = plan.error_px.x;
    fire_input.settle_dy = plan.error_px.y;
    const auto fire_reduction = auto_fire_gate_.reduce(
        fire_input,
        controller_tick,
        pipeline_contract::EventSequence::from(next_command_sequence_++),
        sample_sequence);
    const auto& fire = fire_reduction.decision;
    if (fire.should_fire || manual_fire_pressed(physical)) {
        last_firing_activity_seconds_ = now;
    }
    record_stage_trace(
        "auto_fire", pre_recoil_stick.y, pre_recoil_stick.y,
        fire.before_auto_fire_active, fire.after_auto_fire_active);
    components.auto_fire_requested = plan.fire_requested;
    components.auto_fire_aim_ready = fire.aim_ready;
    components.auto_fire_allowed = fire.pre_takeover_should_fire;
    components.auto_fire_active = fire.should_fire;
    components.auto_fire_pulse_starts = fire.counters.pulse_starts;
    components.auto_fire_pulse_pressed = fire.should_fire;
    components.auto_fire_cadence_wait = fire.pulse_waiting;
    components.auto_fire_block_reason = auto_fire_block_reason_name(fire.block_reason);

    components.before_recoil_stick = {
        pre_recoil_stick.x, pre_recoil_stick.y};
    const bool aim_response_manual_ambiguous =
        std::hypot(physical.right_x, physical.right_y) >= 0.35f ||
        fire.should_fire || manual_fire_pressed(physical) ||
        (last_firing_activity_seconds_ >= 0.0 &&
         now - last_firing_activity_seconds_ <=
             kFiringDisturbanceWindowSeconds);
    // Plant identification and both response-model solvers must share one
    // command coordinate. The solvers operate before inverse curve mapping,
    // so convert the delivered virtual stick back into normalized camera
    // response before learning or subtracting aligned camera work.
    const auto aim_response_command = forward_aim_response_curve(
        pre_recoil_stick, config_.aim_response_curve);
    record_aim_response_command(
        now,
        aim_response_command,
        aim_response_manual_ambiguous);
    const auto recoil_contribution = recoil_.reduce(
        fire.should_fire || manual_fire_pressed(physical),
        physical_aiming_,
        now,
        sample_sequence);

    frame.pre_recoil_command() =
        pipeline_contract::PreRecoilStickCommand::from_stick(
            pre_recoil_stick,
            sample_sequence);

    frame.fire_command() = fire_reduction.command;
    frame.recoil_contribution() = recoil_contribution;

    last_output_components_ = components;
    composed_output_pending_ = true;
    pending_auto_fire_active_ = fire.after_auto_fire_active;
    return frame;
}

void NativeGamepadController::record_aim_response_command(
    double at_seconds,
    pipeline_contract::Vec2f stick,
    bool manual_ambiguous) noexcept {
    if (!std::isfinite(at_seconds) || !pipeline_contract::finite(stick)) {
        return;
    }
    if (aim_response_history_count_ > 0) {
        const std::size_t latest_index =
            (aim_response_history_begin_ + aim_response_history_count_ - 1) %
            kAimResponseHistoryCapacity;
        if (at_seconds <=
            aim_response_command_history_[latest_index].at_seconds) {
            if (at_seconds ==
                aim_response_command_history_[latest_index].at_seconds) {
                aim_response_command_history_[latest_index] = {
                    at_seconds, stick, manual_ambiguous};
            }
            return;
        }
    }
    std::size_t write_index = 0;
    if (aim_response_history_count_ < kAimResponseHistoryCapacity) {
        write_index =
            (aim_response_history_begin_ + aim_response_history_count_) %
            kAimResponseHistoryCapacity;
        ++aim_response_history_count_;
    } else {
        write_index = aim_response_history_begin_;
        aim_response_history_begin_ =
            (aim_response_history_begin_ + 1) %
            kAimResponseHistoryCapacity;
    }
    aim_response_command_history_[write_index] = {
        at_seconds, stick, manual_ambiguous};
}

bool NativeGamepadController::average_aim_response_command(
    double begin_seconds,
    double end_seconds,
    pipeline_contract::Vec2f* average_stick,
    bool* manual_ambiguous) const noexcept {
    if (average_stick == nullptr || manual_ambiguous == nullptr ||
        !std::isfinite(begin_seconds) || !std::isfinite(end_seconds) ||
        end_seconds <= begin_seconds || aim_response_history_count_ == 0) {
        return false;
    }

    std::size_t active_offset = aim_response_history_count_;
    for (std::size_t offset = 0;
         offset < aim_response_history_count_; ++offset) {
        const std::size_t index =
            (aim_response_history_begin_ + offset) %
            kAimResponseHistoryCapacity;
        if (aim_response_command_history_[index].at_seconds <= begin_seconds) {
            active_offset = offset;
        } else {
            break;
        }
    }
    // No command is known to have been active at the start of the causal
    // window. Priming is safer than inventing a zero command.
    if (active_offset == aim_response_history_count_) return false;

    pipeline_contract::Vec2f integral{};
    bool ambiguous = false;
    double cursor = begin_seconds;
    TimedAimResponseCommand active = aim_response_command_history_[
        (aim_response_history_begin_ + active_offset) %
        kAimResponseHistoryCapacity];
    for (std::size_t offset = active_offset + 1;
         offset < aim_response_history_count_; ++offset) {
        const TimedAimResponseCommand& next = aim_response_command_history_[
            (aim_response_history_begin_ + offset) %
            kAimResponseHistoryCapacity];
        if (next.at_seconds >= end_seconds) break;
        const double segment_end = std::max(cursor, next.at_seconds);
        const double duration = segment_end - cursor;
        if (duration > 0.0) {
            integral.x += active.stick.x * static_cast<float>(duration);
            integral.y += active.stick.y * static_cast<float>(duration);
            ambiguous = ambiguous || active.manual_ambiguous;
        }
        cursor = segment_end;
        active = next;
    }
    const double tail_duration = end_seconds - cursor;
    if (tail_duration > 0.0) {
        integral.x += active.stick.x * static_cast<float>(tail_duration);
        integral.y += active.stick.y * static_cast<float>(tail_duration);
        ambiguous = ambiguous || active.manual_ambiguous;
    }
    const float inverse_duration = static_cast<float>(
        1.0 / (end_seconds - begin_seconds));
    *average_stick = {
        integral.x * inverse_duration,
        integral.y * inverse_duration};
    *manual_ambiguous = ambiguous;
    return pipeline_contract::finite(*average_stick);
}

bool NativeGamepadController::manual_fire_pressed(
    const PhysicalGamepadState& physical) const noexcept {
    return physical.rb || physical.right_trigger > 0.04f;
}

void NativeGamepadController::observe_composed_output(
    const GamepadOutputState& output) {
    if (!composed_output_pending_) return;
    const auto before_recoil = last_output_components_.before_recoil_stick;
    last_output_components_.recoil_stick = {
        output.right_x - before_recoil.x,
        output.right_y - before_recoil.y};
    capture_final_output_component(output, &last_output_components_);
    last_acquisition_trace_.post_output = {output.right_x, output.right_y};
    last_acquisition_trace_.final_output_ready_ns = seconds_to_ns(now_seconds());
    record_stage_trace(
        "recoil",
        before_recoil.y,
        output.right_y,
        pending_auto_fire_active_,
        pending_auto_fire_active_);
    composed_output_pending_ = false;
}

NativeAutoFireCounters NativeGamepadController::auto_fire_counters() const {
    return auto_fire_gate_.counters();
}

const NativeControllerStageTraceBuffer& NativeGamepadController::last_pipeline_traces() const {
    return last_pipeline_traces_;
}

const NativeControllerAcquisitionTrace&
NativeGamepadController::last_acquisition_trace() const noexcept {
    return last_acquisition_trace_;
}

const NativeControllerOutputComponents& NativeGamepadController::last_output_components() const {
    return last_output_components_;
}

const NativeControllerVisionState& NativeGamepadController::last_frame_vision_state() const {
    return last_frame_vision_state_;
}

const pipeline_contract::TargetPlan& NativeGamepadController::last_target_plan() const {
    return last_target_plan_;
}

std::uint64_t NativeGamepadController::ads_epoch() const noexcept {
    return ads_epoch_;
}

const std::string& NativeGamepadController::last_ai_aim_mode() const {
    return last_ai_aim_mode_;
}

void NativeGamepadController::record_stage_trace(
    std::string_view stage_name,
    float before_right_y,
    float after_right_y,
    bool before_auto_fire_active,
    bool after_auto_fire_active) {
    last_pipeline_traces_.push_back(NativeControllerStageTrace{
        stage_name,
        before_right_y,
        after_right_y,
        after_right_y - before_right_y,
        before_auto_fire_active,
        after_auto_fire_active,
    });
}

double NativeGamepadController::now_seconds() const {
    return injected_clock_seconds_ != nullptr
        ? *injected_clock_seconds_
        : current_seconds();
}

}  // namespace controller_native
