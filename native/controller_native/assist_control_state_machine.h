#pragma once

#include "pipeline_contract/target_plan.h"

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
};

struct AssistControlStateMachineInput {
    bool aiming = false;
    bool target_authoritative = false;
    bool fresh_observation = false;
    bool cue_continuation = false;
    std::uint64_t target_id = 0;
    std::uint64_t selector_target_generation = 0;
    double now_seconds = 0.0;
    pipeline_contract::Vec2f target_error_px{};
    pipeline_contract::ControlMode mode = pipeline_contract::ControlMode::Manual;
    float visual_authority = 0.0f;
    bool firing = false;
    // Physical stick uses XInput coordinates: positive Y is up-stick.
    pipeline_contract::Vec2f manual_stick{};
    // Filtered manual remains the noise-owned signal used by upstream intent
    // and desired-point interpretation. Final arbitration deliberately uses
    // the physical 2-D gesture: a per-axis filter transition must not change
    // authority while the raw gesture is still present.
    pipeline_contract::Vec2f filtered_manual_stick{};
    // AI-only output is the desired total proposal for the per-axis T-M solve.
    pipeline_contract::Vec2f ai_stick{};
    // TargetCoordinator has already interpreted these axes as a correction of
    // D inside the valid R. They are not generic raw-manual passthrough.
    bool manual_correction_x = false;
    bool manual_correction_y = false;
    bool manual_exit_requested = false;
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

        if (!input.aiming) {
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
            const auto ai = finite_or_zero(input.ai_stick);
            output.manual_correction_x = input.manual_correction_x;
            output.manual_correction_y = input.manual_correction_y;
            output.stick = cooperative_output(input, manual, ai);
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
        const auto ai = finite_or_zero(input.ai_stick);
        // Position error may be near zero while target-relative motion still
        // requires feed-forward. Keep that shaped proposal authoritative until
        // it becomes materially idle instead of releasing on a 1 px crossing.
        output.manual_correction_x = input.manual_correction_x;
        output.manual_correction_y = input.manual_correction_y;
        // M is always the native baseline. A is a desired total proposal, so
        // each axis fills only what is missing. Axis-local arbitration is
        // intentional: a helpful horizontal correction may not spend the
        // user's downward recoil authority on the vertical axis.
        output.stick = cooperative_output(input, manual, ai);
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
        pipeline_contract::Vec2f ai) const noexcept {
        const float material = std::max(
            0.0f, config_.material_ai_axis_output);

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
        // physically released, but that handback is continuous and follows raw
        // magnitude rather than the filtered deadzone crossing.
        const auto release_weight = [&smoothstep](float axis) noexcept {
            constexpr float kRawReleaseBand = 0.02f;
            return 1.0f - smoothstep(std::fabs(axis) / kRawReleaseBand);
        };

        const auto solve_axis = [&input, material](
            float native_axis,
            float orthogonal_native_axis,
            float desired_axis,
            bool vertical,
            const auto& coupling,
            const auto& release) noexcept {
            if (!std::isfinite(desired_axis) ||
                std::fabs(desired_axis) <= material) {
                return native_axis;
            }

            const bool compatible = native_axis * desired_axis > 0.0f;
            if (compatible) {
                // AI is the desired total T, not another stick to add on top
                // of M. Correct manual contribution therefore reduces the
                // missing work; a stronger native request remains untouched.
                if (native_axis * desired_axis > 0.0f &&
                    std::fabs(native_axis) >= std::fabs(desired_axis)) {
                    return native_axis;
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
            const bool downward = vertical && native_axis < 0.0f;
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
            const float native_magnitude = std::fabs(native_axis);
            const float damping = std::min(
                native_magnitude * damping_ratio * evidence,
                std::fabs(desired_axis));
            const float retained = std::max(0.0f, native_magnitude - damping);
            const float retained_axis = native_magnitude > 0.0f
                ? std::copysign(retained, native_axis)
                : 0.0f;

            // Down-stick remains a protected vertical request. It still hands
            // back smoothly near physical release, preventing the old
            // filtered-zero cliff without spending recoil/downward authority.
            const float coupled = downward
                ? 0.0f
                : coupling(native_axis, orthogonal_native_axis) * evidence;
            const float released = release(native_axis);
            const float target_weight = released + (1.0f - released) * coupled;
            return std::clamp(
                retained_axis +
                    (desired_axis - retained_axis) * target_weight,
                -1.0f,
                1.0f);
        };

        return finite_or_zero({
            solve_axis(
                manual.x, manual.y, ai.x, false,
                coupling_weight, release_weight),
            solve_axis(
                manual.y, manual.x, ai.y, true,
                coupling_weight, release_weight),
        });
    }

    AssistControlStateMachineConfig config_{};
    AssistControlPhase phase_ = AssistControlPhase::Manual;
    std::uint64_t active_target_id_ = 0;
    std::uint64_t active_selector_generation_ = 0;
    double capture_started_seconds_ = 0.0;
    std::uint32_t capture_settled_fresh_frames_ = 0;
};

}  // namespace controller_native
