#include "aim_response_curve_plugin.h"
#include "assist_control_state_machine.h"
#include "bodylock_follow_controller.h"
#include "incident_fixture_support.h"
#include "test_support/native_test_registry.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using controller_native::AssistControlPhase;
using controller_native::AssistControlStateMachine;
using controller_native::AssistControlStateMachineInput;
using controller_native::BodylockFollowController;
using controller_native::BodylockFollowControllerConfig;
using controller_native::BodylockFollowControllerOutput;

constexpr const char* kIncidentId =
    "bodylock-position-motion-axis-conflict-20260817";
constexpr float kMinimumManualAcknowledgement = 0.25f;

enum class Axis { X, Y };

const char* axis_name(Axis axis) noexcept {
    return axis == Axis::X ? "x" : "y";
}

float axis_value(pipeline_contract::Vec2f value, Axis axis) noexcept {
    return axis == Axis::X ? value.x : value.y;
}

pipeline_contract::Vec2f primary_manual(Axis axis) noexcept {
    // The controlled component matches the observed incidents. The dominant
    // orthogonal component reproduces the vector mask without making that
    // orthogonal work wrong-direction.
    return axis == Axis::X
        ? pipeline_contract::Vec2f{-0.12f, 0.35f}
        : pipeline_contract::Vec2f{-0.35f, 0.12f};
}

pipeline_contract::TargetPlan incident_plan(
    Axis axis,
    bool opposing_axis_motion) noexcept {
    pipeline_contract::TargetPlan plan;
    plan.target_id = 81701;
    plan.selector_target_generation = 817;
    plan.mode = pipeline_contract::ControlMode::BodyLockFollow;
    plan.lifecycle = pipeline_contract::TargetLifecycle::Observed;
    plan.direct_person_observation = true;
    plan.aim_authority = 1.0f;
    plan.visual_authority = 1.0f;
    plan.reliability = 1.0f;
    plan.response_scale = 500.0f;
    plan.response_confidence = 1.0f;
    if (axis == Axis::X) {
        plan.error_px = {-7.5f, -18.5f};
        plan.error_rate_px_per_sec = {
            opposing_axis_motion ? 190.0f : -190.0f,
            -250.0f};
    } else {
        // Exercise an upward target correction. Down-stick has a separate
        // recoil/user-authority contract and is intentionally not evidence for
        // this position/motion arbitration incident.
        plan.error_px = {-18.5f, -7.5f};
        plan.error_rate_px_per_sec = {
            -250.0f,
            opposing_axis_motion ? 190.0f : -190.0f};
    }
    return plan;
}

BodylockFollowController make_bodylock_controller() {
    BodylockFollowControllerConfig config;
    config.max_force_x = 0.60f;
    config.max_force_y = 0.66f;
    config.feedback_range_x_px = 32.0f;
    config.feedback_range_y_px = 32.0f;
    config.feedforward_gain = 0.72f;
    config.response_curve.algorithm =
        controller_native::AimResponseCurveAlgorithm::Linear;
    return BodylockFollowController(config);
}

AssistControlStateMachineInput arbitration_input(
    const pipeline_contract::TargetPlan& plan,
    pipeline_contract::Vec2f manual,
    pipeline_contract::Vec2f ai) noexcept {
    AssistControlStateMachineInput input;
    input.aiming = true;
    input.target_authoritative = true;
    input.fresh_observation = true;
    input.cue_continuation = false;
    input.target_id = plan.target_id;
    input.selector_target_generation = plan.selector_target_generation;
    input.now_seconds = 100.0;
    input.target_error_px = plan.error_px;
    input.mode = pipeline_contract::ControlMode::BodyLockFollow;
    input.visual_authority = 1.0f;
    input.manual_stick = manual;
    input.centered_manual_stick = manual;
    input.centered_manual_available = true;
    input.filtered_manual_stick = manual;
    input.ai_stick = ai;
    return input;
}

struct AxisResult {
    std::string axis;
    pipeline_contract::Vec2f error_px{};
    pipeline_contract::Vec2f error_rate_px_per_sec{};
    pipeline_contract::Vec2f position{};
    pipeline_contract::Vec2f raw_motion{};
    pipeline_contract::Vec2f effective_motion{};
    pipeline_contract::Vec2f requested_ai{};
    pipeline_contract::Vec2f manual{};
    pipeline_contract::Vec2f final_output{};
    float position_motion_dot = 0.0f;
    float manual_acknowledgement = 0.0f;
    bool trigger_executed = false;
    bool final_reversed_manual = false;
    bool aligned_motion_counterfactual = false;
    bool wrong_coupled_manual_counterfactual = false;
    bool manual_exit_counterfactual = false;
};

AxisResult run_axis(Axis axis) {
    const auto controller = make_bodylock_controller();
    const auto plan = incident_plan(axis, true);
    const BodylockFollowControllerOutput bodylock =
        controller.compute_detailed(plan, {}, 0.001f);
    const auto manual = primary_manual(axis);

    AssistControlStateMachine machine;
    const auto final = machine.update(arbitration_input(
        plan, manual, bodylock.stick));

    const float position_axis = axis_value(bodylock.position_stick, axis);
    const float motion_axis = axis_value(bodylock.motion_stick, axis);
    const float manual_axis = axis_value(manual, axis);
    const float final_axis = axis_value(final.stick, axis);

    AxisResult result;
    result.axis = axis_name(axis);
    result.error_px = plan.error_px;
    result.error_rate_px_per_sec = plan.error_rate_px_per_sec;
    result.position = bodylock.position_stick;
    result.raw_motion = bodylock.motion_stick;
    result.effective_motion = bodylock.effective_motion_stick;
    result.requested_ai = bodylock.stick;
    result.manual = manual;
    result.final_output = final.stick;
    result.position_motion_dot =
        bodylock.position_stick.x * bodylock.motion_stick.x +
        bodylock.position_stick.y * bodylock.motion_stick.y;
    result.manual_acknowledgement = std::fabs(manual_axis) > 1.0e-6f
        ? final_axis * manual_axis / (manual_axis * manual_axis)
        : 0.0f;
    result.final_reversed_manual = final_axis * manual_axis < 0.0f;
    result.trigger_executed =
        final.phase == AssistControlPhase::Track &&
        position_axis * motion_axis < 0.0f &&
        result.position_motion_dot > 0.0f &&
        manual_axis * position_axis > 0.0f;

    // Counterfactual 1: change only the controlled motion sign. Position,
    // manual, orthogonal motion and every authority field remain fixed.
    const auto aligned_plan = incident_plan(axis, false);
    const auto aligned_bodylock = controller.compute_detailed(
        aligned_plan, {}, 0.001f);
    AssistControlStateMachine aligned_machine;
    const auto aligned_final = aligned_machine.update(arbitration_input(
        aligned_plan, manual, aligned_bodylock.stick));
    result.aligned_motion_counterfactual =
        axis_value(aligned_bodylock.stick, axis) * position_axis > 0.0f &&
        axis_value(aligned_final.stick, axis) * position_axis > 0.0f;

    // Counterfactual 2: with position-aligned AI, a minor coupled manual axis
    // deliberately points away from the target. Target-first coupling must
    // still suppress that wrong component; this prevents a manual-first fix.
    auto wrong_plan = aligned_plan;
    if (axis == Axis::Y) {
        // Mirror only this negative control so the wrong manual proposal is
        // up-stick. Down-stick is deliberately protected by the recoil/user
        // authority contract and cannot prove target-first arbitration.
        wrong_plan.error_px.y = 7.5f;
        wrong_plan.error_rate_px_per_sec.y = 190.0f;
    }
    const auto wrong_bodylock = controller.compute_detailed(
        wrong_plan, {}, 0.001f);
    const float wrong_position_axis = axis_value(
        wrong_bodylock.position_stick, axis);
    auto wrong_manual = manual;
    if (axis == Axis::X) {
        wrong_manual.x = 0.05f;
    } else {
        wrong_manual.y = 0.05f;
    }
    AssistControlStateMachine wrong_machine;
    const auto wrong_final = wrong_machine.update(arbitration_input(
        wrong_plan, wrong_manual, wrong_bodylock.stick));
    result.wrong_coupled_manual_counterfactual =
        axis_value(wrong_manual, axis) * wrong_position_axis < 0.0f &&
        axis_value(wrong_final.stick, axis) * wrong_position_axis > 0.0f;

    // Counterfactual 3: explicit exit remains a lifecycle event and passes the
    // physical stick through; the position constraint must not pin ownership.
    AssistControlStateMachine exit_machine;
    auto exit_input = arbitration_input(plan, manual, bodylock.stick);
    exit_input.manual_exit_requested = true;
    const auto exit = exit_machine.update(exit_input);
    result.manual_exit_counterfactual =
        exit.phase == AssistControlPhase::HandoverSeek &&
        exit.handover_requested &&
        std::fabs(exit.stick.x - manual.x) <= 1.0e-6f &&
        std::fabs(exit.stick.y - manual.y) <= 1.0e-6f;
    return result;
}

void write_vec(std::ostream& output, pipeline_contract::Vec2f value) {
    output << '[' << value.x << ',' << value.y << ']';
}

void write_axis(std::ostream& output, const AxisResult& value) {
    output << "{\"axis\":\"" << value.axis << "\",\"error_px\":";
    write_vec(output, value.error_px);
    output << ",\"error_rate_px_per_sec\":";
    write_vec(output, value.error_rate_px_per_sec);
    output << ",\"position_stick\":";
    write_vec(output, value.position);
    output << ",\"raw_motion_stick\":";
    write_vec(output, value.raw_motion);
    output << ",\"effective_motion_stick\":";
    write_vec(output, value.effective_motion);
    output << ",\"requested_ai\":";
    write_vec(output, value.requested_ai);
    output << ",\"manual\":";
    write_vec(output, value.manual);
    output << ",\"final_output\":";
    write_vec(output, value.final_output);
    output << ",\"position_motion_dot\":" << value.position_motion_dot
           << ",\"manual_acknowledgement\":"
           << value.manual_acknowledgement
           << ",\"trigger_executed\":" << value.trigger_executed
           << ",\"final_reversed_manual\":"
           << value.final_reversed_manual
           << ",\"aligned_motion_counterfactual\":"
           << value.aligned_motion_counterfactual
           << ",\"wrong_coupled_manual_counterfactual\":"
           << value.wrong_coupled_manual_counterfactual
           << ",\"manual_exit_counterfactual\":"
           << value.manual_exit_counterfactual << '}';
}

}  // namespace

int run_bodylock_position_motion_conflict_incident_regression(int argc, char** argv) {
    try {
        const auto output_path =
            controller_native::incident_fixture::output_path_from_args(
                argc, argv);
        const std::vector<AxisResult> axes{
            run_axis(Axis::X),
            run_axis(Axis::Y),
        };
        const bool trigger_executed = std::all_of(
            axes.begin(), axes.end(),
            [](const AxisResult& value) { return value.trigger_executed; });
        const int reversal_count = static_cast<int>(std::count_if(
            axes.begin(), axes.end(), [](const AxisResult& value) {
                return value.final_reversed_manual;
            }));
        const float minimum_acknowledgement = std::min(
            axes[0].manual_acknowledgement,
            axes[1].manual_acknowledgement);
        const int aligned_motion_count = static_cast<int>(std::count_if(
            axes.begin(), axes.end(), [](const AxisResult& value) {
                return value.aligned_motion_counterfactual;
            }));
        const int wrong_manual_count = static_cast<int>(std::count_if(
            axes.begin(), axes.end(), [](const AxisResult& value) {
                return value.wrong_coupled_manual_counterfactual;
            }));
        const int exit_count = static_cast<int>(std::count_if(
            axes.begin(), axes.end(), [](const AxisResult& value) {
                return value.manual_exit_counterfactual;
            }));
        const bool counterfactuals_valid = aligned_motion_count == 2 &&
            wrong_manual_count == 2 && exit_count == 2;
        const bool pass = trigger_executed && reversal_count == 0 &&
            minimum_acknowledgement >= kMinimumManualAcknowledgement &&
            counterfactuals_valid;

        auto report = controller_native::incident_fixture::open_report(
            output_path);
        report << std::boolalpha << std::fixed << std::setprecision(6)
               << "{\n  \"schema_version\": 1,\n"
               << "  \"incident_id\": \"" << kIncidentId << "\",\n"
               << "  \"symptom\": \"orthogonal motion masks an axis that moves away from current BodyLock geometry and suppresses aligned manual correction\",\n"
               << "  \"trigger_executed\": " << trigger_executed
               << ",\n  \"axes\": [\n    ";
        write_axis(report, axes[0]);
        report << ",\n    ";
        write_axis(report, axes[1]);
        report << "\n  ],\n  \"metrics\": {"
               << "\"reversal_count\":" << reversal_count << ','
               << "\"minimum_manual_acknowledgement\":"
               << minimum_acknowledgement << ','
               << "\"aligned_motion_counterfactual_count\":"
               << aligned_motion_count << ','
               << "\"wrong_coupled_manual_counterfactual_count\":"
               << wrong_manual_count << ','
               << "\"manual_exit_counterfactual_count\":" << exit_count
               << "},\n  \"oracles\": [\n"
               << "    {\"id\":\"O1\",\"metric\":\"reversal_count\",\"operator\":\"==\",\"threshold\":0,\"observed\":"
               << reversal_count << ",\"pass\":"
               << (reversal_count == 0) << "},\n"
               << "    {\"id\":\"O2\",\"metric\":\"minimum_manual_acknowledgement\",\"operator\":\">=\",\"threshold\":"
               << kMinimumManualAcknowledgement << ",\"observed\":"
               << minimum_acknowledgement << ",\"pass\":"
               << (minimum_acknowledgement >=
                   kMinimumManualAcknowledgement) << "},\n"
               << "    {\"id\":\"O3\",\"metric\":\"aligned_motion_counterfactual_count\",\"operator\":\"==\",\"threshold\":2,\"observed\":"
               << aligned_motion_count << ",\"pass\":"
               << (aligned_motion_count == 2) << "},\n"
               << "    {\"id\":\"O4\",\"metric\":\"wrong_coupled_manual_counterfactual_count\",\"operator\":\"==\",\"threshold\":2,\"observed\":"
               << wrong_manual_count << ",\"pass\":"
               << (wrong_manual_count == 2) << "},\n"
               << "    {\"id\":\"O5\",\"metric\":\"manual_exit_counterfactual_count\",\"operator\":\"==\",\"threshold\":2,\"observed\":"
               << exit_count << ",\"pass\":" << (exit_count == 2)
               << "}\n  ],\n  \"overall_pass\": " << pass << "\n}\n";
        report.close();

        std::cout << "[BodylockPositionMotionConflictIncident] trigger="
                  << (trigger_executed ? 1 : 0)
                  << " reversals=" << reversal_count
                  << " min_ack=" << minimum_acknowledgement
                  << " counterfactuals="
                  << (counterfactuals_valid ? 1 : 0)
                  << " result=" << (pass ? "GREEN" : "RED") << '\n';
        if (!trigger_executed || !counterfactuals_valid) return 3;
        return pass ? 0 : 1;
    } catch (const std::invalid_argument& error) {
        std::cerr << "[BodylockPositionMotionConflictIncident][USAGE] "
                  << error.what() << '\n';
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "[BodylockPositionMotionConflictIncident][ERROR] "
                  << error.what() << '\n';
        return 3;
    }
}

void register_bodylock_position_motion_conflict_incident_regression(native_test::Registry& registry) {
    registry.add_incident_entry("BaseBodyLock", "incident_bodylock_position_motion_conflict", "bodylock_position_motion_conflict_incident.json", run_bodylock_position_motion_conflict_incident_regression);
}
