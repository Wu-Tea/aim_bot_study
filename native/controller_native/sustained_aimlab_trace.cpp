#include "sustained_aimlab_trace.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace controller_native::sustained_aimlab {
namespace {

double magnitude(Vec2d value) noexcept {
    return std::hypot(value.x, value.y);
}

double cosine(Vec2d left, Vec2d right) noexcept {
    const double denominator = magnitude(left) * magnitude(right);
    if (denominator <= 1e-12) return 1.0;
    return (left.x * right.x + left.y * right.y) / denominator;
}

void add_or_merge(std::vector<ConflictEpisode>& episodes,
                  ConflictKind kind, int now_ms, double severity,
                  int merge_gap_ms) {
    if (!episodes.empty() && episodes.back().kind == kind &&
        now_ms - episodes.back().end_ms - 1 <= merge_gap_ms) {
        episodes.back().end_ms = now_ms;
        episodes.back().severity = std::max(episodes.back().severity, severity);
        return;
    }
    episodes.push_back({kind, now_ms, now_ms, severity});
}

int absolute_time_for(const std::vector<SimulationTraceFrame>& trace,
                      std::uint64_t target_id, int elapsed_ms) {
    const auto found = std::find_if(
        trace.begin(), trace.end(), [&](const SimulationTraceFrame& frame) {
            return frame.target_active && frame.target_id == target_id &&
                frame.target_elapsed_ms == elapsed_ms;
        });
    return found == trace.end() ? -1 : found->absolute_ms;
}

}  // namespace

std::vector<EvaluationPoint> generate_fixed_anchors(
    const ScenarioScript& script,
    const std::vector<SimulationTraceFrame>& trace) {
    std::vector<EvaluationPoint> result;
    auto append = [&](const TargetScript& target, int elapsed, AnchorKind kind) {
        if (elapsed < 0) return;
        result.push_back({
            absolute_time_for(trace, target.id, elapsed),
            elapsed,
            target.id,
            kind,
        });
    };

    for (const TargetScript& target : script.targets) {
        if (target.motion == MotionProfile::Reverse) {
            append(target, target.maneuver_at_ms, AnchorKind::TargetReverse);
        } else if (target.motion == MotionProfile::Stop) {
            append(target, target.maneuver_at_ms, AnchorKind::TargetStop);
        } else if (target.motion == MotionProfile::JumpFall &&
                   target.acceleration_px_per_second_squared.y > 0.0 &&
                   target.initial_velocity_px_per_second.y < 0.0) {
            const int apex_ms = static_cast<int>(std::lround(
                -1000.0 * target.initial_velocity_px_per_second.y /
                target.acceleration_px_per_second_squared.y));
            append(target, apex_ms, AnchorKind::JumpApex);
            append(target, apex_ms + 1, AnchorKind::FallTransition);
        }
    }

    bool prior_observed = false;
    bool prior_bodylock = false;
    bool have_prior = false;
    for (const SimulationTraceFrame& frame : trace) {
        if (!frame.target_active) {
            prior_observed = false;
            prior_bodylock = false;
            have_prior = false;
            continue;
        }
        const bool observed = frame.output.target_observed;
        if (have_prior && prior_observed && !observed) {
            result.push_back({frame.absolute_ms, frame.target_elapsed_ms,
                              frame.target_id, AnchorKind::ObservationLoss});
        } else if (have_prior && !prior_observed && observed) {
            result.push_back({frame.absolute_ms, frame.target_elapsed_ms,
                              frame.target_id, AnchorKind::ObservationRecovery});
        }
        if (have_prior && !prior_bodylock && frame.output.bodylock_mode) {
            result.push_back({frame.absolute_ms, frame.target_elapsed_ms,
                              frame.target_id, AnchorKind::AdsBodylockHandoff});
        }
        prior_observed = observed;
        prior_bodylock = frame.output.bodylock_mode;
        have_prior = true;
    }
    return result;
}

std::vector<ConflictEpisode> detect_conflict_episodes(
    const std::vector<SimulationTraceFrame>& trace,
    const ConflictConfig& config) {
    std::map<ConflictKind, std::vector<ConflictEpisode>> by_kind;
    const SimulationTraceFrame* previous = nullptr;
    for (const SimulationTraceFrame& frame : trace) {
        if (!frame.target_active) {
            previous = &frame;
            continue;
        }
        const Vec2d manual = frame.input.manual_stick;
        const Vec2d ai = frame.output.shaped_assist_stick;
        const double manual_magnitude = magnitude(manual);
        const double ai_magnitude = magnitude(ai);
        const double final_magnitude = magnitude(frame.output.final_stick);
        const bool material = manual_magnitude >= config.minimum_manual &&
            ai_magnitude >= config.minimum_ai;
        const bool opposing = material && cosine(manual, ai) <= config.opposing_cosine;
        if (opposing) {
            const ConflictKind kind = final_magnitude <= config.cancellation_output
                ? ConflictKind::NearZeroCancellation
                : ConflictKind::ManualAiOpposition;
            add_or_merge(by_kind[kind], kind, frame.absolute_ms,
                         manual_magnitude + ai_magnitude - final_magnitude,
                         config.merge_gap_ms);
        }

        const double distance = magnitude(frame.true_error_after_px);
        if (distance >= 10.0 && distance <= 20.0 && final_magnitude < 0.03) {
            add_or_merge(by_kind[ConflictKind::StallRing],
                         ConflictKind::StallRing, frame.absolute_ms,
                         distance, config.merge_gap_ms);
        }
        if (previous && previous->target_active &&
            magnitude(previous->output.final_stick) >= 0.05 &&
            final_magnitude >= 0.05 &&
            cosine(previous->output.final_stick, frame.output.final_stick) < -0.5) {
            add_or_merge(by_kind[ConflictKind::DirectionReversal],
                         ConflictKind::DirectionReversal, frame.absolute_ms,
                         final_magnitude, config.merge_gap_ms);
        }
        if (previous && previous->target_active &&
            magnitude(previous->true_error_after_px) <= 24.0 && distance > 24.0) {
            add_or_merge(by_kind[ConflictKind::CircleExit],
                         ConflictKind::CircleExit, frame.absolute_ms,
                         distance - 24.0, config.merge_gap_ms);
        }
        if (previous && previous->target_active &&
            previous->output.bodylock_mode && !frame.output.bodylock_mode &&
            manual_magnitude < 0.45) {
            add_or_merge(by_kind[ConflictKind::FalseInterruption],
                         ConflictKind::FalseInterruption, frame.absolute_ms,
                         distance, config.merge_gap_ms);
        }
        const Vec2d control_direction{
            frame.output.final_stick.x, -frame.output.final_stick.y};
        if (final_magnitude >= 0.03 &&
            control_direction.x * frame.true_error_before_px.x +
                control_direction.y * frame.true_error_before_px.y < 0.0) {
            add_or_merge(by_kind[ConflictKind::WrongWayCommit],
                         ConflictKind::WrongWayCommit, frame.absolute_ms,
                         final_magnitude, config.merge_gap_ms);
        }
        if (material && cosine(manual, ai) > 0.5 &&
            final_magnitude > std::max(manual_magnitude, ai_magnitude) * 1.15) {
            add_or_merge(by_kind[ConflictKind::DestructiveStack],
                         ConflictKind::DestructiveStack, frame.absolute_ms,
                         final_magnitude - std::max(manual_magnitude, ai_magnitude),
                         config.merge_gap_ms);
        }
        previous = &frame;
    }

    std::vector<ConflictEpisode> result;
    for (auto& [kind, episodes] : by_kind) {
        result.insert(result.end(), episodes.begin(), episodes.end());
    }
    return result;
}

std::vector<ConflictEpisode> select_episode_budget(
    std::vector<ConflictEpisode> episodes,
    std::size_t per_kind_limit) {
    std::stable_sort(episodes.begin(), episodes.end(),
        [](const ConflictEpisode& left, const ConflictEpisode& right) {
            if (left.kind != right.kind) return left.kind < right.kind;
            if (left.severity != right.severity) return left.severity > right.severity;
            return left.start_ms < right.start_ms;
        });
    std::map<ConflictKind, std::size_t> selected_per_kind;
    std::vector<ConflictEpisode> result;
    for (const ConflictEpisode& episode : episodes) {
        std::size_t& count = selected_per_kind[episode.kind];
        if (count >= per_kind_limit) continue;
        result.push_back(episode);
        ++count;
    }
    return result;
}

}  // namespace controller_native::sustained_aimlab
