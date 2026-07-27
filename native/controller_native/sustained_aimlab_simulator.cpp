#include "sustained_aimlab_simulator.h"

#include <algorithm>
#include <cmath>
#include <deque>
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
    Vec2d error,
    Vec2d target_velocity) noexcept {
    const Vec2d helpful = normalized_control_direction(error);
    Vec2d tangent{-helpful.y, helpful.x};
    const Vec2d control_velocity{target_velocity.x, -target_velocity.y};
    if (control_velocity.x * tangent.x + control_velocity.y * tangent.y < 0.0) {
        tangent.x = -tangent.x;
        tangent.y = -tangent.y;
    }
    if (target.id % 14 == 7) {
        return {
            helpful.x * -0.30 + tangent.x * 0.24,
            helpful.y * -0.30 + tangent.y * 0.24};
    }
    if (target.id % 14 == 8) {
        return {
            helpful.x * 0.24 - tangent.x * 0.30,
            helpful.y * 0.24 - tangent.y * 0.30};
    }
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

double left_strafe_input(
    const PlayerStrafeScript& strafe,
    int elapsed_ms,
    PlayerStrafeMode mode) noexcept {
    if (mode != PlayerStrafeMode::FullReversal ||
        elapsed_ms < strafe.onset_ms ||
        elapsed_ms >= strafe.release_ms) {
        return 0.0;
    }
    const double direction = static_cast<double>(strafe.initial_direction);
    return elapsed_ms < strafe.reverse_ms ? direction : -direction;
}

}  // namespace

BenchmarkResult run_simulation(
    const ScenarioScript& script,
    ManualProfile manual_profile,
    ControllerStep controller_step,
    BenchmarkCohort cohort,
    SimulationTraceObserver trace_observer,
    PlayerStrafeMode player_strafe_mode) {
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
    double player_velocity_x_px_per_second = 0.0;
    int left_strafe_active_ms = 0;
    int left_strafe_reversals = 0;
    double max_abs_left_x = 0.0;
    double min_sampled_player_top_speed_px_per_second =
        std::numeric_limits<double>::infinity();
    double max_sampled_player_top_speed_px_per_second = 0.0;
    double max_abs_player_speed_px_per_second = 0.0;
    Vec2d obsolete_manual_direction;
    Vec2d obsolete_cross_axis;
    double obsolete_manual_magnitude = 0.0;
    int obsolete_persistence_ms = 0;
    int obsolete_crossed_at_ms = -1;
    bool v1_vertical_crossed = false;
    double v1_maximum_vertical_overshoot_px = 0.0;
    double v1_post_cross_error_area_px_ms = 0.0;
    double v1_post_cross_wrong_way_output_integral = 0.0;
    bool previous_bodylock_mode = false;
    bool saw_ads_mode = false;
    bool pending_ads_to_bodylock_transition = false;
    std::unique_ptr<TargetScorer> scorer;
    std::deque<Vec2d> delayed_controls(
        static_cast<std::size_t>(
            std::max(0, script.config.control_response_delay_ms)),
        Vec2d{});
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
        player_velocity_x_px_per_second = 0.0;
        if (player_strafe_mode == PlayerStrafeMode::FullReversal) {
            min_sampled_player_top_speed_px_per_second = std::min(
                min_sampled_player_top_speed_px_per_second,
                target.player_strafe.top_speed_px_per_second);
            max_sampled_player_top_speed_px_per_second = std::max(
                max_sampled_player_top_speed_px_per_second,
                target.player_strafe.top_speed_px_per_second);
        }
        carried_observation = error;
        obsolete_manual_direction = normalized_control_direction(error);
        const double initial_distance = length(error);
        obsolete_cross_axis = initial_distance > 1e-9
            ? Vec2d{error.x / initial_distance, error.y / initial_distance}
            : Vec2d{};
        obsolete_manual_magnitude =
            0.60 + static_cast<double>((target.id * 37u) % 41u) / 100.0;
        obsolete_persistence_ms =
            80 + static_cast<int>((target.id * 17u) % 61u);
        obsolete_crossed_at_ms = -1;
        v1_vertical_crossed = false;
        v1_maximum_vertical_overshoot_px = 0.0;
        v1_post_cross_error_area_px_ms = 0.0;
        v1_post_cross_wrong_way_output_integral = 0.0;
        previous_bodylock_mode = false;
        saw_ads_mode = false;
        pending_ads_to_bodylock_transition = false;
        scorer = std::make_unique<TargetScorer>(target, script.config);
        if (cohort == BenchmarkCohort::BodyLockFollow) {
            scorer->mark_acquired(0);
        }
    };

    auto finish_target = [&] {
        TargetResult target_result = scorer->finish();
        if (manual_profile == ManualProfile::ObsoleteAfterCrossing) {
            target_result.maximum_vertical_overshoot_px =
                v1_maximum_vertical_overshoot_px;
            target_result.post_cross_error_area_px_ms =
                v1_post_cross_error_area_px_ms;
            target_result.post_cross_wrong_way_output_integral =
                v1_post_cross_wrong_way_output_integral;
        }
        target_results.push_back(std::move(target_result));
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
        bool vision_occluded = false;
        input.now_ms = now_ms;
        if (target_active) {
            const TargetScript& target = script.targets[target_index];
            input.target_present = true;
            input.target_id = target.id;
            if (tracking) {
                vision_occluded = std::any_of(
                    target.vision_occlusion_bursts.begin(),
                    target.vision_occlusion_bursts.end(),
                    [&](const VisionOcclusionBurst& burst) {
                        return tracking_ticks >= burst.tracking_offset_ms &&
                            tracking_ticks <
                                burst.tracking_offset_ms + burst.duration_ms;
                    });
            }
            while (observation_index < target.observation_at_ms.size() &&
                   target.observation_at_ms[observation_index] < target_elapsed_ms) {
                ++observation_index;
            }
            if (observation_index < target.observation_at_ms.size() &&
                target.observation_at_ms[observation_index] == target_elapsed_ms) {
                if (!vision_occluded) {
                    input.fresh_vision = true;
                    ++frame_id;
                    carried_observation = {
                        error.x +
                            target.observation_noise_px[observation_index].x,
                        error.y +
                            target.observation_noise_px[observation_index].y,
                    };
                }
                ++observation_index;
            }
            input.frame_id = frame_id;
            input.observed_error_px = carried_observation;
            const int strafe_elapsed_ms =
                cohort == BenchmarkCohort::BodyLockFollow
                ? (tracking ? tracking_ticks : -1)
                : target_elapsed_ms;
            if (strafe_elapsed_ms >= 0) {
                input.left_x = left_strafe_input(
                    target.player_strafe, strafe_elapsed_ms,
                    player_strafe_mode);
                if (input.left_x != 0.0) {
                    ++left_strafe_active_ms;
                    max_abs_left_x = std::max(
                        max_abs_left_x, std::fabs(input.left_x));
                }
                if (player_strafe_mode == PlayerStrafeMode::FullReversal &&
                    strafe_elapsed_ms == target.player_strafe.reverse_ms) {
                    ++left_strafe_reversals;
                }
            }
            if (manual_profile == ManualProfile::Mixed &&
                (cohort != BenchmarkCohort::BodyLockFollow || tracking)) {
                input.manual_stick = mixed_manual_input(
                    target, target_elapsed_ms, error, target_velocity);
            } else if (manual_profile == ManualProfile::ObsoleteAfterCrossing &&
                       (cohort != BenchmarkCohort::BodyLockFollow || tracking)) {
                if (obsolete_crossed_at_ms < 0 &&
                    dot(error, obsolete_cross_axis) <= 0.0) {
                    obsolete_crossed_at_ms = target_elapsed_ms;
                }
                if (obsolete_crossed_at_ms < 0 ||
                    target_elapsed_ms - obsolete_crossed_at_ms <
                        obsolete_persistence_ms) {
                    input.manual_stick = {
                        obsolete_manual_direction.x * obsolete_manual_magnitude,
                        obsolete_manual_direction.y * obsolete_manual_magnitude,
                    };
                }
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
        trace_frame.vision_occluded = vision_occluded;
        trace_frame.target_id = input.target_id;
        trace_frame.input = input;
        if (target_active) {
            trace_frame.motion = script.targets[target_index].motion;
            trace_frame.true_error_before_px = error;
        }

        const ControllerStepResult output = controller_step(input);
        if (target_active && cohort == BenchmarkCohort::AdsAcquire) {
            if (!output.bodylock_mode) saw_ads_mode = true;
            if (saw_ads_mode && !previous_bodylock_mode &&
                output.bodylock_mode) {
                pending_ads_to_bodylock_transition = true;
            }
        }
        Vec2d plant_control = output.final_stick;
        if (!delayed_controls.empty()) {
            delayed_controls.push_back(output.final_stick);
            plant_control = delayed_controls.front();
            delayed_controls.pop_front();
        }
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
            if (cohort == BenchmarkCohort::AdsAcquire &&
                !previous_bodylock_mode && output.bodylock_mode) {
                scorer->mark_ads_to_bodylock_handoff(
                    target_elapsed_ms,
                    error,
                    output.radial_closing_velocity_px_per_sec);
                pending_ads_to_bodylock_transition = true;
            }
            if (cohort != BenchmarkCohort::BodyLockFollow || tracking) {
                const int motion_elapsed_ms =
                    cohort == BenchmarkCohort::BodyLockFollow
                    ? tracking_ticks : target_elapsed_ms;
                advance_target(target, motion_elapsed_ms, 0.001, error, target_velocity);
            }
            const double time_constant_seconds =
                target.player_strafe.time_constant_ms / 1000.0;
            const double desired_player_velocity =
                input.left_x * target.player_strafe.top_speed_px_per_second;
            const double player_alpha = time_constant_seconds > 0.0
                ? 1.0 - std::exp(-0.001 / time_constant_seconds)
                : 1.0;
            player_velocity_x_px_per_second += player_alpha *
                (desired_player_velocity - player_velocity_x_px_per_second);
            error.x -= player_velocity_x_px_per_second * 0.001;
            max_abs_player_speed_px_per_second = std::max(
                max_abs_player_speed_px_per_second,
                std::fabs(player_velocity_x_px_per_second));
            const double response =
                script.config.camera_response_px_per_stick_second *
                aim_slowdown_multiplier(
                    length(error), target.visible_radius_px, script.config);
            error.x -= plant_control.x * response * 0.001;
            error.y += plant_control.y * response * 0.001;
            if (manual_profile == ManualProfile::ObsoleteAfterCrossing) {
                if (!v1_vertical_crossed && error.y >= 0.0) {
                    v1_vertical_crossed = true;
                }
                if (v1_vertical_crossed) {
                    const double excursion = std::max(0.0, error.y);
                    v1_maximum_vertical_overshoot_px = std::max(
                        v1_maximum_vertical_overshoot_px, excursion);
                    v1_post_cross_error_area_px_ms += excursion;
                    v1_post_cross_wrong_way_output_integral += std::max(
                        0.0, output.final_stick.y);
                }
            }
            trace_frame.true_error_after_px = error;
            trace_frame.target_velocity_px_per_second = target_velocity;
            trace_frame.player_velocity_x_px_per_second =
                player_velocity_x_px_per_second;

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
                    pending_ads_to_bodylock_transition;
                scorer->add_frame(frame);
                if (frame.ads_to_bodylock_transition) {
                    pending_ads_to_bodylock_transition = false;
                }
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
        TargetResult target_result = scorer->finish();
        if (manual_profile == ManualProfile::ObsoleteAfterCrossing) {
            target_result.maximum_vertical_overshoot_px =
                v1_maximum_vertical_overshoot_px;
            target_result.post_cross_error_area_px_ms =
                v1_post_cross_error_area_px_ms;
            target_result.post_cross_wrong_way_output_integral =
                v1_post_cross_wrong_way_output_integral;
        }
        target_results.push_back(std::move(target_result));
    }

    BenchmarkResult result = aggregate(
        script.seed, script.hash, std::move(target_results));
    result.manual_profile = manual_profile;
    result.cohort = cohort;
    result.player_strafe_mode = player_strafe_mode;
    result.ticks = script.config.duration_ms;
    result.left_strafe_active_ms = left_strafe_active_ms;
    result.left_strafe_reversals = left_strafe_reversals;
    result.max_abs_left_x = max_abs_left_x;
    result.min_sampled_player_top_speed_px_per_second =
        std::isfinite(min_sampled_player_top_speed_px_per_second)
        ? min_sampled_player_top_speed_px_per_second : 0.0;
    result.max_sampled_player_top_speed_px_per_second =
        max_sampled_player_top_speed_px_per_second;
    result.max_abs_player_speed_px_per_second =
        max_abs_player_speed_px_per_second;
    return result;
}

}  // namespace controller_native::sustained_aimlab
