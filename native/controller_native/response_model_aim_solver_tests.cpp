#include "response_model_aim_solver.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using namespace controller_native;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void require_near(float actual, float expected, float tolerance,
                  const std::string& message) {
    if (std::fabs(actual - expected) > tolerance) {
        throw std::runtime_error(message + ": expected=" +
            std::to_string(expected) + " actual=" + std::to_string(actual));
    }
}

ResponseModelAimRequest base_request() {
    ResponseModelAimRequest request;
    request.response_px_per_stick_second = 500.0f;
    request.arrival_horizon_seconds = 0.050f;
    request.motion_weight = 1.0f;
    request.max_force = {1.0f, 1.0f};
    request.authority = 1.0f;
    return request;
}

void test_radial_direction_and_y_sign() {
    auto request = base_request();
    request.error_px = {3.0f, 4.0f};
    const auto output = solve_response_model_aim(request);
    require(output.unclamped_stick.x > 0.0f &&
                output.unclamped_stick.y < 0.0f,
            "screen-down target requires negative controller Y");
    require_near(output.unclamped_stick.x / -output.unclamped_stick.y,
                 3.0f / 4.0f, 1e-4f,
                 "solver must preserve radial direction");
}

void test_horizon_and_response_have_physical_units() {
    auto request = base_request();
    request.error_px = {20.0f, 0.0f};
    const auto normal = solve_response_model_aim(request);
    request.arrival_horizon_seconds = 0.025f;
    const auto urgent = solve_response_model_aim(request);
    require_near(urgent.unclamped_stick.x,
                 normal.unclamped_stick.x * 2.0f, 1e-5f,
                 "half horizon requires double position demand");
    request.arrival_horizon_seconds = 0.050f;
    request.response_px_per_stick_second = 250.0f;
    const auto slow = solve_response_model_aim(request);
    require_near(slow.unclamped_stick.x,
                 normal.unclamped_stick.x * 2.0f, 1e-5f,
                 "half camera response requires double stick");
}

void test_motion_feedforward_is_not_multiplied_by_force_cap() {
    auto request = base_request();
    request.relative_velocity_px_per_sec = {100.0f, 0.0f};
    request.max_force = {0.45f, 0.50f};
    const auto output = solve_response_model_aim(request);
    require_near(output.motion_stick.x, 0.20f, 1e-6f,
                 "velocity divided by response is already stick demand");
    require_near(output.stick.x, 0.20f, 1e-6f,
                 "force envelope must not attenuate an in-range motion term");
}

void test_trusted_motion_can_lead_opposite_small_residual() {
    auto request = base_request();
    request.error_px = {-1.0f, 0.0f};
    request.relative_velocity_px_per_sec = {100.0f, 0.0f};
    request.arrival_horizon_seconds = 0.10f;
    const auto output = solve_response_model_aim(request);
    require(output.stick.x > 0.0f,
            "trusted motion must lead before visible lag changes error sign");
}

void test_fresh_position_bounds_only_opposing_radial_motion() {
    auto request = base_request();
    request.error_px = {-10.0f, 0.0f};
    request.relative_velocity_px_per_sec = {260.0f, 120.0f};
    request.fresh_position_authoritative = true;
    const auto output = solve_response_model_aim(request);
    require(output.radial_motion_bound_applied,
            "fresh position fixture must exercise the radial motion bound");
    require(output.bounded_motion_stick.x <= 0.0f,
            "fresh position must not keep an opposing radial motion sign");
    require_near(output.bounded_motion_stick.y, output.motion_stick.y,
                 1e-5f,
                 "fresh radial bound must preserve tangent motion");
    require(output.radial_motion_bound_reason ==
                ResponseModelConstraintReason::FreshPositionRadialMotionBound,
            "fresh radial bound must expose its causal reason");
}

void test_elliptical_force_envelope_scales_one_vector() {
    auto request = base_request();
    request.error_px = {100.0f, 100.0f};
    request.max_force = {0.30f, 0.20f};
    const auto output = solve_response_model_aim(request);
    const float ellipse = std::sqrt(
        std::pow(output.stick.x / 0.30f, 2.0f) +
        std::pow(output.stick.y / 0.20f, 2.0f));
    require_near(ellipse, 1.0f, 1e-5f,
                 "limited request must lie on force ellipse");
    require(output.limited, "large request must report force limiting");
}

}  // namespace

int main() {
    try {
        test_radial_direction_and_y_sign();
        test_horizon_and_response_have_physical_units();
        test_motion_feedforward_is_not_multiplied_by_force_cap();
        test_trusted_motion_can_lead_opposite_small_residual();
        test_fresh_position_bounds_only_opposing_radial_motion();
        test_elliptical_force_envelope_scales_one_vector();
        std::cout << "cod_native_response_model_aim_solver_tests PASS\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "cod_native_response_model_aim_solver_tests FAIL: "
                  << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
