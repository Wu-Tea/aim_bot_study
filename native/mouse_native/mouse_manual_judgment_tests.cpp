#include "controller_native/assist_control_state_machine.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>

namespace {
using namespace controller_native;
struct Report {
    int cases = 0, triggers = 0, failed = 0;
    std::ostream& out;
    void check(const std::string& name, float actual, float expected, bool triggered) {
        const bool pass = triggered && std::abs(actual - expected) < 1e-6f;
        if (cases++) out << ',';
        triggers += triggered; failed += !pass;
        out << "{\"case\":\"" << name << "\",\"actual\":" << actual
            << ",\"expected\":" << expected << ",\"triggered\":" << triggered
            << ",\"pass\":" << pass << '}';
    }
};
void run(Report& report) {
    for (const auto mode : {pipeline_contract::ControlMode::AdsAcquire,
                           pipeline_contract::ControlMode::BodyLockFollow}) {
        for (int axis = 0; axis < 2; ++axis) {
            AssistControlStateMachineConfig config;
            config.direct_mouse_manual = true;
            config.bodylock_manual_weight = 0.25f;
            config.capture_settle_radius_px = 10;
            const auto label = std::string(mode == pipeline_contract::ControlMode::AdsAcquire ? "ads_" : "body_")
                + (axis ? "y_" : "x_");
            const auto check = [&](const char* name, float manual, float target, float error,
                                   float expected, bool correction = false) {
                AssistControlStateMachine owner(config);
                AssistControlStateMachineInput in;
                in.aiming = in.target_authoritative = in.fresh_observation = true;
                in.target_id = in.selector_target_generation = 9;
                in.now_seconds = 1;
                in.mode = mode;
                in.ads_acquisition_state = pipeline_contract::AdsAcquisitionState::AcquiringNominal;
                in.visual_authority = 1;
                in.centered_manual_available = true;
                in.manual_stick = in.centered_manual_stick = axis
                    ? pipeline_contract::Vec2f{0, manual} : pipeline_contract::Vec2f{manual, 0};
                in.ai_stick = axis ? pipeline_contract::Vec2f{0, target} : pipeline_contract::Vec2f{target, 0};
                in.target_error_px = axis ? pipeline_contract::Vec2f{0, -error} : pipeline_contract::Vec2f{error, 0};
                in.manual_correction_x = !axis && correction;
                in.manual_correction_y = axis && correction;
                const auto result = owner.update(in);
                report.check(label + name, axis ? result.stick.y : result.stick.x, expected,
                    result.phase == AssistControlPhase::Track && !result.handover_requested);
                // Matched counterfactual: removing ownership must restore raw
                // input on this same axis even while the AI proposal remains.
                in.target_authoritative = false; in.now_seconds += 0.001;
                const auto free = owner.update(in);
                report.check(label + name + "_loss", axis ? free.stick.y : free.stick.x, manual,
                    free.phase == AssistControlPhase::Manual);
            };
            check("idle_safe", 0.1f, 0, 0, 0.1f);
            check("helpful_faster", 0.4f, 0.2f, 20, 0.4f);
            check("helpful_deficit", 0.1f, 0.2f, 20, 0.2f);
            check("opposing_reduced", -0.4f, 0.2f, 20, 0.1f);
            check("pure_ai", 0, 0.2f, 20, 0.2f);
            check("valid_point_edit", -0.1f, 0, 0, -0.1f, true);
        }
    }
}
}
int main(int argc, char** argv) {
    std::ofstream file;
    if (argc == 2) file.open(argv[1]);
    std::ostream& out = file.is_open() ? file : std::cout;
    Report report{0, 0, 0, out}; out << "{\"samples\":[";
    run(report);
    out << "],\"cases\":" << report.cases << ",\"triggered\":" << report.triggers
        << ",\"failed\":" << report.failed << "}\n";
    return report.failed ? 1 : 0;
}
