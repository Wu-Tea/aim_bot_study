#pragma once

#include "sustained_aimlab_simulator.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>

namespace controller_native::pid_benchmark {

// Benchmark-only baseline.  It deliberately has no knowledge of manual intent,
// gestures, controller modes, or arbitration policy.  Manual input is added only
// after the PID has produced an independent assist vector.
struct PidBenchmarkConfig {
    // Held-out paired AimLab selection: a proportional baseline beat the
    // searched I/D variants on the accuracy-versus-wrong-way trade-off.
    double kp = 0.05;
    double ki = 0.0;
    double kd = 0.0;
    double derivative_filter_tau_ms = 24.0;
    double output_filter_tau_ms = 8.0;
    double integral_output_limit = 0.18;
    double max_assist = 1.0;
    int observation_timeout_ms = 120;
};

class PidBenchmarkController {
public:
    PidBenchmarkController(
        PidBenchmarkConfig config,
        sustained_aimlab::BenchmarkCohort cohort)
        : config_(config), cohort_(cohort) {}

    sustained_aimlab::ControllerStepResult step(
        const sustained_aimlab::ControllerObservation& input) {
        using sustained_aimlab::ControllerStepResult;
        using sustained_aimlab::Vec2d;

        if (!input.target_present && input.fresh_vision) reset();

        if (input.fresh_vision && input.target_present &&
            input.primary_candidate_visible) {
            const Vec2d measured{
                input.observed_error_px.x,
                -input.observed_error_px.y,
            };
            const double capture_seconds = std::isfinite(input.capture_time_seconds)
                ? input.capture_time_seconds
                : static_cast<double>(input.now_ms) / 1000.0;
            if (!has_observation_ || input.target_id != target_id_) {
                reset();
                target_id_ = input.target_id;
                measured_error_ = measured;
                previous_measured_error_ = measured;
                previous_capture_seconds_ = capture_seconds;
                has_observation_ = true;
            } else {
                const double measurement_dt =
                    capture_seconds - previous_capture_seconds_;
                if (measurement_dt > 1.0e-6) {
                    const Vec2d raw_derivative{
                        (measured.x - previous_measured_error_.x) / measurement_dt,
                        (measured.y - previous_measured_error_.y) / measurement_dt,
                    };
                    const double tau_seconds = std::max(
                        0.0, config_.derivative_filter_tau_ms) / 1000.0;
                    const double alpha = tau_seconds > 0.0
                        ? 1.0 - std::exp(-measurement_dt / tau_seconds)
                        : 1.0;
                    derivative_.x += alpha * (raw_derivative.x - derivative_.x);
                    derivative_.y += alpha * (raw_derivative.y - derivative_.y);
                }
                measured_error_ = measured;
                previous_measured_error_ = measured;
                previous_capture_seconds_ = capture_seconds;
            }
            last_observation_ms_ = input.now_ms;
        }

        const bool observation_live = has_observation_ && input.target_present &&
            input.now_ms - last_observation_ms_ <= config_.observation_timeout_ms;
        Vec2d assist;
        if (observation_live) {
            constexpr double kControlDtSeconds = 0.001;
            integral_.x += measured_error_.x * kControlDtSeconds;
            integral_.y += measured_error_.y * kControlDtSeconds;
            if (config_.ki > 0.0) {
                const double state_limit =
                    std::max(0.0, config_.integral_output_limit) / config_.ki;
                integral_.x = std::clamp(integral_.x, -state_limit, state_limit);
                integral_.y = std::clamp(integral_.y, -state_limit, state_limit);
            } else {
                integral_ = {};
            }
            const Vec2d requested{
                config_.kp * measured_error_.x +
                    config_.ki * integral_.x +
                    config_.kd * derivative_.x,
                config_.kp * measured_error_.y +
                    config_.ki * integral_.y +
                    config_.kd * derivative_.y,
            };
            assist = clamp_magnitude(requested, config_.max_assist);
        } else {
            integral_ = {};
            derivative_ = {};
        }

        const double tau_seconds =
            std::max(0.0, config_.output_filter_tau_ms) / 1000.0;
        const double output_alpha = tau_seconds > 0.0
            ? 1.0 - std::exp(-0.001 / tau_seconds)
            : 1.0;
        filtered_assist_.x += output_alpha * (assist.x - filtered_assist_.x);
        filtered_assist_.y += output_alpha * (assist.y - filtered_assist_.y);
        filtered_assist_ = clamp_magnitude(filtered_assist_, config_.max_assist);

        const Vec2d combined = clamp_magnitude(
            {input.manual_stick.x + filtered_assist_.x,
             input.manual_stick.y + filtered_assist_.y},
            1.0);

        ControllerStepResult result;
        result.final_stick = combined;
        result.requested_assist_stick = filtered_assist_;
        result.shaped_assist_stick = filtered_assist_;
        result.predicted_terminal_error_px = {
            measured_error_.x,
            -measured_error_.y,
        };
        result.radial_closing_velocity_px_per_sec = radial_closing_velocity();
        if (cohort_ == sustained_aimlab::BenchmarkCohort::BodyLockFollow) {
            result.bodylock_mode = observation_live;
        } else if (observation_live &&
                   std::hypot(measured_error_.x, measured_error_.y) <= 24.0) {
            ads_entered_bodylock_ = true;
            result.bodylock_mode = true;
        } else {
            result.bodylock_mode = ads_entered_bodylock_ && observation_live;
        }
        result.target_observed = observation_live;
        result.tracker_reliable = observation_live;
        result.controller_target_id = observation_live ? target_id_ : 0;
        result.pre_recoil_stick = combined;
        result.has_pre_recoil_stick = true;
        return result;
    }

private:
    static sustained_aimlab::Vec2d clamp_magnitude(
        sustained_aimlab::Vec2d value,
        double limit) noexcept {
        const double magnitude = std::hypot(value.x, value.y);
        if (limit <= 0.0) return {};
        if (magnitude <= limit || magnitude <= 1.0e-12) return value;
        const double scale = limit / magnitude;
        return {value.x * scale, value.y * scale};
    }

    double radial_closing_velocity() const noexcept {
        const double magnitude = std::hypot(
            measured_error_.x, measured_error_.y);
        if (magnitude <= 1.0e-9) return 0.0;
        return -(
            measured_error_.x * derivative_.x +
            measured_error_.y * derivative_.y) / magnitude;
    }

    void reset() noexcept {
        target_id_ = 0;
        last_observation_ms_ = -1'000'000;
        previous_capture_seconds_ = 0.0;
        measured_error_ = {};
        previous_measured_error_ = {};
        derivative_ = {};
        integral_ = {};
        filtered_assist_ = {};
        has_observation_ = false;
        ads_entered_bodylock_ = false;
    }

    PidBenchmarkConfig config_;
    sustained_aimlab::BenchmarkCohort cohort_;
    std::uint64_t target_id_ = 0;
    int last_observation_ms_ = -1'000'000;
    double previous_capture_seconds_ = 0.0;
    sustained_aimlab::Vec2d measured_error_;
    sustained_aimlab::Vec2d previous_measured_error_;
    sustained_aimlab::Vec2d derivative_;
    sustained_aimlab::Vec2d integral_;
    sustained_aimlab::Vec2d filtered_assist_;
    bool has_observation_ = false;
    bool ads_entered_bodylock_ = false;
};

inline sustained_aimlab::ControllerStep make_pid_controller(
    PidBenchmarkConfig config,
    sustained_aimlab::BenchmarkCohort cohort) {
    auto state = std::make_shared<PidBenchmarkController>(config, cohort);
    return [state](const sustained_aimlab::ControllerObservation& input) {
        return state->step(input);
    };
}

}  // namespace controller_native::pid_benchmark
