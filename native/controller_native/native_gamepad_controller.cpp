#include "native_gamepad_controller.h"
#include "target_geometry.h"

#include <algorithm>
#include <chrono>
#include <cmath>
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

float apply_wrong_way_budget(
    float manual,
    float assist,
    float command_error,
    float wrong_way_budget,
    float stopping_output_budget) noexcept {
    float combined = clamp_unit(manual + assist);
    const float stopping_budget = std::clamp(stopping_output_budget, 0.0f, 1.0f);
    if (stopping_budget < 1.0f && std::fabs(combined) > stopping_budget) {
        combined = std::copysign(stopping_budget, combined);
    }
    const float bounded_budget = std::clamp(wrong_way_budget, 0.0f, 1.0f);
    if (bounded_budget >= 1.0f || combined * command_error >= 0.0f ||
        std::fabs(combined) <= bounded_budget) {
        return combined;
    }
    return std::copysign(bounded_budget, combined);
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

TargetCoordinatorConfig coordinator_config(const GamepadRuntimeConfig& config) {
    TargetCoordinatorConfig result{};
    result.hold_ms = std::max(
        80.0f, std::max(config.ai_aim.target_max_age_ms,
                        config.ai_aim.target_projection_max_age_ms));
    result.settle_radius_px = std::max(6.0f, config.ai_aim.body_lock_box_tolerance_px);
    result.settle_frames = static_cast<std::uint32_t>(
        std::max(1, config.ai_aim.body_lock_confidence_frames));
    result.ads_max_acquisition_ms = static_cast<float>(
        std::max(0, config.ai_aim.ads_snap_window_ms));
    result.bodylock_activation_radius_px = std::max(
        result.settle_radius_px, config.ai_aim.body_lock_activation_box_px);
    return result;
}

AdsAcquisitionControllerConfig ads_config(const GamepadRuntimeConfig& config) {
    AdsAcquisitionControllerConfig result{};
    result.max_force_x = config.ai_aim.ads_snap_max_ai_force;
    result.max_force_y = config.ai_aim.ads_snap_max_ai_force_y;
    result.error_range_x_px = std::max(1.0f, config.ai_aim.max_pixels);
    result.error_range_y_px = std::max(1.0f, config.ai_aim.piecewise_max_pixels_y);
    return result;
}

BodylockFollowControllerConfig bodylock_config(const GamepadRuntimeConfig& config) {
    BodylockFollowControllerConfig result{};
    result.max_force_x = config.ai_aim.body_lock_max_ai_force;
    result.max_force_y = config.ai_aim.body_lock_max_ai_force_y;
    result.feedback_range_x_px = std::max(
        18.0f, config.ai_aim.body_lock_box_tolerance_px * 1.5f);
    result.feedback_range_y_px = result.feedback_range_x_px;
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
      recoil_(config_.recoil),
      auto_fire_gate_(config_.auto_fire, config_.ai_aim),
      clock_(std::move(clock)) {
    recoil_.set_recognizer_state_path(config_.recoil.recognizer_state_path);
    if (!config_.recoil.recognizer_state_path.empty()) {
        recoil_.load_profile_directory(config_.recoil.profile_directory);
    }
    recoil_.load_calibration_directory(config_.recoil.calibration_directory);
}

void NativeGamepadController::reset() {
    intent_filter_.reset();
    target_coordinator_.reset();
    axis_intent_arbiter_.reset();
    dynamics_shaper_.reset();
    recoil_.reset();
    aim_activation_tracker_.reset();
    auto_fire_gate_.reset();
    pending_snapshot_ = {};
    has_pending_snapshot_ = false;
    aiming_ = false;
    previous_aiming_ = false;
    ads_epoch_ = 0;
    legacy_vision_sequence_ = 0;
    last_plan_target_id_ = 0;
    last_plan_normalized_size_ = 0.0f;
    last_tick_seconds_ = 0.0;
    last_pipeline_traces_.clear();
    last_tracker_motion_output_ = {};
    last_output_components_ = {};
    last_frame_vision_state_ = {};
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
    batch.frame_width_px = snapshot.state.screen_center_x > 0.0f
        ? snapshot.state.screen_center_x * 2.0f : 480.0f;
    batch.frame_height_px = snapshot.state.screen_center_y > 0.0f
        ? snapshot.state.screen_center_y * 2.0f : 416.0f;
    batch.capture_fresh = snapshot.frame_updated || snapshot.state.fresh_observation;
    batch.fire_requested = snapshot.state.auto_fire_requested;
    batch.observed_fire_eligible = snapshot.state.fire_authority &&
        (snapshot.state.target_tier == "strong" ||
         snapshot.state.target_tier == "observed_strong");
    batch.has_control_response_hint = snapshot.state.has_camera_attributed_velocity;
    batch.control_response_x_px_per_second =
        snapshot.state.camera_attributed_velocity_x_px_per_sec;
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
            : snapshot.state.selected_observation_id;
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
        destination.normalized_size = std::clamp(
            height / std::max(1.0f, batch.frame_height_px), 0.0f, 1.0f);
        destination.confidence = snapshot.state.aim_authority ? 1.0f : 0.6f;
        destination.reliability = destination.confidence * (height > 0.0f
            ? std::clamp(destination.normalized_size / 0.12f, 0.35f, 1.0f)
            : 1.0f);
        destination.body_cue = snapshot.state.has_body_box;
    }
    return batch;
}

NativeControllerVisionState NativeGamepadController::vision_state_from_plan(
    const pipeline_contract::TargetPlan& plan,
    double now_seconds) const {
    NativeControllerVisionState state{};
    state.vision_sequence = plan.source_frame_id;
    state.selected_track_id = plan.target_id;
    state.selected_observation_id = plan.target_id;
    state.has_target = plan.lifecycle != pipeline_contract::TargetLifecycle::None;
    state.current_observed_target_present =
        plan.lifecycle == pipeline_contract::TargetLifecycle::Observed ||
        plan.lifecycle == pipeline_contract::TargetLifecycle::Reacquiring;
    state.fresh_observation = state.current_observed_target_present;
    state.aim_authority = plan.aim_authority > 0.0f;
    state.fire_authority = plan.fire_authority;
    state.auto_fire_requested = plan.fire_requested;
    state.dx = plan.error_px.x;
    state.dy = plan.error_px.y;
    state.target_x = plan.aim_px.x;
    state.target_y = plan.aim_px.y;
    state.screen_center_x = plan.aim_px.x - plan.error_px.x;
    state.screen_center_y = plan.aim_px.y - plan.error_px.y;
    state.observed_at_seconds = now_seconds - plan.observation_age_ms / 1000.0;
    state.target_tier = state.current_observed_target_present ? "observed_strong" : "predicted";
    state.has_tracker_projection = state.has_target;
    state.tracker_dx = plan.error_px.x;
    state.tracker_dy = plan.error_px.y;
    state.has_camera_attributed_velocity = plan.response_confidence > 0.0f;
    state.camera_attributed_velocity_x_px_per_sec =
        plan.response_scale * plan.response_confidence;
    state.authority_decision_valid = true;
    state.assist_authority_state = !state.aim_authority
        ? pipeline_contract::AssistAuthorityState::Reject
        : plan.lifecycle == pipeline_contract::TargetLifecycle::Coasting
            ? pipeline_contract::AssistAuthorityState::Continuity
            : pipeline_contract::AssistAuthorityState::ObservedStrong;
    return state;
}

GamepadOutputState NativeGamepadController::build_output(const PhysicalGamepadState& physical) {
    last_pipeline_traces_.clear();
    GamepadOutputState output = output_from_physical_input(physical);
    auto components = output_components_from_manual_output(output);
    components.physical_stick = {physical.right_x, physical.right_y};
    const double now = now_seconds();
    const float dt = last_tick_seconds_ > 0.0
        ? static_cast<float>(std::clamp(now - last_tick_seconds_, 0.0001, 0.05))
        : 0.001f;
    last_tick_seconds_ = now;
    aiming_ = aim_activation_tracker_.update(physical, config_.rb_counts_as_aiming);
    if (aiming_ && !previous_aiming_) {
        target_coordinator_.begin_ads_epoch(++ads_epoch_);
        axis_intent_arbiter_.reset();
        auto_fire_gate_.reset_readiness();
    }
    previous_aiming_ = aiming_;
    const auto intent = intent_filter_.update(
        {physical.left_x, physical.left_y},
        {physical.right_x, physical.right_y},
        aiming_, manual_fire_pressed(physical), now);

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
    const auto plan = target_coordinator_.update(observations, intent, now);
    last_frame_vision_state_ = vision_state_from_plan(plan, now);
    last_ai_aim_mode_ = mode_name(plan.mode);

    pipeline_contract::Vec2f requested{};
    if (plan.mode == pipeline_contract::ControlMode::AdsAcquire) {
        requested = ads_controller_.compute(plan, intent, dt);
    } else if (plan.mode == pipeline_contract::ControlMode::BodyLockFollow) {
        requested = bodylock_controller_.compute(plan, intent, dt);
    }
    const bool same_plan_target = plan.target_id != 0 && plan.target_id == last_plan_target_id_;
    const float normalized_size_change = same_plan_target && last_plan_normalized_size_ > 0.0001f
        ? std::fabs(plan.normalized_size - last_plan_normalized_size_) /
            last_plan_normalized_size_
        : 0.0f;
    const float innovation_x = std::fabs(plan.aim_px.x - plan.predicted_aim_px.x);
    const float innovation_y = std::fabs(plan.aim_px.y - plan.predicted_aim_px.y);
    const float escape_threshold = std::clamp(
        config_.ai_aim.body_lock_manual_escape_input_threshold,
        0.0f,
        1.0f);
    AxisIntentInput x_input;
    x_input.error = plan.error_px.x;
    x_input.error_rate = plan.error_rate_px_per_sec.x;
    x_input.requested_assist = requested.x;
    x_input.manual = intent.filtered_right.x;
    x_input.manual_confidence = intent.right_x.confidence;
    x_input.reliability = plan.reliability;
    x_input.target_innovation_px = innovation_x;
    x_input.normalized_size_change = normalized_size_change;
    x_input.manual_escape_threshold = escape_threshold;
    x_input.target_id = plan.target_id;
    x_input.lifecycle = plan.lifecycle;
    x_input.mode = plan.mode;
    AxisIntentInput y_input = x_input;
    y_input.error = -plan.error_px.y;
    y_input.error_rate = -plan.error_rate_px_per_sec.y;
    y_input.requested_assist = requested.y;
    y_input.manual = intent.filtered_right.y;
    y_input.manual_confidence = intent.right_y.confidence;
    y_input.target_innovation_px = innovation_y;
    const AxisDecision x_decision = axis_intent_arbiter_.update(Axis::X, x_input, dt);
    const AxisDecision y_decision = axis_intent_arbiter_.update(Axis::Y, y_input, dt);
    const pipeline_contract::Vec2f arbitrated{
        x_decision.assist_output,
        y_decision.assist_output,
    };
    last_plan_target_id_ = plan.target_id;
    last_plan_normalized_size_ = plan.normalized_size;
    pipeline_contract::Vec2f shaped{};
    if (config_.aim_assist_dynamics.enabled ||
        plan.lifecycle == pipeline_contract::TargetLifecycle::Coasting ||
        plan.lifecycle == pipeline_contract::TargetLifecycle::None) {
        shaped = dynamics_shaper_.shape(arbitrated, plan, dt);
    } else {
        dynamics_shaper_.adopt(arbitrated);
        shaped = arbitrated;
    }
    components.requested_assist_stick = {requested.x, requested.y};
    components.arbitrated_assist_stick = {arbitrated.x, arbitrated.y};
    components.shaped_assist_stick = {shaped.x, shaped.y};
    components.axis_assist_scale = {x_decision.assist_scale, y_decision.assist_scale};
    components.axis_divergence_risk = {
        x_decision.divergence_risk,
        y_decision.divergence_risk,
    };
    components.axis_wrong_way_budget = {
        x_decision.wrong_way_budget,
        y_decision.wrong_way_budget,
    };
    components.axis_stopping_output_budget = {
        x_decision.stopping_output_budget,
        y_decision.stopping_output_budget,
    };
    components.axis_x_reason = to_string(x_decision.reason);
    components.axis_y_reason = to_string(y_decision.reason);
    components.ai_aim_stick = components.shaped_assist_stick;
    output.right_x = apply_wrong_way_budget(
        physical.right_x, shaped.x, x_input.error,
        x_decision.wrong_way_budget, x_decision.stopping_output_budget);
    output.right_y = apply_wrong_way_budget(
        physical.right_y, shaped.y, y_input.error,
        y_decision.wrong_way_budget, y_decision.stopping_output_budget);
    components.post_ai_stick = {output.right_x, output.right_y};
    components.post_dynamic_stick = components.post_ai_stick;
    components.aim_mode = last_ai_aim_mode_;
    components.assist_authority = plan.aim_authority > 0.0f ? "full" : "reject";
    components.assist_authority_reason = "target_plan";
    components.bodylock_lifecycle = lifecycle_name(plan.lifecycle);
    components.bodylock_transition_reason = "target_plan";
    components.assist_limit_reason = "per_axis_intent_arbiter";
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
    if (fire.release_fire_output) auto_fire_gate_.release_fire_output(output);
    auto_fire_gate_.apply_fire_output(output, fire.should_fire);
    record_stage_trace(
        "auto_fire", output.right_y, output,
        fire.before_auto_fire_active, fire.after_auto_fire_active);
    components.auto_fire_requested = plan.fire_requested;
    components.auto_fire_aim_ready = fire.aim_ready;
    components.auto_fire_allowed = fire.pre_takeover_should_fire;
    components.auto_fire_active = fire.should_fire;
    components.auto_fire_block_reason = auto_fire_block_reason_name(fire.block_reason);

    components.before_recoil_stick = {output.right_x, output.right_y};
    const auto before_recoil = output;
    apply_recoil(output, physical, aiming_, fire.should_fire, now);
    record_stage_trace(
        "recoil", before_recoil.right_y, output,
        fire.after_auto_fire_active, fire.after_auto_fire_active);
    capture_output_component_delta(before_recoil, output, &components.recoil_stick);
    capture_final_output_component(output, &components);
    last_tracker_motion_output_ = output;
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
    bool aiming,
    bool auto_fire_active,
    double now_seconds) {
    NativeRecoilInput input{};
    input.fire_active = auto_fire_active || manual_fire_pressed(physical);
    input.aiming = aiming;
    input.now_seconds = now_seconds;
    const auto recoil_output = recoil_.compute(input);
    if (!recoil_output.recoil_active) return;
    output.right_x = clamp_unit(output.right_x + recoil_output.recoil_stick.x);
    output.right_y = clamp_unit(output.right_y + recoil_output.recoil_stick.y);
}

NativeAutoFireCounters NativeGamepadController::auto_fire_counters() const {
    return auto_fire_gate_.counters();
}

const std::vector<NativeControllerStageTrace>& NativeGamepadController::last_pipeline_traces() const {
    return last_pipeline_traces_;
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

const std::string& NativeGamepadController::last_ai_aim_mode() const {
    return last_ai_aim_mode_;
}

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
