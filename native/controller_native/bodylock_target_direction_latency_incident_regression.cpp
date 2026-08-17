#include "assist_control_state_machine.h"
#include "incident_fixture_support.h"

#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {

constexpr const char* kIncidentId =
    "bodylock-target-direction-latency-20260817";
constexpr float kTickSeconds = 0.005f;
constexpr int kMaximumTargetDirectionTick = 1;

controller_native::AssistControlStateMachineInput bodylock_input(
    double now_seconds) {
    controller_native::AssistControlStateMachineInput input;
    input.aiming = true;
    input.target_authoritative = true;
    input.fresh_observation = true;
    input.target_id = 748;
    input.selector_target_generation = 817;
    input.now_seconds = now_seconds;
    input.target_error_px = {-10.0f, 0.0f};
    input.mode = pipeline_contract::ControlMode::BodyLockFollow;
    input.visual_authority = 1.0f;
    input.manual_stick = {0.15f, 0.0f};
    input.centered_manual_stick = input.manual_stick;
    input.centered_manual_available = true;
    input.filtered_manual_stick = input.manual_stick;
    input.ai_stick = {-0.25f, 0.0f};
    return input;
}

struct Report {
    int first_target_direction_tick = -1;
    bool target_direction_within_one_tick = false;
    float first_output_x = 0.0f;
    float last_output_x = 0.0f;
    bool trigger_executed = false;
    bool intentional_d_preserved = false;
    bool explicit_exit_preserved = false;
    bool cue_only_does_not_force_takeover = false;
    bool compatible_manual_preserved = false;
    bool overall_pass = false;
};

Report run() {
    Report report;
    controller_native::AssistControlStateMachine machine;
    for (int tick = 0; tick < 6; ++tick) {
        const auto input = bodylock_input(100.0 + tick * kTickSeconds);
        const auto output = machine.update(input);
        if (tick == 0) report.first_output_x = output.stick.x;
        report.last_output_x = output.stick.x;
        if (report.first_target_direction_tick < 0 &&
            output.stick.x < -1.0e-4f) {
            report.first_target_direction_tick = tick;
        }
        report.trigger_executed = report.trigger_executed ||
            (input.fresh_observation && input.target_error_px.x < 0.0f &&
             input.ai_stick.x < 0.0f && input.manual_stick.x > 0.0f);
    }

    auto intentional = bodylock_input(101.0);
    intentional.manual_correction_x = true;
    controller_native::AssistControlStateMachine intentional_machine;
    report.intentional_d_preserved =
        intentional_machine.update(intentional).stick.x > 0.0f;

    auto exit = bodylock_input(102.0);
    exit.manual_exit_requested = true;
    controller_native::AssistControlStateMachine exit_machine;
    const auto exit_output = exit_machine.update(exit);
    report.explicit_exit_preserved = exit_output.handover_requested &&
        std::fabs(exit_output.stick.x - exit.manual_stick.x) <= 1.0e-6f;

    auto cue = bodylock_input(103.0);
    cue.fresh_observation = false;
    cue.cue_continuation = true;
    controller_native::AssistControlStateMachine cue_machine;
    report.cue_only_does_not_force_takeover =
        cue_machine.update(cue).stick.x > 0.0f;

    auto compatible = bodylock_input(104.0);
    compatible.target_error_px.x = 10.0f;
    compatible.manual_stick.x = 0.15f;
    compatible.centered_manual_stick.x = 0.15f;
    compatible.filtered_manual_stick.x = 0.15f;
    compatible.ai_stick.x = 0.25f;
    controller_native::AssistControlStateMachine compatible_machine;
    report.compatible_manual_preserved =
        compatible_machine.update(compatible).stick.x >= 0.15f;

    report.target_direction_within_one_tick =
        report.first_target_direction_tick >= 0 &&
        report.first_target_direction_tick <= kMaximumTargetDirectionTick;
    report.overall_pass = report.trigger_executed &&
        report.target_direction_within_one_tick &&
        report.intentional_d_preserved && report.explicit_exit_preserved &&
        report.cue_only_does_not_force_takeover &&
        report.compatible_manual_preserved;
    return report;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto output_path =
            controller_native::incident_fixture::output_path_from_args(
                argc, argv);
        const Report result = run();
        auto output = controller_native::incident_fixture::open_report(
            output_path);
        output << std::boolalpha << std::fixed << std::setprecision(6)
               << "{\n  \"schema_version\": 1,\n"
               << "  \"incident_id\": \"" << kIncidentId << "\",\n"
               << "  \"symptom\": \"fresh target-directed BodyLock AI is delayed by retained opposing manual input\",\n"
               << "  \"covariates\": {\"fresh_observation\":true,\"target_count\":1,\"target_error_x_px\":-10.0,\"manual_x\":0.15,\"ai_x\":-0.25,\"tick_ms\":5.0},\n"
               << "  \"trigger_executed\": " << result.trigger_executed
               << ",\n  \"metrics\": {\"first_target_direction_tick\":"
               << result.first_target_direction_tick
               << ",\"target_direction_within_one_tick\":"
               << result.target_direction_within_one_tick
               << ",\"first_output_x\":" << result.first_output_x
               << ",\"last_output_x\":" << result.last_output_x
               << "},\n  \"counterfactuals\": {"
               << "\"intentional_d_preserved\":"
               << result.intentional_d_preserved << ','
               << "\"explicit_exit_preserved\":"
               << result.explicit_exit_preserved << ','
               << "\"cue_only_does_not_force_takeover\":"
               << result.cue_only_does_not_force_takeover << ','
               << "\"compatible_manual_preserved\":"
               << result.compatible_manual_preserved
               << "},\n  \"oracles\": ["
               << "{\"id\":\"O1\",\"metric\":\"target_direction_within_one_tick\",\"operator\":\"==\",\"threshold\":true,\"observed\":"
               << result.target_direction_within_one_tick << ",\"pass\":"
               << result.target_direction_within_one_tick
               << "}],\n  \"overall_pass\": " << result.overall_pass
               << "\n}\n";
        output.close();
        std::cout << "[BodylockTargetDirectionLatencyIncident] first_tick="
                  << result.first_target_direction_tick
                  << " first_output=" << result.first_output_x
                  << " counterfactuals="
                  << (result.intentional_d_preserved &&
                      result.explicit_exit_preserved &&
                      result.cue_only_does_not_force_takeover &&
                      result.compatible_manual_preserved)
                  << " result=" << (result.overall_pass ? "GREEN" : "RED")
                  << '\n';
        if (!result.trigger_executed) return 3;
        return result.overall_pass ? 0 : 1;
    } catch (const std::invalid_argument& error) {
        std::cerr << "[BodylockTargetDirectionLatencyIncident][USAGE] "
                  << error.what() << '\n';
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "[BodylockTargetDirectionLatencyIncident][ERROR] "
                  << error.what() << '\n';
        return 3;
    }
}
