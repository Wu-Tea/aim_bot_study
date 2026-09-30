#pragma once

#include "pipeline_contract/target_plan.h"
#include "pipeline_contract/assist_activation.h"
#include "mouse_manual_judgment.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace controller_native {

enum class AssistControlPhase : unsigned char {
    Manual,
    Capture,
    Track,
    HandoverSeek,
};

inline const char* assist_control_phase_name(AssistControlPhase phase) noexcept {
    switch (phase) {
    case AssistControlPhase::Manual: return "manual";
    case AssistControlPhase::Capture: return "capture";
    case AssistControlPhase::Track: return "track";
    case AssistControlPhase::HandoverSeek: return "handover_seek";
    }
    return "manual";
}

struct AssistControlStateMachineConfig {
    float material_ai_axis_output = 1.0e-4f;
    float capture_settle_radius_px = 10.0f;
    std::uint32_t capture_settle_fresh_frames = 2;
    float capture_timeout_ms = 135.0f;
    float bodylock_manual_weight = 1.0f;
    bool direct_mouse_manual = false;
    // Production gamepad intent and final arbitration consume the same
    // AI-only continuous activity; raw manual_stick remains the passthrough owner.
    bool use_gamepad_intent_for_arbitration = false;
    float mouse_response_px_per_second = 500.0f;
    float mouse_bodylock_deadzone = 0.0f;
};

struct AssistControlStateMachineInput {
    pipeline_contract::AssistActivation activation = pipeline_contract::AssistActivation::Off;
    bool target_authoritative = false;
    bool fresh_observation = false;
    bool cue_continuation = false;
    std::uint64_t target_id = 0;
    std::uint64_t selector_target_generation = 0;
    double now_seconds = 0.0;
    pipeline_contract::Vec2f target_error_px{};
    pipeline_contract::ControlMode mode = pipeline_contract::ControlMode::Manual;
    pipeline_contract::AdsAcquisitionState ads_acquisition_state =
        pipeline_contract::AdsAcquisitionState::Idle;
    float visual_authority = 0.0f;
    bool firing = false;
    // Physical stick uses XInput coordinates: positive Y is up-stick.
    pipeline_contract::Vec2f manual_stick{};
    // Bias-centered raw stick belongs to the adaptive mouse/legacy policy.
    // Production gamepad arbitration blends raw proposals by continuous activity;
    // filtered_manual_stick owns desired-point edits, never raw passthrough.
    // Older focused fixtures may omit it and retain the physical fallback.
    pipeline_contract::Vec2f centered_manual_stick{};
    bool centered_manual_available = false;
    // Shared gamepad signal for purpose, desired-point/lifecycle interpretation
    // and final AI arbitration. Continuous activity below handles carried
    // gesture release without adding another input deadzone.
    pipeline_contract::Vec2f filtered_manual_stick{};
    // AI-only output is the desired total proposal for the per-axis T-M solve.
    pipeline_contract::Vec2f ai_stick{};
    // TargetCoordinator has already interpreted these axes as a correction of
    // D inside the valid R. They are not generic raw-manual passthrough.
    bool manual_correction_x = false;
    bool manual_correction_y = false;
    bool manual_exit_requested = false;
    // A material gesture that began before ADS target ownership must not be
    // reinterpreted as permission for BodyLock to reverse the player's stick
    // after a real ADS handoff.
    bool carried_acquisition_gesture = false;
    // Produced by IntentFilter against each axis's AI intent threshold.
    // Explicit activity is required: a 2-D purpose alone is not a held axis.
    pipeline_contract::Vec2f manual_axis_activity{};
};

struct AssistControlStateMachineOutput {
    pipeline_contract::Vec2f stick{};
    AssistControlPhase phase = AssistControlPhase::Manual;
    bool handover_requested = false;
    bool target_changed = false;
    bool handover_braking = false;
    bool manual_passthrough_x = false;
    bool manual_passthrough_y = false;
    bool manual_correction_x = false;
    bool manual_correction_y = false;
    MouseManualAxisDecision mouse_x{}, mouse_y{};
};

class AssistControlStateMachine {
public:
    explicit AssistControlStateMachine(
        AssistControlStateMachineConfig config = {}) noexcept
        : config_(config) {}

    void reset() noexcept {
        phase_ = AssistControlPhase::Manual;
        active_target_id_ = 0;
        active_selector_generation_ = 0;
        capture_started_seconds_ = 0.0;
        capture_settled_fresh_frames_ = 0;
    }

    AssistControlStateMachineOutput update(
        const AssistControlStateMachineInput& input) noexcept {
        AssistControlStateMachineOutput output;
        output.phase = phase_;

        if (!pipeline_contract::permits_assist(input.activation)) {
            reset();
            output.stick = finite_or_zero(input.manual_stick);
            output.phase = phase_;
            output.manual_passthrough_x = true;
            output.manual_passthrough_y = true;
            return output;
        }

        const bool has_new_target = input.target_authoritative &&
            input.target_id != 0 &&
            (active_target_id_ == 0 || input.target_id != active_target_id_ ||
             (input.selector_target_generation != 0 &&
              active_selector_generation_ != 0 &&
              input.selector_target_generation !=
                  active_selector_generation_));
        output.target_changed = has_new_target;

        if (phase_ == AssistControlPhase::HandoverSeek) {
            if (has_new_target) {
                enter_capture(input);
            } else {
                // Old-target authority is intentionally ignored while Vision
                // looks for the alternative. Do not time out back into a snap
                // toward A during the same held ADS epoch.
                output.stick = finite_or_zero(input.manual_stick);
                output.phase = phase_;
                output.handover_requested = true;
                output.manual_passthrough_x = true;
                output.manual_passthrough_y = true;
                return output;
            }
        } else if (!input.target_authoritative || input.target_id == 0) {
            phase_ = AssistControlPhase::Manual;
            active_target_id_ = 0;
            active_selector_generation_ = 0;
            capture_started_seconds_ = 0.0;
            capture_settled_fresh_frames_ = 0;
            output.stick = finite_or_zero(input.manual_stick);
            output.phase = phase_;
            output.manual_passthrough_x = true;
            output.manual_passthrough_y = true;
            return output;
        } else if (has_new_target) {
            // Ordinary acquisition/replacement needs no extra ownership
            // layer: the target proposal goes straight to Track, whose one
            // 2-D solve preserves the native vector and fills cooperative work.
            phase_ = AssistControlPhase::Track;
            remember_target(input);
        }

        if (phase_ == AssistControlPhase::Manual) {
            phase_ = AssistControlPhase::Track;
            remember_target(input);
        }

        if (phase_ == AssistControlPhase::Capture) {
            // Cue continuation is an uncertain visual bridge, not authority
            // to pin the user's stick during capture.  Let the bounded cue
            // proposal participate under the normal Track rules.
            if (input.cue_continuation) {
                phase_ = AssistControlPhase::Track;
            }
        }

        if (phase_ == AssistControlPhase::Capture) {
            const auto manual = finite_or_zero(input.manual_stick);
            const auto intent_manual = config_.use_gamepad_intent_for_arbitration
                ? manual
                : input.centered_manual_available
                    ? finite_or_zero(input.centered_manual_stick)
                    : manual;
            const auto ai = finite_or_zero(input.ai_stick);
            output.manual_correction_x = input.manual_correction_x;
            output.manual_correction_y = input.manual_correction_y;
            output.stick = cooperative_output(
                input, manual, intent_manual, ai, &output);
            output.manual_passthrough_x =
                std::fabs(output.stick.x - manual.x) <= 1.0e-6f;
            output.manual_passthrough_y =
                std::fabs(output.stick.y - manual.y) <= 1.0e-6f;
            output.handover_braking = true;
            output.phase = phase_;

            const float error = std::hypot(
                input.target_error_px.x, input.target_error_px.y);
            const bool settled_sample = input.fresh_observation &&
                std::isfinite(error) &&
                error <= std::max(1.0f, config_.capture_settle_radius_px);
            if (settled_sample) {
                ++capture_settled_fresh_frames_;
            } else if (input.fresh_observation) {
                capture_settled_fresh_frames_ = 0;
            }

            const float elapsed_ms = capture_started_seconds_ > 0.0 &&
                    input.now_seconds >= capture_started_seconds_
                ? static_cast<float>(
                    (input.now_seconds - capture_started_seconds_) * 1000.0)
                : 0.0f;
            const bool settled = capture_settled_fresh_frames_ >=
                std::max<std::uint32_t>(
                    1, config_.capture_settle_fresh_frames);
            const bool expired = config_.capture_timeout_ms > 0.0f &&
                elapsed_ms >= config_.capture_timeout_ms;
            if (settled || expired) {
                // Keep this tick as the final braking sample. Micro input is
                // restored on the next 1 kHz tick in Track.
                phase_ = AssistControlPhase::Track;
            }
            remember_target(input);
            return output;
        }

        if (phase_ == AssistControlPhase::Track &&
            input.manual_exit_requested) {
            phase_ = AssistControlPhase::HandoverSeek;
            output.stick = finite_or_zero(input.manual_stick);
            output.phase = phase_;
            output.handover_requested = true;
            output.manual_passthrough_x = true;
            output.manual_passthrough_y = true;
            return output;
        }

        const auto manual = finite_or_zero(input.manual_stick);
        const auto intent_manual = config_.use_gamepad_intent_for_arbitration
            ? manual
            : input.centered_manual_available
                ? finite_or_zero(input.centered_manual_stick)
                : manual;
        const auto ai = finite_or_zero(input.ai_stick);
        // Position error may be near zero while target-relative motion still
        // requires feed-forward. Keep that shaped proposal authoritative until
        // it becomes materially idle instead of releasing on a 1 px crossing.
        output.manual_correction_x = input.manual_correction_x;
        output.manual_correction_y = input.manual_correction_y;
        // Physical M remains passthrough when there is no material target
        // proposal. While a target proposal exists, the shared gamepad intent
        // (or adaptive policy's bias-centered M) supplies arbitration evidence;
        // an AI-neutral offset is not a protected output contribution.
        // Axis-local arbitration is intentional: a helpful horizontal
        // correction may not spend downward/recoil authority on Y.
        output.stick = cooperative_output(
            input, manual, intent_manual, ai, &output);
        output.manual_passthrough_x =
            std::fabs(output.stick.x - manual.x) <= 1.0e-6f;
        output.manual_passthrough_y =
            std::fabs(output.stick.y - manual.y) <= 1.0e-6f;
        output.phase = phase_;
        remember_target(input);
        return output;
    }

    AssistControlPhase phase() const noexcept { return phase_; }

private:
    static pipeline_contract::Vec2f finite_or_zero(
        pipeline_contract::Vec2f value) noexcept {
        return {
            std::isfinite(value.x) ? std::clamp(value.x, -1.0f, 1.0f) : 0.0f,
            std::isfinite(value.y) ? std::clamp(value.y, -1.0f, 1.0f) : 0.0f,
        };
    }

    void remember_target(const AssistControlStateMachineInput& input) noexcept {
        if (input.target_id != 0) active_target_id_ = input.target_id;
        if (input.selector_target_generation != 0) {
            active_selector_generation_ = input.selector_target_generation;
        }
    }

    void enter_capture(const AssistControlStateMachineInput& input) noexcept {
        phase_ = AssistControlPhase::Capture;
        capture_started_seconds_ = input.now_seconds;
        capture_settled_fresh_frames_ = 0;
        remember_target(input);
    }

    pipeline_contract::Vec2f cooperative_output(
        const AssistControlStateMachineInput& input,
        pipeline_contract::Vec2f manual,
        pipeline_contract::Vec2f intent_manual,
        pipeline_contract::Vec2f ai,
        AssistControlStateMachineOutput* decision) const noexcept;

    AssistControlStateMachineConfig config_{};
    AssistControlPhase phase_ = AssistControlPhase::Manual;
    std::uint64_t active_target_id_ = 0;
    std::uint64_t active_selector_generation_ = 0;
    double capture_started_seconds_ = 0.0;
    std::uint32_t capture_settled_fresh_frames_ = 0;
};

}  // namespace controller_native
