#include "control_response_window.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
void require(bool value, int line) {
    if (!value) { std::cerr << "require failed at line " << line << '\n'; std::abort(); }
}
void require_near(float actual, float expected, float tolerance, int line) {
    if (std::fabs(actual - expected) > tolerance) {
        std::cerr << "require near failed at line " << line << " actual=" << actual
                  << " expected=" << expected << '\n';
        std::abort();
    }
}
#define REQUIRE(value) require((value), __LINE__)
#define REQUIRE_NEAR(actual, expected, tolerance) require_near((actual), (expected), (tolerance), __LINE__)

runtime_app::ResponseVisionFrame frame(
    std::uint64_t id, std::uint64_t ns, float dx, float dy, std::uint64_t track) {
    runtime_app::ResponseVisionFrame value;
    value.frame_id = id;
    value.captured_at_ns = ns;
    value.frame_width = 640;
    value.frame_height = 512;
    value.target_track_id = track;
    value.identity_quality = runtime_app::TargetIdentityQuality::StrongGeometricMatch;
    value.live = true;
    value.dx = dx;
    value.dy = dy;
    return value;
}

runtime_app::ResponseControllerSample command(
    std::uint64_t seq, std::uint64_t ns, float manual, float ai, float final) {
    runtime_app::ResponseControllerSample value;
    value.sample_seq = seq;
    value.output_sent_ns = ns;
    value.manual_x = manual;
    value.ai_x = ai;
    value.pre_recoil_x = final;
    value.final_x = final;
    return value;
}

void test_pairs_commands_between_consecutive_new_frames() {
    runtime_app::ControlResponseWindowAssembler assembler;
    REQUIRE(!assembler.observe_vision(frame(10, 1'000'000'000, 20, -5, 7)).has_value());
    assembler.observe_controller(command(1, 1'100'000'000, 0.2f, 0.1f, 0.3f));
    assembler.observe_controller(command(2, 1'200'000'000, 0.4f, 0.1f, 0.5f));
    const auto result = assembler.observe_vision(frame(11, 1'300'000'000, 15, -3, 7));
    REQUIRE(result.has_value());
    REQUIRE_NEAR(result->delta_error_x, -5.0f, 0.01f);
    REQUIRE_NEAR(result->delta_error_y, 2.0f, 0.01f);
    REQUIRE_NEAR(result->manual_x_integral, 0.06f, 0.0001f);
    REQUIRE_NEAR(result->ai_x_integral, 0.02f, 0.0001f);
    REQUIRE(result->readiness == runtime_app::TelemetryReadiness::ModelEligible);
    REQUIRE(result->completeness.complete);
}

void test_reused_frame_does_not_create_duplicate_window() {
    runtime_app::ControlResponseWindowAssembler assembler;
    assembler.observe_vision(frame(10, 1'000'000'000, 20, 0, 7));
    assembler.observe_controller(command(1, 1'100'000'000, 0.2f, 0, 0.2f));
    REQUIRE(!assembler.observe_vision(frame(10, 1'200'000'000, 19, 0, 7)).has_value());
    REQUIRE(assembler.observe_vision(frame(11, 1'300'000'000, 18, 0, 7)).has_value());
}

void test_sample_gap_and_target_switch_are_diagnostic() {
    runtime_app::ControlResponseWindowAssembler assembler;
    assembler.observe_vision(frame(1, 1'000'000'000, 20, 0, 7));
    assembler.observe_controller(command(4, 1'100'000'000, 0.2f, 0, 0.2f));
    assembler.observe_controller(command(6, 1'200'000'000, 0.2f, 0, 0.2f));
    const auto result = assembler.observe_vision(frame(2, 1'300'000'000, 18, 0, 8));
    REQUIRE(result.has_value());
    REQUIRE(result->readiness == runtime_app::TelemetryReadiness::Diagnostic);
    REQUIRE(!result->completeness.complete);
    REQUIRE(result->reason == runtime_app::ResponseWindowReason::TargetChanged);
}
}

int main() {
    test_pairs_commands_between_consecutive_new_frames();
    test_reused_frame_does_not_create_duplicate_window();
    test_sample_gap_and_target_switch_are_diagnostic();
    return 0;
}
