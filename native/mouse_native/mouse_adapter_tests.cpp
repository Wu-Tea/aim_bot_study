#include "mouse_native/mouse_actuator_adapter.h"
#include "mouse_native/mouse_rate_adapter.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void require_true(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void require_near(double actual, double expected, double tolerance, const char* message) {
    if (std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(message);
    }
}

mouse_native::MouseResponseProfile profile(
    float counts_per_second = 10'000.0f,
    std::uint64_t generation = 1) {
    mouse_native::MouseResponseProfile result{};
    result.px_per_count_x = 0.05f;
    result.px_per_count_y = 0.05f;
    result.counts_per_u_second_x = counts_per_second;
    result.counts_per_u_second_y = counts_per_second;
    result.confidence = 1.0f;
    result.generation = generation;
    result.calibrated = true;
    return result;
}

void test_rate_adapter_direction_and_scale() {
    mouse_native::MouseRateAdapter adapter;
    const auto result = adapter.adapt({5, -2}, 0.001f, profile());
    require_true(result.valid, "calibrated finite input must be valid");
    require_true(!result.transparent, "bounded input must enter controller");
    require_near(result.x, 0.5, 1.0e-6, "X count rate must normalize");
    require_near(result.y, 0.2, 1.0e-6, "mouse Y sign must invert");
}

void test_rate_adapter_invalid_profile_and_dt_are_transparent() {
    mouse_native::MouseRateAdapter adapter;
    auto invalid = profile();
    invalid.calibrated = false;
    auto result = adapter.adapt({1, 1}, 0.001f, invalid);
    require_true(!result.valid && result.transparent, "uncalibrated input must be transparent");
    result = adapter.adapt({1, 1}, 0.0f, profile());
    require_true(!result.valid && result.transparent, "invalid dt must be transparent");
}

void test_large_flick_requests_transparent_without_clamp() {
    mouse_native::MouseRateAdapter adapter;
    const auto result = adapter.adapt({25, 0}, 0.001f, profile());
    require_true(result.valid, "large finite flick conversion must remain observable");
    require_true(result.envelope_exceeded && result.transparent,
        "large flick must request transparent routing");
    require_near(result.x, 2.5, 1.0e-6, "large flick magnitude must not clamp");
}

void test_actuator_direction_and_subcount_residual() {
    mouse_native::MouseActuatorAdapter adapter;
    const auto p = profile(1'000.0f);
    std::int32_t total_x = 0;
    std::int32_t total_y = 0;
    for (int i = 0; i < 4; ++i) {
        const auto output = adapter.adapt(0.25f, -0.25f, 0.001f, p);
        require_true(output.valid && !output.saturated, "subcount output must remain valid");
        total_x += output.dx;
        total_y += output.dy;
    }
    require_true(total_x == 1 && total_y == 1,
        "four quarter-count commands must accumulate to one count per axis");
    require_near(adapter.residual_x(), 0.0, 1.0e-6, "X residual must discharge");
    require_near(adapter.residual_y(), 0.0, 1.0e-6, "Y residual must discharge");
}

void test_manual_round_trip_conserves_counts() {
    mouse_native::MouseRateAdapter rate;
    mouse_native::MouseActuatorAdapter actuator;
    const auto p = profile();
    const std::vector<mouse_native::MouseSourceCounts> source{
        {5, -2}, {-4, 3}, {0, 0}, {9, -8}, {-10, 10}, {2, 1}};
    std::int64_t source_x = 0;
    std::int64_t source_y = 0;
    std::int64_t final_x = 0;
    std::int64_t final_y = 0;
    for (const auto counts : source) {
        const auto input = rate.adapt(counts, 0.001f, p);
        require_true(input.valid && !input.transparent, "round-trip sample must be in envelope");
        const auto output = actuator.adapt(input.x, input.y, 0.001f, p);
        require_true(output.valid && !output.saturated, "round-trip output must be valid");
        source_x += counts.dx;
        source_y += counts.dy;
        final_x += output.dx;
        final_y += output.dy;
    }
    require_true(source_x == final_x && source_y == final_y,
        "manual count round-trip must conserve both axes");
}

void test_profile_generation_and_passthrough_clear_residual() {
    mouse_native::MouseActuatorAdapter adapter;
    auto output = adapter.adapt(0.25f, 0.0f, 0.001f, profile(1'000.0f, 1));
    require_true(output.valid, "first generation output must be valid");
    require_near(adapter.residual_x(), 0.25, 1.0e-6, "first generation must retain residual");
    output = adapter.adapt(0.0f, 0.0f, 0.001f, profile(1'000.0f, 2));
    require_true(output.valid && output.dx == 0, "new generation must not emit old residual");
    require_near(adapter.residual_x(), 0.0, 1.0e-6, "new generation must reset residual");
    output = adapter.adapt(0.25f, 0.0f, 0.001f, profile(1'000.0f, 2));
    require_near(adapter.residual_x(), 0.25, 1.0e-6, "new residual must be present");
    const auto transparent = adapter.passthrough({7, -3});
    require_true(transparent.valid && transparent.transparent &&
        transparent.dx == 7 && transparent.dy == -3,
        "transparent output must preserve source counts exactly");
    require_near(adapter.residual_x(), 0.0, 1.0e-6, "passthrough must clear X residual");
}

void test_report_saturation_is_explicit_and_has_no_tail() {
    mouse_native::MouseActuatorAdapter adapter({2, 0.0001f, 0.0500f});
    const auto output = adapter.adapt(1.0f, 0.0f, 0.001f, profile(10'000.0f));
    require_true(output.valid && output.saturated && output.dx == 2,
        "descriptor overflow must be explicit and bounded");
    require_near(adapter.residual_x(), 0.0, 1.0e-6,
        "saturation must not leave an unbounded output tail");
}

void test_elapsed_time_controls_distance_under_jitter() {
    const auto p = profile();
    // All schedules cover exactly one second. Frequency must not multiply AI distance.
    for (const auto& schedule : std::vector<std::vector<float>>{
            {0.001f}, {0.004f}, {0.0005f, 0.0015f, 0.003f, 0.001f, 0.004f}}) {
        mouse_native::MouseActuatorAdapter actuator;
        mouse_native::MouseRateAdapter rate;
        double elapsed = 0;
        std::int64_t x = 0, y = 0;
        std::size_t index = 0;
        while (elapsed < 0.999999) {
            const float dt = schedule[index++ % schedule.size()];
            elapsed += dt;
            const auto out = actuator.adapt(0.1234f, -0.0789f, dt, p);
            require_true(out.valid && !out.saturated, "jittered AI integration must remain valid");
            x += out.dx;
            y += out.dy;
            const auto manual = rate.adapt({2, -1}, dt, p);
            mouse_native::MouseActuatorAdapter manual_output;
            const auto echo = manual_output.adapt(manual.x, manual.y, dt, p);
            require_true(echo.dx == 2 && echo.dy == -1,
                "manual counts must survive actual dt normalization and integration");
        }
        require_true(x == 1234 && y == 789,
            "one second of the same AI velocity must yield the same distance at 1000/250/jittered Hz");
    }
}

void test_manual_axis_drops_previous_ai_residual() {
    for (int axis = 0; axis < 2; ++axis) {
        mouse_native::MouseActuatorAdapter actuator;
        const auto p = profile(1000);
        actuator.adapt(axis ? 0 : 0.5f, axis ? -0.5f : 0, 0.001f, p);
        const auto out = actuator.adapt_with_native_axes(axis ? 0.25f : -1.0f,
            axis ? 1.0f : -0.25f, 0.001f, p,
            axis ? mouse_native::MouseSourceCounts{0, -1} : mouse_native::MouseSourceCounts{-1, 0},
            axis == 0, axis == 1);
        std::cout << "manual_axis=" << axis << " expected=-1 actual=" << (axis ? out.dy : out.dx) << '\n';
        require_true((axis ? out.dy : out.dx) == -1,
            "an unchanged physical count must not inherit previous AI rounding residue");
        require_near(axis ? actuator.residual_y() : actuator.residual_x(), 0, 1e-6,
            "manual axis releases its old AI remainder");
        require_near(axis ? actuator.residual_x() : actuator.residual_y(), 0.25, 1e-6,
            "the orthogonal AI axis retains subcount integration");
    }
}

}  // namespace

int main() {
    try {
        test_rate_adapter_direction_and_scale();
        test_rate_adapter_invalid_profile_and_dt_are_transparent();
        test_large_flick_requests_transparent_without_clamp();
        test_actuator_direction_and_subcount_residual();
        test_manual_round_trip_conserves_counts();
        test_profile_generation_and_passthrough_clear_residual();
        test_report_saturation_is_explicit_and_has_no_tail();
        test_elapsed_time_controls_distance_under_jitter();
        test_manual_axis_drops_previous_ai_residual();
    } catch (const std::exception& error) {
        std::cerr << "[NativeMouseAdapterTests] FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "[NativeMouseAdapterTests] PASS\n";
    return 0;
}
