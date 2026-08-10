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
    // A decisive stick motion is selection intent, not a weak manual/AI mix.
    float handover_flick_threshold = 0.55f;
    // If the current target is already near center, a decisive flick may seek
    // another visible candidate even when both lie in a similar direction.
    float handover_center_radius_px = 24.0f;
    // Away/orthogonal input below this alignment may leave the current target.
    float handover_current_alignment_max = 0.25f;
    float material_ai_axis_output = 1.0e-4f;
    float material_target_error_axis_px = 1.0f;
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
    std::uint32_t credible_candidate_count = 0;
    double now_seconds = 0.0;
    pipeline_contract::Vec2f target_error_px{};
    // Physical stick uses gamepad coordinates: positive Y is down-stick.
    pipeline_contract::Vec2f manual_stick{};
    // AI-only output is used to decide whether an axis earned authority.
    pipeline_contract::Vec2f ai_stick{};
    // Target-guided output may include a bounded helpful/manual correction.
    pipeline_contract::Vec2f target_stick{};
};

struct AssistControlStateMachineOutput {
    pipeline_contract::Vec2f stick{};
    AssistControlPhase phase = AssistControlPhase::Manual;
    bool handover_requested = false;
    bool target_changed = false;
    bool handover_braking = false;
    bool manual_passthrough_x = false;
    bool manual_passthrough_y = false;
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
            // layer: the target proposal goes straight to Track, whose
            // per-axis rule already preserves manual wherever AI is idle.
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
            const auto ai = finite_or_zero(input.ai_stick);
            output.stick.x = ai_axis_has_work(
                ai.x, input.target_error_px.x)
                ? ai.x : 0.0f;
            output.stick.y = ai_axis_has_work(
                ai.y, -input.target_error_px.y)
                ? ai.y : 0.0f;
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
            is_handover_request(input)) {
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
        const auto target = finite_or_zero(input.target_stick);
        output.manual_passthrough_x = !ai_axis_has_work(
            ai.x, input.target_error_px.x);
        output.manual_passthrough_y = !ai_axis_has_work(
            ai.y, -input.target_error_px.y);
        output.stick.x = output.manual_passthrough_x ? manual.x : target.x;
        output.stick.y = output.manual_passthrough_y ? manual.y : target.y;
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

    bool ai_axis_has_work(float ai, float target_error) const noexcept {
        return std::isfinite(ai) && std::isfinite(target_error) &&
            std::fabs(ai) > std::max(
                0.0f, config_.material_ai_axis_output) &&
            std::fabs(target_error) > std::max(
                0.0f, config_.material_target_error_axis_px);
    }

    bool is_handover_request(
        const AssistControlStateMachineInput& input) const noexcept {
        if (input.cue_continuation || input.credible_candidate_count < 2) {
            return false;
        }
        const float manual_magnitude = std::hypot(
            input.manual_stick.x, input.manual_stick.y);
        if (!std::isfinite(manual_magnitude) ||
            manual_magnitude < config_.handover_flick_threshold) {
            return false;
        }

        const float error_magnitude = std::hypot(
            input.target_error_px.x, input.target_error_px.y);
        if (!std::isfinite(error_magnitude)) return false;
        if (error_magnitude <= config_.handover_center_radius_px) return true;
        if (error_magnitude <= 0.001f || manual_magnitude <= 0.001f) {
            return false;
        }

        // Target error uses screen Y (down positive), while this runtime's
        // physical right-stick Y is inverted when converted to screen intent.
        const float alignment =
            ((input.target_error_px.x / error_magnitude) *
             (input.manual_stick.x / manual_magnitude)) +
            ((input.target_error_px.y / error_magnitude) *
             (-input.manual_stick.y / manual_magnitude));
        return alignment <= config_.handover_current_alignment_max;
    }

    AssistControlStateMachineConfig config_{};
    AssistControlPhase phase_ = AssistControlPhase::Manual;
    std::uint64_t active_target_id_ = 0;
    std::uint64_t active_selector_generation_ = 0;
    double capture_started_seconds_ = 0.0;
    std::uint32_t capture_settled_fresh_frames_ = 0;
};

}  // namespace controller_native
