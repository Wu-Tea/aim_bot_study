#include "sustained_aimlab_score.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace controller_native::sustained_aimlab {
namespace {

Vec2d subtract(Vec2d left, Vec2d right) noexcept {
    return {left.x - right.x, left.y - right.y};
}

Vec2d add(Vec2d left, Vec2d right) noexcept {
    return {left.x + right.x, left.y + right.y};
}

Vec2d scale(Vec2d value, double amount) noexcept {
    return {value.x * amount, value.y * amount};
}

Vec2d normalized(Vec2d value) noexcept {
    const double magnitude = length(value);
    return magnitude > 1e-9 ? scale(value, 1.0 / magnitude) : Vec2d{};
}

double percentile(std::vector<double> values, double fraction) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const double position = std::clamp(fraction, 0.0, 1.0) *
        static_cast<double>(values.size() - 1);
    const std::size_t lower = static_cast<std::size_t>(std::floor(position));
    const std::size_t upper = static_cast<std::size_t>(std::ceil(position));
    const double blend = position - static_cast<double>(lower);
    return values[lower] + (values[upper] - values[lower]) * blend;
}

Vec2d desired_stick_direction(const ScoreFrame& frame) noexcept {
    Vec2d demand = add(frame.error_px,
        scale(frame.target_velocity_px_per_second, 0.05));
    demand.y = -demand.y;
    return normalized(demand);
}

bool control_demanded(const ScoreFrame& frame, double radius) noexcept {
    return frame.target_observed && frame.tracker_reliable &&
        (length(frame.target_velocity_px_per_second) >= 40.0 ||
         length(frame.error_px) > radius * 0.5);
}

}  // namespace

double length(Vec2d value) noexcept {
    return std::hypot(value.x, value.y);
}

double dot(Vec2d left, Vec2d right) noexcept {
    return left.x * right.x + left.y * right.y;
}

TargetScorer::TargetScorer(TargetScript script, BenchmarkConfig config)
    : script_(std::move(script)), config_(config) {
    result_.id = script_.id;
    result_.motion = script_.motion;
    result_.deadline_ms = script_.acquire_deadline_ms;
    result_.visible_radius_px = script_.visible_radius_px;
}

void TargetScorer::mark_acquired(int entry_ms) {
    if (finished_ || result_.acquired) return;
    result_.acquired = true;
    result_.first_pass_success = true;
    result_.first_entry_ms = std::max(0, entry_ms);
    const double remaining = std::max(
        0, script_.acquire_deadline_ms - result_.first_entry_ms);
    result_.acquire_points = 1000.0 * remaining /
        static_cast<double>(std::max(1, script_.acquire_deadline_ms));
}

void TargetScorer::mark_timed_out() {
    if (finished_ || result_.acquired) return;
    result_.acquisition_timed_out = true;
}

void TargetScorer::mark_bodylock_entered(int entry_ms) {
    if (finished_ || bodylock_seen_) return;
    bodylock_seen_ = true;
    result_.bodylock_entry_ms = std::max(0, entry_ms);
}

void TargetScorer::mark_bodylock_entry_failed() {
    if (finished_ || bodylock_seen_) return;
    result_.bodylock_entry_failed = true;
}

void TargetScorer::add_frame(const ScoreFrame& frame) {
    if (finished_ || !frame.in_tracking_window) return;
    ++tracking_ticks_;
    const double radius = std::max(1e-9, script_.visible_radius_px);
    const double distance = length(frame.error_px);
    result_.max_error_px = std::max(result_.max_error_px, distance);
    result_.tracking_errors_px.push_back(distance);

    double accuracy = 0.0;
    if (distance < radius) {
        const double normalized_distance = distance / radius;
        const double residual = 1.0 - normalized_distance * normalized_distance;
        accuracy = residual * residual;
        result_.tracking_points += accuracy;
    }

    const double output_delta = has_previous_
        ? length(subtract(frame.final_stick, previous_output_)) : 0.0;
    result_.output_deltas.push_back(output_delta);
    const double output_jerk = has_previous_
        ? std::fabs(output_delta - previous_output_delta_) : 0.0;
    result_.output_jerks.push_back(output_jerk);
    if (has_previous_ && distance <= previous_distance_ + 1e-9 && distance < radius) {
        const double variation_quality = std::exp(
            -std::pow(output_delta / 0.04, 2.0));
        result_.smooth_bonus += 0.1 * accuracy * variation_quality;
    }
    if (has_previous_ && output_delta > 0.35) {
        ++result_.direction_discontinuities;
    }

    const bool outside = distance >= radius;
    if (outside && !outside_latched_) {
        ++result_.circle_exit_events;
        outside_latched_ = true;
        if (tracking_ticks_ <= 100) result_.first_pass_success = false;
    } else if (!outside) {
        outside_latched_ = false;
    }

    const double inner_radius = radius / 3.0;
    const double overshoot_radius = radius * 2.0 / 3.0;
    if (!overshoot_latched_ && distance <= inner_radius) {
        if (!overshoot_armed_) {
            overshoot_armed_ = true;
            overshoot_arm_error_ = frame.error_px;
            overshoot_arm_tick_ = tracking_ticks_;
        }
    }
    if (overshoot_armed_ && !overshoot_latched_) {
        const bool crossed = dot(overshoot_arm_error_, frame.error_px) < 0.0;
        const bool grew = distance > overshoot_radius;
        const bool prompt = tracking_ticks_ - overshoot_arm_tick_ <= 150;
        const Vec2d desired = desired_stick_direction(frame);
        const bool continued = dot(frame.final_stick, desired) < 0.0;
        if (crossed && grew && prompt && continued) {
            ++result_.over_events;
            overshoot_latched_ = true;
            overshoot_armed_ = false;
        } else if (!prompt) {
            overshoot_armed_ = false;
        }
    }
    if (overshoot_latched_ && distance <= inner_radius) {
        overshoot_latched_ = false;
    }

    const double target_speed = length(frame.target_velocity_px_per_second);
    double projected_lag = 0.0;
    if (target_speed >= 40.0) {
        projected_lag = dot(
            frame.error_px,
            scale(frame.target_velocity_px_per_second, 1.0 / target_speed));
    }
    const bool not_closing = !has_previous_ || distance >= previous_distance_ - 1e-6;
    const bool undertracking = target_speed >= 40.0 &&
        projected_lag > radius * 0.5 && not_closing;
    if (undertracking) {
        ++undertrack_ticks_;
        ++result_.undertrack_total_ms;
        if (undertrack_ticks_ >= 40 && !undertrack_latched_) {
            ++result_.undertrack_events;
            undertrack_latched_ = true;
        }
    } else {
        undertrack_ticks_ = 0;
        if (projected_lag < radius * 0.25) undertrack_latched_ = false;
    }

    const bool demanded = control_demanded(frame, radius);
    const Vec2d desired = desired_stick_direction(frame);
    const double final_projection = dot(frame.final_stick, desired);
    const double manual_projection = dot(frame.manual_stick, desired);
    const bool false_stop = demanded && !frame.manual_escape &&
        manual_projection < 0.05 && final_projection < 0.05;
    if (false_stop) {
        ++false_stop_ticks_;
        ++result_.false_stop_total_ms;
        ++result_.zero_output_while_demanded_ms;
        if (false_stop_ticks_ >= 20 && !false_stop_latched_) {
            ++result_.false_stop_events;
            false_stop_latched_ = true;
        }
    } else {
        false_stop_ticks_ = 0;
        if (!demanded || final_projection >= 0.08 || frame.manual_escape) {
            false_stop_latched_ = false;
        }
    }

    if (frame.bodylock_mode) {
        if (!bodylock_seen_) {
            result_.bodylock_entry_ms = tracking_ticks_ - 1;
        }
        bodylock_seen_ = true;
        ++result_.bodylock_active_ms;
    } else {
        ++result_.unexpected_mode_ms;
    }
    const bool invalid_mode = bodylock_seen_ && demanded &&
        !frame.manual_escape && !frame.bodylock_mode;
    if (invalid_mode) {
        ++false_mode_exit_ticks_;
        ++result_.interruption_total_ms;
        if (false_mode_exit_ticks_ >= 2 && !false_mode_exit_latched_) {
            ++result_.false_mode_exit_events;
            false_mode_exit_latched_ = true;
        }
    } else {
        false_mode_exit_ticks_ = 0;
        if (!demanded || frame.bodylock_mode || frame.manual_escape) {
            false_mode_exit_latched_ = false;
        }
    }

    if (has_previous_ && demanded && !frame.manual_escape) {
        const double current_assist = length(frame.shaped_assist_stick);
        const double previous_assist = length(previous_shaped_assist_);
        const bool dropped = current_assist < 0.05 &&
            previous_assist >= 0.10 && current_assist < previous_assist * 0.5;
        if (dropped && !assist_dropout_latched_) {
            ++result_.assist_dropout_events;
            assist_dropout_latched_ = true;
        } else if (current_assist >= 0.08 || !demanded) {
            assist_dropout_latched_ = false;
        }
    }

    const bool stale_condition = (!frame.target_observed ||
        (target_speed < 1.0 && distance <= inner_radius)) &&
        length(frame.final_stick) >= 0.20;
    if (stale_condition) {
        ++stale_output_ticks_;
        if (stale_output_ticks_ >= 20 && !stale_output_latched_) {
            ++result_.stale_output_after_stop_events;
            stale_output_latched_ = true;
        }
    } else {
        stale_output_ticks_ = 0;
        if (length(frame.final_stick) < 0.10) stale_output_latched_ = false;
    }

    if (has_previous_ && distance >= 10.0 && distance <= 20.0 &&
        distance >= previous_distance_ - 1e-6) {
        ++result_.stall_ring_ms;
    }

    has_previous_ = true;
    previous_output_ = frame.final_stick;
    previous_shaped_assist_ = frame.shaped_assist_stick;
    previous_distance_ = distance;
    previous_output_delta_ = output_delta;
}

TargetResult TargetScorer::finish() {
    finished_ = true;
    if (tracking_ticks_ > 0 && !bodylock_seen_) {
        result_.bodylock_entry_failed = true;
    }
    return result_;
}

BenchmarkResult aggregate(
    std::uint32_t seed,
    std::uint64_t script_hash,
    std::vector<TargetResult> targets) {
    BenchmarkResult result;
    result.seed = seed;
    result.script_hash = script_hash;
    result.targets_spawned = static_cast<int>(targets.size());
    std::vector<double> errors;
    std::vector<double> output_deltas;
    std::vector<double> output_jerks;
    for (const TargetResult& target : targets) {
        result.acquire_points += target.acquire_points;
        result.tracking_points += target.tracking_points;
        result.smooth_bonus += target.smooth_bonus;
        result.targets_acquired += target.acquired ? 1 : 0;
        result.targets_missed += target.acquisition_timed_out ? 1 : 0;
        result.over_events += target.over_events;
        result.undertrack_events += target.undertrack_events;
        result.false_interruption_events +=
            target.false_mode_exit_events + target.assist_dropout_events;
        result.false_stop_events += target.false_stop_events;
        result.stale_output_after_stop_events +=
            target.stale_output_after_stop_events;
        result.bodylock_entry_failures += target.bodylock_entry_failed ? 1 : 0;
        result.bodylock_active_ms += target.bodylock_active_ms;
        result.unexpected_mode_ms += target.unexpected_mode_ms;
        errors.insert(errors.end(), target.tracking_errors_px.begin(),
                      target.tracking_errors_px.end());
        output_deltas.insert(output_deltas.end(), target.output_deltas.begin(),
                             target.output_deltas.end());
        output_jerks.insert(output_jerks.end(), target.output_jerks.begin(),
                            target.output_jerks.end());
    }
    if (!errors.empty()) {
        result.mean_error_px = std::accumulate(errors.begin(), errors.end(), 0.0) /
            static_cast<double>(errors.size());
    }
    result.p95_error_px = percentile(std::move(errors), 0.95);
    result.p95_output_delta = percentile(std::move(output_deltas), 0.95);
    result.p95_jerk = percentile(std::move(output_jerks), 0.95);
    result.targets = std::move(targets);
    return result;
}

}  // namespace controller_native::sustained_aimlab
