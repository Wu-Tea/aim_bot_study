#include "sustained_aimlab_simulator.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <utility>

namespace controller_native::sustained_aimlab {
namespace {

bool finite(Vec2d value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

Vec2d normalized_control_direction(Vec2d error) noexcept {
    error.y = -error.y;
    const double magnitude = length(error);
    if (magnitude <= 1e-9) return {};
    return {error.x / magnitude, error.y / magnitude};
}

Vec2d mixed_manual_input(
    const TargetScript& target,
    int target_elapsed_ms,
    Vec2d error) noexcept {
    const Vec2d helpful = normalized_control_direction(error);
    switch (target.id % 7) {
    case 0:
        return {0.012, -0.008};
    case 1:
        return {helpful.x * 0.25, helpful.y * 0.25};
    case 2:
        if (target_elapsed_ms >= 80 && target_elapsed_ms < 140) {
            return {-helpful.x * 0.32, -helpful.y * 0.32};
        }
        return {};
    case 3:
        if (target_elapsed_ms >= 120) {
            return {helpful.x * 0.30, helpful.y * 0.30};
        }
        return {};
    case 4:
        return {helpful.x * 0.35, -helpful.y * 0.20};
    case 5:
        if (target_elapsed_ms >= 180 && target_elapsed_ms < 240) {
            return {-helpful.x * 0.44, -helpful.y * 0.44};
        }
        return {helpful.x * 0.18, helpful.y * 0.18};
    case 6:
        if (target_elapsed_ms >= 500 && target_elapsed_ms < 560) {
            return {-helpful.x * 0.60, -helpful.y * 0.60};
        }
        return {};
    }
    return {};
}

}  // namespace

BenchmarkResult run_simulation(
    const ScenarioScript& script,
    ManualProfile manual_profile,
    ControllerStep controller_step,
    BenchmarkCohort cohort,
    SimulationTraceObserver trace_observer) {
    if (!controller_step) {
        throw std::invalid_argument("controller callback is required");
    }
    if (script.config.duration_ms <= 0 || script.config.tick_ms != 1) {
        throw std::invalid_argument("sustained AimLab runner requires 1ms ticks");
    }

    std::vector<TargetResult> target_results;
    std::size_t target_index = 0;
    int gap_remaining_ms = 0;
    bool target_active = false;
    bool pending_fresh_miss = false;
    bool tracking = false;
    int tracking_ticks = 0;
    int target_elapsed_ms = 0;
    std::size_t observation_index = 0;
    std::uint64_t frame_id = 0;
    Vec2d error;
    Vec2d target_velocity;
    Vec2d carried_observation;
    bool previous_bodylock_mode = false;
    std::unique_ptr<TargetScorer> scorer;

    auto spawn_target = [&] {
        if (target_index >= script.targets.size()) {
            throw std::runtime_error("scenario script exhausted before duration");
        }
        const TargetScript& target = script.targets[target_index];
        target_active = true;
        tracking = false;
        tracking_ticks = 0;
        target_elapsed_ms = 0;
        observation_index = 0;
        error = target.initial_error_px;
        if (cohort == BenchmarkCohort::BodyLockFollow) {
            const double initial_distance = length(error);
            const double warm_distance = std::min(
                8.0, target.visible_radius_px * 0.5);
            if (initial_distance > 1e-9) {
                error.x *= warm_distance / initial_distance;
                error.y *= warm_distance / initial_distance;
            }
        }
        target_velocity = target.initial_velocity_px_per_second;
        carried_observation = error;
        previous_bodylock_mode = false;
        scorer = std::make_unique<TargetScorer>(target, script.config);
        if (cohort == BenchmarkCohort::BodyLockFollow) {
            scorer->mark_acquired(0);
        }
    };

    auto finish_target = [&] {
        target_results.push_back(scorer->finish());
        scorer.reset();
        target_active = false;
        tracking = false;
        ++target_index;
        gap_remaining_ms = script.config.inter_target_gap_ms;
        pending_fresh_miss = true;
    };

    for (int now_ms = 0; now_ms < script.config.duration_ms; ++now_ms) {
        if (!target_active && gap_remaining_ms == 0 &&
            target_index < script.targets.size()) {
            spawn_target();
        }

        ControllerObservation input;
        input.now_ms = now_ms;
        if (target_active) {
            const TargetScript& target = script.targets[target_index];
            input.target_present = true;
            input.target_id = target.id;
            while (observation_index < target.observation_at_ms.size() &&
                   target.observation_at_ms[observation_index] < target_elapsed_ms) {
                ++observation_index;
            }
            if (observation_index < target.observation_at_ms.size() &&
                target.observation_at_ms[observation_index] == target_elapsed_ms) {
                input.fresh_vision = true;
                ++frame_id;
                carried_observation = {
                    error.x + target.observation_noise_px[observation_index].x,
                    error.y + target.observation_noise_px[observation_index].y,
                };
                ++observation_index;
            }
            input.frame_id = frame_id;
            input.observed_error_px = carried_observation;
            if (manual_profile == ManualProfile::Mixed &&
                (cohort != BenchmarkCohort::BodyLockFollow || tracking)) {
                input.manual_stick = mixed_manual_input(
                    target, target_elapsed_ms, error);
            }
        } else if (pending_fresh_miss) {
            input.fresh_vision = true;
            input.frame_id = ++frame_id;
            pending_fresh_miss = false;
        }

        SimulationTraceFrame trace_frame;
        trace_frame.absolute_ms = now_ms;
        trace_frame.target_elapsed_ms = target_active ? target_elapsed_ms : -1;
        trace_frame.target_active = target_active;
        trace_frame.fresh_vision = input.fresh_vision;
        trace_frame.target_id = input.target_id;
        trace_frame.input = input;
        if (target_active) {
            trace_frame.motion = script.targets[target_index].motion;
            trace_frame.true_error_before_px = error;
        }

        const ControllerStepResult output = controller_step(input);
        trace_frame.output = output;
        if (!finite(output.final_stick) ||
            !finite(output.requested_assist_stick) ||
            !finite(output.shaped_assist_stick) ||
            !finite(output.predicted_terminal_error_px) ||
            !std::isfinite(output.radial_closing_velocity_px_per_sec)) {
            throw std::runtime_error("controller produced non-finite output");
        }

        if (target_active) {
            const TargetScript& target = script.targets[target_index];
            if (cohort != BenchmarkCohort::BodyLockFollow || tracking) {
                const int motion_elapsed_ms =
                    cohort == BenchmarkCohort::BodyLockFollow
                    ? tracking_ticks : target_elapsed_ms;
                advance_target(target, motion_elapsed_ms, 0.001, error, target_velocity);
            }
            const double response =
                script.config.camera_response_px_per_stick_second *
                aim_slowdown_multiplier(
                    length(error), target.visible_radius_px, script.config);
            error.x -= output.final_stick.x * response * 0.001;
            error.y += output.final_stick.y * response * 0.001;
            trace_frame.true_error_after_px = error;
            trace_frame.target_velocity_px_per_second = target_velocity;

            if (cohort == BenchmarkCohort::BodyLockFollow && !tracking) {
                if (output.bodylock_mode) {
                    scorer->mark_bodylock_entered(target_elapsed_ms);
                    tracking = true;
                    tracking_ticks = 0;
                } else if (target_elapsed_ms + 1 >=
                           script.config.bodylock_entry_timeout_ms) {
                    scorer->mark_bodylock_entry_failed();
                    finish_target();
                }
            }

            if (target_active && tracking) {
                ScoreFrame frame;
                frame.absolute_ms = now_ms;
                frame.target_elapsed_ms = target_elapsed_ms;
                frame.in_tracking_window = true;
                frame.target_observed = output.target_observed;
                frame.tracker_reliable = output.tracker_reliable;
                frame.manual_escape = length(input.manual_stick) >= 0.45;
                frame.bodylock_mode = output.bodylock_mode;
                frame.target_id = target.id;
                frame.error_px = error;
                frame.target_velocity_px_per_second = target_velocity;
                frame.manual_stick = input.manual_stick;
                frame.requested_assist_stick = output.requested_assist_stick;
                frame.shaped_assist_stick = output.shaped_assist_stick;
                frame.final_stick = output.final_stick;
                frame.predicted_terminal_error_px =
                    output.predicted_terminal_error_px;
                frame.radial_closing_velocity_px_per_sec =
                    output.radial_closing_velocity_px_per_sec;
                frame.ads_to_bodylock_transition =
                    !previous_bodylock_mode && output.bodylock_mode;
                scorer->add_frame(frame);
                ++tracking_ticks;
                if (tracking_ticks >= script.config.tracking_window_ms) {
                    finish_target();
                }
            } else if (target_active && cohort == BenchmarkCohort::AdsAcquire &&
                       length(error) < target.visible_radius_px) {
                scorer->mark_acquired(target_elapsed_ms + 1);
                tracking = true;
                tracking_ticks = 0;
            } else if (target_active && cohort == BenchmarkCohort::AdsAcquire &&
                       target_elapsed_ms + 1 >= target.acquire_deadline_ms) {
                scorer->mark_timed_out();
                finish_target();
            }
            if (target_active) ++target_elapsed_ms;
        } else if (gap_remaining_ms > 0) {
            --gap_remaining_ms;
        }
        previous_bodylock_mode = target_active && output.bodylock_mode;
        if (trace_observer) trace_observer(trace_frame);
    }

    if (target_active && scorer) {
        target_results.push_back(scorer->finish());
    }

    BenchmarkResult result = aggregate(
        script.seed, script.hash, std::move(target_results));
    result.manual_profile = manual_profile;
    result.cohort = cohort;
    result.ticks = script.config.duration_ms;
    return result;
}

}  // namespace controller_native::sustained_aimlab
