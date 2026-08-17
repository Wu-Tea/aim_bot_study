#include "pid_benchmark_controller.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace controller_native::pid_benchmark;
using namespace controller_native::sustained_aimlab;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool near(double left, double right, double tolerance = 1.0e-9) {
    return std::fabs(left - right) <= tolerance;
}

ControllerObservation observed(Vec2d manual) {
    ControllerObservation input;
    input.now_ms = 0;
    input.target_present = true;
    input.primary_candidate_visible = true;
    input.fresh_vision = true;
    input.frame_id = 1;
    input.target_id = 42;
    input.capture_time_seconds = 0.0;
    input.ready_time_seconds = 0.0;
    input.observed_error_px = {10.0, -5.0};
    input.manual_stick = manual;
    return input;
}

void test_manual_does_not_enter_pid_state_or_proposal() {
    PidBenchmarkConfig config;
    config.output_filter_tau_ms = 0.0;
    PidBenchmarkController first(config, BenchmarkCohort::BodyLockFollow);
    PidBenchmarkController second(config, BenchmarkCohort::BodyLockFollow);

    const auto without_manual = first.step(observed({}));
    const auto with_manual = second.step(observed({0.10, -0.10}));
    require(near(without_manual.requested_assist_stick.x,
                 with_manual.requested_assist_stick.x) &&
            near(without_manual.requested_assist_stick.y,
                 with_manual.requested_assist_stick.y),
        "manual input changed the independent PID proposal");
    require(near(with_manual.final_stick.x,
                 with_manual.requested_assist_stick.x + 0.10) &&
            near(with_manual.final_stick.y,
                 with_manual.requested_assist_stick.y - 0.10),
        "unsaturated final output is not manual plus PID proposal");
}

void test_no_target_passes_manual_through() {
    PidBenchmarkController controller({}, BenchmarkCohort::BodyLockFollow);
    ControllerObservation input;
    input.fresh_vision = true;
    input.manual_stick = {0.22, -0.31};
    const auto output = controller.step(input);
    require(near(output.final_stick.x, input.manual_stick.x) &&
            near(output.final_stick.y, input.manual_stick.y),
        "no-target path altered manual input");
    require(near(output.requested_assist_stick.x, 0.0) &&
            near(output.requested_assist_stick.y, 0.0),
        "no-target path retained PID output");
}

void test_control_updates_between_vision_frames() {
    PidBenchmarkConfig config;
    config.output_filter_tau_ms = 8.0;
    PidBenchmarkController controller(config, BenchmarkCohort::BodyLockFollow);
    ControllerObservation input = observed({});
    input.observed_error_px = {20.0, 0.0};
    const auto first = controller.step(input);
    input.now_ms = 1;
    input.fresh_vision = false;
    const auto second = controller.step(input);
    require(first.requested_assist_stick.x > 0.0 &&
            second.requested_assist_stick.x >
                first.requested_assist_stick.x &&
            second.requested_assist_stick.x < 1.0,
        "1 kHz output filter did not advance between Vision observations");
}

}  // namespace

int main() {
    try {
        test_manual_does_not_enter_pid_state_or_proposal();
        test_no_target_passes_manual_through();
        test_control_updates_between_vision_frames();
        std::cout << "pid_benchmark_controller_tests PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "pid_benchmark_controller_tests FAIL: "
                  << error.what() << '\n';
        return 1;
    }
}
