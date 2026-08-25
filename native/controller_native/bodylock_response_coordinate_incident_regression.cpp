#include "aim_response_curve_plugin.h"
#include "bodylock_follow_controller.h"
#include "incident_fixture_support.h"
#include "test_support/native_test_registry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>

namespace {

using controller_native::AimResponseCurveAlgorithm;
using controller_native::BodylockFollowController;
using controller_native::BodylockFollowControllerConfig;
using controller_native::GamepadRuntimeConfig;
using controller_native::NativeGamepadController;

constexpr const char* kIncidentId =
    "bodylock-response-coordinate-20260825";
constexpr float kTrueResponsePxPerNormalizedResponseSecond = 500.0f;
constexpr float kMaximumResidualPx = 3.0f;
constexpr std::uint64_t kObservationId = 27001;
constexpr std::uint64_t kSelectorGeneration = 2701;

struct CurveRun {
    const char* name = "";
    float initial_requested_x = 0.0f;
    float learned_response_scale = 0.0f;
    float learned_response_confidence = 0.0f;
    float final_training_error_px = 0.0f;
    float bodylock_requested_x = 0.0f;
    float bodylock_normalized_response_x = 0.0f;
    float bodylock_residual_after_horizon_px = 0.0f;
    int fresh_frames = 0;
    bool target_owned = false;
    bool finite = false;
};

controller_native::incident_fixture::TargetSpec target_spec() {
    controller_native::incident_fixture::TargetSpec target;
    target.observation_id = kObservationId;
    target.selector_generation = kSelectorGeneration;
    target.has_enemy_cue = true;
    target.enemy_identity_confirmed = true;
    return target;
}

GamepadRuntimeConfig config_for(AimResponseCurveAlgorithm algorithm) {
    auto config = controller_native::incident_fixture::base_config(
        1000.0f, 260.0f);
    config.ai_aim.ads_snap_window_ms = 60;
    config.ai_aim.ads_completion_fresh_frames = 1000;
    config.ai_aim.ads_extension_budget_ms = 1000.0f;
    config.ai_aim.aim_response_effect_delay_ms = 0.0f;
    config.ai_aim.visual_authority_enabled = false;
    config.aim_response_curve.algorithm = algorithm;
    config.aim_response_curve.calibration_reference_stick = 0.50f;
    return config;
}

float bodylock_residual(
    float learned_response,
    float learned_confidence,
    const controller_native::AimResponseCurveConfig& curve,
    float* requested_x,
    float* normalized_response_x) {
    BodylockFollowControllerConfig bodylock_config;
    bodylock_config.max_force_x = 0.60f;
    bodylock_config.max_force_y = 0.60f;
    bodylock_config.feedback_range_x_px = 24.0f;
    bodylock_config.feedback_range_y_px = 24.0f;
    bodylock_config.fallback_response_px_per_stick_second =
        kTrueResponsePxPerNormalizedResponseSecond;
    bodylock_config.response_curve = curve;
    BodylockFollowController bodylock(bodylock_config);

    pipeline_contract::TargetPlan plan;
    plan.mode = pipeline_contract::ControlMode::BodyLockFollow;
    plan.lifecycle = pipeline_contract::TargetLifecycle::Observed;
    plan.target_id = 1;
    plan.error_px = {24.0f, 0.0f};
    plan.reliability = 1.0f;
    plan.aim_authority = 1.0f;
    plan.response_scale = learned_response;
    plan.response_confidence = learned_confidence;
    const auto solved = bodylock.compute_detailed(
        plan, pipeline_contract::IntentState{}, 0.001f);
    const auto normalized = controller_native::forward_aim_response_curve(
        solved.stick, curve);
    if (requested_x != nullptr) *requested_x = solved.stick.x;
    if (normalized_response_x != nullptr) {
        *normalized_response_x = normalized.x;
    }
    const float horizon = bodylock_config.feedback_range_x_px /
        (bodylock_config.max_force_x *
         bodylock_config.fallback_response_px_per_stick_second);
    return std::fabs(
        plan.error_px.x -
        normalized.x * kTrueResponsePxPerNormalizedResponseSecond *
            horizon);
}

float initial_ads_request(AimResponseCurveAlgorithm algorithm) {
    auto config = config_for(algorithm);
    double now = 20.0;
    NativeGamepadController controller(config, &now);
    const auto target = target_spec();
    controller.submit_vision_snapshot(
        controller_native::incident_fixture::observed_snapshot(
            target, 1, now, 6.0f, 0.0f, true));
    (void)controller.build_output(
        controller_native::incident_fixture::ads_input());
    return controller.last_output_components().requested_assist_stick.x;
}

CurveRun run_training(AimResponseCurveAlgorithm algorithm, const char* name) {
    auto config = config_for(algorithm);
    double now = 100.0;
    NativeGamepadController controller(config, &now);
    const auto target = target_spec();
    std::uint64_t frame_id = 1;
    float error_px = 200.0f;
    controller_native::GamepadOutputState output{};

    // The simulated game plant is linear in normalized camera-response space,
    // exactly the space consumed by ResponseModelAimSolver before it applies
    // the configured inverse curve. Production learning must use that same
    // coordinate, not the raw virtual-stick coordinate.
    for (int tick = 0; tick < 450; ++tick) {
        if (tick % 5 == 0) {
            controller.submit_vision_snapshot(
                controller_native::incident_fixture::observed_snapshot(
                    target,
                    frame_id++,
                    now,
                    error_px,
                    0.0f,
                    tick == 0));
        }
        output = controller.build_output(
            controller_native::incident_fixture::ads_input());
        const auto normalized =
            controller_native::forward_aim_response_curve(
                {output.right_x, output.right_y},
                config.aim_response_curve);
        error_px -= normalized.x *
            kTrueResponsePxPerNormalizedResponseSecond * 0.001f;
        // Keep the source point on the same side of center. This makes the
        // learned-scale result independent of center-cross completion.
        error_px = std::max(error_px, 10.0f);
        now += 0.001;
    }

    const auto& plan = controller.last_target_plan();
    CurveRun result;
    result.name = name;
    result.initial_requested_x = initial_ads_request(algorithm);
    result.learned_response_scale = plan.response_scale;
    result.learned_response_confidence = plan.response_confidence;
    result.final_training_error_px = error_px;
    result.fresh_frames = static_cast<int>(frame_id - 1);
    result.target_owned = plan.target_id != 0;
    result.bodylock_residual_after_horizon_px = bodylock_residual(
        result.learned_response_scale,
        result.learned_response_confidence,
        config.aim_response_curve,
        &result.bodylock_requested_x,
        &result.bodylock_normalized_response_x);
    result.finite =
        std::isfinite(result.initial_requested_x) &&
        std::isfinite(result.learned_response_scale) &&
        std::isfinite(result.learned_response_confidence) &&
        std::isfinite(result.bodylock_requested_x) &&
        std::isfinite(result.bodylock_normalized_response_x) &&
        std::isfinite(result.bodylock_residual_after_horizon_px);
    return result;
}

struct Report {
    CurveRun linear;
    CurveRun cod_dynamic;
    bool trigger_executed = false;
    bool counterfactuals_valid = false;
    bool ads_uses_configured_curve = false;
    bool response_learning_coordinate_consistent = false;
    bool bodylock_closes_within_horizon = false;
    bool overall_pass = false;
};

Report evaluate() {
    Report report;
    report.linear = run_training(AimResponseCurveAlgorithm::Linear, "linear");
    report.cod_dynamic = run_training(
        AimResponseCurveAlgorithm::CodDynamicLegacyLut,
        "cod_dynamic_legacy_lut");
    report.trigger_executed =
        report.linear.target_owned && report.cod_dynamic.target_owned &&
        report.linear.finite && report.cod_dynamic.finite &&
        report.linear.fresh_frames >= 80 &&
        report.cod_dynamic.fresh_frames >= 80 &&
        report.cod_dynamic.learned_response_confidence > 0.0f;
    report.counterfactuals_valid =
        report.linear.learned_response_scale >= 400.0f &&
        report.linear.learned_response_scale <= 600.0f &&
        report.linear.bodylock_residual_after_horizon_px <=
            kMaximumResidualPx;
    // At a 6 px error and 60 ms horizon, the nonlinear inverse requires more
    // raw stick than the linear curve. Equal outputs prove the ADS config was
    // not wired into its solver.
    report.ads_uses_configured_curve =
        report.cod_dynamic.initial_requested_x >=
            report.linear.initial_requested_x + 0.05f;
    report.response_learning_coordinate_consistent =
        std::fabs(
            report.cod_dynamic.learned_response_scale -
            kTrueResponsePxPerNormalizedResponseSecond) <= 100.0f;
    report.bodylock_closes_within_horizon =
        report.cod_dynamic.bodylock_residual_after_horizon_px <=
            kMaximumResidualPx;
    report.overall_pass =
        report.trigger_executed && report.counterfactuals_valid &&
        report.ads_uses_configured_curve &&
        report.response_learning_coordinate_consistent &&
        report.bodylock_closes_within_horizon;
    return report;
}

void write_run(
    std::ostream& stream,
    const CurveRun& value,
    bool comma) {
    stream << "    \"" << value.name << "\": {"
           << "\"initial_requested_x\":" << value.initial_requested_x
           << ",\"learned_response_scale\":"
           << value.learned_response_scale
           << ",\"learned_response_confidence\":"
           << value.learned_response_confidence
           << ",\"final_training_error_px\":"
           << value.final_training_error_px
           << ",\"bodylock_requested_x\":"
           << value.bodylock_requested_x
           << ",\"bodylock_normalized_response_x\":"
           << value.bodylock_normalized_response_x
           << ",\"bodylock_residual_after_horizon_px\":"
           << value.bodylock_residual_after_horizon_px
           << ",\"fresh_frames\":" << value.fresh_frames
           << ",\"target_owned\":" << value.target_owned
           << ",\"finite\":" << value.finite << "}"
           << (comma ? "," : "") << "\n";
}

void write_report(const std::filesystem::path& output, const Report& report) {
    auto stream = controller_native::incident_fixture::open_report(output);
    stream << std::boolalpha << std::fixed << std::setprecision(6)
           << "{\n"
           << "  \"schema_version\": 1,\n"
           << "  \"incident_id\": \"" << kIncidentId << "\",\n"
           << "  \"symptom\": \"BodyLock request is undersized because response learning and nonlinear solving use different command coordinates\",\n"
           << "  \"known_plant_response_px_per_normalized_response_second\": "
           << kTrueResponsePxPerNormalizedResponseSecond << ",\n"
           << "  \"runs\": {\n";
    write_run(stream, report.linear, true);
    write_run(stream, report.cod_dynamic, false);
    stream << "  },\n"
           << "  \"trigger_executed\": " << report.trigger_executed
           << ",\n"
           << "  \"counterfactuals_valid\": "
           << report.counterfactuals_valid << ",\n"
           << "  \"oracles\": {\n"
           << "    \"ads_uses_configured_curve\": "
           << report.ads_uses_configured_curve << ",\n"
           << "    \"response_learning_coordinate_consistent\": "
           << report.response_learning_coordinate_consistent << ",\n"
           << "    \"bodylock_closes_within_horizon\": "
           << report.bodylock_closes_within_horizon << "\n"
           << "  },\n"
           << "  \"overall_pass\": " << report.overall_pass << "\n"
           << "}\n";
}

}  // namespace

int run_bodylock_response_coordinate_incident_regression(
    int argc,
    char** argv) {
    try {
        const auto output =
            controller_native::incident_fixture::output_path_from_args(
                argc,
                argv,
                "bodylock_response_coordinate_incident.json");
        const auto report = evaluate();
        write_report(output, report);
        std::cout << "incident=" << kIncidentId
                  << " linear_scale="
                  << report.linear.learned_response_scale
                  << " dynamic_scale="
                  << report.cod_dynamic.learned_response_scale
                  << " dynamic_residual="
                  << report.cod_dynamic.bodylock_residual_after_horizon_px
                  << " overall=" << (report.overall_pass ? "GREEN" : "RED")
                  << '\n';
        return report.overall_pass ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "incident fixture failed: " << error.what() << '\n';
        return 2;
    }
}

void register_bodylock_response_coordinate_incident_regression(
    native_test::Registry& registry) {
    registry.add_incident_entry(
        "BaseBodyLock",
        "incident_bodylock_response_coordinate",
        "bodylock_response_coordinate_incident.json",
        run_bodylock_response_coordinate_incident_regression);
}
