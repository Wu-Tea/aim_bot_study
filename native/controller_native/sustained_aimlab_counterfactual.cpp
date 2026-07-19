#include "sustained_aimlab_counterfactual.h"

#include <algorithm>
#include <cmath>
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
    int stable_ticks = 0;
    for (const SimulationTraceFrame& frame : result.trace) {
        if (frame.absolute_ms < request.branch_at_ms ||
            frame.absolute_ms >= end_ms || !frame.target_active) {
            continue;
        }
        const double distance = frame_distance(frame);
        result.error_area_px_ms += distance;
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
        if (distance < reference.script.config.target_radius_px) {
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
    }
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
    (void)run_simulation(
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
    (void)run_simulation(
        reference.script, reference.manual_profile, std::move(controller),
        reference.cohort,
        [&](const SimulationTraceFrame& frame) { result.trace.push_back(frame); });
    if (result.trace.size() != reference.trace.size()) {
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

const char* to_string(BranchPolicy policy) noexcept {
    switch (policy) {
    case BranchPolicy::ActualMix: return "actual_mix";
    case BranchPolicy::ManualOnly: return "manual_only";
    case BranchPolicy::AiOnly: return "ai_only";
    case BranchPolicy::ManualPlusAi25: return "manual_plus_ai_25";
    case BranchPolicy::ManualPlusAi50: return "manual_plus_ai_50";
    case BranchPolicy::ManualPlusAi75: return "manual_plus_ai_75";
    }
    return "unknown";
}

}  // namespace controller_native::sustained_aimlab
