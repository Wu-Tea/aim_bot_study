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

void test_motion_does_not_reverse_current_residual() {
    auto request = base_request();
    request.error_px = {-1.0f, 0.0f};
    request.relative_velocity_px_per_sec = {100.0f, 0.0f};
    request.arrival_horizon_seconds = 0.10f;
    const auto output = solve_response_model_aim(request);
    require(output.stick.x <= 0.0f,
            "motion metadata must not reverse the current source-owned residual");
}

void test_current_position_bounds_only_opposing_axis_motion() {
    auto request = base_request();
    request.error_px = {-10.0f, 0.0f};
    request.relative_velocity_px_per_sec = {260.0f, 120.0f};
    const auto output = solve_response_model_aim(request);
    require(output.radial_motion_bound_applied,
            "current position fixture must exercise the position-motion bound");
    require(output.bounded_motion_stick.x <= 0.0f,
            "fresh position must not keep an opposing radial motion sign");
    require_near(output.bounded_motion_stick.y, output.motion_stick.y,
                 1e-5f,
                 "axis-local bound must preserve orthogonal motion");
    require(output.radial_motion_bound_reason ==
                ResponseModelConstraintReason::PositionRadialMotionBound,
            "position-motion bound must expose its single-path reason");
}

void test_orthogonal_motion_cannot_mask_an_axis_reversal() {
    auto request = base_request();
    request.error_px = {-7.5f, -18.5f};
    request.relative_velocity_px_per_sec = {190.0f, -250.0f};
    const auto output = solve_response_model_aim(request);
    const float vector_dot =
        output.position_stick.x * output.motion_stick.x +
        output.position_stick.y * output.motion_stick.y;

    require(output.position_stick.x * output.motion_stick.x < 0.0f,
            "fixture must oppose the current X position");
    require(vector_dot > 0.0f,
            "orthogonal same-direction motion must mask the old vector test");
    require(output.radial_motion_bound_applied,
            "axis-local conflict must exercise the position-motion bound");
    require_near(output.bounded_motion_stick.y, output.motion_stick.y,
                 1e-6f,
                 "compatible orthogonal motion must remain unchanged");
    require(output.pre_curve_stick.x * output.position_stick.x >= 0.0f,
            "motion must not reverse the current source-owned X position");
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

void test_authority_caps_delivered_force_after_curve_inversion() {
    auto request = base_request();
    request.error_px = {1000.0f, 0.0f};
    request.authority = 0.20f;
    request.response_curve.algorithm =
        AimResponseCurveAlgorithm::CodDynamicLegacyLut;
    const auto output = solve_response_model_aim(request);
    require(output.limited,
            "large low-authority request must exercise its force budget");
    require_near(output.stick.x, 0.20f, 1e-5f,
                 "authority must cap the delivered stick after inverse curve");
}

void test_clear_authority_does_not_multiply_smaller_mode_cap() {
    auto request = base_request();
    request.error_px = {1000.0f, 0.0f};
    request.max_force = {0.30f, 0.42f};
    request.authority = 0.72f;
    const auto output = solve_response_model_aim(request);
    require_near(output.stick.x, 0.30f, 1e-5f,
                 "clear evidence must preserve the smaller BodyLock cap");
}

void test_cod_dynamic_plugin_round_trips_seed_curve() {
    const auto& plugin = resolve_aim_response_curve_plugin(
        AimResponseCurveAlgorithm::CodDynamicLegacyLut);
    require(std::string(plugin.name) == "cod_dynamic_legacy_lut",
            "configured algorithm must resolve to the COD Dynamic plugin");
    require_near(plugin.forward_magnitude(0.59924f), 0.30000f, 1e-6f,
                 "COD Dynamic forward LUT must preserve its seed sample");
    require_near(plugin.inverse_magnitude(0.30000f), 0.59924f, 1e-6f,
                 "COD Dynamic inverse LUT must recover the stick sample");
}

void test_cod_dynamic_curve_shapes_target_t_around_reference() {
    auto request = base_request();
    request.max_force = {2.0f, 2.0f};
    request.response_curve.algorithm =
        AimResponseCurveAlgorithm::CodDynamicLegacyLut;
    request.response_curve.calibration_reference_stick = 0.50f;

    request.error_px = {5.0f, 0.0f};  // linear target magnitude 0.20
    const auto small = solve_response_model_aim(request);
    require_near(small.pre_curve_stick.x, 0.20f, 1e-6f,
                 "curve input must remain the response-model target");
    require(small.unclamped_stick.x > small.pre_curve_stick.x,
            "Dynamic inverse must compensate the low-stick slow region");

    request.error_px = {12.5f, 0.0f};  // configured reference 0.50
    const auto reference = solve_response_model_aim(request);
    require_near(reference.unclamped_stick.x, 0.50f, 1e-5f,
                 "Dynamic correction must be identity at its calibration reference");

    request.error_px = {22.5f, 0.0f};  // linear target magnitude 0.90
    const auto large = solve_response_model_aim(request);
    require(large.unclamped_stick.x < large.pre_curve_stick.x,
            "Dynamic inverse must avoid treating high-stick gain as linear");
}

void test_cod_dynamic_curve_preserves_radial_target_direction() {
    const pipeline_contract::Vec2f input{0.30f, -0.40f};
    AimResponseCurveConfig config{};
    config.algorithm = AimResponseCurveAlgorithm::CodDynamicLegacyLut;
    const auto output = inverse_aim_response_curve(input, config);
    require_near(output.x / -output.y, 0.75f, 1e-5f,
                 "response plugin must reshape magnitude, not target direction");
}

}  // namespace

int main() {
    try {
        test_radial_direction_and_y_sign();
        test_horizon_and_response_have_physical_units();
        test_motion_feedforward_is_not_multiplied_by_force_cap();
        test_motion_does_not_reverse_current_residual();
        test_current_position_bounds_only_opposing_axis_motion();
        test_orthogonal_motion_cannot_mask_an_axis_reversal();
        test_elliptical_force_envelope_scales_one_vector();
        test_authority_caps_delivered_force_after_curve_inversion();
        test_clear_authority_does_not_multiply_smaller_mode_cap();
        test_cod_dynamic_plugin_round_trips_seed_curve();
        test_cod_dynamic_curve_shapes_target_t_around_reference();
        test_cod_dynamic_curve_preserves_radial_target_direction();
        std::cout << "cod_native_response_model_aim_solver_tests PASS\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "cod_native_response_model_aim_solver_tests FAIL: "
                  << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
