#include "assist_control_state_machine.h"

namespace controller_native {

pipeline_contract::Vec2f AssistControlStateMachine::cooperative_output(
    const AssistControlStateMachineInput& input,
    pipeline_contract::Vec2f manual,
    pipeline_contract::Vec2f intent_manual,
    pipeline_contract::Vec2f ai,
    AssistControlStateMachineOutput* decision) const noexcept {
    const float material = std::max(
        0.0f, config_.material_ai_axis_output);
    // Mouse ingress already owns physical-speed escape. Only an explicit
    // per-axis conflict may reduce ordinary native input after target
    // admission. Shared gamepad arbitration remains below, unchanged.
    if (config_.direct_mouse_manual &&
        (input.mode == pipeline_contract::ControlMode::BodyLockFollow ||
         input.mode == pipeline_contract::ControlMode::AdsAcquire)) {
        const bool ads = input.mode == pipeline_contract::ControlMode::AdsAcquire;
        const bool manual_safe = ads &&
            (input.ads_acquisition_state == pipeline_contract::AdsAcquisitionState::AcquiringManualSafe ||
             input.ads_acquisition_state == pipeline_contract::AdsAcquisitionState::AcquiringExtended);
        const auto choose_axis = [&](float original, float target, float error, bool vertical) {
            if (!ads && config_.mouse_bodylock_deadzone > 0.0f &&
                std::fabs(original) <= config_.mouse_bodylock_deadzone &&
                (vertical ? input.filtered_manual_stick.y : input.filtered_manual_stick.x) == 0.0f) {
                // The same BodyLock envelope owns intent AND actuation:
                // noise cannot edit D upstream or reappear as raw M here.
                // Lifecycle/handover passthrough returned before this path.
                auto quiet = judge_mouse_manual_axis(0, target, error,
                    input.visual_authority, config_.bodylock_manual_weight,
                    std::max(1.0f, config_.capture_settle_radius_px),
                    config_.mouse_response_px_per_second, material);
                if (original != 0) {
                    quiet.retention = 0;
                    quiet.conflict = MouseManualConflict::Deadzone;
                }
                return quiet;
            }
            const bool correction = vertical ? input.manual_correction_y : input.manual_correction_x;
            const bool protected_recoil = ads && vertical && input.firing &&
                original < 0 && input.filtered_manual_stick.y < -material;
            const bool safe_manual = manual_safe && std::fabs(original) > material &&
                (std::fabs(target) <= material || original * target < 0);
            if (correction || protected_recoil || safe_manual) {
                MouseManualAxisDecision kept;
                kept.retained_manual = kept.final = original;
                return kept;
            }
            return judge_mouse_manual_axis(original, target, error,
                input.visual_authority,
                manual_safe ? 1.0f : config_.bodylock_manual_weight,
                std::max(1.0f, config_.capture_settle_radius_px),
                config_.mouse_response_px_per_second, material);
        };
        decision->mouse_x = choose_axis(manual.x, ai.x, input.target_error_px.x, false);
        decision->mouse_y = choose_axis(manual.y, ai.y, -input.target_error_px.y, true);
        return finite_or_zero({decision->mouse_x.final, decision->mouse_y.final});
    }
    const float target_settle_radius = std::max(
        1.0f, config_.capture_settle_radius_px);

    const auto smoothstep = [](float value) noexcept {
        const float t = std::clamp(value, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    };

    // A small axis inside a strongly dominant orthogonal gesture is usually
    // stick coupling, not an independent request. The broad smooth band is
    // intentional: geometry must not create a replacement hard threshold.
    const auto coupling_weight = [&smoothstep](
        float axis,
        float orthogonal_axis) noexcept {
        constexpr float kRatioBegin = 1.25f;
        constexpr float kRatioFull = 3.0f;
        constexpr float kRatioDenominatorFloor = 0.02f;
        const float ratio = std::fabs(orthogonal_axis) /
            std::max(std::fabs(axis), kRatioDenominatorFloor);
        return smoothstep(
            (ratio - kRatioBegin) / (kRatioFull - kRatioBegin));
    };

    // A deliberate single-axis pull must eventually hand back to AI when
    // physically released. The handback follows bias-centered motion, so
    // it is continuous across the filtered deadzone without treating a
    // calibrated static offset as a held gesture.
    const auto release_weight = [&smoothstep](float axis) noexcept {
        constexpr float kCenteredIntentReleaseBand = 0.02f;
        return 1.0f - smoothstep(
            std::fabs(axis) / kCenteredIntentReleaseBand);
    };

    const auto solve_axis = [
        this, &input, material, target_settle_radius, &smoothstep](
        float native_axis,
        float intent_axis,
        float orthogonal_intent_axis,
        float desired_axis,
        bool vertical,
        const auto& coupling,
        const auto& release) noexcept {
        const float manual_weight = config_.use_gamepad_intent_for_arbitration
            ? std::clamp(vertical ? input.manual_axis_activity.y : input.manual_axis_activity.x, 0.0f, 1.0f)
            : 1.0f;
        const auto blend_manual = [desired_axis, manual_weight](float proposed) {
            return desired_axis + (proposed - desired_axis) * manual_weight;
        };
        // Down-stick while firing is a separate recoil/user authority. It
        // must remain independent of target positioning on Y; ADS may
        // still own X and every non-downward axis in the same tick.
        if (input.mode == pipeline_contract::ControlMode::AdsAcquire &&
            vertical && input.firing && intent_axis < 0.0f &&
            input.filtered_manual_stick.y < -material) {
            return blend_manual(native_axis);
        }
        // ADS is the positioning owner after target admission. The target
        // controller already publishes the desired total T, so blending a
        // gesture that predates ownership back into T can stop or reverse
        // the snap. An explicit exit is handled before this solve; absent
        // that event, both moving and braking axes follow the target.
        const bool intentional_d = vertical
            ? input.manual_correction_y
            : input.manual_correction_x;
        if (input.mode == pipeline_contract::ControlMode::AdsAcquire) {
            const bool manual_safe_phase =
                input.ads_acquisition_state ==
                    pipeline_contract::AdsAcquisitionState::AcquiringExtended ||
                input.ads_acquisition_state ==
                    pipeline_contract::AdsAcquisitionState::AcquiringManualSafe;
            if (manual_safe_phase) {
                // The nominal target-first interval is over. Keep AI work
                // when the user is neutral or aligned, but a material
                // opposing proposal now wins immediately on that axis.
                if (std::fabs(intent_axis) > material &&
                    (!std::isfinite(desired_axis) ||
                     std::fabs(desired_axis) <= material ||
                     intent_axis * desired_axis < 0.0f)) {
                    return blend_manual(native_axis);
                }
            } else {
                if (std::isfinite(desired_axis) &&
                    std::fabs(desired_axis) > material) {
                    return std::clamp(desired_axis, -1.0f, 1.0f);
                }
                // An owned D correction is a semantic target edit, not
                // stale carry-in. If the target solver has no material
                // work on that axis yet, let the correction move D.
                return intentional_d ? native_axis * manual_weight : 0.0f;
            }
        }
        if (!std::isfinite(desired_axis)) {
            return native_axis;
        }
        if (std::fabs(desired_axis) <= material) {
            // Only the solver can request a position hold. Shaping may cross
            // zero while a moving target still requires nonzero work; retain
            // its existing arbitration rather than reinterpret that transient
            // as arrival. At D, an actual zero request remains an owned T.
            if (!config_.use_gamepad_intent_for_arbitration) return native_axis;
            if (!(vertical ? input.solver_hold_y : input.solver_hold_x)) return native_axis;
            return native_axis * manual_weight;
        }

        if (input.mode == pipeline_contract::ControlMode::BodyLockFollow &&
            input.carried_acquisition_gesture &&
            std::fabs(intent_axis) > material &&
            intent_axis * desired_axis < 0.0f) {
            const float activity = std::clamp(vertical
                ? input.manual_axis_activity.y : input.manual_axis_activity.x,
                0.0f, 1.0f);
            // Preserve a real held acquisition gesture. A released/noisy
            // component cannot veto the entire target proposal, and its
            // protection fades continuously before filtered-neutral.
            return std::clamp(desired_axis +
                (native_axis - desired_axis) * activity, -1.0f, 1.0f);
        }

        const bool compatible = intent_axis * desired_axis > 0.0f;
        if (compatible) {
            // AI is the desired total T, not another stick to add on top
            // of M. Correct manual contribution therefore reduces the
            // missing work; a stronger native request remains untouched.
            if (native_axis * desired_axis > 0.0f &&
                std::fabs(native_axis) >= std::fabs(desired_axis)) {
                return blend_manual(native_axis);
            }
            return std::clamp(desired_axis, -1.0f, 1.0f);
        }

        // Explicit opposing input keeps the existing bounded escape
        // authority. A minor component of a dominant 2-D positioning
        // gesture may instead yield continuously to the target proposal.
        // ADS has full authority for that coupled component; BodyLock earns
        // it only from clear current evidence.
        constexpr float kOrdinaryOpposingDamping = 0.35f;
        constexpr float kDownwardOpposingDamping = 0.10f;
        const bool downward = vertical && intent_axis < 0.0f;
        float damping_ratio = downward
            ? kDownwardOpposingDamping
            : kOrdinaryOpposingDamping;
        if (downward && input.firing) damping_ratio = 0.0f;
        const float evidence =
            input.mode == pipeline_contract::ControlMode::AdsAcquire
            ? 1.0f
            : std::clamp(
                (input.visual_authority - 0.65f) / 0.35f,
                0.0f,
                1.0f);
        // Adaptive input is bias-centered; gamepad input is unmodified raw.
        // Its continuous weight below removes neutral intent from arbitration
        // without filtering the physical passthrough or weighting it twice.
        const float native_magnitude = std::fabs(intent_axis);
        const float damping = std::min(
            native_magnitude * damping_ratio * evidence,
            std::fabs(desired_axis));
        const float retained = std::max(0.0f, native_magnitude - damping);
        const float retained_axis = native_magnitude > 0.0f
            ? std::copysign(retained, intent_axis)
            : 0.0f;

        // Down-stick remains a protected vertical request. It still hands
        // back smoothly near physical release, preventing the old
        // filtered-zero cliff without spending recoil/downward authority.
        const float coupled = downward
            ? 0.0f
            : coupling(intent_axis, orthogonal_intent_axis) * evidence;
        // Correction is a semantic D edit, not an override of actuator weight.
        // One continuous activity controls both owned and carried gestures.
        const float released = config_.use_gamepad_intent_for_arbitration
            ? 1.0f - manual_weight
            : release(intent_axis);
        const float target_weight = released + (1.0f - released) * coupled;
        const float cooperative = std::clamp(
            retained_axis +
                (desired_axis - retained_axis) * target_weight,
            -1.0f,
            1.0f);
        // Once a fresh BodyLock observation says the target lies in the
        // AI proposal's direction, retaining old opposing manual work is
        // a response delay, not cooperation. D corrections and explicit
        // exits have already been interpreted upstream. Use the existing
        // settle radius as a continuous position-ownership envelope so
        // the target takes over quickly outside center without creating a
        // new distance/magnitude gate or strengthening cue-only control.
        const float control_error = vertical
            ? -input.target_error_px.y
            : input.target_error_px.x;
        const bool fresh_position_owner = input.fresh_observation &&
            !input.cue_continuation && !intentional_d &&
            std::isfinite(control_error) &&
            desired_axis * control_error > 0.0f;
        if (!fresh_position_owner) return cooperative;
        const float position_weight = smoothstep(
            std::fabs(control_error) / target_settle_radius) * evidence;
        return std::clamp(
            cooperative +
                (desired_axis - cooperative) * position_weight,
            -1.0f,
            1.0f);
    };

    return finite_or_zero({
        solve_axis(
            manual.x, intent_manual.x, intent_manual.y, ai.x, false,
            coupling_weight, release_weight),
        solve_axis(
            manual.y, intent_manual.y, intent_manual.x, ai.y, true,
            coupling_weight, release_weight),
    });
}

}  // namespace controller_native
