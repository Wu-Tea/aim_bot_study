#include "incident_fixture_support.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using controller_native::ControllerVisionSnapshot;
using controller_native::GamepadRuntimeConfig;
using controller_native::NativeGamepadController;
using controller_native::PhysicalGamepadState;

constexpr float kCenterX = 320.0f;
constexpr float kCenterY = 256.0f;
constexpr float kBodyHeight = 140.0f;
constexpr float kBodyWidth = 56.0f;
constexpr float kAimHeightRatio = 0.365f;

controller_native::incident_fixture::TargetSpec target_spec(
    std::uint64_t observation_id,
    std::uint64_t selector_generation,
    bool fire_authority = true) {
    return {
        kCenterX,
        kCenterY,
        kBodyWidth,
        kBodyHeight,
        kAimHeightRatio,
        observation_id,
        selector_generation,
        true,
        false,
        fire_authority,
    };
}

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void require_near(float actual, float expected, float tolerance,
                  const std::string& message) {
    if (!std::isfinite(actual) || std::fabs(actual - expected) > tolerance) {
        throw std::runtime_error(message);
    }
}

GamepadRuntimeConfig current_config() {
    return controller_native::incident_fixture::base_config(50.0f, 180.0f);
}

PhysicalGamepadState physical_input(
    float right_x = 0.0f,
    float right_y = 0.0f,
    bool aiming = true) {
    auto physical = controller_native::incident_fixture::ads_input(
        right_x, right_y);
    physical.left_trigger = aiming ? 1.0f : 0.0f;
    return physical;
}

pipeline_contract::VisionCandidateSnapshot candidate(
    std::uint64_t id,
    float dx,
    float dy) {
    return controller_native::incident_fixture::observed_candidate(
        target_spec(id, 0), dx, dy);
}

ControllerVisionSnapshot observed_snapshot(
    std::uint64_t frame_id,
    double now,
    float dx,
    float dy,
    std::uint64_t observation_id,
    std::uint64_t selector_generation,
    bool selector_changed = false,
    bool fire_requested = false) {
    auto snapshot = controller_native::incident_fixture::observed_snapshot(
        target_spec(observation_id, selector_generation),
        frame_id,
        now,
        dx,
        dy,
        selector_changed);
    snapshot.state.auto_fire_requested = fire_requested;
    return snapshot;
}

void add_alternative(
    ControllerVisionSnapshot& snapshot,
    std::uint64_t observation_id,
    float dx,
    float dy = 0.0f) {
    snapshot.candidates.push_back(candidate(observation_id, dx, dy));
}

ControllerVisionSnapshot empty_snapshot(std::uint64_t frame_id, double now) {
    return controller_native::incident_fixture::empty_snapshot(
        target_spec(0, 0, false), frame_id, now);
}

ControllerVisionSnapshot cue_snapshot(
    std::uint64_t frame_id,
    double now,
    float dx,
    float dy,
    std::uint64_t selector_generation) {
    return controller_native::incident_fixture::cue_snapshot(
        target_spec(0, selector_generation, false),
        frame_id,
        now,
        dx,
        dy);
}

void require_finite_unit_output(
    const controller_native::GamepadOutputState& output,
    const std::string& context) {
    require(std::isfinite(output.right_x) && std::isfinite(output.right_y),
            context + ": non-finite right stick");
    require(std::fabs(output.right_x) <= 1.0f + 1.0e-6f &&
                std::fabs(output.right_y) <= 1.0f + 1.0e-6f,
            context + ": right stick exceeded unit bounds");
}

void test_no_target_is_physical_passthrough() {
    double now = 10.0;
    NativeGamepadController controller(current_config(), [&now] { return now; });
    const auto physical = physical_input(0.42f, -0.31f);
    const auto output = controller.build_output(physical);

    require_near(output.right_x, physical.right_x, 1.0e-6f,
                 "no target changed physical X");
    require_near(output.right_y, physical.right_y, 1.0e-6f,
                 "no target changed physical Y");
    require(controller.last_target_plan().target_id == 0,
            "no-target tick created target ownership");
    const auto& components = controller.last_output_components();
    require(components.assist_control_phase == "manual" &&
                components.manual_passthrough_x &&
                components.manual_passthrough_y,
            "no-target tick did not stay in manual passthrough");
}

void test_centered_motion_demand_retains_ai_authority() {
    controller_native::AssistControlStateMachine state_machine;
    controller_native::AssistControlStateMachineInput input;
    input.aiming = true;
    input.target_authoritative = true;
    input.fresh_observation = true;
    input.target_id = 1001;
    input.selector_target_generation = 61;
    input.now_seconds = 15.0;
    input.target_error_px = {8.0f, -8.0f};
    input.ai_stick = {0.15f, -0.12f};

    const auto approach = state_machine.update(input);
    const bool trigger_established =
        !approach.manual_passthrough_x &&
        !approach.manual_passthrough_y &&
        std::fabs(approach.stick.x) > 1.0e-4f &&
        std::fabs(approach.stick.y) > 1.0e-4f;

    // A centered moving target still has a non-zero shaped motion proposal.
    // This models left-stick strafing with no physical right-stick input.
    input.now_seconds += 0.005;
    input.target_error_px = {0.25f, -0.25f};
    input.manual_stick = {};
    input.ai_stick = {0.08f, -0.06f};
    const auto centered_motion = state_machine.update(input);

    const int retained_motion_axes =
        (!centered_motion.manual_passthrough_x &&
         std::fabs(centered_motion.stick.x - input.ai_stick.x) <= 1.0e-6f
            ? 1 : 0) +
        (!centered_motion.manual_passthrough_y &&
         std::fabs(centered_motion.stick.y - input.ai_stick.y) <= 1.0e-6f
            ? 1 : 0);

    // Counterfactual: once the shaped AI proposal is genuinely idle, centered
    // tracking must still return both axes to physical manual input.
    input.now_seconds += 0.005;
    input.manual_stick = {0.12f, -0.14f};
    input.ai_stick = {};
    const auto centered_idle = state_machine.update(input);
    const bool idle_counterfactual =
        centered_idle.manual_passthrough_x &&
        centered_idle.manual_passthrough_y &&
        std::fabs(centered_idle.stick.x - input.manual_stick.x) <= 1.0e-6f &&
        std::fabs(centered_idle.stick.y - input.manual_stick.y) <= 1.0e-6f;

    std::cout << "[CenteredMotionAuthority] trigger="
              << (trigger_established ? 1 : 0)
              << " retained_motion_axes=" << retained_motion_axes
              << " centered_output=(" << centered_motion.stick.x << ","
              << centered_motion.stick.y << ") idle_counterfactual="
              << (idle_counterfactual ? 1 : 0) << "\n";

    if (!trigger_established || retained_motion_axes != 2 ||
        !idle_counterfactual) {
        throw std::runtime_error(
            "centered-motion continuity failed: trigger=" +
            std::to_string(trigger_established ? 1 : 0) +
            " retained_motion_axes=" +
            std::to_string(retained_motion_axes) +
            " centered_output=(" +
            std::to_string(centered_motion.stick.x) + "," +
            std::to_string(centered_motion.stick.y) + ") passthrough=(" +
            std::to_string(centered_motion.manual_passthrough_x ? 1 : 0) +
            "," +
            std::to_string(centered_motion.manual_passthrough_y ? 1 : 0) +
            ") idle_counterfactual=" +
            std::to_string(idle_counterfactual ? 1 : 0));
    }
}

void test_valid_point_correction_is_interpreted_not_passthrough() {
    controller_native::AssistControlStateMachine state_machine;
    controller_native::AssistControlStateMachineInput input;
    input.aiming = true;
    input.target_authoritative = true;
    input.fresh_observation = true;
    input.target_id = 1002;
    input.selector_target_generation = 62;
    input.now_seconds = 16.0;
    input.target_error_px = {0.0f, 12.0f};
    input.manual_stick = {0.0f, -0.70f};
    input.filtered_manual_stick = input.manual_stick;
    input.ai_stick = {0.0f, -0.20f};
    input.manual_correction_y = true;

    const auto corrected = state_machine.update(input);
    require_near(
        corrected.stick.y,
        input.manual_stick.y,
        1.0e-6f,
        "stronger aligned D correction was reduced by the AI proposal");
    require(corrected.manual_correction_y,
            "valid D correction lost its semantic authority tag");
    require(corrected.manual_passthrough_y,
            "a stronger native correction should need no AI residual");
}

void test_track_uses_ai_as_total_fill_but_opposition_is_native() {
    controller_native::AssistControlStateMachine state_machine;
    controller_native::AssistControlStateMachineInput input;
    input.aiming = true;
    input.target_authoritative = true;
    input.fresh_observation = true;
    input.target_id = 1003;
    input.selector_target_generation = 63;
    input.now_seconds = 17.0;
    input.target_error_px = {60.0f, 0.0f};
    input.manual_stick = {0.05f, 0.0f};
    input.filtered_manual_stick = input.manual_stick;
    input.ai_stick = {0.40f, 0.0f};
    input.manual_correction_x = true;

    const auto aligned = state_machine.update(input);
    require_near(aligned.stick.x, 0.40f, 1.0e-6f,
                 "aligned micro input removed the AI fill");
    require(aligned.manual_correction_x && !aligned.manual_passthrough_x,
            "aligned micro input lost its D-correction semantics");

    input.now_seconds += 0.005;
    input.manual_stick.x = -0.05f;
    input.filtered_manual_stick.x = -0.05f;
    const auto opposing = state_machine.update(input);
    require_near(opposing.stick.x, -0.05f, 1.0e-6f,
                 "opposing micro input did not receive native authority");

    input.now_seconds += 0.005;
    input.manual_stick.x = 0.55f;
    input.filtered_manual_stick.x = 0.55f;
    const auto stronger_manual = state_machine.update(input);
    require_near(stronger_manual.stick.x, 0.55f, 1.0e-6f,
                 "stronger aligned manual input was reduced to the AI proposal");
}

void test_track_uses_one_two_dimensional_cooperation_decision() {
    controller_native::AssistControlStateMachine state_machine;
    controller_native::AssistControlStateMachineInput input;
    input.aiming = true;
    input.target_authoritative = true;
    input.fresh_observation = true;
    input.target_id = 1004;
    input.selector_target_generation = 64;
    input.now_seconds = 17.25;
    input.target_error_px = {60.0f, 20.0f};
    input.manual_stick = {0.20f, -0.03f};
    input.filtered_manual_stick = input.manual_stick;
    input.ai_stick = {0.35f, 0.10f};
    input.manual_correction_x = true;
    input.manual_correction_y = true;

    const auto diagonal = state_machine.update(input);
    const float full_dot =
        input.filtered_manual_stick.x * input.ai_stick.x +
        input.filtered_manual_stick.y * input.ai_stick.y;
    require(full_dot > 0.0f &&
                input.filtered_manual_stick.y * input.ai_stick.y < 0.0f,
            "diagonal fixture did not exercise one opposite component");
    require(diagonal.stick.x > input.manual_stick.x + 0.05f &&
                diagonal.stick.y > input.manual_stick.y + 0.01f,
            "one opposite component unloaded a cooperative 2-D gesture");

    input.now_seconds += 0.005;
    input.manual_stick = {-0.20f, -0.10f};
    input.filtered_manual_stick = input.manual_stick;
    const auto opposing = state_machine.update(input);
    require_near(opposing.stick.x, input.manual_stick.x, 1.0e-6f,
                 "2-D opposition did not preserve native X");
    require_near(opposing.stick.y, input.manual_stick.y, 1.0e-6f,
                 "2-D opposition did not preserve native Y");
}

void test_capture_brakes_alignment_but_allows_opposing_escape() {
    controller_native::AssistControlStateMachine state_machine;
    controller_native::AssistControlStateMachineInput input;
    input.aiming = true;
    input.target_authoritative = true;
    input.fresh_observation = true;
    input.target_id = 1101;
    input.selector_target_generation = 70;
    input.now_seconds = 17.5;
    input.target_error_px = {40.0f, 0.0f};
    input.ai_stick = {0.30f, 0.0f};
    (void)state_machine.update(input);

    input.now_seconds += 0.005;
    input.manual_exit_requested = true;
    input.manual_stick = {0.90f, 0.0f};
    input.filtered_manual_stick = input.manual_stick;
    const auto seek = state_machine.update(input);
    require(seek.phase == controller_native::AssistControlPhase::HandoverSeek,
            "manual exit did not enter handover seek");

    input.now_seconds += 0.005;
    input.manual_exit_requested = false;
    input.target_id = 1102;
    input.selector_target_generation = 71;
    input.manual_stick = {-0.05f, 0.0f};
    input.filtered_manual_stick = input.manual_stick;
    input.manual_correction_x = true;
    const auto capture_opposition = state_machine.update(input);
    require(capture_opposition.phase ==
                controller_native::AssistControlPhase::Capture &&
                capture_opposition.handover_braking,
            "replacement did not enter bounded capture");
    require_near(capture_opposition.stick.x, -0.05f, 1.0e-6f,
                 "capture braking blocked opposing native escape");
}

void test_small_filtered_input_moves_d_without_a_second_deadzone() {
    double now = 18.0;
    auto config = current_config();
    config.tracker.max_observation_age_ms = 1000.0f;
    NativeGamepadController controller(config, [&now] { return now; });
    controller.submit_vision_snapshot(observed_snapshot(
        1, now, 0.0f, 0.0f, 91, 6));
    (void)controller.build_output(physical_input());
    const auto before = controller.last_target_plan();

    // This is above the shared IntentFilter deadzone but below the retired
    // 0.08 D-specific deadzone that caused real micro corrections to vanish.
    now += 0.005;
    const auto output = controller.build_output(physical_input(0.05f, 0.0f));
    const auto after = controller.last_target_plan();
    const auto& components = controller.last_output_components();

    require(after.target_id == before.target_id && after.target_id != 0,
            "micro correction changed target ownership");
    require(after.desired_point_normalized.x >
                before.desired_point_normalized.x,
            "shared input filter accepted micro input but D did not move");
    require(components.manual_correction_x &&
                components.manual_passthrough_x,
            "micro input was not interpreted as current-target D correction");
    require_near(output.right_x, 0.05f, 1.0e-5f,
                 "micro D correction was swallowed before final output");
}

void test_correction_release_retains_d_without_reverse_authority() {
    double now = 19.0;
    auto config = current_config();
    config.tracker.max_observation_age_ms = 1000.0f;
    config.ai_aim.desired_point_traversal_ms = 100.0f;
    NativeGamepadController controller(config, [&now] { return now; });
    controller.submit_vision_snapshot(observed_snapshot(
        1, now, 0.0f, 0.0f, 92, 7));
    (void)controller.build_output(physical_input());
    const auto initial = controller.last_target_plan();

    for (int tick = 0; tick < 4; ++tick) {
        now += 0.005;
        (void)controller.build_output(physical_input(0.0f, -0.40f));
    }
    const auto corrected = controller.last_target_plan();
    require(corrected.manual_correction_y &&
                corrected.desired_point_normalized.y >
                    initial.desired_point_normalized.y,
            "downward correction did not establish a retained D");

    now += 0.005;
    const auto released_output = controller.build_output(physical_input());
    const auto released = controller.last_target_plan();
    const auto& components = controller.last_output_components();

    require(released.target_id == corrected.target_id &&
                released.target_id != 0,
            "stick release detached the current target");
    require_near(released.desired_point_normalized.y,
                 corrected.desired_point_normalized.y, 1.0e-6f,
                 "stick release reset D to Vision's default point");
    require(released.desired_point_source ==
                pipeline_contract::DesiredPointSource::UserCorrected,
            "stick release lost user ownership of D");
    require(!components.manual_correction_y,
            "neutral stick remained tagged as a manual correction");
    require(released_output.right_y <= 1.0e-4f,
            "stick release produced authority opposite to retained downward D");
}

void test_fresh_target_has_one_current_plan_and_one_output_owner() {
    double now = 20.0;
    NativeGamepadController controller(current_config(), [&now] { return now; });
    controller.submit_vision_snapshot(observed_snapshot(
        1, now, 64.0f, -18.0f, 101, 7));
    const auto output = controller.build_output(physical_input());
    const auto plan = controller.last_target_plan();
    const auto components = controller.last_output_components();

    require(plan.target_id != 0 &&
                plan.source_frame_id == 1 &&
                plan.source_observation_id == 101,
            "fresh selected observation did not become the current target plan");
    require(plan.lifecycle == pipeline_contract::TargetLifecycle::Observed &&
                plan.mode == pipeline_contract::ControlMode::AdsAcquire &&
                pipeline_contract::valid(plan),
            "fresh target produced an invalid current plan");
    require(std::hypot(
                components.requested_assist_stick.x,
                components.requested_assist_stick.y) > 1.0e-4f,
            "fresh target produced no solver request");
    require(components.assist_control_phase == "track",
            "ordinary acquisition created an extra control phase");
    require_near(
        components.final_stick.x,
        output.right_x,
        1.0e-6f,
        "diagnostics and delivered X disagree");
    require_near(
        components.final_stick.y,
        output.right_y,
        1.0e-6f,
        "diagnostics and delivered Y disagree");
    require_finite_unit_output(output, "fresh target");
}

void test_controller_tick_without_source_does_not_project_observation() {
    double now = 30.0;
    NativeGamepadController controller(current_config(), [&now] { return now; });
    controller.submit_vision_snapshot(observed_snapshot(
        1, now, 48.0f, 12.0f, 201, 11));
    (void)controller.build_output(physical_input());
    const auto source_plan = controller.last_target_plan();

    now += 0.001;
    const auto output = controller.build_output(physical_input());
    const auto replay_tick = controller.last_target_plan();

    require(replay_tick.target_id == source_plan.target_id &&
                replay_tick.source_frame_id == source_plan.source_frame_id,
            "controller-only tick changed source ownership");
    require(replay_tick.source_observation_id == 0,
            "controller-only tick fabricated a fresh observation id");
    require_near(replay_tick.aim_px.x, source_plan.aim_px.x, 1.0e-6f,
                 "controller-only tick projected target X");
    require_near(replay_tick.aim_px.y, source_plan.aim_px.y, 1.0e-6f,
                 "controller-only tick projected target Y");
    require_near(replay_tick.velocity_px_per_sec.x,
                 source_plan.velocity_px_per_sec.x, 1.0e-6f,
                 "controller-only tick updated source velocity");
    require(!replay_tick.source_decision_available,
            "controller-only tick fabricated a source decision");
    require_finite_unit_output(output, "controller-only tick");
}

void test_fresh_no_target_removes_authority_immediately() {
    double now = 40.0;
    NativeGamepadController controller(current_config(), [&now] { return now; });
    controller.submit_vision_snapshot(observed_snapshot(
        1, now, 52.0f, 0.0f, 301, 15));
    (void)controller.build_output(physical_input());
    require(controller.last_target_plan().target_id != 0,
            "fixture did not establish target ownership");

    now += 0.005;
    controller.submit_vision_snapshot(empty_snapshot(2, now));
    const auto physical = physical_input(-0.37f, 0.24f);
    const auto output = controller.build_output(physical);
    const auto& plan = controller.last_target_plan();

    require(plan.target_id == 0 && plan.aim_authority == 0.0f &&
                plan.mode == pipeline_contract::ControlMode::Manual,
            "fresh no-selection frame retained generic aim authority");
    require_near(output.right_x, physical.right_x, 1.0e-6f,
                 "fresh no-target did not restore physical X");
    require_near(output.right_y, physical.right_y, 1.0e-6f,
                 "fresh no-target did not restore physical Y");
}

void test_non_cue_target_without_candidates_fails_closed() {
    double now = 45.0;
    NativeGamepadController controller(current_config(), [&now] { return now; });
    controller.submit_vision_snapshot(observed_snapshot(
        1, now, 52.0f, 0.0f, 351, 18));
    (void)controller.build_output(physical_input());
    require(controller.last_target_plan().target_id != 0,
            "fixture did not establish target ownership");

    now += 0.005;
    auto inconsistent = observed_snapshot(
        2, now, 20.0f, -8.0f, 352, 18);
    inconsistent.candidates.clear();
    controller.submit_vision_snapshot(inconsistent);
    const auto physical = physical_input(-0.31f, 0.19f);
    const auto output = controller.build_output(physical);
    const auto& plan = controller.last_target_plan();

    require(plan.target_id == 0 && plan.aim_authority == 0.0f &&
                plan.mode == pipeline_contract::ControlMode::Manual,
            "non-cue flattened target state created a controller candidate");
    require_near(output.right_x, physical.right_x, 1.0e-6f,
                 "fail-closed target mismatch did not restore physical X");
    require_near(output.right_y, physical.right_y, 1.0e-6f,
                 "fail-closed target mismatch did not restore physical Y");
}

void test_cue_continuation_is_same_generation_aim_only_evidence() {
    double now = 50.0;
    auto config = current_config();
    config.ai_aim.cue_hold_body_lock_force_scale = 0.35f;
    NativeGamepadController controller(config, [&now] { return now; });
    controller.submit_vision_snapshot(observed_snapshot(
        1, now, 42.0f, 0.0f, 401, 21));
    (void)controller.build_output(physical_input());
    const auto observed = controller.last_target_plan();

    now += 0.005;
    controller.submit_vision_snapshot(cue_snapshot(
        2, now, 25.0f, -12.0f, 21));
    const auto cue_output = controller.build_output(physical_input());
    const auto cue = controller.last_target_plan();

    require(cue.target_id == observed.target_id && cue.cue_continuation,
            "same-generation cue did not continue the owned target");
    require(cue.source_observation_id == 0 && cue.aim_authority > 0.0f &&
                cue.aim_authority <= 0.35f + 1.0e-5f,
            "cue continuation escaped its bounded aim-only authority");
    require(!cue.fire_authority && !cue.fire_requested && !cue_output.rb,
            "cue continuation acquired synthetic fire authority");

    double cue_only_now = 51.0;
    NativeGamepadController cue_only(config, [&cue_only_now] {
        return cue_only_now;
    });
    cue_only.submit_vision_snapshot(cue_snapshot(
        1, cue_only_now, 20.0f, 0.0f, 21));
    (void)cue_only.build_output(physical_input());
    require(cue_only.last_target_plan().target_id == 0,
            "cue acquired a target without prior person ownership");

    now += 0.005;
    controller.submit_vision_snapshot(cue_snapshot(
        3, now, 20.0f, 0.0f, 22));
    (void)controller.build_output(physical_input());
    require(controller.last_target_plan().target_id == 0,
            "wrong-generation cue retained the prior target");
}

void test_boundary_qualified_handover_releases_then_captures() {
    double now = 60.0;
    auto config = current_config();
    config.tracker.max_observation_age_ms = 1000.0f;
    config.ai_aim.desired_point_traversal_ms = 40.0f;
    config.ai_aim.desired_point_boundary_exit_ms = 50.0f;
    NativeGamepadController controller(config, [&now] { return now; });
    auto first = observed_snapshot(1, now, -52.0f, 0.0f, 501, 31);
    add_alternative(first, 502, 58.0f);
    controller.submit_vision_snapshot(first);
    (void)controller.build_output(physical_input());
    const auto old_target_id = controller.last_target_plan().target_id;
    require(old_target_id != 0 &&
                controller.last_target_plan().ads_candidate_count == 2,
            "boundary handover fixture did not establish two-target ownership");

    const auto initial_d = controller.last_target_plan().desired_point_normalized.x;
    now += 0.005;
    (void)controller.build_output(physical_input(0.90f, 0.0f));
    require(!controller.last_output_components().handover_requested,
            "ordinary in-region direction was misread as an immediate handover");
    require(controller.last_target_plan().desired_point_normalized.x > initial_d,
            "ordinary in-region direction did not move D on the current target");

    controller_native::GamepadOutputState seek_output;
    controller_native::NativeControllerOutputComponents seek_components;
    bool reached_handover = false;
    for (int tick = 0; tick < 80; ++tick) {
        now += 0.005;
        seek_output = controller.build_output(physical_input(0.90f, 0.0f));
        seek_components = controller.last_output_components();
        if (seek_components.handover_requested) {
            reached_handover = true;
            break;
        }
    }
    require(reached_handover,
            "sustained pressure beyond R did not request bounded handover");
    require(seek_components.assist_control_phase == "handover_seek" &&
                seek_components.handover_requested,
            "boundary-qualified intent did not enter handover seek");
    require_near(seek_output.right_x, 0.90f, 1.0e-5f,
                 "handover seek kept steering toward the old target");

    now += 0.005;
    auto replacement = observed_snapshot(
        2, now, 35.0f, 0.0f, 602, 32, true);
    add_alternative(replacement, 601, -48.0f);
    controller.submit_vision_snapshot(replacement);
    const auto capture_output = controller.build_output(
        physical_input(0.90f, 0.0f));
    const auto replacement_plan = controller.last_target_plan();
    const auto capture_components = controller.last_output_components();
    require(replacement_plan.target_id != 0 &&
                replacement_plan.target_id != old_target_id &&
                replacement_plan.selector_target_generation == 32,
            "next fresh selector generation did not replace the target");
    require(capture_components.assist_control_phase == "capture" &&
                capture_components.handover_braking,
            "confirmed replacement did not enter bounded capture braking");
    require_near(capture_output.right_x, 0.90f, 1.0e-5f,
                 "capture reduced the native handover gesture");

    for (std::uint64_t frame = 3; frame <= 4; ++frame) {
        now += 0.005;
        auto centered = observed_snapshot(
            frame, now, 0.0f, 0.0f, 600 + frame, 32);
        add_alternative(centered, 700 + frame, -55.0f);
        controller.submit_vision_snapshot(centered);
        (void)controller.build_output(physical_input());
    }
    now += 0.001;
    const auto micro_output = controller.build_output(
        physical_input(0.12f, 0.0f));
    const auto& settled = controller.last_output_components();
    require(settled.assist_control_phase == "track" &&
                settled.manual_correction_x,
            "settled replacement did not interpret micro input as D correction");
    require(micro_output.right_x >= 0.12f - 1.0e-4f,
            "settled replacement reduced current-target micro correction");
    require(micro_output.right_x <=
                std::max(0.12f, settled.ai_aim_stick.x) + 1.0e-4f,
            "settled replacement added manual and AI output together");
}

void test_autofire_requires_fresh_observed_authority_and_preserves_physical_fire() {
    double now = 70.0;
    auto config = current_config();
    config.ai_aim.auto_fire_ready_frames = 2;
    NativeGamepadController controller(config, [&now] { return now; });

    bool synthetic_fire_seen = false;
    for (std::uint64_t frame = 1; frame <= 3; ++frame) {
        controller.submit_vision_snapshot(observed_snapshot(
            frame, now, 0.0f, 0.0f, 800 + frame, 41, false, true));
        const auto output = controller.build_output(physical_input());
        synthetic_fire_seen = synthetic_fire_seen || output.rb;
        now += 0.005;
    }
    require(synthetic_fire_seen,
            "fresh observed target never reached configured AutoFire readiness");

    controller.submit_vision_snapshot(cue_snapshot(
        4, now, 0.0f, 0.0f, 41));
    const auto cue_output = controller.build_output(physical_input());
    require(!cue_output.rb &&
                !controller.last_output_components().auto_fire_active,
            "aim-only cue continued synthetic fire");

    now += 0.001;
    auto manual_fire = physical_input();
    manual_fire.rb = true;
    const auto physical_fire_output = controller.build_output(manual_fire);
    require(physical_fire_output.rb,
            "AutoFire safety path cleared the physical fire button");
}

void test_current_chain_outputs_remain_finite_and_bounded() {
    double now = 80.0;
    NativeGamepadController controller(current_config(), [&now] { return now; });
    for (std::uint64_t frame = 1; frame <= 200; ++frame) {
        const float phase = static_cast<float>(frame) * 0.13f;
        const float dx = std::sin(phase) * 120.0f;
        const float dy = std::cos(phase * 0.7f) * 70.0f;
        controller.submit_vision_snapshot(observed_snapshot(
            frame, now, dx, dy, 900 + frame, 51));
        const auto output = controller.build_output(physical_input(
            std::sin(phase * 0.4f) * 0.18f,
            std::cos(phase * 0.5f) * 0.18f));
        require_finite_unit_output(output, "bounded sequence");
        require(pipeline_contract::valid(controller.last_target_plan()),
                "bounded sequence emitted invalid plan data");
        now += 0.005;
    }
}

}  // namespace

int main() {
    try {
        test_no_target_is_physical_passthrough();
        test_centered_motion_demand_retains_ai_authority();
        test_valid_point_correction_is_interpreted_not_passthrough();
        test_track_uses_ai_as_total_fill_but_opposition_is_native();
        test_track_uses_one_two_dimensional_cooperation_decision();
        test_capture_brakes_alignment_but_allows_opposing_escape();
        test_small_filtered_input_moves_d_without_a_second_deadzone();
        test_correction_release_retains_d_without_reverse_authority();
        test_fresh_target_has_one_current_plan_and_one_output_owner();
        test_controller_tick_without_source_does_not_project_observation();
        test_fresh_no_target_removes_authority_immediately();
        test_non_cue_target_without_candidates_fails_closed();
        test_cue_continuation_is_same_generation_aim_only_evidence();
        test_boundary_qualified_handover_releases_then_captures();
        test_autofire_requires_fresh_observed_authority_and_preserves_physical_fire();
        test_current_chain_outputs_remain_finite_and_bounded();
        std::cout << "[TargetPipelineIntegrationTests] PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[TargetPipelineIntegrationTests][FAIL] "
                  << error.what() << '\n';
        return 1;
    }
}
