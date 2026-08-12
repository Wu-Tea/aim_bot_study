#include "native_gamepad_controller.h"
#include "../tracking_native/tracker_authority.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <utility>

namespace controller_native {
namespace {

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
    result.ads_max_acquisition_ms = std::max(0.0f, config.ai_aim.ads_max_acquisition_ms);
    result.ads_activation_radius_px = std::max(
        result.settle_radius_px, config.ai_aim.ads_activation_radius_px);
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
        0.060f, 0.350f);
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
    std::function<double()> clock)
    : config_(config),
      target_coordinator_(coordinator_config(config)),
      ads_controller_(ads_config(config)),
      bodylock_controller_(bodylock_config(config)),
      assist_control_state_machine_(assist_control_config(config)),
      recoil_(config_.recoil),
      adaptive_recoil_feedback_(config_.recoil),
      auto_fire_gate_(config_.auto_fire, config_.ai_aim),
      clock_(std::move(clock)) {
    if (config_.recoil.profile_playback_enabled) {
        recoil_.set_recognizer_state_path(config_.recoil.recognizer_state_path);
        recoil_.load_profile_directory(config_.recoil.profile_directory);
        recoil_.load_calibration_directory(config_.recoil.calibration_directory);
    }
}

void NativeGamepadController::reset() {
    intent_filter_.reset();
    target_coordinator_.reset();
    aim_response_estimator_.reset();
    dynamics_shaper_.reset();
    assist_control_state_machine_.reset();
    recoil_.reset();
    adaptive_recoil_feedback_.reset();
    aim_activation_tracker_.reset();
    auto_fire_gate_.reset();
    pending_snapshot_ = {};
    has_pending_snapshot_ = false;
    aiming_ = false;
    previous_aiming_ = false;
    last_firing_activity_seconds_ = -1.0;
    ads_epoch_ = 0;
    last_tick_seconds_ = 0.0;
    aim_response_command_sum_ = {};
    aim_response_command_count_ = 0;
    last_aim_response_frame_id_ = 0;
    last_aim_response_observed_seconds_ = 0.0;
    aim_response_manual_ambiguous_ = false;
    sampled_physical_ = {};
    sampled_intent_ = {};
    sampled_now_seconds_ = 0.0;
    sampled_dt_seconds_ = 0.001f;
    has_sampled_input_ = false;
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
            ? std::clamp(destination.normalized_size / 0.12f, 0.35f, 1.0f)
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

const pipeline_contract::IntentState& NativeGamepadController::sample_input(
    const PhysicalGamepadState& physical) {
    const double now = now_seconds();
    const float dt = last_tick_seconds_ > 0.0
        ? static_cast<float>(std::clamp(now - last_tick_seconds_, 0.0001, 0.05))
        : 0.001f;
    last_tick_seconds_ = now;
    aiming_ = aim_activation_tracker_.update(physical, config_.rb_counts_as_aiming);
    if (aiming_ && !previous_aiming_) {
        target_coordinator_.begin_ads_epoch(++ads_epoch_, now);
        assist_control_state_machine_.reset();
        auto_fire_gate_.reset_readiness();
    }
    previous_aiming_ = aiming_;
    sampled_physical_ = physical;
    sampled_now_seconds_ = now;
    sampled_dt_seconds_ = dt;
    sampled_intent_ = intent_filter_.update(
        {physical.left_x, physical.left_y},
        {physical.right_x, physical.right_y},
        aiming_, manual_fire_pressed(physical), now,
        last_target_plan_.target_id != 0,
        last_output_components_.handover_requested);
    has_sampled_input_ = true;
    return sampled_intent_;
}

GamepadOutputState NativeGamepadController::build_output(
    const PhysicalGamepadState& physical) {
    (void)sample_input(physical);
    return build_output_from_sampled_input();
}

GamepadOutputState NativeGamepadController::build_output_from_sampled_input() {
    if (!has_sampled_input_) return {};
    const PhysicalGamepadState physical = sampled_physical_;
    const pipeline_contract::IntentState intent = sampled_intent_;
    const double now = sampled_now_seconds_;
    const float dt = sampled_dt_seconds_;
    has_sampled_input_ = false;

    last_pipeline_traces_.clear();
    GamepadOutputState output = output_from_physical_input(physical);
    auto components = output_components_from_manual_output(output);
    components.physical_stick = {physical.right_x, physical.right_y};
    components.manual_stick = {physical.right_x, physical.right_y};
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
    }
    if (plan.cue_continuation) {
        // A same-generation cue may continue ADS identity, but must not create
        // an arrival-time gain step. Only conservative BodyLock continuation
        // is evidence-scaled after the ADS acquisition lifecycle has ended.
        if (plan.mode == pipeline_contract::ControlMode::BodyLockFollow) {
            plan.aim_authority *= std::clamp(
                config_.ai_aim.cue_hold_body_lock_force_scale, 0.0f, 1.0f);
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
    last_target_plan_ = plan;
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
        if (last_aim_response_observed_seconds_ > 0.0 &&
            aim_response_command_count_ > 0) {
            const float inverse_count = 1.0f /
                static_cast<float>(aim_response_command_count_);
            aim_response_estimator_.update({
                {aim_response_command_sum_.x * inverse_count,
                 aim_response_command_sum_.y * inverse_count},
                plan.velocity_px_per_sec,
                plan.target_id,
                static_cast<float>(now - last_aim_response_observed_seconds_),
                plan.reliability,
                // TargetCoordinator acceleration is relative screen acceleration and
                // therefore contains the very camera response this estimator needs.
                // It is not a target-only maneuver signal, so do not mislabel it as
                // unexplained target acceleration here.
                0.0f,
                aim_response_zone_weight,
                plan.lifecycle == pipeline_contract::TargetLifecycle::Observed,
                aim_response_manual_ambiguous_,
            });
        } else {
            aim_response_estimator_.begin_target(plan.target_id);
        }
        aim_response_command_sum_ = {};
        aim_response_command_count_ = 0;
        aim_response_manual_ambiguous_ = false;
        last_aim_response_frame_id_ = observations.frame_id;
        last_aim_response_observed_seconds_ = now;
    }
    last_frame_vision_state_ = vision_state_from_plan(
        plan, now, observations.capture_fresh, plan.error_px);
    last_ai_aim_mode_ = mode_name(plan.mode);

    // ADS/BodyLock own the complete AI proposal. Manual authority is decided
    // once, after the target solver and dynamics shaper, by the explicit assist
    // control state machine below.
    auto assist_generation_intent = intent;
    assist_generation_intent.filtered_right = {};
    assist_generation_intent.right_x.confidence = 0.0f;
    assist_generation_intent.right_y.confidence = 0.0f;
    assist_generation_intent.right_confidence = 0.0f;
    const bool target_authoritative =
        plan.target_id != 0 && plan.aim_authority > 0.0f &&
        plan.mode != pipeline_contract::ControlMode::Manual;
    pipeline_contract::Vec2f requested{};
    BodylockFollowControllerOutput bodylock_diagnostics{};
    if (plan.mode == pipeline_contract::ControlMode::AdsAcquire) {
        requested = ads_controller_.compute(plan, assist_generation_intent, dt);
    } else if (plan.mode == pipeline_contract::ControlMode::BodyLockFollow) {
        bodylock_diagnostics = bodylock_controller_.compute_detailed(
            plan,
            assist_generation_intent,
            dt);
        requested = bodylock_diagnostics.stick;
    }
    components.bodylock_error_rate_px_per_sec = {
        plan.error_rate_px_per_sec.x,
        plan.error_rate_px_per_sec.y};
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
    const pipeline_contract::Vec2f shaped = dynamics_shaper_.shape(
        requested,
        assist_generation_intent,
        plan,
        dt,
        {});
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
    {
        // Production has one explicit authority owner instead of routing the
        // same stick through separate mix, escape and per-axis patches.
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
        control_input.visual_authority = plan.visual_authority;
        // Preserve the firing/downward invariant across short fire-pulse and
        // controller ordering gaps as well as on the physical fire tick.
        control_input.firing = control_feedback.firing_recently;
        control_input.manual_stick = {
            physical.right_x, physical.right_y};
        control_input.filtered_manual_stick = intent.filtered_right;
        control_input.ai_stick = {shaped.x, shaped.y};
        control_input.manual_correction_x = plan.manual_correction_x;
        control_input.manual_correction_y = plan.manual_correction_y;
        control_input.manual_exit_requested = plan.manual_exit_requested;
        const auto decision = assist_control_state_machine_.update(
            control_input);
        output.right_x = clamp_unit(decision.stick.x);
        output.right_y = clamp_unit(decision.stick.y);
        assist_control_phase = decision.phase;
        assist_handover_requested = decision.handover_requested;
        assist_handover_braking = decision.handover_braking;
        target_manual_passthrough_x = decision.manual_passthrough_x;
        target_manual_passthrough_y = decision.manual_passthrough_y;
        target_manual_correction_x = decision.manual_correction_x;
        target_manual_correction_y = decision.manual_correction_y;

    }
    last_acquisition_trace_.fused_output = {output.right_x, output.right_y};
    if (plan.target_acquisition_id != 0 &&
        plan.mode != pipeline_contract::ControlMode::Manual &&
        !last_acquisition_trace_.has_first_fused_output &&
        has_material_vector(last_acquisition_trace_.fused_output)) {
        last_acquisition_trace_.has_first_fused_output = true;
        last_acquisition_trace_.first_fused_output_ns = seconds_to_ns(now);
        last_acquisition_trace_.first_fused_output =
            last_acquisition_trace_.fused_output;
    }
    components.target_final_stick = {output.right_x, output.right_y};
    components.ai_correction_stick = {
        components.target_final_stick.x - components.manual_stick.x,
        components.target_final_stick.y - components.manual_stick.y};
    components.manual_authority_mode = assist_handover_requested ||
            assist_control_phase == AssistControlPhase::HandoverSeek
        ? "handover_seek_manual"
        : assist_handover_braking
            ? "capture_ai_brake"
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
    record_stage_trace("target_plan_aim", physical.right_y, output, false, false);

    AutoFireGateInput fire_input{};
    fire_input.vision_state = last_frame_vision_state_;
    fire_input.aiming = aiming_;
    fire_input.ads_min_elapsed = true;
    fire_input.manual_fire_pressed = manual_fire_pressed(physical);
    fire_input.now_seconds = now;
    fire_input.manual_right_x = physical.right_x;
    fire_input.manual_right_y = physical.right_y;
    fire_input.output_right_x = output.right_x;
    fire_input.output_right_y = output.right_y;
    fire_input.settle_dx = plan.error_px.x;
    fire_input.settle_dy = plan.error_px.y;
    const auto fire = auto_fire_gate_.evaluate(fire_input);
    auto_fire_gate_.apply_fire_output(output, fire.should_fire);
    // AutoFire owns only the synthetic contribution. Physical fire is an
    // unconditional passthrough invariant, including the first controller
    // tick where LT and RB/RT rise together.
    output.rb = output.rb || physical.rb;
    output.right_trigger = std::max(
        output.right_trigger, physical.right_trigger);
    if (fire.should_fire || manual_fire_pressed(physical)) {
        last_firing_activity_seconds_ = now;
    }
    record_stage_trace(
        "auto_fire", output.right_y, output,
        fire.before_auto_fire_active, fire.after_auto_fire_active);
    components.auto_fire_requested = plan.fire_requested;
    components.auto_fire_aim_ready = fire.aim_ready;
    components.auto_fire_allowed = fire.pre_takeover_should_fire;
    components.auto_fire_active = fire.should_fire;
    components.auto_fire_pulse_starts = fire.counters.pulse_starts;
    components.auto_fire_pulse_pressed = fire.should_fire;
    components.auto_fire_cadence_wait = fire.pulse_waiting;
    components.auto_fire_block_reason = auto_fire_block_reason_name(fire.block_reason);

    components.before_recoil_stick = {output.right_x, output.right_y};
    aim_response_command_sum_.x += output.right_x;
    aim_response_command_sum_.y += output.right_y;
    ++aim_response_command_count_;
    aim_response_manual_ambiguous_ = aim_response_manual_ambiguous_ ||
        std::hypot(physical.right_x, physical.right_y) >= 0.35f ||
        fire.should_fire || manual_fire_pressed(physical) ||
        (last_firing_activity_seconds_ >= 0.0 &&
         now - last_firing_activity_seconds_ <=
             kFiringDisturbanceWindowSeconds);
    const auto before_recoil = output;
    apply_recoil(
        output,
        physical,
        plan,
        source_error_px,
        observations.capture_fresh,
        aiming_,
        fire.should_fire,
        now);
    record_stage_trace(
        "recoil", before_recoil.right_y, output,
        fire.after_auto_fire_active, fire.after_auto_fire_active);
    capture_output_component_delta(before_recoil, output, &components.recoil_stick);
    capture_final_output_component(output, &components);
    last_acquisition_trace_.post_output = {output.right_x, output.right_y};
    last_acquisition_trace_.final_output_ready_ns = seconds_to_ns(now_seconds());
    last_output_components_ = components;
    return output;
}

bool NativeGamepadController::manual_fire_pressed(
    const PhysicalGamepadState& physical) const noexcept {
    return physical.rb || physical.right_trigger > 0.04f;
}

void NativeGamepadController::apply_recoil(
    GamepadOutputState& output,
    const PhysicalGamepadState& physical,
    const pipeline_contract::TargetPlan& plan,
    pipeline_contract::Vec2f observed_error_px,
    bool capture_fresh,
    bool aiming,
    bool auto_fire_active,
    double now_seconds) {
    NativeRecoilInput input{};
    input.fire_active = auto_fire_active || manual_fire_pressed(physical);
    input.aiming = aiming;
    input.now_seconds = now_seconds;
    auto recoil_output = recoil_.compute(input);
    if (!recoil_output.recoil_active) return;
    const auto adaptive = adaptive_recoil_feedback_.update({
        input.fire_active,
        aiming,
        plan.target_id != 0 && plan.aim_authority > 0.0f,
        plan.ads_candidate_count <= 1,
        capture_fresh &&
            plan.lifecycle == pipeline_contract::TargetLifecycle::Observed,
        plan.cue_continuation,
        plan.target_id,
        plan.source_frame_id,
        observed_error_px.y,
        physical.right_y,
        plan.motion,
        std::max(0.0f, -recoil_output.recoil_stick.y),
        now_seconds,
    });
    if (recoil_output.recoil_stick.y < 0.0f) {
        // Downward manual is the user's anti-recoil contribution. Only add the
        // missing remainder; never stack the full automatic amount on top.
        const float manual_down = std::max(0.0f, -physical.right_y);
        recoil_output.recoil_stick.y = -std::max(
            0.0f, adaptive.amount - manual_down);
    }
    output.right_x = clamp_unit(output.right_x + recoil_output.recoil_stick.x);
    output.right_y = clamp_unit(output.right_y + recoil_output.recoil_stick.y);
}

NativeAutoFireCounters NativeGamepadController::auto_fire_counters() const {
    return auto_fire_gate_.counters();
}

const std::vector<NativeControllerStageTrace>& NativeGamepadController::last_pipeline_traces() const {
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
    const std::string& stage_name,
    float before_right_y,
    const GamepadOutputState& output,
    bool before_auto_fire_active,
    bool after_auto_fire_active) {
    last_pipeline_traces_.push_back(NativeControllerStageTrace{
        stage_name,
        before_right_y,
        output.right_y,
        output.right_y - before_right_y,
        before_auto_fire_active,
        after_auto_fire_active,
    });
}

double NativeGamepadController::now_seconds() const {
    return clock_ ? clock_() : current_seconds();
}

}  // namespace controller_native
