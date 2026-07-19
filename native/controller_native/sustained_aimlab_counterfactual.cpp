#include "sustained_aimlab_counterfactual.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace controller_native::sustained_aimlab {
namespace {

constexpr double kReplayTolerance = 1e-6;

bool near(double left, double right) noexcept {
    return std::fabs(left - right) <= kReplayTolerance;
}

bool same(Vec2d left, Vec2d right) noexcept {
    return near(left.x, right.x) && near(left.y, right.y);
}

bool same_output(const ControllerStepResult& left,
                 const ControllerStepResult& right) noexcept {
    return same(left.final_stick, right.final_stick) &&
        same(left.requested_assist_stick, right.requested_assist_stick) &&
        same(left.shaped_assist_stick, right.shaped_assist_stick) &&
        same(left.predicted_terminal_error_px,
             right.predicted_terminal_error_px) &&
        near(left.radial_closing_velocity_px_per_sec,
             right.radial_closing_velocity_px_per_sec) &&
        left.bodylock_mode == right.bodylock_mode &&
        left.target_observed == right.target_observed &&
        left.tracker_reliable == right.tracker_reliable;
}

bool same_frame(const SimulationTraceFrame& left,
                const SimulationTraceFrame& right) noexcept {
    return left.absolute_ms == right.absolute_ms &&
        left.target_elapsed_ms == right.target_elapsed_ms &&
        left.target_active == right.target_active &&
        left.target_id == right.target_id &&
        same(left.true_error_before_px, right.true_error_before_px) &&
        same(left.true_error_after_px, right.true_error_after_px) &&
        same(left.target_velocity_px_per_second,
             right.target_velocity_px_per_second) &&
        same_output(left.output, right.output);
}

double frame_distance(const SimulationTraceFrame& frame) noexcept {
    return std::hypot(
        frame.true_error_after_px.x, frame.true_error_after_px.y);
}

void summarize_branch(const ReplayReference& reference,
                      const ReplayRequest& request,
                      BranchResult& result) {
    const int end_ms = std::min(
        reference.script.config.duration_ms,
        request.branch_at_ms + request.horizon_ms);
    const SimulationTraceFrame* previous = nullptr;
    double previous_distance = 0.0;
    int stable_ticks = 0;
    for (const SimulationTraceFrame& frame : result.trace) {
        if (frame.absolute_ms < request.branch_at_ms ||
            frame.absolute_ms >= end_ms || !frame.target_active) {
            continue;
        }
        const double distance = frame_distance(frame);
        result.error_area_px_ms += previous
            ? 0.5 * (previous_distance + distance)
            : distance;
        result.final_error_px = distance;
        const double response =
            reference.script.config.camera_response_px_per_stick_second *
            aim_slowdown_multiplier(
                std::hypot(frame.true_error_before_px.x,
                           frame.true_error_before_px.y),
                reference.script.config.target_radius_px,
                reference.script.config);
        result.path_px += std::hypot(
            frame.output.final_stick.x,
            frame.output.final_stick.y) * response * 0.001;
        if (previous) {
            const double previous_magnitude = std::hypot(
                previous->output.final_stick.x,
                previous->output.final_stick.y);
            const double current_magnitude = std::hypot(
                frame.output.final_stick.x,
                frame.output.final_stick.y);
            const double direction_dot =
                previous->output.final_stick.x * frame.output.final_stick.x +
                previous->output.final_stick.y * frame.output.final_stick.y;
            if (previous_magnitude >= 0.05 && current_magnitude >= 0.05 &&
                direction_dot < 0.0) {
                ++result.correction_reversals;
            }
        }
        const bool mode_stable =
            reference.cohort != BenchmarkCohort::BodyLockFollow ||
            frame.output.bodylock_mode;
        if (distance < reference.script.config.target_radius_px &&
            std::fabs(frame.output.radial_closing_velocity_px_per_sec) <= 25.0 &&
            mode_stable) {
            if (result.time_to_acquire_ms < 0) {
                result.time_to_acquire_ms =
                    frame.absolute_ms - request.branch_at_ms;
            }
            ++stable_ticks;
            if (stable_ticks >= 30 && result.settle_ms < 0) {
                result.settle_ms =
                    frame.absolute_ms - request.branch_at_ms - 29;
            }
        } else {
            stable_ticks = 0;
        }
        previous = &frame;
        previous_distance = distance;
    }
}

double area_at(const BranchResult& result, int branch_at_ms, int horizon_ms) {
    const int end_ms = branch_at_ms + horizon_ms;
    bool have_previous = false;
    double previous = 0.0;
    double area = 0.0;
    for (const SimulationTraceFrame& frame : result.trace) {
        if (frame.absolute_ms < branch_at_ms || frame.absolute_ms >= end_ms ||
            !frame.target_active) {
            continue;
        }
        const double distance = frame_distance(frame);
        area += have_previous ? 0.5 * (previous + distance) : distance;
        previous = distance;
        have_previous = true;
    }
    return area;
}

const BranchResult& branch_for(
    const std::vector<BranchResult>& branches, BranchPolicy policy) {
    const auto found = std::find_if(
        branches.begin(), branches.end(), [policy](const BranchResult& branch) {
            return branch.policy == policy;
        });
    if (found == branches.end()) throw std::runtime_error("missing branch policy");
    return *found;
}

double minimum_area(const std::vector<BranchResult>& branches,
                    int branch_at_ms, int horizon_ms) {
    double result = std::numeric_limits<double>::infinity();
    for (const BranchResult& branch : branches) {
        result = std::min(result, area_at(branch, branch_at_ms, horizon_ms));
    }
    return result;
}

Vec2d estimated_observation_velocity(
    const ReplayReference& reference, int branch_at_ms) {
    const SimulationTraceFrame* previous = nullptr;
    const SimulationTraceFrame* latest = nullptr;
    for (const SimulationTraceFrame& frame : reference.trace) {
        if (frame.absolute_ms > branch_at_ms) break;
        if (!frame.target_active || !frame.input.fresh_vision) continue;
        previous = latest;
        latest = &frame;
    }
    if (!previous || !latest || latest->absolute_ms == previous->absolute_ms) {
        return {};
    }
    const double inverse_seconds = 1000.0 /
        static_cast<double>(latest->absolute_ms - previous->absolute_ms);
    return {
        (latest->input.observed_error_px.x -
         previous->input.observed_error_px.x) * inverse_seconds,
        (latest->input.observed_error_px.y -
         previous->input.observed_error_px.y) * inverse_seconds,
    };
}

double predicted_causal_cost(const ReplayReference& reference,
                             const BranchResult& branch,
                             int branch_at_ms) {
    if (branch_at_ms < 0 ||
        branch_at_ms >= static_cast<int>(reference.trace.size()) ||
        branch_at_ms >= static_cast<int>(branch.trace.size())) {
        return std::numeric_limits<double>::infinity();
    }
    const SimulationTraceFrame& current = reference.trace[branch_at_ms];
    const SimulationTraceFrame& candidate = branch.trace[branch_at_ms];
    Vec2d predicted = current.input.observed_error_px;
    const Vec2d velocity = estimated_observation_velocity(reference, branch_at_ms);
    double cost = 0.0;
    for (int ms = 0; ms < 160; ++ms) {
        predicted.x += velocity.x * 0.001;
        predicted.y += velocity.y * 0.001;
        const double response =
            reference.script.config.camera_response_px_per_stick_second *
            aim_slowdown_multiplier(
                std::hypot(predicted.x, predicted.y),
                reference.script.config);
        predicted.x -= candidate.output.final_stick.x * response * 0.001;
        predicted.y += candidate.output.final_stick.y * response * 0.001;
        cost += std::hypot(predicted.x, predicted.y);
    }
    return cost;
}

BranchResult select_causal(const ReplayReference& reference,
                           const std::vector<BranchResult>& branches,
                           int branch_at_ms) {
    const BranchResult* best = nullptr;
    double best_cost = std::numeric_limits<double>::infinity();
    double best_magnitude = std::numeric_limits<double>::infinity();
    for (const BranchResult& branch : branches) {
        const double cost = predicted_causal_cost(reference, branch, branch_at_ms);
        const Vec2d stick = branch.trace[branch_at_ms].output.final_stick;
        const double stick_magnitude = std::hypot(stick.x, stick.y);
        if (!best || cost < best_cost - 1e-9 ||
            (near(cost, best_cost) && stick_magnitude < best_magnitude - 1e-9) ||
            (near(cost, best_cost) && near(stick_magnitude, best_magnitude) &&
             branch.policy < best->policy)) {
            best = &branch;
            best_cost = cost;
            best_magnitude = stick_magnitude;
        }
    }
    if (!best) throw std::runtime_error("causal oracle has no candidates");
    return *best;
}

BranchResult select_hindsight(const std::vector<BranchResult>& branches) {
    const auto best = std::min_element(
        branches.begin(), branches.end(),
        [](const BranchResult& left, const BranchResult& right) {
            if (!near(left.error_area_px_ms, right.error_area_px_ms)) {
                return left.error_area_px_ms < right.error_area_px_ms;
            }
            return left.policy < right.policy;
        });
    if (best == branches.end()) {
        throw std::runtime_error("hindsight oracle has no candidates");
    }
    return *best;
}

double immediate_progress(const ReplayReference& reference,
                          const BranchResult& actual,
                          int branch_at_ms) {
    const SimulationTraceFrame& frame = actual.trace[branch_at_ms];
    const double distance = std::hypot(
        frame.true_error_before_px.x, frame.true_error_before_px.y);
    if (distance <= 1e-9) return 0.0;
    const double response =
        reference.script.config.camera_response_px_per_stick_second *
        aim_slowdown_multiplier(distance, reference.script.config);
    const Vec2d control{
        frame.output.final_stick.x, -frame.output.final_stick.y};
    return response * 0.001 *
        (control.x * frame.true_error_before_px.x +
         control.y * frame.true_error_before_px.y) / distance;
}

}  // namespace

ReplayReference record_reference(
    const ScenarioScript& script,
    ManualProfile manual_profile,
    BenchmarkCohort cohort,
    const ReplayControllerFactory& factory) {
    if (!factory) throw std::invalid_argument("replay factory is required");
    ReplayReference result;
    result.script = script;
    result.manual_profile = manual_profile;
    result.cohort = cohort;
    const BranchSchedule inactive{};
    ControllerStep controller = factory(inactive);
    result.benchmark_result = run_simulation(
        script, manual_profile, std::move(controller), cohort,
        [&](const SimulationTraceFrame& frame) { result.trace.push_back(frame); });
    if (result.trace.size() != static_cast<std::size_t>(script.config.duration_ms)) {
        throw std::runtime_error("reference trace duration mismatch");
    }
    return result;
}

BranchResult replay_branch(
    const ReplayReference& reference,
    const ReplayRequest& request,
    const ReplayControllerFactory& factory) {
    if (!factory) throw std::invalid_argument("replay factory is required");
    if (request.branch_at_ms < 0 || request.substitution_ms <= 0 ||
        request.horizon_ms <= 0 ||
        request.branch_at_ms >= reference.script.config.duration_ms) {
        throw std::invalid_argument("invalid replay window");
    }
    BranchResult result;
    result.policy = request.policy;
    const BranchSchedule schedule{
        request.branch_at_ms, request.substitution_ms, request.policy};
    ControllerStep controller = factory(schedule);
    ScenarioScript replay_script = reference.script;
    replay_script.config.duration_ms = std::min(
        reference.script.config.duration_ms,
        request.branch_at_ms + request.horizon_ms);
    (void)run_simulation(
        replay_script, reference.manual_profile, std::move(controller),
        reference.cohort,
        [&](const SimulationTraceFrame& frame) { result.trace.push_back(frame); });
    if (result.trace.size() !=
        static_cast<std::size_t>(replay_script.config.duration_ms)) {
        throw std::runtime_error("branch trace duration mismatch");
    }

    result.prebranch_verified = true;
    for (std::size_t index = 0; index < result.trace.size(); ++index) {
        if (same_frame(reference.trace[index], result.trace[index])) continue;
        result.first_divergence_ms = result.trace[index].absolute_ms;
        if (result.trace[index].absolute_ms < request.branch_at_ms) {
            result.prebranch_verified = false;
            throw std::runtime_error("replay diverged before branch point");
        }
        break;
    }
    summarize_branch(reference, request, result);
    return result;
}

CounterfactualEpisode analyze_episode(
    const ReplayReference& reference,
    int branch_at_ms,
    const ReplayControllerFactory& factory,
    const AnalysisBudget& budget) {
    if (branch_at_ms < 0 ||
        branch_at_ms >= reference.script.config.duration_ms ||
        budget.substitution_ms <= 0 || budget.stable_horizon_ms < 160) {
        throw std::invalid_argument("invalid counterfactual analysis budget");
    }
    constexpr std::array policies{
        BranchPolicy::ActualMix,
        BranchPolicy::Neutral,
        BranchPolicy::ManualOnly,
        BranchPolicy::AiOnly,
        BranchPolicy::ManualPlusAi25,
        BranchPolicy::ManualPlusAi50,
        BranchPolicy::ManualPlusAi75,
    };

    CounterfactualEpisode result;
    result.branch_at_ms = branch_at_ms;
    result.candidates.reserve(policies.size());
    for (const BranchPolicy policy : policies) {
        ReplayRequest request;
        request.branch_at_ms = branch_at_ms;
        request.substitution_ms = budget.substitution_ms;
        request.horizon_ms = budget.stable_horizon_ms;
        request.policy = policy;
        result.candidates.push_back(replay_branch(reference, request, factory));
    }

    result.causal_oracle = select_causal(
        reference, result.candidates, branch_at_ms);
    result.hindsight_oracle = select_hindsight(result.candidates);
    const BranchResult& actual = branch_for(
        result.candidates, BranchPolicy::ActualMix);
    const BranchResult& manual = branch_for(
        result.candidates, BranchPolicy::ManualOnly);
    const BranchResult& ai = branch_for(
        result.candidates, BranchPolicy::AiOnly);

    const double actual_40 = area_at(actual, branch_at_ms, 40);
    const double actual_80 = area_at(actual, branch_at_ms, 80);
    const double actual_160 = area_at(actual, branch_at_ms, 160);
    const double manual_80 = area_at(manual, branch_at_ms, 80);
    const double ai_80 = area_at(ai, branch_at_ms, 80);
    const double best_40 = minimum_area(result.candidates, branch_at_ms, 40);
    const double best_80 = minimum_area(result.candidates, branch_at_ms, 80);
    const double best_160 = minimum_area(result.candidates, branch_at_ms, 160);

    result.actual.instant_progress_px = immediate_progress(
        reference, actual, branch_at_ms);
    result.actual.regret_40_px_ms = std::max(0.0, actual_40 - best_40);
    result.actual.regret_80_px_ms = std::max(0.0, actual_80 - best_80);
    result.actual.regret_160_px_ms = std::max(0.0, actual_160 - best_160);
    result.actual.future_burden_px_ms = std::max(
        0.0, actual.error_area_px_ms -
             result.hindsight_oracle.error_area_px_ms);
    if (actual.settle_ms >= 0 && result.hindsight_oracle.settle_ms >= 0) {
        result.actual.future_settle_delay_ms = std::max(
            0, actual.settle_ms - result.hindsight_oracle.settle_ms);
    } else if (actual.settle_ms < 0 && result.hindsight_oracle.settle_ms >= 0) {
        result.actual.future_settle_delay_ms =
            budget.stable_horizon_ms - result.hindsight_oracle.settle_ms;
    }
    result.actual.extra_path_px = std::max(
        0.0, actual.path_px - result.hindsight_oracle.path_px);
    result.actual.correction_reversals = actual.correction_reversals;
    result.actual.false_interrupt_ms = actual.false_interrupt_ms;
    result.actual.reacquire_delay_ms = actual.reacquire_delay_ms;
    if (manual_80 + 1e-6 < actual_80) {
        result.actual.manual_helped_but_suppressed_ms =
            std::min(80, budget.substitution_ms);
    }
    if (ai_80 + 1e-6 < actual_80) {
        result.actual.ai_helped_but_suppressed_ms =
            std::min(80, budget.substitution_ms);
    }
    if (manual_80 > best_80 * 1.01 && ai_80 > best_80 * 1.01) {
        result.actual.both_harmful_ms = std::min(80, budget.substitution_ms);
    }
    if (actual_80 > manual_80 * 1.01 && actual_80 > ai_80 * 1.01) {
        result.actual.destructive_stack_ms = std::min(80, budget.substitution_ms);
    }
    const int wrong_way_end = std::min(
        static_cast<int>(actual.trace.size()),
        branch_at_ms + budget.substitution_ms);
    for (int ms = branch_at_ms; ms < wrong_way_end; ++ms) {
        const SimulationTraceFrame& frame = actual.trace[ms];
        if (!frame.target_active) continue;
        const Vec2d control{
            frame.output.final_stick.x, -frame.output.final_stick.y};
        if (control.x * frame.true_error_before_px.x +
                control.y * frame.true_error_before_px.y < 0.0) {
            ++result.actual.wrong_way_commit_ms;
        }
    }

    const bool local_helpful = result.actual.instant_progress_px > 0.0;
    const bool global_helpful = actual.error_area_px_ms <=
        result.hindsight_oracle.error_area_px_ms * 1.01 + 1e-9;
    if (local_helpful && global_helpful) {
        result.actual.classification =
            OutcomeClass::LocalHelpfulGlobalHelpful;
    } else if (local_helpful) {
        result.actual.classification =
            OutcomeClass::LocalHelpfulGlobalHarmful;
    } else if (global_helpful) {
        result.actual.classification =
            OutcomeClass::LocalHarmfulGlobalHelpful;
    } else {
        result.actual.classification =
            OutcomeClass::LocalHarmfulGlobalHarmful;
    }
    return result;
}

const char* to_string(BranchPolicy policy) noexcept {
    switch (policy) {
    case BranchPolicy::ActualMix: return "actual_mix";
    case BranchPolicy::Neutral: return "neutral";
    case BranchPolicy::ManualOnly: return "manual_only";
    case BranchPolicy::AiOnly: return "ai_only";
    case BranchPolicy::ManualPlusAi25: return "manual_plus_ai_25";
    case BranchPolicy::ManualPlusAi50: return "manual_plus_ai_50";
    case BranchPolicy::ManualPlusAi75: return "manual_plus_ai_75";
    }
    return "unknown";
}

const char* to_string(OutcomeClass outcome) noexcept {
    switch (outcome) {
    case OutcomeClass::LocalHelpfulGlobalHelpful:
        return "local_helpful_global_helpful";
    case OutcomeClass::LocalHelpfulGlobalHarmful:
        return "local_helpful_global_harmful";
    case OutcomeClass::LocalHarmfulGlobalHelpful:
        return "local_harmful_global_helpful";
    case OutcomeClass::LocalHarmfulGlobalHarmful:
        return "local_harmful_global_harmful";
    }
    return "unknown";
}

}  // namespace controller_native::sustained_aimlab
