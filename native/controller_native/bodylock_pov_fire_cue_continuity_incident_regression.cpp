#include "aim_response_curve_plugin.h"
#include "incident_fixture_support.h"
#include "test_support/native_test_registry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using controller_native::NativeGamepadController;
using controller_native::incident_fixture::TargetSpec;

constexpr const char* kIncidentId =
    "bodylock-pov-fire-cue-continuity-20260903";
constexpr int kDurationMs = 430;
constexpr int kCueBeginMs = 300;
constexpr int kCueDurationMs = 48;
constexpr int kVisionIntervalMs = 5;
constexpr float kPlantResponse = 500.0f;
constexpr float kPovInducedTargetRatePxPerSecond = 200.0f;
constexpr float kRequiredSustainingStick =
    kPovInducedTargetRatePxPerSecond / kPlantResponse;
constexpr float kMinimumCueMotionCoverageRatio = 0.90f;
constexpr float kMinimumCueOutputRatio = 0.75f;
constexpr float kMaximumCueErrorGrowthPx = 4.0f;
constexpr float kMaximumStaticCueOutput = 0.10f;
constexpr float kMaximumReplacementResidualOutput = 0.10f;

enum class CueVariant {
    MovingSameGeneration,
    StaticSameGeneration,
    FreshReplacement,
};

struct ScenarioResult {
    std::string name;
    int fresh_observations = 0;
    int bodylock_before_cue_ms = 0;
    int firing_cue_ms = 0;
    int same_generation_cue_ms = 0;
    int cue_motion_valid_ms = 0;
    int replacement_observed_ms = 0;
    std::uint64_t pre_cue_target_id = 0;
    std::uint64_t pre_cue_generation = 0;
    std::uint64_t pre_cue_ads_epoch = 0;
    std::uint64_t cue_target_id = 0;
    std::uint64_t cue_generation = 0;
    std::uint64_t cue_ads_epoch = 0;
    float mean_cue_output_x = 0.0f;
    float maximum_abs_cue_output_x = 0.0f;
    float cue_start_error_px = 0.0f;
    float cue_end_error_px = 0.0f;
    float cue_error_growth_px = 0.0f;
    float maximum_replacement_output_x = 0.0f;
    float replacement_end_output_x = 0.0f;
    bool finite_outputs = true;
    bool trigger_executed = false;
};

controller_native::GamepadRuntimeConfig incident_config() {
    auto config = controller_native::incident_fixture::base_config(
        75.0f, 180.0f);
    config.aim_response_curve.algorithm =
        controller_native::AimResponseCurveAlgorithm::Linear;
    config.ai_aim.ads_completion_radius_px = 8.0f;
    config.ai_aim.ads_completion_fresh_frames = 2;
    config.ai_aim.ads_snap_window_ms = 60;
    config.ai_aim.ads_extension_budget_ms = 80.0f;
    config.ai_aim.body_lock_max_ai_force = 0.60f;
    config.ai_aim.body_lock_max_ai_force_y = 0.60f;
    config.ai_aim.cue_hold_body_lock_force_scale = 0.35f;
    config.ai_aim.cue_hold_full_force_min_target_height_ratio = 0.25f;
    config.recoil.enabled = false;
    return config;
}

TargetSpec target(std::uint64_t observation_id, std::uint64_t generation) {
    TargetSpec value;
    value.body_width = 56.0f;
    value.body_height = 140.0f;
    value.observation_id = observation_id;
    value.selector_generation = generation;
    value.color_classified = true;
    value.has_enemy_cue = true;
    value.enemy_identity_confirmed = true;
    return value;
}

ScenarioResult run_scenario(CueVariant variant) {
    const bool moving = variant != CueVariant::StaticSameGeneration;
    const bool replacement = variant == CueVariant::FreshReplacement;
    const TargetSpec original = target(701, 91);
    // Keep the source observation ID constant so this counterfactual proves
    // that selector generation, not an incidental ID change, invalidates the
    // pre-fire motion snapshot.
    const TargetSpec alternate = target(701, 92);
    ScenarioResult result;
    result.name = moving
        ? replacement ? "fresh_generation_replacement" :
            "moving_same_generation_firing_cue"
        : "static_same_generation_firing_cue";

    double now_seconds = 0.0;
    NativeGamepadController controller(incident_config(), &now_seconds);
    float true_error_x = 0.0f;
    float held_cue_error_x = 0.0f;
    std::uint64_t frame_id = 0;
    std::uint64_t previous_source_frame_id = 0;
    float cue_output_sum = 0.0f;

    for (int now_ms = 0; now_ms < kDurationMs; ++now_ms) {
        now_seconds = static_cast<double>(now_ms) / 1000.0;
        const bool in_cue = now_ms >= kCueBeginMs &&
            now_ms < kCueBeginMs + kCueDurationMs;
        const bool firing = in_cue;

        if (now_ms == kCueBeginMs) {
            held_cue_error_x = true_error_x;
            result.cue_start_error_px = true_error_x;
        }
        if (now_ms % kVisionIntervalMs == 0) {
            if (in_cue && !replacement) {
                controller.submit_vision_snapshot(
                    controller_native::incident_fixture::cue_snapshot(
                        original,
                        ++frame_id,
                        now_seconds,
                        held_cue_error_x,
                        0.0f));
            } else {
                const TargetSpec& selected = in_cue && replacement
                    ? alternate : original;
                const float observed_error = in_cue && replacement
                    ? 0.0f : true_error_x;
                controller.submit_vision_snapshot(
                    controller_native::incident_fixture::observed_snapshot(
                        selected,
                        ++frame_id,
                        now_seconds,
                        observed_error,
                        0.0f,
                        now_ms == 0 ||
                            (replacement && now_ms == kCueBeginMs)));
            }
        }

        auto physical = controller_native::incident_fixture::ads_input(
            0.0f, 0.0f, firing);
        // Keep the lateral POV trigger present in every variant. The static
        // case proves left-stick input alone cannot invent motion; the
        // replacement case first forms the same moving snapshot and then
        // changes only selector generation at the cue boundary.
        physical.left_x = 1.0f;
        const auto output = controller.build_output(physical);
        const auto& plan = controller.last_target_plan();
        const auto& components = controller.last_output_components();
        result.finite_outputs = result.finite_outputs &&
            controller_native::incident_fixture::finite_unit(output.right_x) &&
            controller_native::incident_fixture::finite_unit(output.right_y);

        if (plan.source_frame_id != 0 &&
            plan.source_frame_id != previous_source_frame_id) {
            previous_source_frame_id = plan.source_frame_id;
            ++result.fresh_observations;
        }
        const bool bodylock =
            plan.mode == pipeline_contract::ControlMode::BodyLockFollow &&
            plan.target_id != 0;
        if (now_ms < kCueBeginMs && bodylock) {
            ++result.bodylock_before_cue_ms;
            result.pre_cue_target_id = plan.target_id;
            result.pre_cue_generation = plan.selector_target_generation;
            result.pre_cue_ads_epoch = plan.physical_ads_epoch;
        }
        if (in_cue) {
            if (result.cue_target_id == 0) {
                result.cue_target_id = plan.target_id;
                result.cue_generation = plan.selector_target_generation;
                result.cue_ads_epoch = plan.physical_ads_epoch;
            }
            ++result.firing_cue_ms;
            const float output_x = components.before_recoil_stick.x;
            result.maximum_abs_cue_output_x = std::max(
                result.maximum_abs_cue_output_x,
                std::fabs(output_x));
            if (!replacement) {
                if (bodylock && plan.cue_continuation &&
                    plan.selector_target_generation ==
                        original.selector_generation) {
                    ++result.same_generation_cue_ms;
                }
                if (plan.bodylock_target_motion_valid) {
                    ++result.cue_motion_valid_ms;
                }
                cue_output_sum += output_x;
            } else if (plan.selector_target_generation ==
                       alternate.selector_generation) {
                ++result.replacement_observed_ms;
                result.maximum_replacement_output_x = std::max(
                    result.maximum_replacement_output_x,
                    std::max(0.0f, output_x));
                if (now_ms + 1 == kCueBeginMs + kCueDurationMs) {
                    result.replacement_end_output_x = std::fabs(output_x);
                }
            }
        }

        const float target_rate = moving && (!replacement || !in_cue)
            ? kPovInducedTargetRatePxPerSecond : 0.0f;
        true_error_x +=
            (target_rate - output.right_x * kPlantResponse) * 0.001f;
        if (now_ms + 1 == kCueBeginMs + kCueDurationMs) {
            result.cue_end_error_px = true_error_x;
        }
    }

    if (!replacement) {
        result.mean_cue_output_x =
            cue_output_sum / static_cast<float>(kCueDurationMs);
        result.cue_error_growth_px = std::max(
            0.0f,
            result.cue_end_error_px - result.cue_start_error_px);
        result.trigger_executed = result.finite_outputs &&
            result.fresh_observations >= 60 &&
            result.bodylock_before_cue_ms >= 180 &&
            result.firing_cue_ms == kCueDurationMs &&
            result.same_generation_cue_ms >= 9;
    } else {
        result.trigger_executed = result.finite_outputs &&
            result.fresh_observations >= 60 &&
            result.bodylock_before_cue_ms >= 180 &&
            result.firing_cue_ms == kCueDurationMs &&
            result.replacement_observed_ms >= 9;
    }
    return result;
}

void write_scenario(std::ostream& out, const ScenarioResult& value) {
    out << std::boolalpha << std::fixed << std::setprecision(6)
        << "{\"name\":\"" << value.name
        << "\",\"fresh_observations\":" << value.fresh_observations
        << ",\"bodylock_before_cue_ms\":"
        << value.bodylock_before_cue_ms
        << ",\"firing_cue_ms\":" << value.firing_cue_ms
        << ",\"same_generation_cue_ms\":"
        << value.same_generation_cue_ms
        << ",\"cue_motion_valid_ms\":" << value.cue_motion_valid_ms
        << ",\"replacement_observed_ms\":"
        << value.replacement_observed_ms
        << ",\"pre_cue_target_id\":" << value.pre_cue_target_id
        << ",\"pre_cue_generation\":" << value.pre_cue_generation
        << ",\"pre_cue_ads_epoch\":" << value.pre_cue_ads_epoch
        << ",\"cue_target_id\":" << value.cue_target_id
        << ",\"cue_generation\":" << value.cue_generation
        << ",\"cue_ads_epoch\":" << value.cue_ads_epoch
        << ",\"mean_cue_output_x\":" << value.mean_cue_output_x
        << ",\"maximum_abs_cue_output_x\":"
        << value.maximum_abs_cue_output_x
        << ",\"cue_start_error_px\":" << value.cue_start_error_px
        << ",\"cue_end_error_px\":" << value.cue_end_error_px
        << ",\"cue_error_growth_px\":" << value.cue_error_growth_px
        << ",\"maximum_replacement_output_x\":"
        << value.maximum_replacement_output_x
        << ",\"replacement_end_output_x\":"
        << value.replacement_end_output_x
        << ",\"finite_outputs\":" << value.finite_outputs
        << ",\"trigger_executed\":" << value.trigger_executed << '}';
}

}  // namespace

int run_bodylock_pov_fire_cue_continuity_incident_regression(
    int argc,
    char** argv) {
    try {
        const auto output_path =
            controller_native::incident_fixture::output_path_from_args(
                argc, argv);
        const ScenarioResult incident = run_scenario(
            CueVariant::MovingSameGeneration);
        const ScenarioResult stationary = run_scenario(
            CueVariant::StaticSameGeneration);
        const ScenarioResult replacement = run_scenario(
            CueVariant::FreshReplacement);

        const float motion_coverage_ratio =
            static_cast<float>(incident.cue_motion_valid_ms) /
            static_cast<float>(kCueDurationMs);
        const float output_ratio = incident.mean_cue_output_x /
            kRequiredSustainingStick;
        const bool motion_oracle =
            motion_coverage_ratio >= kMinimumCueMotionCoverageRatio;
        const bool output_oracle = output_ratio >= kMinimumCueOutputRatio;
        const bool error_oracle =
            incident.cue_error_growth_px <= kMaximumCueErrorGrowthPx;
        const bool stationary_counterfactual =
            stationary.trigger_executed &&
            stationary.maximum_abs_cue_output_x <=
                kMaximumStaticCueOutput;
        const bool replacement_counterfactual =
            replacement.trigger_executed &&
            replacement.cue_motion_valid_ms == 0 &&
            replacement.replacement_end_output_x <=
                kMaximumReplacementResidualOutput;
        const bool trigger_executed = incident.trigger_executed &&
            stationary.trigger_executed && replacement.trigger_executed;
        const bool counterfactuals_valid = stationary_counterfactual &&
            replacement_counterfactual;
        const bool pass = trigger_executed && counterfactuals_valid &&
            motion_oracle && output_oracle && error_oracle;

        auto report =
            controller_native::incident_fixture::open_report(output_path);
        report << std::boolalpha << std::fixed << std::setprecision(6)
            << "{\n  \"schema_version\": 1,\n"
            << "  \"incident_id\": \"" << kIncidentId << "\",\n"
            << "  \"symptom\": \"BodyLock loses sustaining target-relative demand during POV motion plus a firing smoke/kick cue gap\",\n"
            << "  \"truth\": {\"pov_induced_target_rate_px_per_second\":"
            << kPovInducedTargetRatePxPerSecond
            << ",\"required_sustaining_stick\":"
            << kRequiredSustainingStick << "},\n"
            << "  \"covariates\": {\"controller_hz\":1000,\"vision_hz\":200,\"cue_duration_ms\":"
            << kCueDurationMs
            << ",\"physical_fire_during_cue\":true,\"recoil_output\":\"disabled to isolate same-generation cue continuity\",\"target_count\":1,\"manual_right_stick\":\"neutral\",\"plant_response_px_per_stick_second\":"
            << kPlantResponse << "},\n"
            << "  \"trigger_executed\": " << trigger_executed
            << ",\n  \"scenarios\": [\n    ";
        write_scenario(report, incident);
        report << ",\n    ";
        write_scenario(report, stationary);
        report << ",\n    ";
        write_scenario(report, replacement);
        report << "\n  ],\n  \"counterfactuals\": {\"stationary_cue_valid\":"
            << stationary_counterfactual
            << ",\"fresh_replacement_resets_motion\":"
            << replacement_counterfactual << "},\n"
            << "  \"oracles\": [\n"
            << "    {\"id\":\"O1\",\"metric\":\"cue_motion_valid_coverage_ratio\",\"operator\":\">=\",\"threshold\":"
            << kMinimumCueMotionCoverageRatio << ",\"observed\":"
            << motion_coverage_ratio << ",\"pass\":" << motion_oracle
            << "},\n"
            << "    {\"id\":\"O2\",\"metric\":\"mean_cue_output_over_required_stick\",\"operator\":\">=\",\"threshold\":"
            << kMinimumCueOutputRatio << ",\"observed\":"
            << output_ratio << ",\"pass\":" << output_oracle << "},\n"
            << "    {\"id\":\"O3\",\"metric\":\"cue_error_growth_px\",\"operator\":\"<=\",\"threshold\":"
            << kMaximumCueErrorGrowthPx << ",\"observed\":"
            << incident.cue_error_growth_px << ",\"pass\":"
            << error_oracle << "}\n  ],\n"
            << "  \"overall_pass\": " << pass << "\n}\n";
        report.close();

        std::cout
            << "[BodylockPovFireCueContinuityIncident] motion_coverage="
            << motion_coverage_ratio << " output_ratio=" << output_ratio
            << " error_growth_px=" << incident.cue_error_growth_px
            << " counterfactuals=" << counterfactuals_valid
            << " result=" << (pass ? "GREEN" : "RED") << '\n';
        if (!trigger_executed || !counterfactuals_valid) return 3;
        return pass ? 0 : 1;
    } catch (const std::invalid_argument& error) {
        std::cerr << "[BodylockPovFireCueContinuityIncident][USAGE] "
                  << error.what() << '\n';
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "[BodylockPovFireCueContinuityIncident][ERROR] "
                  << error.what() << '\n';
        return 3;
    }
}

void register_bodylock_pov_fire_cue_continuity_incident_regression(
    native_test::Registry& registry) {
    registry.add_incident_entry(
        "BaseBodyLock",
        "incident_bodylock_pov_fire_cue_continuity",
        "bodylock_pov_fire_cue_continuity_incident.json",
        run_bodylock_pov_fire_cue_continuity_incident_regression);
}
