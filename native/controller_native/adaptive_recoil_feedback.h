#pragma once

#include "runtime_config.h"

#include "pipeline_contract/target_plan.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace controller_native {

struct AdaptiveRecoilFeedbackInput {
    bool firing = false;
    bool aiming = false;
    bool target_valid = false;
    bool single_target = false;
    bool fresh_observation = false;
    bool cue_continuation = false;
    std::uint64_t target_id = 0;
    std::uint64_t source_frame_id = 0;
    float observed_error_y_px = 0.0f;
    float manual_y = 0.0f;
    pipeline_contract::TargetMotion target_motion =
        pipeline_contract::TargetMotion::Ambiguous;
    float default_amount = 0.0f;
    double now_seconds = 0.0;
};

struct AdaptiveRecoilFeedbackOutput {
    float amount = 0.0f;
    bool adaptive = false;
    bool updated = false;
};

// Recoil profile/fallback playback remains pure feed-forward. This controller-
// owned feedback layer only adjusts that baseline from de-duplicated, fresh
// target residuals while firing. It never creates a second stick output owner.
class AdaptiveRecoilFeedback {
public:
    explicit AdaptiveRecoilFeedback(GamepadRecoilConfig config = {})
        : config_(config) {}

    void reset() noexcept {
        active_target_id_ = 0;
        last_source_frame_id_ = 0;
        last_observation_seconds_ = 0.0;
        last_error_y_px_ = 0.0f;
        filtered_error_rate_y_px_per_sec_ = 0.0f;
        amount_ = 0.0f;
        has_observation_ = false;
    }

    AdaptiveRecoilFeedbackOutput update(
        const AdaptiveRecoilFeedbackInput& input) noexcept {
        AdaptiveRecoilFeedbackOutput output;
        const float base = std::max(0.0f, input.default_amount);
        output.amount = base;
        if (!config_.adaptive_feedback_enabled ||
            config_.profile_playback_enabled) {
            reset();
            return output;
        }

        if (!input.firing || !input.aiming) {
            reset();
            return output;
        }

        const float minimum = std::max(0.0f, config_.adaptive_min_amount);
        const float maximum = std::max(minimum, config_.adaptive_max_amount);
        if (!input.target_valid || !input.single_target || input.target_id == 0) {
            reset();
            output.amount = std::clamp(base, minimum, maximum);
            return output;
        }

        if (active_target_id_ != input.target_id) {
            reset();
            active_target_id_ = input.target_id;
            amount_ = std::clamp(base, minimum, maximum);
        }
        output.adaptive = true;

        const bool new_observation = input.fresh_observation &&
            !input.cue_continuation && input.source_frame_id != 0 &&
            input.source_frame_id != last_source_frame_id_;
        if (new_observation) {
            const bool vertical_target_maneuver =
                input.target_motion == pipeline_contract::TargetMotion::Jump ||
                input.target_motion == pipeline_contract::TargetMotion::Fall;
            const bool manual_ambiguous =
                std::fabs(input.manual_y) >= 0.06f;
            const bool near_lock =
                std::fabs(input.observed_error_y_px) <= 32.0f;
            const double elapsed = has_observation_
                ? input.now_seconds - last_observation_seconds_ : 0.0;
            const bool usable_pair = has_observation_ && elapsed >= 0.002 &&
                elapsed <= 0.050 && !vertical_target_maneuver &&
                !manual_ambiguous && near_lock;
            if (usable_pair) {
                const float dt = static_cast<float>(elapsed);
                const float error_rate =
                    (input.observed_error_y_px - last_error_y_px_) / dt;
                const float rate_alpha = dt / (0.030f + dt);
                filtered_error_rate_y_px_per_sec_ += rate_alpha *
                    (error_rate - filtered_error_rate_y_px_per_sec_);
                const float rate_drive = std::clamp(
                    filtered_error_rate_y_px_per_sec_ / 250.0f, -1.0f, 1.0f);
                const float position_drive = std::clamp(
                    input.observed_error_y_px / 24.0f, -1.0f, 1.0f);
                const float drive = std::clamp(
                    rate_drive * 0.75f + position_drive * 0.25f,
                    -1.0f, 1.0f);
                const float desired = std::clamp(
                    base + drive * 0.16f, minimum, maximum);
                const float amount_alpha = dt / (0.025f + dt);
                amount_ += amount_alpha * (desired - amount_);
                amount_ = std::clamp(amount_, minimum, maximum);
                output.updated = true;
            }

            last_source_frame_id_ = input.source_frame_id;
            last_observation_seconds_ = input.now_seconds;
            last_error_y_px_ = input.observed_error_y_px;
            has_observation_ = true;
        } else if (has_observation_ &&
                   input.now_seconds - last_observation_seconds_ > 0.075) {
            // Long blind intervals cannot identify recoil from target motion.
            // Fail back to the configured feed-forward amount instead of
            // preserving a stale learned correction.
            amount_ = std::clamp(base, minimum, maximum);
            filtered_error_rate_y_px_per_sec_ = 0.0f;
            has_observation_ = false;
        }

        output.amount = std::clamp(amount_, minimum, maximum);
        return output;
    }

private:
    GamepadRecoilConfig config_{};
    std::uint64_t active_target_id_ = 0;
    std::uint64_t last_source_frame_id_ = 0;
    double last_observation_seconds_ = 0.0;
    float last_error_y_px_ = 0.0f;
    float filtered_error_rate_y_px_per_sec_ = 0.0f;
    float amount_ = 0.0f;
    bool has_observation_ = false;
};

}  // namespace controller_native
