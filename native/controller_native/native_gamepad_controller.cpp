#include "native_gamepad_controller.h"

#include "controller_pipeline.h"

#include "../tracking_native/tracker_authority.h"

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

float clamp_unit(float value) {
    return std::max(-1.0f, std::min(1.0f, value));
}

float axis_sign(float value) {
    if (value > 0.0f) {
        return 1.0f;
    }
    if (value < 0.0f) {
        return -1.0f;
    }
    return 0.0f;
}

float apply_near_target_axis_brake(
    float target_error_px,
    float output_axis,
    float manual_axis,
    float reticle_speed_px_per_sec) {
    constexpr float kAxisDeadzone = 0.015f;
    constexpr float kNearErrorPx = 18.0f;
    constexpr float kFarErrorPx = 36.0f;
    constexpr float kNearHorizonSeconds = 0.022f;
    constexpr float kFarHorizonSeconds = 0.016f;
    constexpr float kNearOvershootBudgetPx = 12.0f;
    constexpr float kFarOvershootBudgetPx = 18.0f;

    const float abs_error = std::fabs(target_error_px);
    if (abs_error <= 0.0f || abs_error > kFarErrorPx ||
        std::fabs(output_axis) <= kAxisDeadzone) {
        return output_axis;
    }

    const float target_direction = axis_sign(target_error_px);
    if (target_direction == 0.0f || axis_sign(output_axis) != target_direction) {
        return output_axis;
    }

    const float t =
        std::max(0.0f, std::min(1.0f, (abs_error - kNearErrorPx) / (kFarErrorPx - kNearErrorPx)));
    const float horizon_seconds =
        kNearHorizonSeconds + ((kFarHorizonSeconds - kNearHorizonSeconds) * t);
    const float overshoot_budget_px =
        kNearOvershootBudgetPx + ((kFarOvershootBudgetPx - kNearOvershootBudgetPx) * t);
    const float safe_speed =
        std::max(1.0f, std::fabs(reticle_speed_px_per_sec));
    const float allowed_abs_axis =
        std::max(kAxisDeadzone, (abs_error + overshoot_budget_px) / (safe_speed * horizon_seconds));
    if (std::fabs(output_axis) <= allowed_abs_axis) {
        return output_axis;
    }

    const float desired_axis = target_direction * allowed_abs_axis;
    const float raw_assist_axis = output_axis - manual_axis;
    float planned_assist_axis = desired_axis - manual_axis;

    if (std::fabs(raw_assist_axis) <= kAxisDeadzone) {
        return output_axis;
    }
    if (axis_sign(planned_assist_axis) != axis_sign(raw_assist_axis)) {
        planned_assist_axis = 0.0f;
    } else if (std::fabs(planned_assist_axis) > std::fabs(raw_assist_axis)) {
        planned_assist_axis = raw_assist_axis;
    }
    return clamp_unit(manual_axis + planned_assist_axis);
}

}  // namespace

NativeGamepadController::NativeGamepadController(
    GamepadRuntimeConfig config,
    std::function<double()> clock)
    : config_(std::move(config)),
      ai_aim_(config_.ai_aim),
      aim_assist_dynamics_(config_.aim_assist_dynamics),
      recoil_(config_.recoil),
      auto_fire_gate_(config_.auto_fire, config_.ai_aim),
      body_lock_short_plan_policy_(config_.ai_aim),
      ads_carry_brake_policy_(config_.ai_aim),
      output_validation_policy_(config_.ai_aim),
      target_snapshot_provider_(config_.ai_aim, config_.tracker_backend),
      clock_(std::move(clock)) {
    recoil_.set_recognizer_state_path(config_.recoil.recognizer_state_path);
    if (!config_.recoil.recognizer_state_path.empty()) {
        recoil_.load_profile_directory(config_.recoil.profile_directory);
    }
    recoil_.load_calibration_directory(config_.recoil.calibration_directory);
}

void NativeGamepadController::reset() {
    target_snapshot_provider_.reset();
    ai_aim_.reset();
    aim_assist_dynamics_.reset();
    recoil_.reset();
    last_pipeline_traces_.clear();
    last_tracker_motion_output_ = GamepadOutputState{};
    last_output_components_ = NativeControllerOutputComponents{};
    last_frame_vision_state_ = NativeControllerVisionState{};
    auto_fire_gate_.reset();
    body_lock_short_plan_policy_.reset();
    ads_carry_brake_policy_.reset();
    output_validation_policy_.reset();
    ads_state_tracker_.reset();
    aim_activation_tracker_.reset();
    last_ads_stopped_at_seconds_ = 0.0;
}

void NativeGamepadController::submit_vision_state(const NativeControllerVisionState& state) {
    target_snapshot_provider_.submit_vision_state(
        state,
        now_seconds(),
        ads_state_tracker_.active());
}

void NativeGamepadController::submit_vision_snapshot(const ControllerVisionSnapshot& snapshot) {
    target_snapshot_provider_.submit_vision_snapshot(
        snapshot,
        now_seconds(),
        ads_state_tracker_.active());
}

GamepadOutputState NativeGamepadController::build_output(const PhysicalGamepadState& physical) {
    last_pipeline_traces_.clear();

    GamepadOutputState output = output_from_physical_input(physical);
    NativeControllerOutputComponents output_components =
        output_components_from_manual_output(output);
    output_components.physical_stick = {physical.right_x, physical.right_y};

    const double now = now_seconds();
    const bool aiming = is_aiming(physical);
    update_ads_state(aiming, now);
    const NativeControllerVisionState frame_vision_state =
        target_snapshot_provider_.vision_state_for_frame(now, ads_state_tracker_.active());
    last_frame_vision_state_ = frame_vision_state;
    const float manual_right_x = output.right_x;
    const float manual_right_y = output.right_y;

    // Pipeline contract: vision/tracker state feeds controller assistance first;
    // recoil stays the final feed-forward stage and does not receive target error.
    GamepadOutputState stage_before_output = output;
    float stage_before_right_y = output.right_y;
    apply_ai_aim(output, physical, frame_vision_state, now);
    capture_output_component_delta(
        stage_before_output,
        output,
        &output_components.ai_aim_stick);
    output_components.post_ai_stick = {output.right_x, output.right_y};
    output_components.aim_mode = ai_aim_.last_mode();
    record_stage_trace("ai_aim", stage_before_right_y, output, false, false);

    float settle_dx = frame_vision_state.dx;
    float settle_dy = frame_vision_state.dy;
    float body_lock_dx = 0.0f;
    float body_lock_dy = 0.0f;
    if (body_lock_error_for_state(frame_vision_state, &body_lock_dx, &body_lock_dy)) {
        settle_dx = body_lock_dx;
        settle_dy = body_lock_dy;
    }

    AutoFireGateInput auto_fire_input;
    auto_fire_input.vision_state = frame_vision_state;
    auto_fire_input.aiming = aiming;
    auto_fire_input.ads_min_elapsed = ads_state_tracker_.min_ads_elapsed(
        config_.ai_aim.auto_fire_ready_min_ads_ms,
        now);
    auto_fire_input.manual_fire_pressed = manual_fire_pressed(physical);
    auto_fire_input.now_seconds = now;
    auto_fire_input.manual_right_x = manual_right_x;
    auto_fire_input.manual_right_y = manual_right_y;
    auto_fire_input.output_right_x = output.right_x;
    auto_fire_input.output_right_y = output.right_y;
    auto_fire_input.settle_dx = settle_dx;
    auto_fire_input.settle_dy = settle_dy;
    const AutoFireGateDecision auto_fire_decision =
        auto_fire_gate_.evaluate(auto_fire_input);

    stage_before_output = output;
    stage_before_right_y = output.right_y;
    apply_aim_assist_dynamics(
        output,
        manual_right_x,
        manual_right_y,
        physical,
        auto_fire_decision.pre_takeover_should_fire);
    capture_output_component_delta(
        stage_before_output,
        output,
        &output_components.dynamic_adjustment_stick);
    output_components.post_dynamic_stick = {output.right_x, output.right_y};
    record_stage_trace(
        "aim_assist_dynamics",
        stage_before_right_y,
        output,
        auto_fire_decision.pre_takeover_should_fire,
        auto_fire_decision.pre_takeover_should_fire);

    if (auto_fire_decision.release_fire_output) {
        auto_fire_gate_.release_fire_output(output);
    }
    stage_before_right_y = output.right_y;
    auto_fire_gate_.apply_fire_output(output, auto_fire_decision.should_fire);
    record_stage_trace(
        "auto_fire",
        stage_before_right_y,
        output,
        false,
        auto_fire_decision.should_fire);

    stage_before_output = output;
    apply_ads_near_target_brake(
        output,
        manual_right_x,
        manual_right_y,
        frame_vision_state,
        now);
    capture_output_component_delta(
        stage_before_output,
        output,
        &output_components.ads_brake_stick);
    output_components.post_ads_brake_stick = {output.right_x, output.right_y};
    output_components.ads_brake_error_px = {frame_vision_state.dx, frame_vision_state.dy};
    output_components.ads_brake_active =
        std::fabs(output_components.ads_brake_stick.x) > 0.0001f ||
        std::fabs(output_components.ads_brake_stick.y) > 0.0001f;

    const bool candidate_output_hold_active =
        target_snapshot_provider_.candidate_output_hold_active(now);
    stage_before_output = output;
    apply_ads_carry_brake(
        output,
        manual_right_x,
        manual_right_y,
        frame_vision_state,
        settle_dx,
        settle_dy,
        now,
        candidate_output_hold_active);
    capture_output_component_delta(
        stage_before_output,
        output,
        &output_components.ads_carry_brake_stick);
    output_components.post_ads_carry_brake_stick = {output.right_x, output.right_y};
    output_components.ads_carry_brake_active =
        std::fabs(output_components.ads_carry_brake_stick.x) > 0.0001f ||
        std::fabs(output_components.ads_carry_brake_stick.y) > 0.0001f;
    output_components.ads_brake_active =
        output_components.ads_brake_active ||
        output_components.ads_carry_brake_active;

    stage_before_output = output;
    stage_before_right_y = output.right_y;
    output_components.before_recoil_stick = {output.right_x, output.right_y};
    apply_recoil(output, physical, auto_fire_decision.should_fire, now);
    capture_output_component_delta(
        stage_before_output,
        output,
        &output_components.recoil_stick);
    record_stage_trace(
        "recoil",
        stage_before_right_y,
        output,
        auto_fire_decision.should_fire,
        auto_fire_decision.should_fire);
    capture_final_output_component(output, &output_components);
    // Tracker receives final camera motion for ego projection, while the
    // component split preserves manual/assist/dynamics/recoil attribution.
    record_target_tracker_output(output_components, now);
    last_output_components_ = output_components;
    return output;
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
    return ai_aim_.last_mode();
}

bool NativeGamepadController::body_lock_manual_takeover_active() const {
    return ai_aim_.manual_takeover_active();
}

bool NativeGamepadController::is_aiming(const PhysicalGamepadState& physical) {
    return aim_activation_tracker_.update(physical, config_.rb_counts_as_aiming);
}

bool NativeGamepadController::has_fresh_aim_target(
    const NativeControllerVisionState& vision_state,
    double now_seconds) const {
    const float max_age_ms = config_.ai_aim.target_max_age_ms;
    if (max_age_ms <= 0.0f || vision_state.observed_at_seconds <= 0.0 ||
        now_seconds <= 0.0) {
        return vision_state.has_target && vision_state.aim_authority;
    }
    const double age_seconds = std::max(0.0, now_seconds - vision_state.observed_at_seconds);
    return vision_state.has_target &&
        vision_state.aim_authority &&
        age_seconds <= (static_cast<double>(max_age_ms) / 1000.0);
}

void NativeGamepadController::update_ads_state(bool aiming, double now_seconds) {
    const AdsStateTransition transition = ads_state_tracker_.update(aiming, now_seconds);
    if (transition.started) {
        target_snapshot_provider_.clear_target_state_observed_before(last_ads_stopped_at_seconds_);
        auto_fire_gate_.reset_readiness();
        return;
    }
    if (transition.stopped) {
        last_ads_stopped_at_seconds_ = now_seconds;
        target_snapshot_provider_.clear_ads_transient_state();
        auto_fire_gate_.reset_readiness();
    }
}

bool NativeGamepadController::is_strong_aim_target(
    const NativeControllerVisionState& vision_state) const {
    const tracking_native::TargetAuthorityDecision authority =
        tracking_native::classify_target_authority(
            vision_state.has_target,
            vision_state.aim_authority,
            vision_state.fire_authority,
            vision_state.target_tier);
    return authority.is_strong_aim_target;
}

bool NativeGamepadController::ads_snap_active_for_frame(
    const NativeControllerVisionState& vision_state,
    bool aiming,
    double now_seconds) const {
    if (!aiming || !has_fresh_aim_target(vision_state, now_seconds) ||
        !is_strong_aim_target(vision_state)) {
        return false;
    }
    if (target_snapshot_provider_.candidate_reacquire_snap_active(now_seconds)) {
        return true;
    }
    return ads_state_tracker_.snap_window_active(
        config_.ai_aim.ads_snap_window_ms,
        now_seconds);
}

float NativeGamepadController::ads_snap_progress_ratio(double now_seconds) const {
    return ads_state_tracker_.snap_progress_ratio(
        config_.ai_aim.ads_snap_window_ms,
        now_seconds);
}

float NativeGamepadController::ads_snap_remaining_seconds(double now_seconds) const {
    return ads_state_tracker_.snap_remaining_seconds(
        config_.ai_aim.ads_snap_window_ms,
        now_seconds);
}

bool NativeGamepadController::body_lock_error_for_state(
    const NativeControllerVisionState& vision_state,
    float* out_dx,
    float* out_dy) const {
    if (!vision_state.has_target || !vision_state.aim_authority ||
        !vision_state.has_body_box ||
        vision_state.body_x2 <= vision_state.body_x1 ||
        vision_state.body_y2 <= vision_state.body_y1) {
        return false;
    }
    const float tolerance = std::max(0.0f, config_.ai_aim.body_lock_box_tolerance_px);
    if (vision_state.screen_center_x < vision_state.body_x1 - tolerance ||
        vision_state.screen_center_x > vision_state.body_x2 + tolerance ||
        vision_state.screen_center_y < vision_state.body_y1 - tolerance ||
        vision_state.screen_center_y > vision_state.body_y2 + tolerance) {
        return false;
    }

    const float ratio = std::max(
        0.0f,
        std::min(1.0f, config_.ai_aim.body_lock_upper_body_ratio));
    const float lock_x = (vision_state.body_x1 + vision_state.body_x2) * 0.5f;
    const float lock_y =
        vision_state.body_y1 + ((vision_state.body_y2 - vision_state.body_y1) * ratio);
    const float lock_dx = lock_x - vision_state.screen_center_x;
    const float lock_dy = lock_y - vision_state.screen_center_y;
    const float activation_half =
        std::max(0.0f, config_.ai_aim.body_lock_activation_box_px) * 0.5f;
    if (std::fabs(lock_dx * config_.ai_aim.ai_delta_gain) > activation_half ||
        std::fabs(lock_dy * config_.ai_aim.ai_delta_gain) > activation_half) {
        return false;
    }

    if (out_dx != nullptr) {
        *out_dx = lock_dx;
    }
    if (out_dy != nullptr) {
        *out_dy = lock_dy;
    }
    return true;
}

bool NativeGamepadController::manual_fire_pressed(const PhysicalGamepadState& physical) const {
    return physical.rb || physical.right_trigger > 0.04f;
}

void NativeGamepadController::apply_ai_aim(
    GamepadOutputState& output,
    const PhysicalGamepadState& physical,
    const NativeControllerVisionState& vision_state,
    double now_seconds) {
    NativeAiAimInput input;
    input.aiming = is_aiming(physical);
    input.has_target = vision_state.has_target;
    input.aim_authority = vision_state.aim_authority;
    input.ads_snap_active = ads_snap_active_for_frame(vision_state, input.aiming, now_seconds);
    input.fire_active =
        physical.rb || physical.right_trigger > 0.04f ||
        vision_state.auto_fire_requested || auto_fire_gate_.active();
    input.ads_snap_progress_ratio = ads_snap_progress_ratio(now_seconds);
    input.ads_snap_remaining_seconds = ads_snap_remaining_seconds(now_seconds);
    input.dx = vision_state.dx;
    input.dy = vision_state.dy;
    input.has_mixing_reference = vision_state.has_tracker_projection;
    input.mixing_reference_dx = vision_state.tracker_dx;
    input.mixing_reference_dy = vision_state.tracker_dy;
    input.target_x = vision_state.target_x;
    input.target_y = vision_state.target_y;
    input.screen_center_x = vision_state.screen_center_x;
    input.screen_center_y = vision_state.screen_center_y;
    input.has_body_box = vision_state.has_body_box;
    input.body_x1 = vision_state.body_x1;
    input.body_y1 = vision_state.body_y1;
    input.body_x2 = vision_state.body_x2;
    input.body_y2 = vision_state.body_y2;
    input.target_tier = vision_state.target_tier;
    input.observed_at_seconds = vision_state.observed_at_seconds;
    input.now_seconds = now_seconds;
    input.manual_right_x = output.right_x;
    input.manual_right_y = output.right_y;

    const NativeAiAimOutput assist = ai_aim_.compute(input);
    if (assist.has_assist) {
        output.right_x = clamp_unit(output.right_x + assist.assist_x);
        output.right_y = clamp_unit(output.right_y + assist.assist_y);
    }
    apply_body_lock_short_plan(
        output,
        input.manual_right_x,
        input.manual_right_y,
        !input.fire_active,
        vision_state,
        now_seconds);
    OutputValidationPolicyInput validation_input;
    validation_input.vision_state = vision_state;
    validation_input.output = output;
    validation_input.manual_right_x = input.manual_right_x;
    validation_input.manual_right_y = input.manual_right_y;
    validation_input.ads_active = ads_state_tracker_.active();
    validation_input.candidate_output_hold_active =
        target_snapshot_provider_.candidate_output_hold_active(now_seconds);
    validation_input.now_seconds = now_seconds;
    output = output_validation_policy_.apply(validation_input);
}

void NativeGamepadController::apply_body_lock_short_plan(
    GamepadOutputState& output,
    float manual_right_x,
    float manual_right_y,
    bool vertical_plan_allowed,
    const NativeControllerVisionState& vision_state,
    double now_seconds) {
    float lock_dx = 0.0f;
    float lock_dy = 0.0f;
    const bool body_lock_available =
        ai_aim_.last_mode() == "body_lock" &&
        body_lock_error_for_state(vision_state, &lock_dx, &lock_dy);
    BodyLockShortPlanInput plan_input;
    plan_input.vision_state = vision_state;
    plan_input.output = output;
    plan_input.manual_right_x = manual_right_x;
    plan_input.manual_right_y = manual_right_y;
    plan_input.vertical_plan_allowed = vertical_plan_allowed;
    plan_input.body_lock_available = body_lock_available;
    plan_input.lock_dx = lock_dx;
    plan_input.lock_dy = lock_dy;
    plan_input.now_seconds = now_seconds;
    output = body_lock_short_plan_policy_.apply(plan_input);
}

void NativeGamepadController::apply_aim_assist_dynamics(
    GamepadOutputState& output,
    float manual_right_x,
    float manual_right_y,
    const PhysicalGamepadState& physical,
    bool auto_fire_active) {
    NativeAimAssistDynamicsInput input;
    input.manual_right_x = manual_right_x;
    input.manual_right_y = manual_right_y;
    input.assisted_right_x = output.right_x;
    input.assisted_right_y = output.right_y;
    input.recoil_active = false;
    input.manual_fire_active = physical.rb || physical.right_trigger > 0.04f;
    input.auto_fire_active = auto_fire_active;
    input.now_seconds = now_seconds();

    const NativeAimAssistDynamicsOutput shaped = aim_assist_dynamics_.apply(input);
    output.right_x = shaped.right_x;
    output.right_y = shaped.right_y;
}

void NativeGamepadController::apply_ads_near_target_brake(
    GamepadOutputState& output,
    float manual_right_x,
    float manual_right_y,
    const NativeControllerVisionState& vision_state,
    double now_seconds) const {
    if (!ads_state_tracker_.active() || ai_aim_.last_mode() != "ads_snap" ||
        !has_fresh_aim_target(vision_state, now_seconds)) {
        return;
    }

    const float reticle_speed =
        std::max(1.0f, config_.ai_aim.target_projection_reticle_speed_px_per_sec);
    output.right_x = apply_near_target_axis_brake(
        vision_state.dx,
        output.right_x,
        manual_right_x,
        reticle_speed);

    const float output_move_y = -output.right_y;
    const float manual_move_y = -manual_right_y;
    const float shaped_move_y = apply_near_target_axis_brake(
        vision_state.dy,
        output_move_y,
        manual_move_y,
        reticle_speed);
    output.right_y = clamp_unit(-shaped_move_y);
}

void NativeGamepadController::apply_ads_carry_brake(
    GamepadOutputState& output,
    float manual_right_x,
    float manual_right_y,
    const NativeControllerVisionState& vision_state,
    float target_error_x,
    float target_error_y,
    double now_seconds,
    bool candidate_output_hold_active) const {
    AdsCarryBrakeInput input;
    input.output = output;
    input.manual_right_x = manual_right_x;
    input.manual_right_y = manual_right_y;
    input.target_error_x = target_error_x;
    input.target_error_y = target_error_y;
    input.ads_active = ads_state_tracker_.active();
    const double ads_elapsed_seconds = ads_state_tracker_.active()
        ? std::max(0.0, now_seconds - ads_state_tracker_.started_at_seconds())
        : 0.0;
    const double carry_window_seconds =
        (static_cast<double>(std::max(1, config_.ai_aim.ads_snap_window_ms)) + 120.0) /
        1000.0;
    input.ads_acquisition_active =
        ads_state_tracker_.active() && ads_elapsed_seconds <= carry_window_seconds;
    input.body_lock_active = ai_aim_.last_mode() == "body_lock";
    input.has_fresh_target = has_fresh_aim_target(vision_state, now_seconds);
    input.candidate_output_hold_active = candidate_output_hold_active;
    input.reticle_speed_px_per_sec =
        std::max(1.0f, config_.ai_aim.target_projection_reticle_speed_px_per_sec);
    output = ads_carry_brake_policy_.apply(input);
}

void NativeGamepadController::apply_recoil(
    GamepadOutputState& output,
    const PhysicalGamepadState& physical,
    bool auto_fire_active,
    double now_seconds) {
    NativeRecoilInput input;
    input.fire_active = auto_fire_active || physical.rb || physical.right_trigger > 0.04f;
    input.aiming = is_aiming(physical);
    input.now_seconds = now_seconds;

    const recoil_native::RecoilBoundaryOutput recoil_output = recoil_.compute(input);
    if (!recoil_output.recoil_active) {
        return;
    }
    output.right_x = clamp_unit(output.right_x + recoil_output.recoil_stick.x);
    output.right_y = clamp_unit(output.right_y + recoil_output.recoil_stick.y);
}

void NativeGamepadController::record_target_tracker_output(
    const NativeControllerOutputComponents& components,
    double now_seconds) {
    last_tracker_motion_output_ = GamepadOutputState{};
    last_tracker_motion_output_.right_x = components.final_stick.x;
    last_tracker_motion_output_.right_y = components.final_stick.y;
    last_tracker_motion_output_.rb = components.fire_button;
    target_snapshot_provider_.record_output(components, now_seconds);
}

void NativeGamepadController::record_stage_trace(
    const std::string& stage_name,
    float before_right_y,
    const GamepadOutputState& output,
    bool before_auto_fire_active,
    bool after_auto_fire_active) {
    NativeControllerStageTrace trace;
    trace.stage_name = stage_name;
    trace.before_right_y = before_right_y;
    trace.after_right_y = output.right_y;
    trace.delta_right_y = output.right_y - before_right_y;
    trace.before_auto_fire_active = before_auto_fire_active;
    trace.after_auto_fire_active = after_auto_fire_active;
    last_pipeline_traces_.push_back(std::move(trace));
}

double NativeGamepadController::now_seconds() const {
    return clock_ ? clock_() : current_seconds();
}

}  // namespace controller_native
