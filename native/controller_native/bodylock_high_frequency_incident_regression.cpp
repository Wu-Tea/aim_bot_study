#include "aim_response_curve_plugin.h"
#include "incident_fixture_support.h"
#include "test_support/native_test_registry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <fstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

using controller_native::NativeGamepadController;
using controller_native::ControllerVisionSnapshot;
using controller_native::incident_fixture::TargetSpec;

constexpr int kDurationMs = 1'650;
constexpr int kDisturbanceAtMs = 250;
constexpr int kSteadyBeginMs = 400;
constexpr int kVisionResultDelayMs = 3;
constexpr float kDisturbancePx = 20.0f;
constexpr float kSettleRadiusPx = 8.0f;
constexpr float kSignificantOutput = 0.08f;

struct PendingObservation {
    int ready_ms = 0;
    ControllerVisionSnapshot snapshot{};
};

struct FreshSample {
    int at_ms = 0;
    float pre_recoil_y = 0.0f;
};

struct ScenarioResult {
    std::string name;
    int fresh_observations = 0;
    int bodylock_active_ms = 0;
    int target_identity_changes = 0;
    float maximum_desired_point_drift = 0.0f;
    float error_p95_abs_px = 0.0f;
    float error_peak_to_peak_px = 0.0f;
    float output_peak_to_peak = 0.0f;
    int significant_output_reversals = 0;
    float median_reversal_interval_ms = -1.0f;
    float inferred_cycle_hz = 0.0f;
    int stable_settle_ms = -1;
    int permanent_settle_ms = -1;
    float minimum_response_scale = std::numeric_limits<float>::infinity();
    float maximum_response_scale = 0.0f;
    float maximum_response_confidence = 0.0f;
    bool trigger_covered = false;
    bool incident_signature = false;
    bool accuracy_oracle = false;
    bool settle_oracle = false;
    std::vector<int> reversal_at_ms;
};

float percentile95_abs(std::vector<float> values) {
    if (values.empty()) return 0.0f;
    for (float& value : values) value = std::fabs(value);
    std::sort(values.begin(), values.end());
    const std::size_t index = static_cast<std::size_t>(
        std::floor(0.95 * static_cast<double>(values.size() - 1)));
    return values[index];
}

float peak_to_peak(const std::vector<float>& values) {
    if (values.empty()) return 0.0f;
    const auto bounds = std::minmax_element(values.begin(), values.end());
    return *bounds.second - *bounds.first;
}

float median(std::vector<float> values) {
    if (values.empty()) return -1.0f;
    std::sort(values.begin(), values.end());
    const std::size_t middle = values.size() / 2;
    if ((values.size() % 2u) != 0u) return values[middle];
    return 0.5f * (values[middle - 1] + values[middle]);
}

ScenarioResult run_scenario(
    std::string name,
    float plant_response_px_per_stick_second,
    int control_effect_delay_ms,
    int disturbance_at_ms = kDisturbanceAtMs,
    float disturbance_px = kDisturbancePx,
    float target_velocity_y_px_per_sec = 0.0f,
    float configured_response = 0.f) {
    auto config = controller_native::incident_fixture::base_config(
        1000.0f, 180.0f);
    config.aim_response_curve.algorithm =
        controller_native::AimResponseCurveAlgorithm::Linear;
    config.ai_aim.ads_completion_radius_px = kSettleRadiusPx;
    config.ai_aim.ads_completion_fresh_frames = 2;
    config.ai_aim.ads_snap_window_ms = 60;
    config.ai_aim.ads_extension_budget_ms = 80.0f;
    config.ai_aim.body_lock_max_ai_force = 0.45f;
    config.ai_aim.body_lock_max_ai_force_y = 0.50f;
    config.ai_aim.body_lock_box_tolerance_px = kSettleRadiusPx;
    config.ai_aim.body_lock_activation_box_px = 150.0f;
    config.ai_aim.visual_authority_enabled = true;
    // Passive measurement no longer repairs a deliberately wrong control gain.
    // Stability scenarios now explicitly adopt the known synthetic calibration.
    config.ai_aim.ads_free_initial_scale = configured_response > 0.f
        ? configured_response : plant_response_px_per_stick_second;
    config.recoil.enabled = false;
    config.recoil.profile_playback_enabled = false;

    double now_seconds = 0.0;
    NativeGamepadController controller(
        config, &now_seconds);
    const auto physical = controller_native::incident_fixture::ads_input();

    TargetSpec target;
    target.observation_id = 898;
    target.selector_generation = 145;
    target.color_classified = true;
    target.has_enemy_cue = true;
    target.enemy_identity_confirmed = true;

    float true_error_y = 0.0f;
    std::uint64_t frame_id = 0;
    std::uint64_t previous_source_frame_id = 0;
    std::uint64_t stable_target_id = 0;
    pipeline_contract::Vec2f reference_desired{};
    bool has_reference_desired = false;
    int next_capture_ms = 0;
    bool use_six_ms_interval = true;
    std::deque<PendingObservation> pending_observations;
    std::deque<float> delayed_controls(
        static_cast<std::size_t>(std::max(0, control_effect_delay_ms)),
        0.0f);
    std::vector<float> steady_errors;
    std::vector<float> steady_outputs;
    std::vector<float> error_by_ms;
    std::vector<FreshSample> fresh_samples;

    ScenarioResult result;
    result.name = std::move(name);

    for (int now_ms = 0; now_ms < kDurationMs; ++now_ms) {
        now_seconds = static_cast<double>(now_ms) / 1000.0;
        if (now_ms == disturbance_at_ms) true_error_y += disturbance_px;

        if (now_ms == next_capture_ms) {
            const double capture_seconds = now_seconds;
            const int ready_ms = now_ms + kVisionResultDelayMs;
            auto snapshot =
                controller_native::incident_fixture::observed_snapshot(
                    target,
                    ++frame_id,
                    capture_seconds,
                    0.0f,
                    true_error_y,
                    frame_id == 1);
            snapshot.capture_time_seconds = capture_seconds;
            snapshot.ready_time_seconds =
                static_cast<double>(ready_ms) / 1000.0;
            snapshot.state.observed_at_seconds = capture_seconds;
            pending_observations.push_back({ready_ms, std::move(snapshot)});
            next_capture_ms += use_six_ms_interval ? 6 : 5;
            use_six_ms_interval = !use_six_ms_interval;
        }

        if (!pending_observations.empty() &&
            pending_observations.front().ready_ms == now_ms) {
            controller.submit_vision_snapshot(
                pending_observations.front().snapshot);
            pending_observations.pop_front();
        }

        const auto output = controller.build_output(physical);
        const auto& plan = controller.last_target_plan();
        const auto& components = controller.last_output_components();
        const bool bodylock =
            plan.mode == pipeline_contract::ControlMode::BodyLockFollow &&
            plan.target_id != 0;
        if (bodylock) {
            ++result.bodylock_active_ms;
            if (stable_target_id == 0) stable_target_id = plan.target_id;
            if (plan.target_id != stable_target_id) {
                ++result.target_identity_changes;
                stable_target_id = plan.target_id;
            }
            if (!has_reference_desired) {
                reference_desired = plan.desired_point_normalized;
                has_reference_desired = true;
            }
            result.maximum_desired_point_drift = std::max(
                result.maximum_desired_point_drift,
                std::hypot(
                    plan.desired_point_normalized.x - reference_desired.x,
                    plan.desired_point_normalized.y - reference_desired.y));
            result.minimum_response_scale = std::min(
                result.minimum_response_scale, plan.response_scale);
            result.maximum_response_scale = std::max(
                result.maximum_response_scale, plan.response_scale);
            result.maximum_response_confidence = std::max(
                result.maximum_response_confidence,
                plan.response_confidence);
        }

        const bool consumed_fresh = plan.source_frame_id != 0 &&
            plan.source_frame_id != previous_source_frame_id;
        if (consumed_fresh) {
            previous_source_frame_id = plan.source_frame_id;
            ++result.fresh_observations;
            if (now_ms >= kSteadyBeginMs && bodylock) {
                fresh_samples.push_back({
                    now_ms,
                    components.before_recoil_stick.y,
                });
            }
        }

        float applied_y = output.right_y;
        if (!delayed_controls.empty()) {
            delayed_controls.push_back(output.right_y);
            applied_y = delayed_controls.front();
            delayed_controls.pop_front();
        }
        true_error_y += applied_y *
            plant_response_px_per_stick_second * 0.001f;
        true_error_y += target_velocity_y_px_per_sec * 0.001f;

        if (now_ms >= kSteadyBeginMs && bodylock) {
            steady_errors.push_back(true_error_y);
            steady_outputs.push_back(components.before_recoil_stick.y);
            error_by_ms.push_back(true_error_y);
        }
    }

    std::vector<float> reversal_intervals;
    int previous_sign = 0;
    int previous_reversal_ms = -1;
    for (const FreshSample& sample : fresh_samples) {
        if (std::fabs(sample.pre_recoil_y) < kSignificantOutput) continue;
        const int sign = sample.pre_recoil_y > 0.0f ? 1 : -1;
        if (previous_sign != 0 && sign != previous_sign) {
            result.reversal_at_ms.push_back(sample.at_ms);
            if (previous_reversal_ms >= 0) {
                reversal_intervals.push_back(
                    static_cast<float>(sample.at_ms - previous_reversal_ms));
            }
            previous_reversal_ms = sample.at_ms;
        }
        previous_sign = sign;
    }
    result.significant_output_reversals =
        static_cast<int>(result.reversal_at_ms.size());
    result.median_reversal_interval_ms = median(reversal_intervals);
    if (result.median_reversal_interval_ms > 0.0f) {
        result.inferred_cycle_hz =
            500.0f / result.median_reversal_interval_ms;
    }
    result.error_p95_abs_px = percentile95_abs(steady_errors);
    result.error_peak_to_peak_px = peak_to_peak(steady_errors);
    result.output_peak_to_peak = peak_to_peak(steady_outputs);

    constexpr int kStableWindowMs = 120;
    const int settle_search_begin_ms = std::max(
        kSteadyBeginMs, disturbance_at_ms);
    const std::size_t settle_search_begin_index = static_cast<std::size_t>(
        settle_search_begin_ms - kSteadyBeginMs);
    int inside_run = 0;
    for (std::size_t index = settle_search_begin_index;
         index < error_by_ms.size(); ++index) {
        if (std::fabs(error_by_ms[index]) <= kSettleRadiusPx) {
            ++inside_run;
            if (inside_run >= kStableWindowMs) {
                result.stable_settle_ms =
                    kSteadyBeginMs + static_cast<int>(index) -
                    kStableWindowMs + 1 - disturbance_at_ms;
                break;
            }
        } else {
            inside_run = 0;
        }
    }

    // A limit cycle may briefly spend 120 ms inside the radius and then leave
    // again. Record the first point after which the remainder of the run stays
    // captured; this is the stronger no-late-regression settle oracle.
    int final_outside_index = static_cast<int>(
        settle_search_begin_index) - 1;
    for (std::size_t index = settle_search_begin_index;
         index < error_by_ms.size(); ++index) {
        if (std::fabs(error_by_ms[index]) > kSettleRadiusPx) {
            final_outside_index = static_cast<int>(index);
        }
    }
    const int permanent_index = final_outside_index + 1;
    if (permanent_index < static_cast<int>(error_by_ms.size()) &&
        static_cast<int>(error_by_ms.size()) - permanent_index >=
            kStableWindowMs) {
        result.permanent_settle_ms =
            kSteadyBeginMs + permanent_index - disturbance_at_ms;
    }

    result.trigger_covered = result.fresh_observations >= 200 &&
        result.bodylock_active_ms >= 1'200 &&
        result.target_identity_changes == 0 &&
        result.maximum_desired_point_drift <= 1.0e-5f;
    if (!std::isfinite(result.minimum_response_scale)) {
        result.minimum_response_scale = 0.0f;
    }
    const bool incident_frequency = result.inferred_cycle_hz >= 8.0f &&
        result.inferred_cycle_hz <= 24.0f;
    result.incident_signature =
        result.significant_output_reversals >= 6 &&
        incident_frequency &&
        result.output_peak_to_peak >= 0.30f &&
        result.error_peak_to_peak_px >= 16.0f;
    result.accuracy_oracle = result.error_p95_abs_px <= kSettleRadiusPx;
    result.settle_oracle = result.stable_settle_ms >= 0 &&
        result.stable_settle_ms <= 250 &&
        result.permanent_settle_ms >= 0 &&
        result.permanent_settle_ms <= 250;
    return result;
}

// Same seven plants and acceptance checks as the original incident fixture.
// Keep the controller/capture schedule in run_scenario; reports are diagnostic.
void check_incident(const native_test::TestContext& context) {
    std::ofstream report(context.artifact_path("bodylock-high-frequency.csv"));
    if (!report) throw std::runtime_error("cannot write incident report");
    report << "scenario,fresh_frames,bodylock_ms,identity_changes,point_drift,p95_error_px,error_peak_to_peak,output_peak_to_peak,reversals,cycle_hz,stable_settle_ms,permanent_settle_ms,min_response,max_response,max_response_confidence,pass\n";
    bool all_pass = true;
    const auto check = [&](const ScenarioResult& r, bool require_settle, bool fixed_gain = false) {
        const bool pass = fixed_gain
            ? r.minimum_response_scale == 500.f && r.maximum_response_scale == 500.f && r.maximum_response_confidence == 0.f
            : r.trigger_covered && !r.incident_signature && r.accuracy_oracle && (!require_settle || r.settle_oracle);
        all_pass &= pass;
        report << r.name << ',' << r.fresh_observations << ',' << r.bodylock_active_ms << ','
            << r.target_identity_changes << ',' << r.maximum_desired_point_drift << ','
            << r.error_p95_abs_px << ',' << r.error_peak_to_peak_px << ',' << r.output_peak_to_peak << ','
            << r.significant_output_reversals << ',' << r.inferred_cycle_hz << ','
            << r.stable_settle_ms << ',' << r.permanent_settle_ms << ','
            << r.minimum_response_scale << ',' << r.maximum_response_scale << ','
            << r.maximum_response_confidence << ',' << pass << '\n';
    };
    check(run_scenario("apex_like_fixed_response", 2375.f, 9), true);
    check(run_scenario("matched_fallback_response", 500.f, 9), false);
    check(run_scenario("apex_like_delay_6ms", 2375.f, 6), true);
    check(run_scenario("apex_like_delay_12ms", 2375.f, 12), true);
    check(run_scenario("apex_like_calibrated_step", 2375.f, 9, 1000, 20.f), true);
    check(run_scenario("apex_like_moving_target", 2375.f, 9, 250, 20.f, 80.f), false);
    check(run_scenario("configured_gain_stays_fixed", 2375.f, 9, 250, 20.f, 0.f, 500.f), false, true);
    if (!all_pass) throw std::runtime_error("BodyLock incident regression; see bodylock-high-frequency.csv");
}

}  // namespace

void register_bodylock_high_frequency_incident_regression(native_test::Registry& registry) {
    registry.add_context_case("BaseBodyLock", "incident_bodylock_high_frequency", check_incident);
}
