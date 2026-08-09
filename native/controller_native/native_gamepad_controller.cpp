#include "native_gamepad_controller.h"
#include "helpful_manual_overdrive.h"
#include "target_geometry.h"
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
    case pipeline_contract::TargetLifecycle::Coasting: return "coast";
    case pipeline_contract::TargetLifecycle::Reacquiring: return "reacquiring";
    case pipeline_contract::TargetLifecycle::None: return "inactive";
    }
    return "inactive";
}

const char* causal_memory_status_name(
    CausalMotionLedgerStatus status) noexcept {
    switch (status) {
    case CausalMotionLedgerStatus::Valid: return "valid";
    case CausalMotionLedgerStatus::Empty: return "empty";
    case CausalMotionLedgerStatus::InvalidRequest: return "invalid_request";
    case CausalMotionLedgerStatus::HorizonExceeded: return "horizon_exceeded";
    case CausalMotionLedgerStatus::IncompleteHistory: return "incomplete_history";
    case CausalMotionLedgerStatus::LifecycleMismatch: return "lifecycle_mismatch";
    case CausalMotionLedgerStatus::InvalidSample: return "invalid_sample";
    case CausalMotionLedgerStatus::BackendStateUnknown: return "backend_unknown";
    case CausalMotionLedgerStatus::NonMonotonicClock: return "non_monotonic_clock";
    case CausalMotionLedgerStatus::DeviceEpochChanged: return "device_epoch_changed";
    case CausalMotionLedgerStatus::CapturePairIncompatible: return "capture_pair_incompatible";
    case CausalMotionLedgerStatus::InvalidResponseModel: return "invalid_response_model";
    }
    return "unknown";
}

TargetCoordinatorConfig coordinator_config(const GamepadRuntimeConfig& config) {
    TargetCoordinatorConfig result{};
    result.hold_ms = std::max(
        80.0f, std::max(config.ai_aim.target_max_age_ms,
                        config.ai_aim.target_projection_max_age_ms));
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
    // The production 80-100 Hz stream benefits from a slower velocity update:
    // position remains fresh, while one-frame gun/camera kick contributes less
    // to predictive lead. Benchmarks retain an explicit override for A/B.
    result.motion_velocity_alpha = 0.15f;
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
    result.start_delay_ms = std::max(0.0f, config.ai_aim.ads_start_delay_ms);
    result.start_ramp_ms = std::max(0.0f, config.ai_aim.ads_start_ramp_ms);
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

VectorIntentFusionConfig vector_intent_fusion_config(
    const GamepadRuntimeConfig& config) {
    VectorIntentFusionConfig result{};
    result.manual_escape_threshold = std::min(
        config.ai_aim.body_lock_manual_escape_input_threshold,
        config.ai_aim.body_lock_manual_takeover_input_threshold);
    result.manual_preservation_floor = std::clamp(
        config.ai_aim.body_lock_manual_escape_preservation,
        0.0f, 1.0f);
    result.fresh_vision_wrong_way_manual_floor = std::clamp(
        config.intent.fresh_vision_wrong_way_manual_floor,
        0.0f, 1.0f);
    return result;
}

float firing_vertical_intent_offset_px(
    const GamepadRuntimeConfig& config,
    const pipeline_contract::TargetPlan& plan,
    float manual_y,
    bool aiming,
    bool firing) noexcept {
    if (!config.recoil.firing_vertical_intent_enabled || !aiming || !firing ||
        plan.target_id == 0 || plan.aim_authority <= 0.0f ||
        plan.mode == pipeline_contract::ControlMode::Manual ||
        plan.ads_candidate_count > 1 || !std::isfinite(manual_y)) {
        return 0.0f;
    }
    const float deadzone = std::clamp(
        config.recoil.firing_vertical_intent_deadzone, 0.0f, 0.25f);
    const float magnitude = std::fabs(manual_y);
    if (magnitude <= deadzone) return 0.0f;

    const float normalized = std::clamp(
        (magnitude - deadzone) / std::max(0.001f, 1.0f - deadzone),
        0.0f, 1.0f);
    const float configured_max = std::max(
        0.0f, config.recoil.firing_vertical_intent_max_offset_px);
    if (configured_max <= 0.0f) return 0.0f;
    const float scale_bound = std::clamp(
        plan.normalized_size * 120.0f,
        std::min(12.0f, configured_max),
        configured_max);
    // Stick Y is negative for a downward camera request, while a target below
    // the reticle is positive error Y. Convert manual intent into D, then let
    // the existing ADS/BodyLock solver produce the sole final T.
    return -std::copysign(normalized * scale_bound, manual_y);
}

}  // namespace

NativeGamepadController::NativeGamepadController(
    GamepadRuntimeConfig config,
    std::function<double()> clock)
    : config_(config),
      target_coordinator_(coordinator_config(config)),
      ads_controller_(ads_config(config)),
      bodylock_controller_(bodylock_config(config)),
      vector_intent_fuser_(vector_intent_fusion_config(config)),
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
    axis_intent_arbiter_.reset();
    vector_intent_fuser_.reset();
#if defined(COD_BENCHMARK_MIX_OVERRIDE)
    pending_control_motion_.reset();
#endif
    causal_motion_ledger_.reset();
    recoil_.reset();
    adaptive_recoil_feedback_.reset();
    aim_activation_tracker_.reset();
    auto_fire_gate_.reset();
    pending_snapshot_ = {};
    has_pending_snapshot_ = false;
    aiming_ = false;
    previous_aiming_ = false;
    previous_jump_button_ = false;
    previous_slide_button_ = false;
    last_jump_action_seconds_ = -1.0;
    last_slide_action_seconds_ = -1.0;
    last_firing_activity_seconds_ = -1.0;
    ads_epoch_ = 0;
    legacy_vision_sequence_ = 0;
    last_tick_seconds_ = 0.0;
    previous_plan_normalized_size_ = 0.0f;
    previous_plan_target_id_ = 0;
    previous_target_first_authoritative_ = false;
    last_observed_ads_candidate_count_ = 0;
    aim_response_command_sum_ = {};
    aim_response_command_count_ = 0;
    last_aim_response_frame_id_ = 0;
    last_aim_response_observed_seconds_ = 0.0;
    aim_response_manual_ambiguous_ = false;
    last_pipeline_traces_.clear();
    last_acquisition_trace_ = {};
    acquisition_trace_target_id_ = 0;
    last_tracker_motion_output_ = {};
    last_output_components_ = {};
    last_frame_vision_state_ = {};
    last_target_plan_ = {};
    last_causal_memory_estimate_ = {};
    causal_memory_previous_capture_seconds_ = 0.0;
    causal_memory_current_capture_seconds_ = 0.0;
    causal_memory_previous_source_frame_id_ = 0;
    causal_memory_previous_source_observation_id_ = 0;
    causal_memory_previous_present_ns_ = 0;
    causal_memory_previous_present_calibration_id_ = 0;
    causal_memory_previous_present_qpc_frequency_ = 0;
    causal_memory_previous_capture_target_id_ = 0;
    causal_memory_previous_capture_ads_epoch_ = 0;
    causal_memory_capture_target_id_ = 0;
    causal_memory_capture_ads_epoch_ = 0;
    causal_memory_capture_pair_compatible_ = false;
    causal_memory_physical_actuator_epoch_ = 0;
    previous_fusion_manual_escape_ = false;
    last_ai_aim_mode_ = "manual";
}

void NativeGamepadController::submit_vision_state(const NativeControllerVisionState& state) {
    ControllerVisionSnapshot snapshot{};
    snapshot.frame_updated = true;
    snapshot.state = state;
    snapshot.frame_id = state.vision_sequence != 0
        ? state.vision_sequence
        : ++legacy_vision_sequence_;
    snapshot.selected_observation_id = state.selected_observation_id;
    snapshot.capture_time_seconds = state.observed_at_seconds;
    snapshot.ready_time_seconds = now_seconds();
    if (state.has_target) {
        pipeline_contract::VisionCandidateSnapshot candidate{};
        candidate.id = state.selected_observation_id != 0
            ? state.selected_observation_id
            : state.selected_track_id != 0 ? state.selected_track_id : 1;
        snapshot.selected_observation_id = candidate.id;
        candidate.valid = true;
        candidate.has_aim_point = true;
        const float center_x = state.screen_center_x > 0.0f ? state.screen_center_x : 240.0f;
        const float center_y = state.screen_center_y > 0.0f ? state.screen_center_y : 208.0f;
        const bool has_absolute_aim = state.target_x != 0.0f || state.target_y != 0.0f;
        candidate.aim_point_px = has_absolute_aim
            ? common_native::Vec2f{state.target_x, state.target_y}
            : common_native::Vec2f{center_x + state.dx, center_y + state.dy};
        candidate.confidence = state.aim_authority ? 1.0f : 0.5f;
        candidate.has_cue_point = state.fresh_observation;
        candidate.cue_score = state.fresh_observation ? 1.0f : 0.0f;
        candidate.body_box_px = {
            state.body_x1,
            state.body_y1,
            std::max(0.0f, state.body_x2 - state.body_x1),
            std::max(0.0f, state.body_y2 - state.body_y1)};
        snapshot.candidates.push_back(candidate);
    }
    submit_vision_snapshot(snapshot);
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
    batch.actuator_effect_present_qpc =
        snapshot.actuator_effect_present_qpc;
    batch.actuator_effect_present_qpc_frequency =
        snapshot.actuator_effect_present_qpc_frequency;
    batch.actuator_effect_present_steady_ns =
        snapshot.actuator_effect_present_steady_ns;
    batch.actuator_effect_present_calibration_id =
        snapshot.actuator_effect_present_calibration_id;
    batch.actuator_effect_present_calibration_uncertainty_ns =
        snapshot.actuator_effect_present_calibration_uncertainty_ns;
    batch.actuator_effect_present_time_seconds =
        snapshot.actuator_effect_present_time_seconds;
    batch.actuator_effect_present_raw_available =
        snapshot.actuator_effect_present_raw_available;
    batch.actuator_effect_present_steady_available =
        snapshot.actuator_effect_present_steady_available;
    batch.actuator_effect_present_time_valid =
        snapshot.actuator_effect_present_time_valid;
    batch.frame_width_px = snapshot.state.screen_center_x > 0.0f
        ? snapshot.state.screen_center_x * 2.0f : 480.0f;
    batch.frame_height_px = snapshot.state.screen_center_y > 0.0f
        ? snapshot.state.screen_center_y * 2.0f : 416.0f;
    batch.capture_fresh = snapshot.frame_updated || snapshot.state.fresh_observation;
    batch.selector_identity_protocol = snapshot.selector_identity_protocol;
    batch.selector_target_generation = snapshot.selector_target_generation;
    batch.selector_target_changed = snapshot.selector_target_changed;
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
    batch.has_control_response_hint = snapshot.state.has_camera_attributed_velocity;
    batch.control_response_x_px_per_second =
        snapshot.state.camera_attributed_velocity_x_px_per_sec;
    batch.rejected_friendly_count = snapshot.rejected_friendly_count;
    batch.rejected_low_reliability_count = snapshot.rejected_low_reliability_count;
    const auto limit = std::min<std::size_t>(
        snapshot.candidates.size(), pipeline_contract::kMaxVisionCandidates);
    for (std::size_t index = 0; index < limit; ++index) {
        const auto& source = snapshot.candidates[index];
        if (!source.valid || !source.has_aim_point || source.is_friendly) continue;
        auto& destination = batch.candidates[batch.count++];
        destination.source_id = source.id;
        const float width = std::max(0.0f, source.body_box_px.w);
        const float height = std::max(0.0f, source.body_box_px.h);
        const auto geometry = resolve_target_geometry(
            {source.aim_point_px, source.body_box_px, width > 0.0f && height > 0.0f},
            {config_.tracker.aim_height_ratio});
        destination.aim_px = {geometry.aim_px.x, geometry.aim_px.y};
        destination.box_size_px = {width, height};
        destination.body_box_px = source.body_box_px;
        destination.has_body_box = width > 0.0f && height > 0.0f;
        destination.motion_anchor_px = {
            source.motion_anchor_px.x, source.motion_anchor_px.y};
        destination.motion_anchor_score =
            std::clamp(source.motion_anchor_score, 0.0f, 1.0f);
        destination.has_motion_anchor =
            source.has_motion_anchor &&
            source.motion_anchor_score >= 0.20f;
        destination.confidence = std::clamp(source.confidence, 0.0f, 1.0f);
        destination.cue_confidence = std::clamp(source.cue_score, 0.0f, 1.0f);
        destination.normalized_size = std::clamp(
            height / std::max(1.0f, batch.frame_height_px), 0.0f, 1.0f);
        const float size_weight = height > 0.0f
            ? std::clamp(destination.normalized_size / 0.12f, 0.2f, 1.0f)
            : 1.0f;
        destination.reliability = destination.confidence * size_weight;
        destination.body_cue = height > 0.0f;
    }
    if (batch.count == 0 && snapshot.state.has_target) {
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
        const auto geometry = resolve_target_geometry(
            {vision_aim, body_box, snapshot.state.has_body_box},
            {config_.tracker.aim_height_ratio});
        destination.aim_px = {geometry.aim_px.x, geometry.aim_px.y};
        destination.box_size_px = {
            body_box.w, height};
        destination.body_box_px = body_box;
        destination.has_body_box = snapshot.state.has_body_box;
        destination.normalized_size = std::clamp(
            height / std::max(1.0f, batch.frame_height_px), 0.0f, 1.0f);
        destination.confidence = snapshot.state.aim_authority ? 1.0f : 0.6f;
        destination.cue_confidence =
            batch.selector_cue_continuation ? 1.0f : 0.0f;
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
         plan.lifecycle == pipeline_contract::TargetLifecycle::Reacquiring ||
         plan.fire_authority);
    state.fresh_observation =
        capture_fresh && state.current_observed_target_present;
    state.aim_authority = plan.aim_authority > 0.0f;
    state.fire_authority = !plan.cue_continuation && plan.fire_authority;
    state.auto_fire_requested = !plan.cue_continuation && plan.fire_requested;
    // D is observation geometry.  plan.error_px may now be R=D-P, which is
    // the control request but must never be used to reconstruct the viewport
    // center or target geometry.
    state.dx = observed_error_px.x;
    state.dy = observed_error_px.y;
    state.target_x = plan.aim_px.x;
    state.target_y = plan.aim_px.y;
    state.screen_center_x = plan.aim_px.x - observed_error_px.x;
    state.screen_center_y = plan.aim_px.y - observed_error_px.y;
    state.observed_at_seconds = now_seconds - plan.observation_age_ms / 1000.0;
    state.target_tier = plan.cue_continuation
        ? "cue_hold"
        : state.current_observed_target_present ? "observed_strong" : "predicted";
    state.has_tracker_projection = state.has_target && !plan.cue_continuation;
    state.tracker_dx = observed_error_px.x;
    state.tracker_dy = observed_error_px.y;
    state.has_camera_attributed_velocity = plan.response_confidence > 0.0f;
    state.camera_attributed_velocity_x_px_per_sec =
        plan.response_scale * plan.response_confidence;
    state.authority_decision_valid = true;
    state.assist_authority_state = !state.aim_authority
        ? pipeline_contract::AssistAuthorityState::Reject
        : plan.cue_continuation ||
              plan.lifecycle == pipeline_contract::TargetLifecycle::Coasting
            ? pipeline_contract::AssistAuthorityState::Continuity
            : pipeline_contract::AssistAuthorityState::ObservedStrong;
    state.assist_authority_reason = !state.aim_authority
        ? pipeline_contract::AssistAuthorityReason::InvalidTarget
        : plan.cue_continuation
            ? pipeline_contract::AssistAuthorityReason::CueOnly
            : plan.lifecycle == pipeline_contract::TargetLifecycle::Coasting
                ? pipeline_contract::AssistAuthorityReason::ShortEvidenceGap
                : pipeline_contract::AssistAuthorityReason::StrongObserved;
    return state;
}

void NativeGamepadController::refresh_causal_memory_estimate(
    const pipeline_contract::VisionObservationBatch& observations,
    const pipeline_contract::TargetPlan& plan,
    double now) noexcept {
    if (!config_.tracker.causal_memory_enabled) {
        last_causal_memory_estimate_ = {};
        return;
    }

    // The legacy source_time_seconds is copy-complete and remains the
    // tracker/controller clock. W5 shadow capture boundaries require the
    // independent calibrated source-present endpoint; missing provenance is
    // an explicit unavailable result, never a fallback to copy-complete.
    const bool source_capture_valid = observations.capture_fresh &&
        observations.frame_id != 0 &&
        observations.actuator_effect_present_time_valid &&
        std::isfinite(observations.actuator_effect_present_time_seconds) &&
        observations.actuator_effect_present_time_seconds > 0.0 &&
        observations.actuator_effect_present_time_seconds <= now;
    const bool new_capture = source_capture_valid &&
        (causal_memory_current_capture_seconds_ <= 0.0 ||
         observations.actuator_effect_present_time_seconds >
             causal_memory_current_capture_seconds_ + 1.0e-9);
    if (new_capture) {
        causal_memory_previous_capture_seconds_ =
            causal_memory_current_capture_seconds_;
        causal_memory_previous_source_frame_id_ =
            causal_memory_current_source_frame_id_;
        causal_memory_previous_source_observation_id_ =
            causal_memory_current_source_observation_id_;
        causal_memory_previous_present_ns_ =
            causal_memory_current_present_ns_;
        causal_memory_previous_present_calibration_id_ =
            causal_memory_current_present_calibration_id_;
        causal_memory_previous_present_qpc_frequency_ =
            causal_memory_current_present_qpc_frequency_;
        causal_memory_previous_capture_target_id_ =
            causal_memory_capture_target_id_;
        causal_memory_previous_capture_ads_epoch_ =
            causal_memory_capture_ads_epoch_;
        causal_memory_current_capture_seconds_ =
            observations.actuator_effect_present_time_seconds;
        causal_memory_current_source_frame_id_ = observations.frame_id;
        causal_memory_current_source_observation_id_ =
            plan.source_observation_id != 0
                ? plan.source_observation_id : observations.preferred_source_id;
        causal_memory_current_present_ns_ =
            observations.actuator_effect_present_steady_ns;
        causal_memory_current_present_calibration_id_ =
            observations.actuator_effect_present_calibration_id;
        causal_memory_current_present_qpc_frequency_ =
            observations.actuator_effect_present_qpc_frequency;
        causal_memory_capture_target_id_ = plan.target_id;
        causal_memory_capture_ads_epoch_ = ads_epoch_;
        causal_memory_capture_pair_compatible_ =
            causal_memory_previous_capture_seconds_ > 0.0 &&
            causal_memory_previous_capture_target_id_ == plan.target_id &&
            causal_memory_previous_capture_ads_epoch_ == ads_epoch_;
    }

    if (causal_memory_current_capture_seconds_ <= 0.0) {
        last_causal_memory_estimate_ = {};
        return;
    }

    // A target/ADS change without a fresh compatible source capture makes only
    // the realized pair unknown.  The global actuator ring and pending work
    // remain intact for the next decision.
    if (plan.target_id != causal_memory_capture_target_id_ ||
        ads_epoch_ != causal_memory_capture_ads_epoch_) {
        causal_memory_capture_pair_compatible_ = false;
    }

    CausalMotionPhaseRequest request;
    request.previous_capture_seconds =
        causal_memory_previous_capture_seconds_;
    request.current_capture_seconds =
        causal_memory_current_capture_seconds_;
    request.decision_seconds = now;
    request.response_delay_ms =
        config_.tracker.causal_memory_response_delay_ms;
    request.memory_horizon_ms = config_.tracker.causal_memory_horizon_ms;
    request.target_id = causal_memory_capture_target_id_;
    request.ads_epoch = causal_memory_capture_ads_epoch_;
    request.capture_pair_compatible =
        causal_memory_capture_pair_compatible_;
    request.physical_actuator_epoch =
        causal_memory_physical_actuator_epoch_;
    request.previous_source_frame_id =
        causal_memory_previous_source_frame_id_;
    request.previous_source_observation_id =
        causal_memory_previous_source_observation_id_;
    request.previous_present_steady_ns =
        causal_memory_previous_present_ns_;
    request.previous_present_calibration_id =
        causal_memory_previous_present_calibration_id_;
    request.previous_present_qpc_frequency =
        causal_memory_previous_present_qpc_frequency_;
    request.previous_target_id = causal_memory_previous_capture_target_id_;
    request.previous_ads_epoch = causal_memory_previous_capture_ads_epoch_;
    request.source_frame_id = causal_memory_current_source_frame_id_;
    request.source_observation_id = causal_memory_current_source_observation_id_;
    request.current_target_id = causal_memory_capture_target_id_;
    request.current_ads_epoch = causal_memory_capture_ads_epoch_;
    request.current_present_steady_ns = causal_memory_current_present_ns_;
    request.present_calibration_id =
        causal_memory_current_present_calibration_id_;
    request.present_qpc_frequency =
        causal_memory_current_present_qpc_frequency_;
    request.present_time_valid = source_capture_valid ||
        (causal_memory_current_present_ns_ != 0 &&
         causal_memory_current_present_calibration_id_ != 0 &&
         causal_memory_current_present_qpc_frequency_ != 0);
    last_causal_memory_estimate_ = causal_motion_ledger_.estimate(request);
}

GamepadOutputState NativeGamepadController::build_output(const PhysicalGamepadState& physical) {
    last_pipeline_traces_.clear();
    GamepadOutputState output = output_from_physical_input(physical);
    auto components = output_components_from_manual_output(output);
    components.physical_stick = {physical.right_x, physical.right_y};
    components.manual_stick = {physical.right_x, physical.right_y};
    const double now = now_seconds();
    const float dt = last_tick_seconds_ > 0.0
        ? static_cast<float>(std::clamp(now - last_tick_seconds_, 0.0001, 0.05))
        : 0.001f;
    last_tick_seconds_ = now;
    const bool jump_button = physical.a;
    if (jump_button && !previous_jump_button_) {
        last_jump_action_seconds_ = now;
    }
    previous_jump_button_ = jump_button;
    const bool slide_button = physical.b;
    if (slide_button && !previous_slide_button_) {
        last_slide_action_seconds_ = now;
    }
    previous_slide_button_ = slide_button;
    aiming_ = aim_activation_tracker_.update(physical, config_.rb_counts_as_aiming);
    if (aiming_ && !previous_aiming_) {
        target_coordinator_.begin_ads_epoch(++ads_epoch_, now);
        axis_intent_arbiter_.reset();
        vector_intent_fuser_.reset();
        previous_fusion_manual_escape_ = false;
        auto_fire_gate_.reset_readiness();
    }
    previous_aiming_ = aiming_;
    const auto intent = intent_filter_.update(
        {physical.left_x, physical.left_y},
        {physical.right_x, physical.right_y},
        aiming_, manual_fire_pressed(physical), now);
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
    control_feedback.fusion_manual_escape = previous_fusion_manual_escape_;
    control_feedback.player_jump_action_age_ms =
        last_jump_action_seconds_ >= 0.0
        ? static_cast<float>((now - last_jump_action_seconds_) * 1000.0)
        : -1.0f;
    control_feedback.player_slide_action_age_ms =
        last_slide_action_seconds_ >= 0.0
        ? static_cast<float>((now - last_slide_action_seconds_) * 1000.0)
        : -1.0f;
#if defined(COD_BENCHMARK_MIX_OVERRIDE)
    control_feedback.has_player_motion_oracle =
        benchmark_player_motion_oracle_valid_;
    control_feedback.has_player_motion_rate_oracle =
        benchmark_player_motion_rate_oracle_valid_;
    control_feedback.player_error_delta_px =
        benchmark_player_error_delta_px_;
    control_feedback.player_error_rate_px_per_sec =
        benchmark_player_error_rate_px_per_sec_;
#endif
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
        // Cue geometry is useful continuity evidence, but it is deliberately
        // weaker than a person observation and can never inherit full force.
        plan.aim_authority *= std::clamp(
            config_.ai_aim.cue_hold_body_lock_force_scale, 0.0f, 1.0f);
        plan.fire_authority = false;
        plan.fire_requested = false;
        plan.fire_suppression = pipeline_contract::FireSuppressionReason::AimOnly;
    }
#if defined(COD_BENCHMARK_MIX_OVERRIDE)
    if (benchmark_remaining_work_mode_ ==
            BenchmarkRemainingWorkMode::DeliveredAdjusted &&
        plan.target_id != 0 && plan.source_capture_age_ms > 0.0f) {
        const auto pending_motion = pending_control_motion_.estimate(
            now,
            plan.source_capture_age_ms,
            plan.response_scale,
            plan.target_id);
        if (pending_motion.valid) {
            const auto delivered_work = delivered_camera_work_px(
                pending_motion.camera_displacement_px.front());
            plan.delivered_camera_motion_since_capture_px = delivered_work;
            plan.remaining_work_px = remaining_work_after_delivery(
                plan.error_px, delivered_work);
            const pipeline_contract::Vec2f work_delta{
                plan.remaining_work_px.x - plan.error_px.x,
                plan.remaining_work_px.y - plan.error_px.y,
            };
            plan.error_px = plan.remaining_work_px;
            plan.predicted_terminal_error_px.x += work_delta.x;
            plan.predicted_terminal_error_px.y += work_delta.y;
            plan.remaining_work_confidence =
                std::clamp(plan.response_confidence, 0.0f, 1.0f);
            plan.remaining_work_valid = true;
        }
    }
#endif
    const auto raw_error_px = plan.error_px;
    const auto raw_predicted_terminal_error_px =
        plan.predicted_terminal_error_px;
    refresh_causal_memory_estimate(observations, plan, now);

    const auto& causal_estimate = last_causal_memory_estimate_;
    const bool causal_current_provenance_matches =
        causal_estimate.source_frame_id != 0 &&
        causal_estimate.source_frame_id ==
            causal_memory_current_source_frame_id_ &&
        causal_estimate.source_observation_id != 0 &&
        causal_estimate.source_observation_id ==
            causal_memory_current_source_observation_id_ &&
        causal_estimate.current_present_steady_ns != 0 &&
        causal_estimate.current_present_steady_ns ==
            causal_memory_current_present_ns_ &&
        causal_estimate.present_calibration_id ==
            causal_memory_current_present_calibration_id_ &&
        causal_estimate.present_qpc_frequency ==
            causal_memory_current_present_qpc_frequency_ &&
        causal_estimate.current_target_id == plan.target_id &&
        causal_estimate.current_ads_epoch == ads_epoch_ &&
        causal_estimate.present_time_valid &&
        causal_estimate.physical_actuator_epoch != 0 &&
        causal_estimate.physical_actuator_epoch ==
            causal_memory_physical_actuator_epoch_ &&
        causal_estimate.previous_source_frame_id ==
            causal_memory_previous_source_frame_id_ &&
        causal_estimate.previous_source_observation_id ==
            causal_memory_previous_source_observation_id_ &&
        causal_estimate.previous_present_steady_ns ==
            causal_memory_previous_present_ns_ &&
        causal_estimate.previous_present_calibration_id ==
            causal_memory_previous_present_calibration_id_ &&
        causal_estimate.previous_present_qpc_frequency ==
            causal_memory_previous_present_qpc_frequency_ &&
        causal_estimate.previous_target_id ==
            causal_memory_previous_capture_target_id_ &&
        causal_estimate.previous_ads_epoch ==
            causal_memory_previous_capture_ads_epoch_;
    const bool causal_pending_usable =
        config_.tracker.causal_memory_enabled &&
        plan.target_id != 0 && plan.aim_authority > 0.0f &&
        !plan.cue_continuation &&
        causal_estimate.status == CausalMotionLedgerStatus::Valid &&
        causal_estimate.valid && causal_estimate.pending_valid &&
        causal_current_provenance_matches &&
        pipeline_contract::finite(causal_estimate.pending_total_px);
    if (causal_pending_usable) {
        const auto pending = causal_estimate.pending_total_px;
        plan.error_px = remaining_work_after_delivery(raw_error_px, pending);
        const pipeline_contract::Vec2f delta{
            plan.error_px.x - raw_error_px.x,
            plan.error_px.y - raw_error_px.y,
        };
        plan.predicted_terminal_error_px = {
            raw_predicted_terminal_error_px.x + delta.x,
            raw_predicted_terminal_error_px.y + delta.y,
        };
        plan.delivered_camera_motion_since_capture_px = pending;
        plan.remaining_work_px = plan.error_px;
        plan.remaining_work_confidence = causal_estimate.pending_response_confidence;
        plan.remaining_work_valid = true;
    } else {
        // Missing/invalid history is fail-open. Never inherit the previous
        // P when the current observation, epoch or response window is not
        // proven compatible.
        plan.error_px = raw_error_px;
        plan.predicted_terminal_error_px = raw_predicted_terminal_error_px;
        plan.delivered_camera_motion_since_capture_px = {};
        plan.remaining_work_px = {};
        plan.remaining_work_confidence = 0.0f;
        plan.remaining_work_valid = false;
    }
    const bool firing_input = manual_fire_pressed(physical);
    const float firing_vertical_offset = firing_vertical_intent_offset_px(
        config_, plan, physical.right_y, aiming_, firing_input);
    plan.error_px.y += firing_vertical_offset;
    plan.predicted_terminal_error_px.y += firing_vertical_offset;
    components.observed_error_px = {raw_error_px.x, raw_error_px.y};
    components.pending_motion_px = causal_pending_usable
        ? common_native::Vec2f{
            causal_estimate.pending_total_px.x,
            causal_estimate.pending_total_px.y}
        : common_native::Vec2f{};
    components.control_error_px = {plan.error_px.x, plan.error_px.y};
    components.pending_motion_confidence = causal_pending_usable
        ? causal_estimate.pending_response_confidence : 0.0f;
    components.pending_motion_valid = causal_pending_usable;
    components.memory_applied = causal_pending_usable;
    if (!config_.tracker.causal_memory_enabled) {
        components.memory_status = "disabled";
    } else if (plan.target_id == 0) {
        components.memory_status = "no_target";
    } else if (plan.aim_authority <= 0.0f) {
        components.memory_status = "no_authority";
    } else if (causal_estimate.status != CausalMotionLedgerStatus::Valid) {
        components.memory_status =
            causal_memory_status_name(causal_estimate.status);
    } else if (!causal_current_provenance_matches) {
        components.memory_status = "provenance_mismatch";
    } else if (!causal_estimate.valid || !causal_estimate.pending_valid) {
        components.memory_status = "pending_invalid";
    } else if (!pipeline_contract::finite(causal_estimate.pending_total_px)) {
        components.memory_status = "nonfinite";
    } else {
        components.memory_status = "applied";
    }
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
        plan, now, observations.capture_fresh, raw_error_px);
    last_ai_aim_mode_ = mode_name(plan.mode);

    auto controller_intent = intent;
    controller_intent.right_x.confidence = intent.right_confidence;
    controller_intent.right_y.confidence = intent.right_confidence;
#if defined(COD_BENCHMARK_MIX_OVERRIDE)
    const bool use_vector_fusion = benchmark_intent_fusion_mode_ ==
            BenchmarkIntentFusionMode::CausalVector ||
        benchmark_intent_fusion_mode_ ==
            BenchmarkIntentFusionMode::CausalVectorBaseline;
#else
    constexpr bool use_vector_fusion = true;
#endif
    auto assist_generation_intent = controller_intent;
    if (use_vector_fusion) {
        // ADS/BodyLock and the dynamics shaper generate the complete AI proposal.
        // Manual/AI ownership is decided exactly once by VectorIntentFuser below;
        // applying the old opposing/cooperative attenuation here would make the
        // fuser attenuate an already weakened proposal.
        assist_generation_intent.filtered_right = {};
        assist_generation_intent.right_x.confidence = 0.0f;
        assist_generation_intent.right_y.confidence = 0.0f;
        assist_generation_intent.right_confidence = 0.0f;
    }
    // TargetPlan does not expose observation innovation. Predicted displacement is
    // target motion, not innovation, so it must not be used as a stability gate.
    const float target_innovation = 0.0f;
    const float normalized_size_change =
        previous_plan_target_id_ == plan.target_id && plan.target_id != 0
        ? std::fabs(plan.normalized_size - previous_plan_normalized_size_)
        : 0.0f;
    auto arbitrate_axis = [&](Axis axis, float error, float error_rate,
                              float manual, float axis_confidence) {
        AxisIntentInput input{};
        input.error = error;
        input.error_rate = error_rate;
        input.manual = manual;
        input.manual_confidence = axis_confidence;
        input.reliability = plan.reliability;
        input.target_innovation_px = target_innovation;
        input.normalized_size_change = normalized_size_change;
        input.manual_escape_threshold =
            config_.ai_aim.body_lock_manual_escape_input_threshold;
        input.manual_preservation_floor =
            config_.intent.wrong_way_manual_preservation_floor;
        input.target_id = plan.target_id;
        input.lifecycle = plan.lifecycle;
        input.mode = plan.mode;
        return axis_intent_arbiter_.update(axis, input, dt);
    };
    AxisDecision x_decision{};
    AxisDecision y_decision{};
    if (!use_vector_fusion) {
        x_decision = arbitrate_axis(
            Axis::X, plan.error_px.x, plan.error_rate_px_per_sec.x,
            intent.filtered_right.x, intent.right_x.confidence);
        y_decision = arbitrate_axis(
            Axis::Y, -plan.error_px.y, -plan.error_rate_px_per_sec.y,
            intent.filtered_right.y, intent.right_y.confidence);
        if (x_decision.intervention) controller_intent.right_x.confidence = 0.0f;
        if (y_decision.intervention) controller_intent.right_y.confidence = 0.0f;
    }
    // The production-equivalent vector path has one target-first output T.
    // Benchmark CausalVector retains its historical mix semantics for old
    // research fixtures; only the explicit baseline mode mirrors production.
#if defined(COD_BENCHMARK_MIX_OVERRIDE)
    const bool target_first_final_path =
        benchmark_intent_fusion_mode_ ==
        BenchmarkIntentFusionMode::CausalVectorBaseline;
#else
    constexpr bool target_first_final_path = true;
#endif
    const bool target_authoritative = target_first_final_path &&
        plan.target_id != 0 && plan.aim_authority > 0.0f &&
        plan.mode != pipeline_contract::ControlMode::Manual;
    const bool target_first_entry = target_authoritative &&
        (!previous_target_first_authoritative_ ||
         previous_plan_target_id_ != plan.target_id);
    if (observations.capture_fresh) {
        last_observed_ads_candidate_count_ = plan.ads_candidate_count;
    } else if (!target_authoritative) {
        last_observed_ads_candidate_count_ = 0;
    }
    previous_plan_normalized_size_ = plan.normalized_size;
    previous_plan_target_id_ = plan.target_id;
    previous_target_first_authoritative_ = target_authoritative;

    const bool fresh_single_target_observation =
        observations.capture_fresh && plan.ads_candidate_count == 1 &&
        plan.lifecycle == pipeline_contract::TargetLifecycle::Observed;
    pipeline_contract::Vec2f requested{};
    BodylockFollowControllerOutput bodylock_diagnostics{};
    if (plan.mode == pipeline_contract::ControlMode::AdsAcquire) {
        requested = ads_controller_.compute(plan, assist_generation_intent, dt);
    } else if (plan.mode == pipeline_contract::ControlMode::BodyLockFollow) {
        bodylock_diagnostics = bodylock_controller_.compute_detailed(
            plan,
            assist_generation_intent,
            dt,
            fresh_single_target_observation);
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
    case ResponseModelConstraintReason::FreshPositionRadialMotionBound:
        components.bodylock_constraint_reason = "fresh_position_radial_motion_bound";
        break;
    case ResponseModelConstraintReason::LifecycleStaleMotionDiscarded:
        components.bodylock_constraint_reason = "lifecycle_stale_motion_discarded";
        break;
    case ResponseModelConstraintReason::None:
        components.bodylock_constraint_reason = "none";
        break;
    }
    pipeline_contract::Vec2f shaped{};
    if (config_.aim_assist_dynamics.enabled ||
        plan.lifecycle == pipeline_contract::TargetLifecycle::Coasting ||
        plan.lifecycle == pipeline_contract::TargetLifecycle::None) {
        shaped = dynamics_shaper_.shape(
            requested,
            assist_generation_intent,
            plan,
            dt,
            {x_decision.intervention ? 1.0f : 0.0f,
             y_decision.intervention ? 1.0f : 0.0f});
    } else {
        dynamics_shaper_.adopt(requested);
        shaped = requested;
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
    components.axis_intent_intervention = {
        x_decision.intervention ? 1.0f : 0.0f,
        y_decision.intervention ? 1.0f : 0.0f};
    components.axis_intent_wrong_way = {
        x_decision.wrong_way ? 1.0f : 0.0f,
        y_decision.wrong_way ? 1.0f : 0.0f};
    components.axis_intent_evidence_stable = {
        x_decision.evidence_stable ? 1.0f : 0.0f,
        y_decision.evidence_stable ? 1.0f : 0.0f};
    components.axis_intent_error_worsening = {
        x_decision.error_worsening ? 1.0f : 0.0f,
        y_decision.error_worsening ? 1.0f : 0.0f};
    components.axis_manual_retention = {
        x_decision.manual_retention,
        y_decision.manual_retention};
    components.ai_aim_stick = components.shaped_assist_stick;
    if (use_vector_fusion) {
        if (target_first_entry) {
            // A no-target/manual tick or a previous target may have left M in
            // VectorIntentFuser's previous-output/reentry state.  A new
            // target-authoritative decision must start from the target
            // proposal on this same tick; this is an ownership boundary, not
            // generic smoothing and does not reset the physical ledger.
            vector_intent_fuser_.reset();
            previous_fusion_manual_escape_ = false;
        }
        VectorIntentFusionInput fusion_input;
        fusion_input.manual_stick = target_authoritative
            ? pipeline_contract::Vec2f{}
            : pipeline_contract::Vec2f{physical.right_x, physical.right_y};
        fusion_input.shaped_ai_stick = {shaped.x, shaped.y};
        fusion_input.plan = plan;
        fusion_input.manual_confidence = target_authoritative
            ? 0.0f : intent.right_confidence;
        fusion_input.fresh_single_target_observation =
            fresh_single_target_observation;
        fusion_input.response_curve = config_.aim_response_curve;
        if (plan.mode == pipeline_contract::ControlMode::BodyLockFollow &&
            bodylock_diagnostics.response_envelope_valid) {
            fusion_input.response_horizon_seconds =
                bodylock_diagnostics.response_horizon_seconds;
            fusion_input.response_horizon_y_seconds =
                bodylock_diagnostics.response_horizon_y_seconds;
            fusion_input.response_max_force =
                bodylock_diagnostics.response_max_force;
            fusion_input.response_envelope_valid = true;
            fusion_input.response_envelope_source =
                bodylock_diagnostics.response_envelope_source;
        } else if (plan.mode == pipeline_contract::ControlMode::AdsAcquire) {
            // Keep this in lockstep with ads_config() and
            // AdsAcquisitionController's response-model request.
            fusion_input.response_horizon_seconds = std::clamp(
                static_cast<float>(config_.ai_aim.ads_snap_window_ms) /
                    1000.0f,
                0.060f, 0.350f);
            fusion_input.response_horizon_y_seconds =
                fusion_input.response_horizon_seconds;
            const float force_headroom = std::sqrt(2.0f);
            fusion_input.response_max_force = {
                config_.ai_aim.ads_snap_max_ai_force * force_headroom,
                config_.ai_aim.ads_snap_max_ai_force_y * force_headroom};
            fusion_input.response_envelope_valid = true;
            fusion_input.response_envelope_source =
                "ads_acquisition_response_model";
        }
#if defined(COD_BENCHMARK_MIX_OVERRIDE)
        if (benchmark_intent_fusion_mode_ ==
            BenchmarkIntentFusionMode::CausalVector) {
            const auto pending_motion = pending_control_motion_.estimate(
                now,
                plan.observation_age_ms,
                plan.response_scale,
                plan.target_id);
            fusion_input.pending_camera_px =
                pending_motion.camera_displacement_px;
            fusion_input.pending_camera_valid = pending_motion.valid;
            fusion_input.causal_mix_enabled = true;
        }
#endif
        const auto fusion = vector_intent_fuser_.update(fusion_input, dt);
        // Coordinator consumes this decision on the next tick. Keeping the
        // feedback at the fuser boundary prevents a second manual escape
        // classifier from growing inside TargetCoordinator.
        previous_fusion_manual_escape_ = target_authoritative
            ? false : fusion.manual_escape;
        output.right_x = clamp_unit(fusion.fused_stick.x);
        output.right_y = clamp_unit(fusion.fused_stick.y);
        const bool current_single_target_observation =
            target_authoritative && last_observed_ads_candidate_count_ == 1 &&
            !plan.cue_continuation &&
            std::isfinite(plan.observation_age_ms) &&
            plan.observation_age_ms <=
                config_.tracker.max_observation_age_ms;
        if (current_single_target_observation &&
            config_.intent.helpful_manual_overdrive_enabled) {
            const auto overdriven = apply_helpful_manual_overdrive(
                {output.right_x, output.right_y},
                {physical.right_x, physical.right_y},
                config_.intent.helpful_manual_overdrive_max_scale);
            output.right_x = clamp_unit(overdriven.x);
            output.right_y = clamp_unit(overdriven.y);
        }
        components.intent_fusion_mode = target_first_final_path
            ? "target_first_final" : "continuous_vector";
        components.intent_fusion_candidate =
            static_cast<int>(fusion.candidate);
        components.intent_fusion_manual_weight =
            fusion.applied_manual_weight;
        components.intent_fusion_ai_weight = fusion.applied_ai_weight;
        if (target_authoritative) {
            // These legacy fields are retained for schema compatibility, but
            // target-first has no manual/AI allocation to report: the fuser
            // receives no M proposal and emits one final target proposal T.
            components.intent_fusion_manual_weight = 0.0f;
            components.intent_fusion_ai_weight = 1.0f;
        }
        components.intent_fusion_winner_margin = fusion.winner_margin;
        components.intent_fusion_fallback = fusion.fallback;
        components.intent_fusion_manual_escape = fusion.manual_escape;
        components.intent_fusion_fresh_vision_policy_applied =
            fusion.fresh_vision_wrong_way_policy_applied;
        components.intent_fusion_fresh_manual_radial_scale =
            fusion.fresh_vision_manual_radial_scale;
        components.intent_fusion_fresh_validated_manual_proposal = {
            fusion.fresh_vision_validated_manual_proposal.x,
            fusion.fresh_vision_validated_manual_proposal.y};
        components.intent_fusion_fresh_validated_ai_proposal = {
            fusion.fresh_vision_validated_ai_proposal.x,
            fusion.fresh_vision_validated_ai_proposal.y};
        components.intent_fusion_fresh_ai_radial_bound =
            fusion.fresh_vision_ai_radial_bound_applied;
        components.intent_fusion_fresh_ai_radial_scale =
            fusion.fresh_vision_ai_radial_scale;
        components.intent_fusion_predictive_envelope_applied =
            fusion.fresh_vision_predictive_envelope_applied;
        components.intent_fusion_fresh_escape_latched =
            fusion.manual_escape_latched;
        components.intent_fusion_fresh_authoritative_error_px = {
            fusion.fresh_vision_authoritative_error_px.x,
            fusion.fresh_vision_authoritative_error_px.y};
        components.intent_fusion_fresh_predicted_error_px = {
            fusion.fresh_vision_predicted_error_px.x,
            fusion.fresh_vision_predicted_error_px.y};
        components.intent_fusion_fresh_raw_manual_radial =
            fusion.fresh_vision_manual_radial;
        components.intent_fusion_fresh_raw_ai_radial =
            fusion.fresh_vision_raw_ai_radial;
        components.intent_fusion_fresh_strongest_valid_radial =
            fusion.fresh_vision_strongest_valid_radial;
        components.intent_fusion_fresh_stopping_radial =
            fusion.fresh_vision_stopping_radial;
        components.intent_fusion_fresh_permitted_radial =
            fusion.fresh_vision_permitted_radial;
        components.intent_fusion_fresh_pre_slew_radial =
            fusion.fresh_vision_pre_slew_radial;
        components.intent_fusion_fresh_final_radial =
            fusion.fresh_vision_final_radial;
        components.intent_fusion_fresh_horizon_seconds =
            fusion.fresh_vision_envelope_horizon_seconds;
        components.intent_fusion_fresh_horizon_y_seconds =
            fusion.fresh_vision_envelope_horizon_y_seconds;
        components.intent_fusion_fresh_max_force = {
            fusion.fresh_vision_envelope_max_force.x,
            fusion.fresh_vision_envelope_max_force.y};
        components.intent_fusion_fresh_envelope_target_stick = {
            fusion.fresh_vision_envelope_target_stick.x,
            fusion.fresh_vision_envelope_target_stick.y};
        components.intent_fusion_fresh_envelope_reason =
            fusion.fresh_vision_envelope_reason != nullptr
            ? fusion.fresh_vision_envelope_reason : "none";
        components.intent_fusion_fresh_envelope_source =
            fusion.fresh_vision_envelope_source != nullptr
            ? fusion.fresh_vision_envelope_source : "unavailable";
    } else {
        previous_fusion_manual_escape_ = false;
        output.right_x = clamp_unit(
            physical.right_x * x_decision.manual_retention + shaped.x);
        output.right_y = clamp_unit(
            physical.right_y * y_decision.manual_retention + shaped.y);
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
#if defined(COD_BENCHMARK_MIX_OVERRIDE)
    if (benchmark_mix_transform_) {
        const auto replacement = benchmark_mix_transform_(
            physical.right_x,
            physical.right_y,
            output.right_x,
            output.right_y,
            components);
        output.right_x = clamp_unit(replacement.x);
        output.right_y = clamp_unit(replacement.y);
    }
#endif
    components.post_ai_stick = {output.right_x, output.right_y};
    components.post_dynamic_stick = components.post_ai_stick;
    components.target_final_stick = components.post_ai_stick;
    components.ai_correction_stick = {
        components.target_final_stick.x - components.manual_stick.x,
        components.target_final_stick.y - components.manual_stick.y};
    if (target_first_final_path) {
        components.manual_authority_mode = target_authoritative
            ? (plan.ads_candidate_count > 1
                ? "multi_target_selector_handover"
                : "single_target_authoritative")
            : "no_target_passthrough";
    }
    components.aim_mode = last_ai_aim_mode_;
    components.assist_authority = plan.aim_authority > 0.0f
        ? (plan.cue_continuation ? "continuity" : "full")
        : "reject";
    components.assist_authority_reason = plan.cue_continuation
        ? "cue_only" : "target_plan";
    components.bodylock_lifecycle = lifecycle_name(plan.lifecycle);
    components.bodylock_transition_reason = "target_plan";
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
        raw_error_px,
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
    last_tracker_motion_output_ = output;
    last_output_components_ = components;
    return output;
}

void NativeGamepadController::report_output_delivery(
    bool delivered,
    bool output_enabled,
    double delivered_at_seconds,
    std::uint64_t physical_actuator_epoch) noexcept {
    const bool causal_memory_required = config_.tracker.causal_memory_enabled;
#if defined(COD_BENCHMARK_MIX_OVERRIDE)
    const bool benchmark_legacy_required =
        benchmark_remaining_work_mode_ !=
        BenchmarkRemainingWorkMode::UseRuntimeConfig;
#else
    constexpr bool benchmark_legacy_required = false;
#endif
    if (!causal_memory_required && !benchmark_legacy_required) return;
    const bool delivery_timestamp_valid =
        std::isfinite(delivered_at_seconds) &&
        delivered_at_seconds > 0.0;
    const bool causal_delivery_valid = delivered && output_enabled &&
        delivery_timestamp_valid && physical_actuator_epoch != 0;
    if (causal_memory_required) {
        if (!causal_delivery_valid) {
            const auto reason = !delivered || !output_enabled ||
                physical_actuator_epoch == 0
                ? CausalMotionLedgerStatus::BackendStateUnknown
                : CausalMotionLedgerStatus::NonMonotonicClock;
            if (physical_actuator_epoch != 0 &&
                (causal_memory_physical_actuator_epoch_ == 0 ||
                 causal_memory_physical_actuator_epoch_ ==
                     physical_actuator_epoch)) {
                causal_memory_physical_actuator_epoch_ =
                    physical_actuator_epoch;
            }
            causal_motion_ledger_.invalidate(
                reason, physical_actuator_epoch);
            last_causal_memory_estimate_ = {};
            last_causal_memory_estimate_.status = reason;
            last_causal_memory_estimate_.physical_actuator_epoch =
                causal_memory_physical_actuator_epoch_;
        } else {
            const auto final_stick = last_output_components_.final_stick;
            const auto normalized_camera_response =
                forward_aim_response_curve(
                    {final_stick.x, final_stick.y},
                    config_.aim_response_curve);
            const auto response_estimate = aim_response_estimator_.estimate();
            float response_scale = 0.0f;
            float response_confidence = 0.0f;
            if (last_target_plan_.target_id != 0 &&
                std::isfinite(last_target_plan_.response_scale) &&
                last_target_plan_.response_scale >= 50.0f) {
                response_scale = last_target_plan_.response_scale;
                response_confidence = std::clamp(
                    last_target_plan_.response_confidence, 0.0f, 1.0f);
            } else {
                response_scale = response_estimate.scale_px_per_stick_second;
                response_confidence = response_estimate.confidence;
            }
            const bool response_model_valid =
                std::isfinite(response_scale) && response_scale > 0.0f &&
                pipeline_contract::finite(normalized_camera_response);
            if (!response_model_valid) {
                response_scale = 500.0f;
                response_confidence = 0.0f;
            }
            DeliveredFinalCommandSample causal_sample;
            causal_sample.delivered_at_seconds = delivered_at_seconds;
            causal_sample.final_stick = {final_stick.x, final_stick.y};
            causal_sample.camera_velocity_px_per_second = {
                normalized_camera_response.x * response_scale,
                -normalized_camera_response.y * response_scale,
            };
            causal_sample.target_id = last_target_plan_.target_id;
            causal_sample.ads_epoch = ads_epoch_;
            causal_sample.delivered = delivered;
            causal_sample.output_enabled = output_enabled;
            causal_sample.physical_actuator_epoch =
                physical_actuator_epoch;
            causal_sample.response_confidence = response_confidence;
            causal_sample.response_model_valid = response_model_valid;
            if (!causal_motion_ledger_.observe(causal_sample)) {
                last_causal_memory_estimate_ = {};
                last_causal_memory_estimate_.status =
                    causal_motion_ledger_.last_invalidation_status();
                last_causal_memory_estimate_.physical_actuator_epoch =
                    physical_actuator_epoch;
            } else {
                causal_memory_physical_actuator_epoch_ =
                    physical_actuator_epoch;
            }
        }
    }
#if defined(COD_BENCHMARK_MIX_OVERRIDE)
    // The old PendingControlMotion path is retained only for benchmark
    // fixtures.  It records the same final delivered output and has no
    // production ownership or target/lifecycle reset semantics.
    if (!benchmark_legacy_required) return;
    if (!causal_delivery_valid) {
        pending_control_motion_.reset();
        remaining_work_accounted_seconds_ = 0.0;
        remaining_work_delivery_target_id_ = 0;
        remaining_work_delivery_ads_epoch_ = 0;
        remaining_work_reset_pending_ = true;
        return;
    }
    if (remaining_work_delivery_target_id_ !=
            last_target_plan_.target_id ||
        remaining_work_delivery_ads_epoch_ != ads_epoch_) {
        pending_control_motion_.reset();
        remaining_work_accounted_seconds_ = delivered_at_seconds;
        remaining_work_delivery_target_id_ =
            last_target_plan_.target_id;
        remaining_work_delivery_ads_epoch_ = ads_epoch_;
        remaining_work_reset_pending_ = true;
    }
    auto delivered_stick =
        last_output_components_.before_recoil_stick;
    if (benchmark_remaining_work_mode_ ==
        BenchmarkRemainingWorkMode::ControllerIntegratedAssistOnly) {
        delivered_stick = last_output_components_.shaped_assist_stick;
    }
    const bool recorded = pending_control_motion_.observe({
        delivered_at_seconds,
        {delivered_stick.x, delivered_stick.y},
        last_target_plan_.target_id,
        delivered,
        output_enabled,
    });
    if (!recorded) {
        remaining_work_accounted_seconds_ = 0.0;
        remaining_work_delivery_target_id_ = 0;
        remaining_work_delivery_ads_epoch_ = 0;
        remaining_work_reset_pending_ = true;
    }
#endif
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
        recoil_output.recoil_stick.y = -adaptive.amount;
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

GamepadOutputState NativeGamepadController::last_tracker_motion_output() const {
    return last_tracker_motion_output_;
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

const CausalMotionPhaseEstimate&
NativeGamepadController::last_causal_memory_estimate() const noexcept {
    return last_causal_memory_estimate_;
}

std::uint64_t NativeGamepadController::ads_epoch() const noexcept {
    return ads_epoch_;
}

const std::string& NativeGamepadController::last_ai_aim_mode() const {
    return last_ai_aim_mode_;
}

#if defined(COD_BENCHMARK_MIX_OVERRIDE)
void NativeGamepadController::set_benchmark_mix_transform(
    BenchmarkMixTransform transform) {
    benchmark_mix_transform_ = std::move(transform);
}

void NativeGamepadController::set_benchmark_intent_fusion_mode(
    BenchmarkIntentFusionMode mode) {
    benchmark_intent_fusion_mode_ = mode;
    axis_intent_arbiter_.reset();
    vector_intent_fuser_.reset();
    pending_control_motion_.reset();
}

void NativeGamepadController::set_benchmark_remaining_work_mode(
    BenchmarkRemainingWorkMode mode) {
    benchmark_remaining_work_mode_ = mode;
    pending_control_motion_.reset();
}

void NativeGamepadController::set_benchmark_remaining_work_scale(
    float scale) noexcept {
    benchmark_remaining_work_scale_ = std::clamp(scale, 0.0f, 1.5f);
}

void NativeGamepadController::set_benchmark_tracker_velocity_alpha(
    float alpha) {
    target_coordinator_.set_motion_velocity_alpha_for_benchmark(alpha);
}

void NativeGamepadController::
set_benchmark_firing_body_geometry_stabilizer_enabled(
    bool enabled) noexcept {
    target_coordinator_.
        set_firing_body_geometry_stabilizer_enabled_for_benchmark(enabled);
}

void NativeGamepadController::
set_benchmark_firing_disturbance_observer_enabled(
    bool enabled) noexcept {
    target_coordinator_.
        set_firing_disturbance_observer_enabled_for_benchmark(enabled);
}

void NativeGamepadController::set_benchmark_player_motion_oracle(
    bool valid,
    bool rate_valid,
    pipeline_contract::Vec2f error_delta_px,
    pipeline_contract::Vec2f error_rate_px_per_sec) noexcept {
    benchmark_player_motion_oracle_valid_ = valid;
    benchmark_player_motion_rate_oracle_valid_ = valid && rate_valid;
    benchmark_player_error_delta_px_ = error_delta_px;
    benchmark_player_error_rate_px_per_sec_ = error_rate_px_per_sec;
}

void NativeGamepadController::set_benchmark_causal_player_motion_enabled(
    bool state_enabled,
    bool forecast_enabled) noexcept {
    target_coordinator_.set_causal_player_motion_enabled_for_benchmark(
        state_enabled, forecast_enabled);
}
#endif

bool NativeGamepadController::body_lock_manual_takeover_active() const {
    return false;
}

RelativeMotionEstimate NativeGamepadController::body_lock_relative_motion_estimate() const {
    return {};
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
