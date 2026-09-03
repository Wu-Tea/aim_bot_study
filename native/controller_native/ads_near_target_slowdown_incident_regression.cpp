#include "ads_acquisition_controller.h"
#include "aim_response_curve_plugin.h"
#include "ads_response_estimator.h"
#include "bodylock_follow_controller.h"
#include "incident_fixture_support.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using controller_native::NativeGamepadController;
using controller_native::incident_fixture::TargetSpec;

constexpr const char* kIncidentId =
    "ads-near-target-slowdown-20260903";
constexpr int kDurationMs = 7000;
constexpr float kInitialErrorPx = 120.0f;
constexpr float kFreeResponsePxPerStickSecond = 500.0f;
constexpr float kSlowResponsePxPerStickSecond = 200.0f;
constexpr int kControlEffectDelayMs = 9;
constexpr float kVisibleFrameBudgetMs = 1000.0f / 30.0f;
constexpr float kBodyWidthPx = 56.0f;
constexpr float kBodyHeightPx = 140.0f;
constexpr std::uint64_t kObservationId = 90301;
constexpr std::uint64_t kSelectorGeneration = 9031;

enum class Axis {
    X,
    Y,
};

struct ScenarioSpec {
    const char* name = "";
    Axis axis = Axis::X;
    bool slow_zone = false;
    bool allow_handoff = false;
    bool visual_authority_enabled = true;
};

struct ScenarioResult {
    std::string name;
    int fresh_observations = 0;
    int ads_ticks = 0;
    int bodylock_ticks = 0;
    int slow_zone_ticks = 0;
    int enter_40_ms = -1;
    int enter_20_ms = -1;
    int enter_8_ms = -1;
    int enter_2_ms = -1;
    float response_at_20_px = 0.0f;
    float response_confidence_at_20_px = 0.0f;
    float response_at_8_px = 0.0f;
    float response_confidence_at_8_px = 0.0f;
    int near_20_to_2_ms = -1;
    int handoff_ms = -1;
    float handoff_error_px = 0.0f;
    float handoff_authority = 0.0f;
    float handoff_visual_authority = 0.0f;
    float last_ads_requested = 0.0f;
    float first_bodylock_requested = 0.0f;
    float last_ads_shaped = 0.0f;
    float first_bodylock_shaped = 0.0f;
    float last_ads_final = 0.0f;
    float first_bodylock_final = 0.0f;
    float first_bodylock_shaped_retention = 0.0f;
    float final_error_px = 0.0f;
    float final_response_scale = 0.0f;
    float final_response_confidence = 0.0f;
    float mean_closing_speed_40_to_20 = 0.0f;
    float mean_closing_speed_20_to_8 = 0.0f;
    float mean_closing_speed_8_to_2 = 0.0f;
    int fresh_slow_command_pairs = 0;
    int fresh_slow_command_pairs_over_learning_threshold = 0;
    float maximum_fresh_slow_command_delta = 0.0f;
    bool target_admitted = false;
    bool remained_ads_until_2px = false;
    bool handoff_executed = false;
    bool finite_outputs = true;
};

struct LearningProbe {
    std::string name;
    int samples = 0;
    int accepted_updates = 0;
    float command_delta = 0.0f;
    float estimated_scale = 0.0f;
    float confidence = 0.0f;
};

struct SweepPoint {
    float error_px = 0.0f;
    float ads_requested = 0.0f;
    float full_bodylock_requested = 0.0f;
    float low_bodylock_requested = 0.0f;
    float low_to_ads_retention = 0.0f;
    float low_to_full_bodylock_retention = 0.0f;
    bool low_bodylock_at_authority_cap = false;
};

struct AxisSweep {
    Axis axis = Axis::X;
    std::array<SweepPoint, 5> points{};
};

struct IncidentReport {
    ScenarioResult uniform_ads_x;
    ScenarioResult uniform_ads_y;
    ScenarioResult slow_ads_x;
    ScenarioResult slow_ads_y;
    ScenarioResult slow_full_handoff_x;
    ScenarioResult slow_full_handoff_y;
    ScenarioResult slow_low_handoff_x;
    ScenarioResult slow_low_handoff_y;
    AxisSweep sweep_x;
    AxisSweep sweep_y;
    LearningProbe smooth_learning_probe;
    LearningProbe excited_learning_probe;
    float slow_zone_extra_tail_x_ms = 0.0f;
    float slow_zone_extra_tail_y_ms = 0.0f;
    float low_authority_extra_tail_x_ms = 0.0f;
    float low_authority_extra_tail_y_ms = 0.0f;
    bool all_triggers_executed = false;
    bool counterfactuals_valid = false;
    bool slow_zone_compensation_x_pass = false;
    bool slow_zone_compensation_y_pass = false;
    bool overall_pass = false;
};

const char* axis_name(Axis axis) noexcept {
    return axis == Axis::X ? "x" : "y";
}

template <typename Vector>
float axis_value(const Vector& value, Axis axis) noexcept {
    // Positive means motion toward a target with positive screen-space error.
    return axis == Axis::X ? value.x : -value.y;
}

pipeline_contract::Vec2f axis_error(float error, Axis axis) noexcept {
    return axis == Axis::X
        ? pipeline_contract::Vec2f{error, 0.0f}
        : pipeline_contract::Vec2f{0.0f, error};
}

controller_native::GamepadRuntimeConfig incident_config(
    bool allow_handoff,
    bool visual_authority_enabled) {
    auto config = controller_native::incident_fixture::base_config(
        75.0f, 180.0f);
    config.aim_response_curve.algorithm =
        controller_native::AimResponseCurveAlgorithm::CodDynamicLegacyLut;
    config.aim_response_curve.calibration_reference_stick = 0.50f;
    config.ai_aim.ads_snap_window_ms = 135;
    config.ai_aim.ads_completion_radius_px = 8.0f;
    config.ai_aim.ads_completion_fresh_frames = allow_handoff ? 3 : 1000;
    config.ai_aim.ads_target_wait_ms = 220.0f;
    config.ai_aim.ads_extension_budget_ms = 2500.0f;
    config.ai_aim.body_lock_max_ai_force = 0.60f;
    config.ai_aim.body_lock_max_ai_force_y = 0.66f;
    config.ai_aim.body_lock_box_tolerance_px = 16.0f;
    config.ai_aim.body_lock_activation_box_px = 120.0f;
    config.ai_aim.aim_response_effect_delay_ms =
        static_cast<float>(kControlEffectDelayMs);
    config.ai_aim.visual_authority_enabled = visual_authority_enabled;
    config.recoil.enabled = false;
    config.recoil.profile_playback_enabled = false;
    return config;
}

TargetSpec target_spec() {
    TargetSpec spec;
    spec.body_width = kBodyWidthPx;
    spec.body_height = kBodyHeightPx;
    spec.observation_id = kObservationId;
    spec.selector_generation = kSelectorGeneration;
    spec.color_classified = true;
    spec.has_enemy_cue = false;
    spec.enemy_identity_confirmed = false;
    spec.confidence = 0.95f;
    return spec;
}

void note_threshold(int now_ms, float error, float threshold, int& value) {
    if (value < 0 && error <= threshold) value = now_ms + 1;
}

ScenarioResult run_scenario(const ScenarioSpec& spec) {
    const auto config = incident_config(
        spec.allow_handoff, spec.visual_authority_enabled);
    const auto target = target_spec();
    double now_seconds = 0.0;
    NativeGamepadController controller(config, &now_seconds);
    std::deque<float> delayed_commands(
        static_cast<std::size_t>(kControlEffectDelayMs), 0.0f);
    ScenarioResult result;
    result.name = spec.name;
    float error = kInitialErrorPx;
    std::uint64_t frame_id = 0;
    std::uint64_t previous_source_frame_id = 0;
    int next_capture_ms = 0;
    bool use_six_ms_interval = true;
    pipeline_contract::ControlMode previous_mode =
        pipeline_contract::ControlMode::Manual;
    float previous_requested = 0.0f;
    float previous_shaped = 0.0f;
    float previous_final = 0.0f;
    float previous_fresh_slow_command = 0.0f;
    bool has_previous_fresh_slow_command = false;
    double closing_40_to_20_sum = 0.0;
    double closing_20_to_8_sum = 0.0;
    double closing_8_to_2_sum = 0.0;
    int closing_40_to_20_count = 0;
    int closing_20_to_8_count = 0;
    int closing_8_to_2_count = 0;

    for (int now_ms = 0; now_ms < kDurationMs; ++now_ms) {
        now_seconds = static_cast<double>(now_ms) / 1000.0;
        if (now_ms == next_capture_ms) {
            const float dx = spec.axis == Axis::X ? error : 0.0f;
            const float dy = spec.axis == Axis::Y ? error : 0.0f;
            controller.submit_vision_snapshot(
                controller_native::incident_fixture::observed_snapshot(
                    target,
                    ++frame_id,
                    now_seconds,
                    dx,
                    dy,
                    now_ms == 0));
            next_capture_ms += use_six_ms_interval ? 6 : 5;
            use_six_ms_interval = !use_six_ms_interval;
        }

        const auto output = controller.build_output(
            controller_native::incident_fixture::ads_input());
        const auto& plan = controller.last_target_plan();
        const auto& components = controller.last_output_components();
        result.finite_outputs = result.finite_outputs &&
            controller_native::incident_fixture::finite_unit(output.right_x) &&
            controller_native::incident_fixture::finite_unit(output.right_y);
        const bool new_source_frame = plan.source_frame_id != 0 &&
            plan.source_frame_id != previous_source_frame_id;
        if (new_source_frame) {
            previous_source_frame_id = plan.source_frame_id;
            ++result.fresh_observations;
        }
        result.target_admitted = result.target_admitted ||
            (plan.target_id != 0 && plan.ads_plan_admitted);
        if (plan.mode == pipeline_contract::ControlMode::AdsAcquire) {
            ++result.ads_ticks;
        } else if (
            plan.mode == pipeline_contract::ControlMode::BodyLockFollow) {
            ++result.bodylock_ticks;
        }

        const float requested = axis_value(
            components.requested_assist_stick, spec.axis);
        const float shaped = axis_value(
            components.shaped_assist_stick, spec.axis);
        const float final_output = spec.axis == Axis::X
            ? output.right_x : -output.right_y;
        if (previous_mode == pipeline_contract::ControlMode::AdsAcquire &&
            plan.mode == pipeline_contract::ControlMode::BodyLockFollow &&
            !result.handoff_executed) {
            result.handoff_executed = true;
            result.handoff_ms = now_ms;
            result.handoff_error_px = std::fabs(error);
            result.handoff_authority = plan.aim_authority;
            result.handoff_visual_authority = plan.visual_authority;
            result.last_ads_requested = previous_requested;
            result.first_bodylock_requested = requested;
            result.last_ads_shaped = previous_shaped;
            result.first_bodylock_shaped = shaped;
            result.last_ads_final = previous_final;
            result.first_bodylock_final = final_output;
            result.first_bodylock_shaped_retention =
                std::fabs(previous_shaped) > 1.0e-6f
                ? std::fabs(shaped / previous_shaped) : 0.0f;
        }

        const auto response_command =
            controller_native::forward_aim_response_curve(
                {output.right_x, output.right_y},
                config.aim_response_curve);
        delayed_commands.push_back(axis_value(response_command, spec.axis));
        const float applied_command = delayed_commands.front();
        delayed_commands.pop_front();
        const float slow_weight = spec.slow_zone
            ? controller_native::aim_response_slow_zone_weight(
                axis_error(error, spec.axis),
                {kBodyWidthPx, kBodyHeightPx})
            : 0.0f;
        if (slow_weight >= 0.5f) ++result.slow_zone_ticks;
        if (new_source_frame && slow_weight >= 0.5f) {
            const float current_command =
                axis_value(response_command, spec.axis);
            if (has_previous_fresh_slow_command) {
                const float delta = std::fabs(
                    current_command - previous_fresh_slow_command);
                ++result.fresh_slow_command_pairs;
                result.maximum_fresh_slow_command_delta = std::max(
                    result.maximum_fresh_slow_command_delta, delta);
                if (delta >= 0.04f) {
                    ++result.fresh_slow_command_pairs_over_learning_threshold;
                }
            }
            previous_fresh_slow_command = current_command;
            has_previous_fresh_slow_command = true;
        }
        const float plant_response = kFreeResponsePxPerStickSecond +
            slow_weight *
                (kSlowResponsePxPerStickSecond -
                 kFreeResponsePxPerStickSecond);
        const float error_before = std::fabs(error);
        error -= applied_command * plant_response * 0.001f;
        const float error_after = std::fabs(error);
        const float closing_speed = (error_before - error_after) * 1000.0f;
        if (error_before <= 40.0f && error_before > 20.0f) {
            closing_40_to_20_sum += closing_speed;
            ++closing_40_to_20_count;
        } else if (error_before <= 20.0f && error_before > 8.0f) {
            closing_20_to_8_sum += closing_speed;
            ++closing_20_to_8_count;
        } else if (error_before <= 8.0f && error_before > 2.0f) {
            closing_8_to_2_sum += closing_speed;
            ++closing_8_to_2_count;
        }
        note_threshold(now_ms, error_after, 40.0f, result.enter_40_ms);
        const bool entered_20_now = result.enter_20_ms < 0 && error_after <= 20.0f;
        note_threshold(now_ms, error_after, 20.0f, result.enter_20_ms);
        if (entered_20_now) {
            result.response_at_20_px = plan.response_scale;
            result.response_confidence_at_20_px = plan.response_confidence;
        }
        const bool entered_8_now = result.enter_8_ms < 0 && error_after <= 8.0f;
        note_threshold(now_ms, error_after, 8.0f, result.enter_8_ms);
        if (entered_8_now) {
            result.response_at_8_px = plan.response_scale;
            result.response_confidence_at_8_px = plan.response_confidence;
        }
        note_threshold(now_ms, error_after, 2.0f, result.enter_2_ms);
        if (result.enter_2_ms >= 0) {
            result.remained_ads_until_2px =
                plan.mode == pipeline_contract::ControlMode::AdsAcquire;
            result.final_error_px = error_after;
            result.final_response_scale = plan.response_scale;
            result.final_response_confidence = plan.response_confidence;
            break;
        }

        result.final_error_px = error_after;
        result.final_response_scale = plan.response_scale;
        result.final_response_confidence = plan.response_confidence;
        previous_mode = plan.mode;
        previous_requested = requested;
        previous_shaped = shaped;
        previous_final = final_output;
    }

    if (result.enter_20_ms >= 0 && result.enter_2_ms >= 0) {
        result.near_20_to_2_ms = result.enter_2_ms - result.enter_20_ms;
    }
    if (closing_40_to_20_count > 0) {
        result.mean_closing_speed_40_to_20 = static_cast<float>(
            closing_40_to_20_sum / closing_40_to_20_count);
    }
    if (closing_20_to_8_count > 0) {
        result.mean_closing_speed_20_to_8 = static_cast<float>(
            closing_20_to_8_sum / closing_20_to_8_count);
    }
    if (closing_8_to_2_count > 0) {
        result.mean_closing_speed_8_to_2 = static_cast<float>(
            closing_8_to_2_sum / closing_8_to_2_count);
    }
    return result;
}

pipeline_contract::TargetPlan controller_plan(
    Axis axis,
    float error,
    pipeline_contract::ControlMode mode,
    float authority) {
    pipeline_contract::TargetPlan plan;
    plan.target_id = kObservationId;
    plan.target_acquisition_id = 1;
    plan.ads_acquisition_exists = true;
    plan.ads_acquisition_active =
        mode == pipeline_contract::ControlMode::AdsAcquire;
    plan.mode = mode;
    plan.lifecycle = pipeline_contract::TargetLifecycle::Observed;
    plan.error_px = axis_error(error, axis);
    plan.aim_authority = authority;
    plan.visual_authority = authority;
    plan.reliability = 0.95f;
    plan.confidence = 0.95f;
    plan.response_scale = kFreeResponsePxPerStickSecond;
    plan.response_confidence = 1.0f;
    plan.normalized_size = kBodyHeightPx / 512.0f;
    plan.ads_target_size_px = {kBodyWidthPx, kBodyHeightPx};
    return plan;
}

AxisSweep run_same_error_sweep(Axis axis) {
    controller_native::AdsAcquisitionControllerConfig ads_config;
    ads_config.arrival_horizon_seconds = 0.135f;
    ads_config.max_force_x = 1.0f;
    ads_config.max_force_y = 1.0f;
    ads_config.response_curve.algorithm =
        controller_native::AimResponseCurveAlgorithm::CodDynamicLegacyLut;
    controller_native::AdsAcquisitionController ads(ads_config);

    controller_native::BodylockFollowControllerConfig body_config;
    body_config.max_force_x = 0.60f;
    body_config.max_force_y = 0.66f;
    body_config.feedback_range_x_px = 24.0f;
    body_config.feedback_range_y_px = 24.0f;
    body_config.response_curve = ads_config.response_curve;
    controller_native::BodylockFollowController body(body_config);

    constexpr std::array<float, 5> kErrors{40.0f, 20.0f, 12.0f, 8.0f, 4.0f};
    AxisSweep sweep;
    sweep.axis = axis;
    for (std::size_t index = 0; index < kErrors.size(); ++index) {
        auto& point = sweep.points[index];
        point.error_px = kErrors[index];
        const auto ads_plan = controller_plan(
            axis, point.error_px,
            pipeline_contract::ControlMode::AdsAcquire, 1.0f);
        const auto full_body_plan = controller_plan(
            axis, point.error_px,
            pipeline_contract::ControlMode::BodyLockFollow, 0.95f);
        const auto low_body_plan = controller_plan(
            axis, point.error_px,
            pipeline_contract::ControlMode::BodyLockFollow, 0.076f);
        point.ads_requested = std::fabs(axis_value(
            ads.compute(ads_plan, {}, 0.001f), axis));
        point.full_bodylock_requested = std::fabs(axis_value(
            body.compute(full_body_plan, {}, 0.001f), axis));
        point.low_bodylock_requested = std::fabs(axis_value(
            body.compute(low_body_plan, {}, 0.001f), axis));
        point.low_to_ads_retention = point.ads_requested > 1.0e-6f
            ? point.low_bodylock_requested / point.ads_requested : 0.0f;
        point.low_to_full_bodylock_retention =
            point.full_bodylock_requested > 1.0e-6f
            ? point.low_bodylock_requested /
                point.full_bodylock_requested : 0.0f;
        point.low_bodylock_at_authority_cap =
            std::fabs(point.low_bodylock_requested - 0.076f) <= 0.001f;
    }
    return sweep;
}

LearningProbe run_learning_probe(bool excited) {
    controller_native::AimResponseEstimatorConfig config;
    controller_native::AdsResponseEstimator estimator(config);
    estimator.begin_target(kObservationId);
    LearningProbe result;
    result.name = excited
        ? "alternating_excited_commands"
        : "smooth_arrival_commands";
    constexpr int kSamples = 100;
    for (int index = 0; index < kSamples; ++index) {
        const float command = excited
            ? (index % 2 == 0 ? 0.20f : 0.55f)
            : 0.30f - static_cast<float>(index) * 0.0025f;
        if (index > 0) {
            const float previous = excited
                ? ((index - 1) % 2 == 0 ? 0.20f : 0.55f)
                : 0.30f - static_cast<float>(index - 1) * 0.0025f;
            result.command_delta = std::max(
                result.command_delta, std::fabs(command - previous));
        }
        controller_native::AimResponseInterval interval;
        interval.average_final_stick = {command, 0.0f};
        interval.observed_error_rate_px_per_sec = {
            -command * kSlowResponsePxPerStickSecond, 0.0f};
        interval.target_id = kObservationId;
        interval.dt_seconds = 0.006f;
        interval.reliability = 0.95f;
        interval.target_acceleration_px_per_sec2 = 0.0f;
        interval.slow_zone_weight = 1.0f;
        interval.observed = true;
        interval.manual_ambiguous = false;
        if (estimator.update(interval)) ++result.accepted_updates;
        ++result.samples;
    }
    const auto estimate = estimator.estimate(1.0f);
    result.estimated_scale = estimate.scale_px_per_stick_second;
    result.confidence = estimate.confidence;
    return result;
}

bool valid_ads_counterfactual(
    const ScenarioResult& uniform,
    const ScenarioResult& slow) {
    return uniform.target_admitted && slow.target_admitted &&
        uniform.fresh_observations >= 20 && slow.fresh_observations >= 20 &&
        uniform.enter_2_ms >= 0 && slow.enter_2_ms >= 0 &&
        uniform.remained_ads_until_2px && slow.remained_ads_until_2px &&
        uniform.bodylock_ticks == 0 && slow.bodylock_ticks == 0 &&
        slow.slow_zone_ticks > 0 && uniform.slow_zone_ticks == 0 &&
        uniform.finite_outputs && slow.finite_outputs;
}

bool valid_handoff_counterfactual(
    const ScenarioResult& full,
    const ScenarioResult& low) {
    return full.target_admitted && low.target_admitted &&
        full.handoff_executed && low.handoff_executed &&
        full.enter_2_ms >= 0 && low.enter_2_ms >= 0 &&
        full.handoff_visual_authority >= 0.90f &&
        low.handoff_visual_authority >= 0.070f &&
        low.handoff_visual_authority <= 0.085f &&
        full.handoff_authority > 0.0f &&
        low.handoff_authority > 0.0f &&
        std::fabs(full.handoff_error_px - low.handoff_error_px) <= 1.0f &&
        full.finite_outputs && low.finite_outputs;
}

float requested_handoff_retention(const ScenarioResult& value) {
    return std::fabs(value.last_ads_requested) > 1.0e-6f
        ? std::fabs(value.first_bodylock_requested /
                    value.last_ads_requested)
        : 0.0f;
}

IncidentReport evaluate() {
    IncidentReport report;
    report.uniform_ads_x = run_scenario(
        {"uniform_ads_x", Axis::X, false, false, true});
    report.uniform_ads_y = run_scenario(
        {"uniform_ads_y", Axis::Y, false, false, true});
    report.slow_ads_x = run_scenario(
        {"slow_zone_ads_x", Axis::X, true, false, true});
    report.slow_ads_y = run_scenario(
        {"slow_zone_ads_y", Axis::Y, true, false, true});
    report.slow_full_handoff_x = run_scenario(
        {"slow_zone_full_handoff_x", Axis::X, true, true, false});
    report.slow_full_handoff_y = run_scenario(
        {"slow_zone_full_handoff_y", Axis::Y, true, true, false});
    report.slow_low_handoff_x = run_scenario(
        {"slow_zone_low_handoff_x", Axis::X, true, true, true});
    report.slow_low_handoff_y = run_scenario(
        {"slow_zone_low_handoff_y", Axis::Y, true, true, true});
    report.sweep_x = run_same_error_sweep(Axis::X);
    report.sweep_y = run_same_error_sweep(Axis::Y);
    report.smooth_learning_probe = run_learning_probe(false);
    report.excited_learning_probe = run_learning_probe(true);

    report.slow_zone_extra_tail_x_ms = static_cast<float>(
        report.slow_ads_x.near_20_to_2_ms -
        report.uniform_ads_x.near_20_to_2_ms);
    report.slow_zone_extra_tail_y_ms = static_cast<float>(
        report.slow_ads_y.near_20_to_2_ms -
        report.uniform_ads_y.near_20_to_2_ms);
    report.low_authority_extra_tail_x_ms = static_cast<float>(
        report.slow_low_handoff_x.near_20_to_2_ms -
        report.slow_full_handoff_x.near_20_to_2_ms);
    report.low_authority_extra_tail_y_ms = static_cast<float>(
        report.slow_low_handoff_y.near_20_to_2_ms -
        report.slow_full_handoff_y.near_20_to_2_ms);

    report.all_triggers_executed =
        valid_ads_counterfactual(report.uniform_ads_x, report.slow_ads_x) &&
        valid_ads_counterfactual(report.uniform_ads_y, report.slow_ads_y) &&
        valid_handoff_counterfactual(
            report.slow_full_handoff_x, report.slow_low_handoff_x) &&
        valid_handoff_counterfactual(
            report.slow_full_handoff_y, report.slow_low_handoff_y);
    report.counterfactuals_valid =
        report.sweep_x.points[3].ads_requested > 0.0f &&
        report.sweep_x.points[3].full_bodylock_requested > 0.0f &&
        report.sweep_y.points[3].ads_requested > 0.0f &&
        report.sweep_y.points[3].full_bodylock_requested > 0.0f;
    report.slow_zone_compensation_x_pass =
        report.slow_zone_extra_tail_x_ms <= kVisibleFrameBudgetMs;
    report.slow_zone_compensation_y_pass =
        report.slow_zone_extra_tail_y_ms <= kVisibleFrameBudgetMs;
    report.overall_pass = report.all_triggers_executed &&
        report.counterfactuals_valid &&
        report.slow_zone_compensation_x_pass &&
        report.slow_zone_compensation_y_pass;
    return report;
}

void write_scenario(std::ostream& out, const ScenarioResult& value) {
    out << std::boolalpha << std::fixed << std::setprecision(6)
        << "{\"fresh_observations\":" << value.fresh_observations
        << ",\"ads_ticks\":" << value.ads_ticks
        << ",\"bodylock_ticks\":" << value.bodylock_ticks
        << ",\"slow_zone_ticks\":" << value.slow_zone_ticks
        << ",\"enter_40_ms\":" << value.enter_40_ms
        << ",\"enter_20_ms\":" << value.enter_20_ms
        << ",\"enter_8_ms\":" << value.enter_8_ms
        << ",\"enter_2_ms\":" << value.enter_2_ms
        << ",\"response_at_20_px\":" << value.response_at_20_px
        << ",\"response_confidence_at_20_px\":"
        << value.response_confidence_at_20_px
        << ",\"response_at_8_px\":" << value.response_at_8_px
        << ",\"response_confidence_at_8_px\":"
        << value.response_confidence_at_8_px
        << ",\"near_20_to_2_ms\":" << value.near_20_to_2_ms
        << ",\"handoff_ms\":" << value.handoff_ms
        << ",\"handoff_error_px\":" << value.handoff_error_px
        << ",\"handoff_authority\":" << value.handoff_authority
        << ",\"handoff_visual_authority\":"
        << value.handoff_visual_authority
        << ",\"last_ads_requested\":" << value.last_ads_requested
        << ",\"first_bodylock_requested\":"
        << value.first_bodylock_requested
        << ",\"last_ads_shaped\":" << value.last_ads_shaped
        << ",\"first_bodylock_shaped\":" << value.first_bodylock_shaped
        << ",\"last_ads_final\":" << value.last_ads_final
        << ",\"first_bodylock_final\":" << value.first_bodylock_final
        << ",\"first_bodylock_shaped_retention\":"
        << value.first_bodylock_shaped_retention
        << ",\"final_error_px\":" << value.final_error_px
        << ",\"final_response_scale\":" << value.final_response_scale
        << ",\"final_response_confidence\":"
        << value.final_response_confidence
        << ",\"mean_closing_speed_40_to_20\":"
        << value.mean_closing_speed_40_to_20
        << ",\"mean_closing_speed_20_to_8\":"
        << value.mean_closing_speed_20_to_8
        << ",\"mean_closing_speed_8_to_2\":"
        << value.mean_closing_speed_8_to_2
        << ",\"fresh_slow_command_pairs\":"
        << value.fresh_slow_command_pairs
        << ",\"fresh_slow_command_pairs_over_learning_threshold\":"
        << value.fresh_slow_command_pairs_over_learning_threshold
        << ",\"maximum_fresh_slow_command_delta\":"
        << value.maximum_fresh_slow_command_delta
        << ",\"target_admitted\":" << value.target_admitted
        << ",\"remained_ads_until_2px\":"
        << value.remained_ads_until_2px
        << ",\"handoff_executed\":" << value.handoff_executed
        << ",\"finite_outputs\":" << value.finite_outputs << '}';
}

void write_sweep(std::ostream& out, const AxisSweep& sweep) {
    out << "[";
    for (std::size_t index = 0; index < sweep.points.size(); ++index) {
        if (index != 0) out << ',';
        const auto& point = sweep.points[index];
        out << "{\"error_px\":" << point.error_px
            << ",\"ads_requested\":" << point.ads_requested
            << ",\"full_bodylock_requested\":"
            << point.full_bodylock_requested
            << ",\"low_bodylock_requested\":"
            << point.low_bodylock_requested
            << ",\"low_to_ads_retention\":"
            << point.low_to_ads_retention
            << ",\"low_to_full_bodylock_retention\":"
            << point.low_to_full_bodylock_retention
            << ",\"low_bodylock_at_authority_cap\":"
            << point.low_bodylock_at_authority_cap << '}';
    }
    out << "]";
}

void write_learning_probe(std::ostream& out, const LearningProbe& value) {
    out << "{\"samples\":" << value.samples
        << ",\"accepted_updates\":" << value.accepted_updates
        << ",\"maximum_command_delta\":" << value.command_delta
        << ",\"estimated_scale\":" << value.estimated_scale
        << ",\"confidence\":" << value.confidence << '}';
}

void write_report(
    const std::filesystem::path& output_path,
    const IncidentReport& report) {
    auto out = controller_native::incident_fixture::open_report(
        output_path, true);
    out << std::boolalpha << std::fixed << std::setprecision(6)
        << "{\n"
        << "  \"schema_version\": 1,\n"
        << "  \"incident_id\": \"" << kIncidentId << "\",\n"
        << "  \"symptom\": \"Repeated ADS acquisitions visibly lose same-direction closing speed near the selected target before final capture\",\n"
        << "  \"evidence_scope\": \"video establishes the repeated visible slowdown; the fixture tests controller hypotheses without claiming a frame-to-tick join\",\n"
        << "  \"covariates\": {\"controller_hz\":1000,\"vision_hz\":\"alternating 6/5 ms (181.8 Hz)\",\"control_effect_delay_ms\":"
        << kControlEffectDelayMs
        << ",\"target_generation\":" << kSelectorGeneration
        << ",\"target_count\":1,\"target_body_px\":["
        << kBodyWidthPx << ',' << kBodyHeightPx
        << "],\"initial_error_px\":" << kInitialErrorPx
        << ",\"right_stick_manual\":\"zero\",\"left_stick_manual\":\"zero\",\"recoil_firing\":\"disabled and not firing\",\"aim_curve\":\"cod_dynamic_legacy_lut\",\"free_response_px_per_stick_second\":"
        << kFreeResponsePxPerStickSecond
        << ",\"slow_response_px_per_stick_second\":"
        << kSlowResponsePxPerStickSecond
        << ",\"logging_mode\":\"fixture JSON only\"},\n"
        << "  \"scenarios\": {\n";
    const auto scenario = [&](const char* name, const ScenarioResult& value,
                              bool comma) {
        out << "    \"" << name << "\": ";
        write_scenario(out, value);
        out << (comma ? "," : "") << "\n";
    };
    scenario("uniform_ads_x", report.uniform_ads_x, true);
    scenario("uniform_ads_y", report.uniform_ads_y, true);
    scenario("slow_ads_x", report.slow_ads_x, true);
    scenario("slow_ads_y", report.slow_ads_y, true);
    scenario("slow_full_handoff_x", report.slow_full_handoff_x, true);
    scenario("slow_full_handoff_y", report.slow_full_handoff_y, true);
    scenario("slow_low_handoff_x", report.slow_low_handoff_x, true);
    scenario("slow_low_handoff_y", report.slow_low_handoff_y, false);
    out << "  },\n"
        << "  \"same_error_sweeps\": {\"x\":";
    write_sweep(out, report.sweep_x);
    out << ",\"y\":";
    write_sweep(out, report.sweep_y);
    out << "},\n"
        << "  \"learning_probes\": {\"smooth_arrival\":";
    write_learning_probe(out, report.smooth_learning_probe);
    out << ",\"alternating_excited\":";
    write_learning_probe(out, report.excited_learning_probe);
    out << "},\n"
        << "  \"attribution\": {\"slow_zone_extra_tail_x_ms\":"
        << report.slow_zone_extra_tail_x_ms
        << ",\"slow_zone_extra_tail_y_ms\":"
        << report.slow_zone_extra_tail_y_ms
        << ",\"low_authority_extra_tail_x_ms\":"
        << report.low_authority_extra_tail_x_ms
        << ",\"low_authority_extra_tail_y_ms\":"
        << report.low_authority_extra_tail_y_ms << "},\n"
        << "  \"all_triggers_executed\": "
        << report.all_triggers_executed << ",\n"
        << "  \"counterfactuals_valid\": "
        << report.counterfactuals_valid << ",\n"
        << "  \"oracles\": [\n"
        << "    {\"id\":\"O1\",\"metric\":\"slow_zone_extra_near_tail_x_ms\",\"operator\":\"<=\",\"threshold\":"
        << kVisibleFrameBudgetMs << ",\"observed\":"
        << report.slow_zone_extra_tail_x_ms << ",\"pass\":"
        << report.slow_zone_compensation_x_pass << "},\n"
        << "    {\"id\":\"O2\",\"metric\":\"slow_zone_extra_near_tail_y_ms\",\"operator\":\"<=\",\"threshold\":"
        << kVisibleFrameBudgetMs << ",\"observed\":"
        << report.slow_zone_extra_tail_y_ms << ",\"pass\":"
        << report.slow_zone_compensation_y_pass << "}\n"
        << "  ],\n"
        << "  \"overall_pass\": " << report.overall_pass << "\n"
        << "}\n";
}

std::filesystem::path output_path_from_args(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--output") {
        return std::filesystem::path(argv[2]);
    }
    throw std::invalid_argument("usage: fixture --output <report.json>");
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto output = output_path_from_args(argc, argv);
        const auto report = evaluate();
        write_report(output, report);
        std::cout << "incident=" << kIncidentId
                  << " triggers=" << report.all_triggers_executed
                  << " slow_tail_x_ms=" << report.slow_zone_extra_tail_x_ms
                  << " authority_tail_x_ms="
                  << report.low_authority_extra_tail_x_ms
                  << " handoff_retention_x="
                  << requested_handoff_retention(
                         report.slow_low_handoff_x)
                  << " overall=" << (report.overall_pass ? "GREEN" : "RED")
                  << '\n';
        if (!report.all_triggers_executed ||
            !report.counterfactuals_valid) {
            return 3;
        }
        return report.overall_pass ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "incident fixture failed: " << error.what() << '\n';
        return 3;
    }
}
