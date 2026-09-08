#include "mouse_native/mouse_controller_facade.h"

#include "controller_native/aim_response_curve_plugin.h"
#include "pipeline_contract/target_acquisition.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <limits>

namespace mouse_native {

bool valid(MouseControllerTuning tuning) noexcept {
    return std::isfinite(tuning.speed) && tuning.speed >= 0.5f && tuning.speed <= 3.0f &&
        std::isfinite(tuning.breakaway) && tuning.breakaway >= 1.0f && tuning.breakaway <= 8.0f &&
        tuning.breakaway >= tuning.speed && std::isfinite(tuning.bodylock_deadzone) &&
        tuning.bodylock_deadzone >= 0.0f && tuning.bodylock_deadzone <= 0.9f &&
        std::isfinite(tuning.bodylock_range_px) && (tuning.bodylock_range_px == 0 ||
            (tuning.bodylock_range_px >= 16 && tuning.bodylock_range_px <= 2048)) &&
        std::isfinite(tuning.bodylock_accel_ms) && (tuning.bodylock_accel_ms == 0 ||
            (tuning.bodylock_accel_ms >= 1 && tuning.bodylock_accel_ms <= 250)) &&
        std::isfinite(tuning.bodylock_decel_ms) && (tuning.bodylock_decel_ms == 0 ||
            (tuning.bodylock_decel_ms >= 1 && tuning.bodylock_decel_ms <= 250)) &&
        std::isfinite(tuning.bodylock_point_tolerance_px) &&
        tuning.bodylock_point_tolerance_px >= 0 && tuning.bodylock_point_tolerance_px <= 16;
}

controller_native::GamepadRuntimeConfig make_mouse_controller_config(
    controller_native::GamepadRuntimeConfig config, MouseControllerTuning tuning) {
    if (!valid(tuning)) throw std::invalid_argument(
        "invalid mouse tuning: speed 0.5..3, breakaway 1..8 and >= speed, deadzone 0..0.9, range 0 or 16..2048, ramp 0 or 1..250 ms");
    // Raise the physical range represented by u=1, then express the AI caps
    // in those same units. This keeps speed and manual-release tuning separate.
    config.ai_aim.adapter_response_px_per_second = 500.0f * tuning.breakaway;
    config.ai_aim.adapter_ads_speed = tuning.speed;
    const float force_scale = tuning.speed / tuning.breakaway;
    config.ai_aim.adapter_force_budget_scale = force_scale;
    config.ai_aim.adapter_bodylock_manual_weight = 1.0f / tuning.breakaway;
    config.ai_aim.adapter_direct_mouse_manual = true;
    config.ai_aim.adapter_bodylock_deadzone = tuning.bodylock_deadzone;
    if(tuning.bodylock_range_px>0) config.ai_aim.body_lock_activation_box_px=tuning.bodylock_range_px;
    config.ai_aim.adapter_bodylock_accel_ms=tuning.bodylock_accel_ms;
    config.ai_aim.adapter_bodylock_decel_ms=tuning.bodylock_decel_ms;
    config.ai_aim.adapter_bodylock_point_tolerance_px=tuning.bodylock_point_tolerance_px;
    // AutoFire readiness is a physical motion limit, independent of tuning.
    config.ai_aim.auto_fire_ready_max_ai_stick /= tuning.breakaway;
    config.ai_aim.ads_snap_max_ai_force *= force_scale;
    config.ai_aim.ads_snap_max_ai_force_y *= force_scale;
    config.ai_aim.body_lock_max_ai_force *= force_scale;
    config.ai_aim.body_lock_max_ai_force_y *= force_scale;
    config.rb_counts_as_aiming = false;
    config.enemy_mark.enabled = false;
    config.ai_aim.aim_response_learning_enabled = false;
    config.aim_response_curve.algorithm =
        controller_native::AimResponseCurveAlgorithm::Linear;
    config.recoil.enabled = false;
    config.recoil.selection_log_enabled = false;
    config.recoil.profile_playback_enabled = false;
    config.recoil.native_recognizer_enabled = false;
    config.recoil.recognizer_log_enabled = false;
    return config;
}

MouseControllerFacade::MouseControllerFacade(MouseControllerFacadeConfig config)
    : controller_config_(make_mouse_controller_config(config.controller, config.tuning)),
      controller_(controller_config_, &controller_clock_seconds_),
      rate_adapter_(config.rate_adapter),
      actuator_adapter_(config.actuator_adapter),
      tuning_(config.tuning), recoil_(config.recoil) {}

void MouseControllerFacade::submit_vision_snapshot(
    const controller_native::ControllerVisionSnapshot& snapshot) {
    pending_snapshot_ = snapshot;
    has_pending_snapshot_ = true;
    // Every new observation owns validity; a lost target revokes the old point.
    has_calibration_observation_ = false;

    // Calibration must work before a response profile exists. Reading the
    // already-selected person point directly from the fresh Vision snapshot
    // avoids the circular dependency of first running the controller. The
    // selector generation is the same-target identity needed by the range
    // dummy probe; selected_observation_id itself is frame-local.
    const double observed_seconds = snapshot.capture_time_seconds > 0.0
        ? snapshot.capture_time_seconds
        : snapshot.ready_time_seconds;
    if (snapshot.frame_updated && snapshot.frame_id != 0 &&
        snapshot.selector_identity_protocol &&
        snapshot.selector_target_generation != 0 &&
        snapshot.selected_observation_id != 0 && snapshot.state.has_target &&
        std::isfinite(snapshot.state.target_x) &&
        std::isfinite(snapshot.state.target_y) &&
        std::isfinite(observed_seconds) && observed_seconds > 0.0) {
        last_calibration_observation_.frame_id = snapshot.frame_id;
        last_calibration_observation_.target_id =
            snapshot.selector_target_generation;
        last_calibration_observation_.target_generation =
            snapshot.selector_target_generation;
        last_calibration_observation_.observed_at_ns =
            static_cast<std::uint64_t>(observed_seconds * 1.0e9);
        last_calibration_observation_.person_x_px = snapshot.state.target_x;
        last_calibration_observation_.person_y_px = snapshot.state.target_y;
        last_calibration_observation_.fresh = true;
        has_calibration_observation_ = true;
    }
}

MouseControllerTickResult MouseControllerFacade::tick(
    const MouseControllerTickInput& input) {
    auto result = tick_aim(input);
    result.aim_counts = {result.actuation.dx, result.actuation.dy};
    result.aim_residual_x = actuator_adapter_.residual_x();
    result.aim_residual_y = actuator_adapter_.residual_y();
    result.recoil = recoil_.tick(input.now_seconds, input.right_button_down,
        input.left_button_down || result.auto_fire_active,
        valid(input.response_profile) && result.actuation.valid);
    // Positive relative mouse Y points down. Compose once, after aim judgment;
    // calibration's invalid profile disables this feed-forward completely.
    const auto room = static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max()) - result.actuation.dy;
    result.recoil.dy = static_cast<std::int32_t>(std::min<std::int64_t>(result.recoil.dy, room));
    result.actuation.dy += result.recoil.dy;
    if (result.recoil.active) result.transparent = false;
    if (result.controller_used) {
        controller_native::GamepadOutputState observed{};
        observed.right_x = result.final_u_x;
        observed.right_y = result.final_u_y - static_cast<float>(result.recoil.dy /
            (input.response_profile.counts_per_u_second_y * tuning_.breakaway * result.dt_seconds));
        observed.left_trigger = input.right_button_down ? 1.0f : 0.0f;
        observed.right_trigger = input.left_button_down || result.auto_fire_active ? 1.0f : 0.0f;
        // The shared camera observer sees the recoil contribution too, so it
        // cannot misclassify our own downward output as target motion.
        controller_.observe_composed_output(observed);
    }
    return result;
}

MouseControllerTickResult MouseControllerFacade::tick_aim(
    const MouseControllerTickInput& input) {
    MouseControllerTickResult result{};
    const bool restarting = !controller_active_ ||
        active_profile_generation_ != input.response_profile.generation;
    const float dt_seconds = restarting
        ? 0.001f : tick_dt(input.now_seconds, previous_tick_seconds_);
    result.dt_seconds = dt_seconds;
    previous_tick_seconds_ = input.now_seconds;
    auto response = input.response_profile;
    response.counts_per_u_second_x *= tuning_.breakaway;
    response.counts_per_u_second_y *= tuning_.breakaway;
    result.manual_input = rate_adapter_.adapt(
        input.source_counts, dt_seconds, response);

    if (!result.manual_input.valid || result.manual_input.transparent) {
        reset_controller_path();
        has_pending_snapshot_ = false;
        result.actuation = actuator_adapter_.passthrough(input.source_counts);
        result.transparent = true;
        return result;
    }

    if (restarting) {
        controller_.reset();
        actuator_adapter_.reset();
        controller_active_ = true;
        active_profile_generation_ = input.response_profile.generation;
        // A controller reset starts with its accepted 1 ms first tick. The
        // source packet belongs to that same first active tick.
        previous_tick_seconds_ = input.now_seconds;
    }

    controller_native::PhysicalGamepadState physical{};
    physical.connected = true;
    physical.right_x = result.manual_input.x;
    physical.right_y = result.manual_input.y;
    physical.left_trigger = input.right_button_down ? 1.0f : 0.0f;
    physical.right_trigger = input.left_button_down ? 1.0f : 0.0f;

    controller_clock_seconds_ = input.now_seconds;
    const std::uint64_t tick_id = input.tick_id != 0
        ? input.tick_id
        : next_tick_id_++;
    const auto& preparation = controller_.begin_tick(physical, tick_id);
    // Use the shared classifier's purpose so Vision and the controller agree
    // on acquire/correct/handover; physical counts are never a second selector.
    auto& intent = result.vision_intent;
    intent.intent_id = tick_id;
    intent.timestamp = common_native::TimeSeconds{input.now_seconds};
    intent.aiming = preparation.scope.assist_active;
    intent.purpose = preparation.intent.right_purpose;
    const auto filtered = preparation.intent.filtered_right;
    const float strength = std::hypot(filtered.x, filtered.y);
    if (intent.aiming && strength > 0.0f) {
        intent.strength = std::min(1.0f, strength);
        intent.valid = true;
        intent.has_direction = true;
        intent.direction = {filtered.x / strength, -filtered.y / strength};
    }

    if (has_pending_snapshot_) {
        controller_.submit_vision_snapshot(pending_snapshot_);
        has_pending_snapshot_ = false;
    }

    const controller_native::ControlFrame frame =
        controller_.resolve_control_frame();
    const auto& command = frame.pre_recoil_command();
    if (!frame.valid() || !command.available || !command.valid()) {
        reset_controller_path();
        result.actuation = actuator_adapter_.passthrough(input.source_counts);
        result.transparent = true;
        return result;
    }

    result.final_u_x = command.stick.x;
    result.final_u_y = command.stick.y;
    result.actuation = actuator_adapter_.adapt_with_native_axes(
        result.final_u_x,
        result.final_u_y,
        dt_seconds,
        response,
        input.source_counts,
        result.final_u_x == result.manual_input.x,
        result.final_u_y == result.manual_input.y);
    if (!result.actuation.valid || result.actuation.saturated) {
        reset_controller_path();
        result.actuation = actuator_adapter_.passthrough(input.source_counts);
        result.transparent = true;
        return result;
    }

    result.auto_fire_active = frame.fire_command().valid() &&
        frame.fire_command().synthetic_active;
    result.controller_used = true;
    result.transparent = false;

    return result;
}

void MouseControllerFacade::reset() {
    recoil_.reset();
    reset_controller_path();
    has_pending_snapshot_ = false;
    has_calibration_observation_ = false;
    next_tick_id_ = 1;
}

bool MouseControllerFacade::last_calibration_observation(
    MouseCalibrationObservation* observation) const noexcept {
    if (!has_calibration_observation_ || observation == nullptr) return false;
    *observation = last_calibration_observation_;
    return true;
}

const controller_native::NativeGamepadController&
MouseControllerFacade::controller() const noexcept {
    return controller_;
}

float MouseControllerFacade::bodylock_effective_range_px() const noexcept {
    const auto& plan=controller_.last_target_plan();
    return pipeline_contract::target_scaled_pickup_radius(
        std::max(controller_config_.ai_aim.ads_completion_radius_px,
            controller_config_.ai_aim.body_lock_activation_box_px),
        plan.lifecycle==pipeline_contract::TargetLifecycle::Observed ? plan.normalized_size:0);
}

float MouseControllerFacade::tick_dt(
    double now_seconds,
    double previous_seconds) noexcept {
    if (!std::isfinite(now_seconds) || now_seconds <= 0.0 ||
        !std::isfinite(previous_seconds) || previous_seconds <= 0.0 ||
        now_seconds <= previous_seconds) {
        return 0.001f;
    }
    return static_cast<float>(std::clamp(
        now_seconds - previous_seconds, 0.0001, 0.05));
}

void MouseControllerFacade::reset_controller_path() noexcept {
    if (controller_active_) controller_.reset();
    actuator_adapter_.reset();
    controller_active_ = false;
    active_profile_generation_ = 0;
    previous_tick_seconds_ = 0.0;
}

}  // namespace mouse_native
