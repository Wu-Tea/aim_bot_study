#include "bodylock_follow_controller.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require_true(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

pipeline_contract::TargetPlan moving_plan(float authority = 1.0f) {
    pipeline_contract::TargetPlan plan{};
    plan.lifecycle = pipeline_contract::TargetLifecycle::Observed;
    plan.mode = pipeline_contract::ControlMode::BodyLockFollow;
    plan.error_px = {2.0f, 0.0f};
    plan.error_rate_px_per_sec = {180.0f, 0.0f};
    plan.reliability = authority;
    plan.aim_authority = authority;
    plan.bodylock_demand = 1.0f;
    plan.response_scale = 300.0f;
    plan.response_confidence = 1.0f;
    return plan;
}

void test_motion_feedforward_stays_active_near_center() {
    controller_native::BodylockFollowController controller;
    const auto output = controller.compute(moving_plan(), {}, 0.01f);
    require_true(output.x > 0.15f,
                 "BodyLock must follow motion even when positional error is small");
}

void test_coasting_authority_decays_continuously() {
    controller_native::BodylockFollowController controller;
    auto observed = moving_plan(1.0f);
    auto coasting = moving_plan(0.3f);
    coasting.lifecycle = pipeline_contract::TargetLifecycle::Coasting;
    const auto strong = controller.compute(observed, {}, 0.01f);
    const auto weak = controller.compute(coasting, {}, 0.01f);
    require_true(weak.x > 0.0f && weak.x < strong.x * 0.5f,
                 "coasting must decay rather than drop or remain full strength");
}

void test_manual_correction_remains_available() {
    controller_native::BodylockFollowController controller;
    pipeline_contract::IntentState correction{};
    correction.filtered_right.x = -0.5f;
    correction.right_confidence = 1.0f;
    correction.right_x.confidence = 1.0f;
    const auto neutral = controller.compute(moving_plan(), {}, 0.01f);
    const auto opposed = controller.compute(moving_plan(), correction, 0.01f);
    require_true(opposed.x > 0.0f && opposed.x < neutral.x,
                 "BodyLock must yield smoothly to manual correction");
}

void test_closing_target_brakes_before_crossing() {
    controller_native::BodylockFollowController controller;
    auto stationary = moving_plan();
    stationary.error_px.x = 18.0f;
    stationary.error_rate_px_per_sec.x = 0.0f;
    stationary.response_scale = 500.0f;
    auto closing = stationary;
    closing.error_rate_px_per_sec.x = -240.0f;

    const auto normal = controller.compute(stationary, {}, 0.01f);
    const auto braking = controller.compute(closing, {}, 0.01f);
    require_true(std::fabs(braking.x) < std::fabs(normal.x),
                 "closing BodyLock must reduce the positional request before crossing");
}

void test_near_target_error_has_legacy_grip() {
    controller_native::BodylockFollowController controller;
    auto plan = moving_plan();
    plan.error_px.x = 12.0f;
    plan.error_rate_px_per_sec.x = 0.0f;
    const auto output = controller.compute(plan, {}, 0.01f);
    require_true(output.x >= 0.10f,
                 "near-target BodyLock feedback must retain a perceptible grip");
}

void test_left_strafe_yields_positional_grip_without_dropping_follow() {
    controller_native::BodylockFollowController controller;
    auto plan = moving_plan();
    plan.error_px.x = 12.0f;
    plan.error_rate_px_per_sec.x = 0.0f;
    const auto neutral = controller.compute(plan, {}, 0.01f);
    pipeline_contract::IntentState strafe{};
    strafe.filtered_left.x = 0.8f;
    strafe.left_confidence = 1.0f;
    const auto moving = controller.compute(plan, strafe, 0.01f);
    require_true(std::fabs(moving.x - neutral.x) < 0.0001f,
                 "left stick must affect relative motion estimation, not silently weaken BodyLock grip");
}

void test_feedforward_is_not_scaled_by_force_twice() {
    controller_native::BodylockFollowControllerConfig config{};
    config.max_force_x = 0.40f;
    controller_native::BodylockFollowController controller(config);
    auto plan = moving_plan();
    plan.error_px.x = 0.0f;
    plan.error_rate_px_per_sec.x = 200.0f;
    plan.response_scale = 400.0f;
    const auto output = controller.compute(plan, {}, 0.01f);
    require_true(output.x > 0.25f,
                 "response-normalized motion feedforward must not be multiplied by max force twice");
}

void test_predictive_lead_may_cross_residual_error_direction() {
    controller_native::BodylockFollowController controller;
    auto plan = moving_plan();
    plan.error_px.x = 2.0f;
    plan.error_rate_px_per_sec.x = -180.0f;
    const auto output = controller.compute(plan, {}, 0.01f);
    require_true(output.x < 0.0f,
                 "trusted predictive lead must survive even when it opposes the tiny residual error");
}

void test_learned_response_changes_output_without_changing_nominal_horizon() {
    controller_native::BodylockFollowController controller;
    auto nominal = moving_plan();
    nominal.error_px = {24.0f, 0.0f};
    nominal.error_rate_px_per_sec = {};
    nominal.response_scale = 500.0f;
    const auto nominal_output = controller.compute_detailed(
        nominal, {}, 0.01f, true);

    auto learned = nominal;
    learned.response_scale = 250.0f;
    const auto learned_output = controller.compute_detailed(
        learned, {}, 0.01f, true);
    require_true(learned_output.stick.x > nominal_output.stick.x + 0.01f,
                 "learned BodyLock response must still change controller output");
    require_true(std::fabs(learned_output.response_horizon_seconds -
                               nominal_output.response_horizon_seconds) <= 0.0001f &&
                     std::fabs(learned_output.response_horizon_y_seconds -
                               nominal_output.response_horizon_y_seconds) <= 0.0001f,
                 "learned response must not recompute the fixed nominal horizon");
}

void test_coasting_prediction_cannot_reverse_across_residual_error() {
    controller_native::BodylockFollowController controller;
    auto plan = moving_plan();
    plan.lifecycle = pipeline_contract::TargetLifecycle::Coasting;
    plan.error_px.x = 8.0f;
    plan.error_rate_px_per_sec.x = -240.0f;
    const auto output = controller.compute(plan, {}, 0.01f);
    require_true(output.x >= 0.0f,
                 "stale coasting velocity must not pull away from the remaining target error");
}

void test_fresh_observed_position_bounds_stale_motion_tail() {
    controller_native::BodylockFollowController controller;
    bool saw_bound = false;
    for (const auto& shape : {
             std::pair<float, float>{-6.0f, 180.0f},
             std::pair<float, float>{-10.0f, 260.0f},
             std::pair<float, float>{-14.0f, 320.0f}}) {
        auto plan = moving_plan();
        plan.error_px.x = shape.first;
        plan.error_rate_px_per_sec.x = shape.second;
        const auto output = controller.compute(plan, {}, 0.01f, true);
        require_true(
            output.x <= 0.0f,
            "fresh observed position must not be reversed by stale radial motion");
        const auto detailed = controller.compute_detailed(plan, {}, 0.01f, true);
        require_true(detailed.position_stick.x < 0.0f &&
                         detailed.motion_stick.x > 0.0f,
                     "fixture must expose opposing position and stale motion proposals");
        require_true(detailed.effective_motion_stick.x <= 0.0f,
                     "effective motion must not retain an opposing fresh radial sign");
        saw_bound = saw_bound || detailed.radial_motion_bound_applied;
        require_true(
            detailed.constraint_reason ==
                controller_native::ResponseModelConstraintReason::FreshPositionRadialMotionBound,
            "fresh BodyLock diagnostic must name the radial motion constraint");
    }
    require_true(saw_bound, "fresh stale-motion fixture never exercised the radial bound");
}

void test_fresh_forecast_cannot_reverse_rotated_position() {
    controller_native::BodylockFollowController controller;
    auto plan = moving_plan();
    plan.error_px = {-8.0f, 4.0f};
    plan.error_rate_px_per_sec = {};
    plan.player_motion_forecast_px = {40.0f, -30.0f};
    plan.player_motion_confidence = 1.0f;

    const auto output = controller.compute(plan, {}, 0.01f, true);
    const pipeline_contract::Vec2f control_error{
        plan.error_px.x, -plan.error_px.y};
    require_true(
        output.x * control_error.x + output.y * control_error.y >= -0.0001f,
        "fresh forecast must not reverse the radial position correction");
}

void test_fresh_forecast_preserves_tangent_motion() {
    controller_native::BodylockFollowController controller;
    auto plan = moving_plan();
    plan.error_px = {-8.0f, 4.0f};
    plan.error_rate_px_per_sec = {};
    plan.player_motion_forecast_px = {40.0f, 30.0f};
    plan.player_motion_confidence = 1.0f;
    const auto detailed = controller.compute_detailed(plan, {}, 0.01f, true);
    const pipeline_contract::Vec2f control_error{
        plan.error_px.x, -plan.error_px.y};
    const float error_length = std::hypot(control_error.x, control_error.y);
    const pipeline_contract::Vec2f radial{
        control_error.x / error_length, control_error.y / error_length};
    const pipeline_contract::Vec2f tangent{-radial.y, radial.x};
    const float effective_radial =
        detailed.effective_motion_stick.x * radial.x +
        detailed.effective_motion_stick.y * radial.y;
    const float raw_tangent =
        detailed.motion_stick.x * tangent.x +
        detailed.motion_stick.y * tangent.y;
    const float effective_tangent =
        detailed.effective_motion_stick.x * tangent.x +
        detailed.effective_motion_stick.y * tangent.y;
    require_true(effective_radial >= -0.0001f,
                 "fresh forecast radial proposal must be bounded at the solver");
    require_true(std::fabs(effective_tangent - raw_tangent) <= 0.0001f,
                 "fresh radial forecast bound must preserve tangent motion");
}

void test_fresh_near_center_forecast_envelope_is_continuous() {
    controller_native::BodylockFollowController controller;
    float previous = 0.0f;
    bool first = true;
    for (const float error_x : {0.05f, 0.08f, 0.12f, 0.20f}) {
        auto plan = moving_plan();
        plan.error_px = {error_x, 0.0f};
        plan.error_rate_px_per_sec = {};
        plan.player_motion_forecast_px = {-20.0f, 0.0f};
        plan.player_motion_confidence = 1.0f;
        const auto output = controller.compute_detailed(plan, {}, 0.01f, true);
        require_true(output.stick.x >= -0.0001f,
                     "near-center fresh forecast must not reverse position");
        if (!first) {
            require_true(std::fabs(output.stick.x - previous) < 0.08f,
                         "near-center fresh forecast envelope was discontinuous");
        }
        previous = output.stick.x;
        first = false;
    }
}

void test_nonfresh_forecast_path_remains_numerically_equivalent() {
    controller_native::BodylockFollowController controller;
    auto with_forecast = moving_plan();
    with_forecast.error_px = {12.0f, -7.0f};
    with_forecast.error_rate_px_per_sec = {140.0f, -90.0f};
    with_forecast.player_motion_forecast_px = {18.0f, -11.0f};
    with_forecast.player_motion_confidence = 0.8f;
    const auto original = controller.compute_detailed(
        with_forecast, {}, 0.01f, false);

    auto equivalent = with_forecast;
    equivalent.error_px.x +=
        equivalent.player_motion_forecast_px.x *
        equivalent.player_motion_confidence * 0.65f;
    equivalent.error_px.y +=
        equivalent.player_motion_forecast_px.y *
        equivalent.player_motion_confidence * 0.65f;
    equivalent.player_motion_forecast_px = {};
    equivalent.player_motion_confidence = 0.0f;
    const auto legacy_equivalent = controller.compute_detailed(
        equivalent, {}, 0.01f, false);
    require_true(std::fabs(original.stick.x - legacy_equivalent.stick.x) <= 0.0001f &&
                     std::fabs(original.stick.y - legacy_equivalent.stick.y) <= 0.0001f,
                 "non-fresh forecast path changed its prior numerical behavior");
}

struct CoastResidualRegressionMetrics {
    controller_native::BodylockFollowControllerOutput horizontal{};
    controller_native::BodylockFollowControllerOutput vertical{};
    controller_native::BodylockFollowControllerOutput horizontal_counterfactual{};
    controller_native::BodylockFollowControllerOutput vertical_counterfactual{};
};

CoastResidualRegressionMetrics measure_coast_residual_regression() {
    controller_native::BodylockFollowController controller;
    auto horizontal_plan = moving_plan();
    horizontal_plan.lifecycle = pipeline_contract::TargetLifecycle::Coasting;
    horizontal_plan.error_px = {-10.0f, 0.0f};
    horizontal_plan.error_rate_px_per_sec = {240.0f, 0.0f};

    auto vertical_plan = moving_plan();
    vertical_plan.lifecycle = pipeline_contract::TargetLifecycle::Coasting;
    vertical_plan.error_px = {0.0f, 10.0f};
    vertical_plan.error_rate_px_per_sec = {0.0f, -240.0f};

    auto horizontal_counterfactual = horizontal_plan;
    horizontal_counterfactual.error_rate_px_per_sec = {};
    auto vertical_counterfactual = vertical_plan;
    vertical_counterfactual.error_rate_px_per_sec = {};

    CoastResidualRegressionMetrics metrics;
    metrics.horizontal = controller.compute_detailed(
        horizontal_plan, {}, 0.01f, false);
    metrics.vertical = controller.compute_detailed(
        vertical_plan, {}, 0.01f, false);
    metrics.horizontal_counterfactual = controller.compute_detailed(
        horizontal_counterfactual, {}, 0.01f, false);
    metrics.vertical_counterfactual = controller.compute_detailed(
        vertical_counterfactual, {}, 0.01f, false);
    return metrics;
}

void write_coast_residual_report(
    const std::string& path,
    const CoastResidualRegressionMetrics& metrics) {
    std::ofstream report(path, std::ios::binary | std::ios::trunc);
    if (!report) {
        throw std::runtime_error("could not open coast residual report path");
    }
    const auto reason_is_stale = [](const auto& output) {
        return output.constraint_reason ==
            controller_native::ResponseModelConstraintReason::
                LifecycleStaleMotionDiscarded;
    };
    report << "{\n"
           << "  \"schema_version\": 1,\n"
           << "  \"incident_id\": \"bodylock-coast-residual-hole\",\n"
           << "  \"trigger_count\": 2,\n"
           << "  \"horizontal\": {\n"
           << "    \"error_px\": -10.0,\n"
           << "    \"stale_velocity_px_per_sec\": 240.0,\n"
           << "    \"position_stick\": " << metrics.horizontal.position_stick.x << ",\n"
           << "    \"motion_stick\": " << metrics.horizontal.motion_stick.x << ",\n"
           << "    \"final_stick\": " << metrics.horizontal.stick.x << ",\n"
           << "    \"stale_motion_triggered\": "
           << (reason_is_stale(metrics.horizontal) ? "true" : "false") << "\n"
           << "  },\n"
           << "  \"vertical\": {\n"
           << "    \"error_px\": 10.0,\n"
           << "    \"stale_velocity_px_per_sec\": -240.0,\n"
           << "    \"position_stick\": " << metrics.vertical.position_stick.y << ",\n"
           << "    \"motion_stick\": " << metrics.vertical.motion_stick.y << ",\n"
           << "    \"final_stick\": " << metrics.vertical.stick.y << ",\n"
           << "    \"stale_motion_triggered\": "
           << (reason_is_stale(metrics.vertical) ? "true" : "false") << "\n"
           << "  },\n"
           << "  \"counterfactual_no_stale_velocity\": {\n"
           << "    \"horizontal_final_stick\": "
           << metrics.horizontal_counterfactual.stick.x << ",\n"
           << "    \"vertical_final_stick\": "
           << metrics.vertical_counterfactual.stick.y << "\n"
           << "  }\n"
           << "}\n";
}

void validate_coast_residual_regression(
    const CoastResidualRegressionMetrics& metrics) {
    require_true(
        metrics.horizontal.constraint_reason ==
            controller_native::ResponseModelConstraintReason::
                LifecycleStaleMotionDiscarded &&
        metrics.vertical.constraint_reason ==
            controller_native::ResponseModelConstraintReason::
                LifecycleStaleMotionDiscarded,
        "fixture must execute stale-motion rejection on both axes");
    require_true(
        metrics.horizontal.position_stick.x < -0.05f &&
            metrics.horizontal.motion_stick.x > 0.05f &&
            metrics.vertical.position_stick.y < -0.05f &&
            metrics.vertical.motion_stick.y > 0.05f,
        "fixture must expose opposing position and stale-motion proposals");
    require_true(
        metrics.horizontal_counterfactual.stick.x < -0.05f &&
            metrics.vertical_counterfactual.stick.y < -0.05f,
        "removing stale velocity must restore the toward-residual counterfactual");
    require_true(
        metrics.horizontal.stick.x < -0.05f,
        "Coasting stale X motion discarded the valid 10px position correction");
    require_true(
        metrics.vertical.stick.y < -0.05f,
        "Coasting stale Y motion discarded the valid 10px position correction");
}

void test_coasting_stale_motion_preserves_residual_position_correction() {
    validate_coast_residual_regression(measure_coast_residual_regression());
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--incident-report") {
            const auto metrics = measure_coast_residual_regression();
            write_coast_residual_report(argv[2], metrics);
            validate_coast_residual_regression(metrics);
            return 0;
        }
        test_motion_feedforward_stays_active_near_center();
        test_coasting_authority_decays_continuously();
        test_manual_correction_remains_available();
        test_closing_target_brakes_before_crossing();
        test_near_target_error_has_legacy_grip();
        test_left_strafe_yields_positional_grip_without_dropping_follow();
        test_feedforward_is_not_scaled_by_force_twice();
        test_learned_response_changes_output_without_changing_nominal_horizon();
        test_predictive_lead_may_cross_residual_error_direction();
        test_coasting_prediction_cannot_reverse_across_residual_error();
        test_fresh_observed_position_bounds_stale_motion_tail();
        test_fresh_forecast_cannot_reverse_rotated_position();
        test_fresh_forecast_preserves_tangent_motion();
        test_fresh_near_center_forecast_envelope_is_continuous();
        test_nonfresh_forecast_path_remains_numerically_equivalent();
        test_coasting_stale_motion_preserves_residual_position_correction();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[BodylockFollowControllerTests] FAIL " << error.what() << '\n';
        return 1;
    }
}
