#include "aim_response_curve_plugin.h"
#include "incident_fixture_support.h"
#include "test_support/native_test_registry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

using controller_native::ControllerVisionSnapshot;
using controller_native::NativeGamepadController;
using controller_native::incident_fixture::TargetSpec;

constexpr const char* kIncidentId =
    "bodylock-target-motion-total-20260818";
constexpr int kDurationMs = 900;
constexpr int kStepAtMs = 250;
constexpr int kRecoverAtMs = 550;
constexpr int kReplacementAtMs = 550;
constexpr int kVisionDelayMs = 3;
constexpr int kControlEffectDelayMs = 9;
constexpr float kPlantResponse = 500.0f;
constexpr float kManualX = 0.40f;
constexpr float kBaselineTargetVelocity = 200.0f;
constexpr float kFastTargetVelocity = 300.0f;
constexpr float kRequiredFastStick =
    kFastTargetVelocity / kPlantResponse;
constexpr float kRequiredOutputThreshold = kRequiredFastStick - 0.03f;
constexpr int kRequiredOutputHoldMs = 12;
constexpr int kMaximumResponseLatencyMs = 60;
constexpr float kMaximumStepErrorGrowthPx = 8.0f;
constexpr float kMaximumRecoveryOvershootPx = 8.0f;
constexpr float kMaximumFinalRecoveryErrorPx = 8.0f;
constexpr float kMaximumMatchedErrorGrowthPx = 2.0f;
constexpr float kMaximumReplacementExcessOutput = 0.08f;

struct PendingObservation {
    int ready_ms = 0;
    ControllerVisionSnapshot snapshot{};
};

struct ScenarioSpec {
    std::string name;
    float step_velocity_px_per_sec = kFastTargetVelocity;
    bool recover = true;
    bool replace_target_at_recovery = false;
};

struct ScenarioResult {
    std::string name;
    int fresh_observations = 0;
    int bodylock_active_ms = 0;
    int bodylock_before_step_ms = 0;
    int target_identity_changes = 0;
    int response_latency_ms = -1;
    float error_at_step_px = 0.0f;
    float peak_step_error_px = 0.0f;
    float step_error_growth_px = 0.0f;
    float minimum_recovery_error_px = 0.0f;
    float recovery_overshoot_px = 0.0f;
    float final_error_px = 0.0f;
    float maximum_desired_point_drift = 0.0f;
    float maximum_post_replacement_excess_output = 0.0f;
    bool speed_step_present = false;
    bool stable_target_before_step = false;
    bool d_preserved = false;
    bool trigger_executed = false;
    bool response_oracle = false;
    bool step_error_oracle = false;
    bool recovery_oracle = false;
    bool final_recovery_oracle = false;
    bool replacement_reset_oracle = false;
};

ScenarioResult run_scenario(const ScenarioSpec& spec) {
    auto config = controller_native::incident_fixture::base_config(
        1000.0f, 180.0f);
    config.aim_response_curve.algorithm =
        controller_native::AimResponseCurveAlgorithm::Linear;
    config.ai_aim.ads_completion_radius_px = 8.0f;
    config.ai_aim.ads_completion_fresh_frames = 2;
    config.ai_aim.ads_snap_window_ms = 60;
    config.ai_aim.ads_max_acquisition_ms = 80.0f;
    config.ai_aim.body_lock_max_ai_force = 0.60f;
    config.ai_aim.body_lock_max_ai_force_y = 0.66f;
    config.ai_aim.body_lock_box_tolerance_px = 16.0f;
    config.ai_aim.body_lock_activation_box_px = 150.0f;
    config.ai_aim.visual_authority_enabled = true;
    config.recoil.enabled = false;
    config.recoil.profile_playback_enabled = false;

    double now_seconds = 0.0;
    NativeGamepadController controller(config, &now_seconds);
    const auto physical = controller_native::incident_fixture::ads_input(
        kManualX, 0.0f, false);

    TargetSpec target;
    target.observation_id = 445;
    target.selector_generation = 817;
    target.color_classified = true;
    target.has_enemy_cue = true;
    target.enemy_identity_confirmed = true;

    float true_error_x = 0.0f;
    float target_velocity = kBaselineTargetVelocity;
    std::uint64_t frame_id = 0;
    std::uint64_t previous_source_frame_id = 0;
    std::uint64_t stable_target_id = 0;
    pipeline_contract::Vec2f reference_desired{};
    bool has_reference_desired = false;
    bool use_six_ms_interval = true;
    int next_capture_ms = 0;
    int required_output_run_ms = 0;
    std::deque<PendingObservation> pending;
    std::deque<float> delayed_controls(
        static_cast<std::size_t>(kControlEffectDelayMs), 0.0f);

    ScenarioResult result;
    result.name = spec.name;
    result.speed_step_present =
        spec.step_velocity_px_per_sec >
            kBaselineTargetVelocity + 1.0e-4f;
    result.minimum_recovery_error_px =
        std::numeric_limits<float>::infinity();

    for (int now_ms = 0; now_ms < kDurationMs; ++now_ms) {
        now_seconds = static_cast<double>(now_ms) / 1000.0;
        if (now_ms == kStepAtMs) {
            target_velocity = spec.step_velocity_px_per_sec;
            result.error_at_step_px = true_error_x;
            result.peak_step_error_px = true_error_x;
        }
        if (spec.recover && now_ms == kRecoverAtMs) {
            target_velocity = kBaselineTargetVelocity;
            if (spec.replace_target_at_recovery) {
                target.observation_id = 626;
                target.selector_generation = 818;
                true_error_x = 0.0f;
            }
        }

        if (now_ms == next_capture_ms) {
            const int ready_ms = now_ms + kVisionDelayMs;
            const bool selector_changed = frame_id == 0 ||
                (spec.replace_target_at_recovery &&
                 now_ms == kReplacementAtMs);
            auto snapshot =
                controller_native::incident_fixture::observed_snapshot(
                    target,
                    ++frame_id,
                    now_seconds,
                    true_error_x,
                    0.0f,
                    selector_changed);
            snapshot.ready_time_seconds =
                static_cast<double>(ready_ms) / 1000.0;
            pending.push_back({ready_ms, std::move(snapshot)});
            next_capture_ms += use_six_ms_interval ? 6 : 5;
            use_six_ms_interval = !use_six_ms_interval;
        }

        if (!pending.empty() && pending.front().ready_ms == now_ms) {
            controller.submit_vision_snapshot(pending.front().snapshot);
            pending.pop_front();
        }

        const auto output = controller.build_output(physical);
        const auto& plan = controller.last_target_plan();
        const auto& components = controller.last_output_components();
        const bool bodylock =
            plan.mode == pipeline_contract::ControlMode::BodyLockFollow &&
            plan.target_id != 0;
        if (bodylock) {
            ++result.bodylock_active_ms;
            if (now_ms < kStepAtMs) ++result.bodylock_before_step_ms;
            if (stable_target_id == 0) stable_target_id = plan.target_id;
            if (plan.target_id != stable_target_id) {
                ++result.target_identity_changes;
                stable_target_id = plan.target_id;
            }
            if (!has_reference_desired) {
                reference_desired = plan.desired_point_normalized;
                has_reference_desired = true;
            }
            if (!(spec.replace_target_at_recovery &&
                  now_ms >= kReplacementAtMs)) {
                result.maximum_desired_point_drift = std::max(
                    result.maximum_desired_point_drift,
                    std::hypot(
                        plan.desired_point_normalized.x -
                            reference_desired.x,
                        plan.desired_point_normalized.y -
                            reference_desired.y));
            }
        }

        const bool consumed_fresh = plan.source_frame_id != 0 &&
            plan.source_frame_id != previous_source_frame_id;
        if (consumed_fresh) {
            previous_source_frame_id = plan.source_frame_id;
            ++result.fresh_observations;
        }

        if (now_ms >= kStepAtMs && now_ms < kRecoverAtMs && bodylock) {
            result.peak_step_error_px = std::max(
                result.peak_step_error_px, true_error_x);
            if (components.before_recoil_stick.x >=
                kRequiredOutputThreshold) {
                ++required_output_run_ms;
                if (result.response_latency_ms < 0 &&
                    required_output_run_ms >= kRequiredOutputHoldMs) {
                    result.response_latency_ms =
                        now_ms - kStepAtMs - kRequiredOutputHoldMs + 1;
                }
            } else {
                required_output_run_ms = 0;
            }
        }

        if (now_ms >= kRecoverAtMs && bodylock) {
            result.minimum_recovery_error_px = std::min(
                result.minimum_recovery_error_px, true_error_x);
            if (spec.replace_target_at_recovery &&
                result.target_identity_changes >= 1 &&
                now_ms < kReplacementAtMs + 80) {
                result.maximum_post_replacement_excess_output = std::max(
                    result.maximum_post_replacement_excess_output,
                    std::max(
                        0.0f,
                        components.before_recoil_stick.x - kManualX));
            }
        }

        delayed_controls.push_back(output.right_x);
        const float applied_x = delayed_controls.front();
        delayed_controls.pop_front();
        true_error_x +=
            (target_velocity - applied_x * kPlantResponse) * 0.001f;
    }

    result.final_error_px = true_error_x;
    if (result.response_latency_ms < 0) {
        // Keep the report's failed latency numerically comparable with the
        // declared <= oracle. A value beyond the fixture duration means the
        // required total output was never held long enough.
        result.response_latency_ms = kDurationMs + 1;
    }
    result.step_error_growth_px = std::max(
        0.0f, result.peak_step_error_px - result.error_at_step_px);
    if (!std::isfinite(result.minimum_recovery_error_px)) {
        result.minimum_recovery_error_px = result.final_error_px;
    }
    result.recovery_overshoot_px = std::max(
        0.0f, -result.minimum_recovery_error_px);
    result.stable_target_before_step =
        result.bodylock_before_step_ms >= 150 &&
        result.target_identity_changes ==
            (spec.replace_target_at_recovery ? 1 : 0);
    result.d_preserved = result.maximum_desired_point_drift <= 1.0e-5f;
    result.trigger_executed = result.speed_step_present &&
        result.stable_target_before_step && result.d_preserved &&
        result.fresh_observations >= 120 &&
        std::fabs(result.error_at_step_px) <= 8.0f;
    result.response_oracle = result.response_latency_ms >= 0 &&
        result.response_latency_ms <= kMaximumResponseLatencyMs;
    result.step_error_oracle =
        result.step_error_growth_px <= kMaximumStepErrorGrowthPx;
    result.recovery_oracle =
        result.recovery_overshoot_px <= kMaximumRecoveryOvershootPx;
    result.final_recovery_oracle =
        std::fabs(result.final_error_px) <= kMaximumFinalRecoveryErrorPx;
    result.replacement_reset_oracle =
        !spec.replace_target_at_recovery ||
        result.maximum_post_replacement_excess_output <=
            kMaximumReplacementExcessOutput;
    return result;
}

void write_scenario(std::ostream& out, const ScenarioResult& value) {
    out << std::fixed << std::setprecision(6)
        << "{\"name\":\"" << value.name
        << "\",\"fresh_observations\":" << value.fresh_observations
        << ",\"bodylock_active_ms\":" << value.bodylock_active_ms
        << ",\"bodylock_before_step_ms\":"
        << value.bodylock_before_step_ms
        << ",\"target_identity_changes\":"
        << value.target_identity_changes
        << ",\"response_latency_ms\":" << value.response_latency_ms
        << ",\"error_at_step_px\":" << value.error_at_step_px
        << ",\"peak_step_error_px\":" << value.peak_step_error_px
        << ",\"step_error_growth_px\":" << value.step_error_growth_px
        << ",\"minimum_recovery_error_px\":"
        << value.minimum_recovery_error_px
        << ",\"recovery_overshoot_px\":"
        << value.recovery_overshoot_px
        << ",\"final_error_px\":" << value.final_error_px
        << ",\"maximum_desired_point_drift\":"
        << value.maximum_desired_point_drift
        << ",\"maximum_post_replacement_excess_output\":"
        << value.maximum_post_replacement_excess_output
        << ",\"speed_step_present\":" << std::boolalpha
        << value.speed_step_present
        << ",\"stable_target_before_step\":"
        << value.stable_target_before_step
        << ",\"d_preserved\":" << value.d_preserved
        << ",\"trigger_executed\":" << value.trigger_executed
        << ",\"response_oracle\":" << value.response_oracle
        << ",\"step_error_oracle\":" << value.step_error_oracle
        << ",\"recovery_oracle\":" << value.recovery_oracle
        << ",\"final_recovery_oracle\":"
        << value.final_recovery_oracle
        << ",\"replacement_reset_oracle\":"
        << value.replacement_reset_oracle << '}';
}

}  // namespace

int run_bodylock_target_motion_total_incident_regression(int argc, char** argv) {
    try {
        const std::filesystem::path output_path =
            controller_native::incident_fixture::output_path_from_args(
                argc, argv);
        const auto incident = run_scenario({
            "sustained_same_direction_speed_step",
            kFastTargetVelocity,
            true,
            false,
        });
        const auto no_step = run_scenario({
            "matched_no_speed_step",
            kBaselineTargetVelocity,
            true,
            false,
        });
        const auto replacement = run_scenario({
            "target_generation_replacement",
            kFastTargetVelocity,
            true,
            true,
        });

        const bool no_step_counterfactual =
            !no_step.speed_step_present &&
            no_step.step_error_growth_px <= kMaximumMatchedErrorGrowthPx;
        const bool replacement_counterfactual =
            replacement.target_identity_changes == 1 &&
            replacement.replacement_reset_oracle;
        const bool counterfactuals_valid =
            no_step_counterfactual && replacement_counterfactual;
        const bool pass = incident.trigger_executed &&
            incident.response_oracle && incident.step_error_oracle &&
            incident.recovery_oracle && incident.final_recovery_oracle &&
            counterfactuals_valid;

        auto report =
            controller_native::incident_fixture::open_report(output_path);
        report << std::boolalpha << std::fixed << std::setprecision(6)
            << "{\n  \"schema_version\": 1,\n"
            << "  \"incident_id\": \"" << kIncidentId << "\",\n"
            << "  \"symptom\": \"BodyLock reacts to accumulated horizontal error instead of promptly publishing the total camera command required to sustain a moving target\",\n"
            << "  \"covariates\": {\"controller_hz\":1000,\"vision_hz\":180,\"vision_delay_ms\":"
            << kVisionDelayMs
            << ",\"control_effect_delay_ms\":" << kControlEffectDelayMs
            << ",\"plant_response_px_per_stick_second\":"
            << kPlantResponse << ",\"manual_x\":" << kManualX
            << ",\"baseline_target_velocity_px_per_second\":"
            << kBaselineTargetVelocity
            << ",\"fast_target_velocity_px_per_second\":"
            << kFastTargetVelocity
            << ",\"required_fast_stick\":" << kRequiredFastStick
            << "},\n  \"trigger_executed\": "
            << incident.trigger_executed << ",\n  \"scenarios\": [\n    ";
        write_scenario(report, incident);
        report << ",\n    ";
        write_scenario(report, no_step);
        report << ",\n    ";
        write_scenario(report, replacement);
        report << "\n  ],\n  \"counterfactuals\": {"
            << "\"no_speed_step_valid\":" << no_step_counterfactual
            << ",\"target_replacement_reset_valid\":"
            << replacement_counterfactual
            << "},\n  \"oracles\": [\n"
            << "    {\"id\":\"O1\",\"metric\":\"response_latency_ms\",\"operator\":\"<=\",\"threshold\":"
            << kMaximumResponseLatencyMs << ",\"observed\":"
            << incident.response_latency_ms << ",\"pass\":"
            << incident.response_oracle << "},\n"
            << "    {\"id\":\"O2\",\"metric\":\"step_error_growth_px\",\"operator\":\"<=\",\"threshold\":"
            << kMaximumStepErrorGrowthPx << ",\"observed\":"
            << incident.step_error_growth_px << ",\"pass\":"
            << incident.step_error_oracle << "},\n"
            << "    {\"id\":\"O3\",\"metric\":\"recovery_overshoot_px\",\"operator\":\"<=\",\"threshold\":"
            << kMaximumRecoveryOvershootPx << ",\"observed\":"
            << incident.recovery_overshoot_px << ",\"pass\":"
            << incident.recovery_oracle << "},\n"
            << "    {\"id\":\"O4\",\"metric\":\"absolute_final_recovery_error_px\",\"operator\":\"<=\",\"threshold\":"
            << kMaximumFinalRecoveryErrorPx << ",\"observed\":"
            << std::fabs(incident.final_error_px) << ",\"pass\":"
            << incident.final_recovery_oracle << "}\n"
            << "  ],\n  \"overall_pass\": " << pass << "\n}\n";
        report.close();

        std::cout
            << "[BodylockTargetMotionTotalIncident] latency_ms="
            << incident.response_latency_ms
            << " error_growth_px=" << incident.step_error_growth_px
            << " recovery_overshoot_px="
            << incident.recovery_overshoot_px
            << " counterfactuals=" << counterfactuals_valid
            << " result=" << (pass ? "GREEN" : "RED") << '\n';
        if (!incident.trigger_executed || !counterfactuals_valid) return 3;
        return pass ? 0 : 1;
    } catch (const std::invalid_argument& error) {
        std::cerr << "[BodylockTargetMotionTotalIncident][USAGE] "
                  << error.what() << '\n';
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "[BodylockTargetMotionTotalIncident][ERROR] "
                  << error.what() << '\n';
        return 3;
    }
}

void register_bodylock_target_motion_total_incident_regression(native_test::Registry& registry) {
    registry.add_incident_entry("BaseBodyLock", "incident_bodylock_target_motion_total", "bodylock_target_motion_total_incident.json", run_bodylock_target_motion_total_incident_regression);
}
