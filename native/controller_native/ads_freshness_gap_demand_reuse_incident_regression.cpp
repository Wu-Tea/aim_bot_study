#include "incident_fixture_support.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using controller_native::GamepadRuntimeConfig;
using controller_native::NativeGamepadController;
using controller_native::PhysicalGamepadState;

constexpr const char* kIncidentId =
    "ads-freshness-gap-demand-reuse-20260819";
constexpr int kControllerHz = 1000;
constexpr int kNormalFrameIntervalMs = 7;
constexpr int kGapDurationMs = 30;
constexpr int kControlEffectDelayMs = 9;
constexpr float kPlantResponsePxPerStickSecond = 2000.0f;
constexpr float kInitialErrorPx = 24.0f;
constexpr float kMaterialOutput = 0.01f;
constexpr float kMaximumUnauthorizedWorkPx = 1.0f;

constexpr std::uint64_t kObservationId = 81913510619ull;
constexpr std::uint64_t kSelectorGeneration = 81919;
constexpr controller_native::incident_fixture::TargetSpec kTarget{
    320.0f,
    256.0f,
    64.0f,
    184.0f,
    0.30f,
    kObservationId,
    kSelectorGeneration,
    true,
    false,
    false,
    false,
    false,
    0.95f,
};

enum class Axis {
    Horizontal,
    VerticalAbove,
};

struct ScenarioSpec {
    const char* name = "";
    Axis axis = Axis::Horizontal;
    int duration_ms = 0;
    int explicit_loss_at_ms = -1;
};

struct ScenarioResult {
    const char* name = "";
    Axis axis = Axis::Horizontal;
    int duration_ms = 0;
    int fresh_target_frames_after_lt = 0;
    int fresh_empty_frames = 0;
    int nonfresh_ticks = 0;
    int ads_ticks = 0;
    int material_output_ticks = 0;
    int material_output_ticks_after_loss = 0;
    std::uint64_t acquisition_id = 0;
    std::uint64_t selector_generation = 0;
    float normalized_size = 0.0f;
    float initial_error_px = 0.0f;
    float scheduled_work_px = 0.0f;
    float realized_work_px = 0.0f;
    float unauthorized_scheduled_work_px = 0.0f;
    float final_true_error_px = 0.0f;
    float realized_overshoot_px = 0.0f;
    float peak_pre_recoil_output = 0.0f;
    bool initial_ads_active = false;
    bool full_authority = false;
    bool target_cleared_after_loss = false;
};

struct Report {
    ScenarioResult long_gap_x{};
    ScenarioResult long_gap_y{};
    ScenarioResult normal_gap_x{};
    ScenarioResult normal_gap_y{};
    ScenarioResult explicit_loss{};
    bool trigger_executed = false;
    bool counterfactuals_valid = false;
    bool horizontal_single_consumption_oracle = false;
    bool vertical_single_consumption_oracle = false;
    bool realized_overshoot_oracle = false;
    bool overall_pass = false;
};

GamepadRuntimeConfig incident_config() {
    auto config = controller_native::incident_fixture::base_config(
        50.0f,
        135.0f);
    config.ai_aim.ads_snap_window_ms = 135;
    config.ai_aim.ads_completion_fresh_frames = 1000;
    config.ai_aim.ads_max_acquisition_ms = 500.0f;
    config.aim_response_curve.algorithm =
        controller_native::AimResponseCurveAlgorithm::Linear;
    return config;
}

PhysicalGamepadState neutral_input() {
    PhysicalGamepadState physical{};
    physical.connected = true;
    return physical;
}

pipeline_contract::Vec2f source_error(Axis axis, float magnitude) {
    if (axis == Axis::VerticalAbove) return {0.0f, -magnitude};
    return {magnitude, 0.0f};
}

controller_native::ControllerVisionSnapshot target_snapshot(
    Axis axis,
    std::uint64_t frame_id,
    double now,
    bool selector_changed) {
    const auto error = source_error(axis, kInitialErrorPx);
    return controller_native::incident_fixture::observed_snapshot(
        kTarget,
        frame_id,
        now,
        error.x,
        error.y,
        selector_changed);
}

float axis_output(
    Axis axis,
    const controller_native::NativeControllerOutputComponents& components) {
    return axis == Axis::VerticalAbove
        ? components.before_recoil_stick.y
        : components.before_recoil_stick.x;
}

float advance_true_error(Axis axis, float error, float work_px) {
    return axis == Axis::VerticalAbove
        ? error + work_px
        : error - work_px;
}

float overshoot(Axis axis, float error) {
    return axis == Axis::VerticalAbove
        ? std::max(0.0f, error)
        : std::max(0.0f, -error);
}

ScenarioResult run_scenario(const ScenarioSpec& spec) {
    double now = 100.0 + static_cast<double>(spec.duration_ms) * 0.01 +
        (spec.axis == Axis::VerticalAbove ? 10.0 : 0.0);
    NativeGamepadController controller(incident_config(), &now);
    std::deque<float> delayed_output(
        static_cast<std::size_t>(kControlEffectDelayMs),
        0.0f);

    const float signed_initial_error =
        spec.axis == Axis::VerticalAbove ? -kInitialErrorPx : kInitialErrorPx;
    float true_error = signed_initial_error;
    bool loss_published = false;

    controller.submit_vision_snapshot(
        target_snapshot(spec.axis, 1, now, true));
    (void)controller.build_output(neutral_input());

    now += 0.001;
    controller.submit_vision_snapshot(
        target_snapshot(spec.axis, 2, now, false));
    (void)controller.build_output(
        controller_native::incident_fixture::ads_input());

    ScenarioResult result{};
    result.name = spec.name;
    result.axis = spec.axis;
    result.duration_ms = spec.duration_ms;
    result.fresh_target_frames_after_lt = 1;
    result.initial_error_px = kInitialErrorPx;

    const auto& initial_plan = controller.last_target_plan();
    result.initial_ads_active =
        initial_plan.mode == pipeline_contract::ControlMode::AdsAcquire &&
        initial_plan.ads_acquisition_active;
    result.full_authority = initial_plan.aim_authority >= 0.999f;
    result.acquisition_id = initial_plan.target_acquisition_id;
    result.selector_generation = initial_plan.selector_target_generation;
    result.normalized_size = initial_plan.normalized_size;

    auto consume_output = [&](bool after_loss) {
        const float output = axis_output(
            spec.axis,
            controller.last_output_components());
        result.peak_pre_recoil_output = std::max(
            result.peak_pre_recoil_output,
            std::fabs(output));
        if (std::fabs(output) >= kMaterialOutput) {
            ++result.material_output_ticks;
            if (after_loss) ++result.material_output_ticks_after_loss;
        }
        const float scheduled = output *
            kPlantResponsePxPerStickSecond * 0.001f;
        result.scheduled_work_px += scheduled;
        delayed_output.push_back(output);
        const float applied = delayed_output.front();
        delayed_output.pop_front();
        const float realized = applied *
            kPlantResponsePxPerStickSecond * 0.001f;
        result.realized_work_px += realized;
        true_error = advance_true_error(spec.axis, true_error, realized);
    };

    consume_output(false);

    for (int elapsed_ms = 1; elapsed_ms <= spec.duration_ms; ++elapsed_ms) {
        now += 0.001;
        if (elapsed_ms == spec.explicit_loss_at_ms) {
            controller.submit_vision_snapshot(
                controller_native::incident_fixture::empty_snapshot(
                    kTarget,
                    3,
                    now));
            loss_published = true;
            ++result.fresh_empty_frames;
        } else {
            ++result.nonfresh_ticks;
        }

        (void)controller.build_output(
            controller_native::incident_fixture::ads_input());
        const auto& plan = controller.last_target_plan();
        if (plan.mode == pipeline_contract::ControlMode::AdsAcquire) {
            ++result.ads_ticks;
        }
        if (loss_published && plan.target_id == 0) {
            result.target_cleared_after_loss = true;
        }
        consume_output(loss_published);
    }

    while (!delayed_output.empty()) {
        const float applied = delayed_output.front();
        delayed_output.pop_front();
        const float realized = applied *
            kPlantResponsePxPerStickSecond * 0.001f;
        result.realized_work_px += realized;
        true_error = advance_true_error(spec.axis, true_error, realized);
    }

    result.unauthorized_scheduled_work_px = std::max(
        0.0f,
        result.scheduled_work_px - kInitialErrorPx);
    result.final_true_error_px = true_error;
    result.realized_overshoot_px = overshoot(spec.axis, true_error);
    return result;
}

bool primary_trigger(const ScenarioResult& value) {
    return value.initial_ads_active && value.full_authority &&
        value.acquisition_id != 0 &&
        value.selector_generation == kSelectorGeneration &&
        value.fresh_target_frames_after_lt == 1 &&
        value.fresh_empty_frames == 0 &&
        value.nonfresh_ticks == kGapDurationMs &&
        value.ads_ticks == kGapDurationMs &&
        value.material_output_ticks >= kGapDurationMs / 2;
}

bool normal_gap_counterfactual(const ScenarioResult& value) {
    return value.initial_ads_active && value.acquisition_id != 0 &&
        value.nonfresh_ticks == kNormalFrameIntervalMs &&
        value.material_output_ticks > 0 &&
        value.unauthorized_scheduled_work_px <=
            kMaximumUnauthorizedWorkPx &&
        value.realized_overshoot_px <= kMaximumUnauthorizedWorkPx;
}

Report evaluate() {
    Report report{};
    report.long_gap_x = run_scenario(
        {"long_gap_horizontal", Axis::Horizontal, kGapDurationMs, -1});
    report.long_gap_y = run_scenario(
        {"long_gap_vertical_above", Axis::VerticalAbove, kGapDurationMs, -1});
    report.normal_gap_x = run_scenario(
        {"normal_gap_horizontal", Axis::Horizontal, kNormalFrameIntervalMs, -1});
    report.normal_gap_y = run_scenario(
        {"normal_gap_vertical_above", Axis::VerticalAbove, kNormalFrameIntervalMs, -1});
    report.explicit_loss = run_scenario(
        {"fresh_explicit_target_loss", Axis::VerticalAbove,
         kGapDurationMs, kNormalFrameIntervalMs});

    report.trigger_executed =
        primary_trigger(report.long_gap_x) &&
        primary_trigger(report.long_gap_y);
    report.counterfactuals_valid =
        normal_gap_counterfactual(report.normal_gap_x) &&
        normal_gap_counterfactual(report.normal_gap_y) &&
        report.explicit_loss.fresh_empty_frames == 1 &&
        report.explicit_loss.target_cleared_after_loss &&
        report.explicit_loss.material_output_ticks_after_loss == 0 &&
        report.explicit_loss.unauthorized_scheduled_work_px <=
            kMaximumUnauthorizedWorkPx;
    report.horizontal_single_consumption_oracle =
        report.long_gap_x.unauthorized_scheduled_work_px <=
            kMaximumUnauthorizedWorkPx;
    report.vertical_single_consumption_oracle =
        report.long_gap_y.unauthorized_scheduled_work_px <=
            kMaximumUnauthorizedWorkPx;
    report.realized_overshoot_oracle =
        report.long_gap_x.realized_overshoot_px <=
            kMaximumUnauthorizedWorkPx &&
        report.long_gap_y.realized_overshoot_px <=
            kMaximumUnauthorizedWorkPx;
    report.overall_pass =
        report.trigger_executed && report.counterfactuals_valid &&
        report.horizontal_single_consumption_oracle &&
        report.vertical_single_consumption_oracle &&
        report.realized_overshoot_oracle;
    return report;
}

const char* axis_name(Axis axis) {
    return axis == Axis::VerticalAbove ? "vertical_above" : "horizontal";
}

void write_scenario(std::ostream& stream, const ScenarioResult& value) {
    stream << std::boolalpha << std::fixed << std::setprecision(6)
           << "{\"axis\":\"" << axis_name(value.axis)
           << "\",\"duration_ms\":" << value.duration_ms
           << ",\"fresh_target_frames_after_lt\":"
           << value.fresh_target_frames_after_lt
           << ",\"fresh_empty_frames\":" << value.fresh_empty_frames
           << ",\"nonfresh_ticks\":" << value.nonfresh_ticks
           << ",\"ads_ticks\":" << value.ads_ticks
           << ",\"material_output_ticks\":"
           << value.material_output_ticks
           << ",\"material_output_ticks_after_loss\":"
           << value.material_output_ticks_after_loss
           << ",\"acquisition_id\":" << value.acquisition_id
           << ",\"selector_generation\":" << value.selector_generation
           << ",\"normalized_size\":" << value.normalized_size
           << ",\"initial_error_px\":" << value.initial_error_px
           << ",\"scheduled_work_px\":" << value.scheduled_work_px
           << ",\"realized_work_px\":" << value.realized_work_px
           << ",\"unauthorized_scheduled_work_px\":"
           << value.unauthorized_scheduled_work_px
           << ",\"final_true_error_px\":" << value.final_true_error_px
           << ",\"realized_overshoot_px\":"
           << value.realized_overshoot_px
           << ",\"peak_pre_recoil_output\":"
           << value.peak_pre_recoil_output
           << ",\"initial_ads_active\":" << value.initial_ads_active
           << ",\"full_authority\":" << value.full_authority
           << ",\"target_cleared_after_loss\":"
           << value.target_cleared_after_loss << "}";
}

void write_report(
    const std::filesystem::path& output,
    const Report& report) {
    auto stream = controller_native::incident_fixture::open_report(output);
    stream << std::boolalpha << std::fixed << std::setprecision(6)
           << "{\n"
           << "  \"schema_version\": 1,\n"
           << "  \"incident_id\": \"" << kIncidentId << "\",\n"
           << "  \"symptom\": \"During a short gap with no new Vision evidence, ADS schedules more same-direction camera displacement than the last fresh target error authorized, so already-issued work carries the crosshair past the target\",\n"
           << "  \"covariates\": {\"controller_hz\":" << kControllerHz
           << ",\"nominal_game_refresh_hz\":135"
           << ",\"normal_frame_interval_ms\":" << kNormalFrameIntervalMs
           << ",\"gap_duration_ms\":" << kGapDurationMs
           << ",\"control_effect_delay_ms\":" << kControlEffectDelayMs
           << ",\"plant_response_px_per_stick_second\":"
           << kPlantResponsePxPerStickSecond
           << ",\"initial_error_px\":" << kInitialErrorPx
           << ",\"target_generation\":" << kSelectorGeneration
           << ",\"target_count\":1"
           << ",\"right_stick_manual\":\"zero\""
           << ",\"left_stick_manual\":\"zero\""
           << ",\"recoil_firing\":\"disabled and not firing\""
           << ",\"controller_mode\":\"production NativeGamepadController ADS\""
           << ",\"logging_mode\":\"fixture JSON only\"},\n"
           << "  \"thresholds\": {\"maximum_unauthorized_work_px\":"
           << kMaximumUnauthorizedWorkPx << "},\n"
           << "  \"scenarios\": {\n"
           << "    \"long_gap_horizontal\": ";
    write_scenario(stream, report.long_gap_x);
    stream << ",\n    \"long_gap_vertical_above\": ";
    write_scenario(stream, report.long_gap_y);
    stream << ",\n    \"normal_gap_horizontal\": ";
    write_scenario(stream, report.normal_gap_x);
    stream << ",\n    \"normal_gap_vertical_above\": ";
    write_scenario(stream, report.normal_gap_y);
    stream << ",\n    \"fresh_explicit_target_loss\": ";
    write_scenario(stream, report.explicit_loss);
    stream << "\n  },\n"
           << "  \"trigger_executed\": " << report.trigger_executed << ",\n"
           << "  \"counterfactuals_valid\": "
           << report.counterfactuals_valid << ",\n"
           << "  \"oracles\": [\n"
           << "    {\"id\":\"O1\",\"metric\":\"horizontal_unauthorized_scheduled_work_px\",\"operator\":\"<=\",\"threshold\":"
           << kMaximumUnauthorizedWorkPx << ",\"observed\":"
           << report.long_gap_x.unauthorized_scheduled_work_px
           << ",\"pass\":" << report.horizontal_single_consumption_oracle
           << "},\n"
           << "    {\"id\":\"O2\",\"metric\":\"vertical_above_unauthorized_scheduled_work_px\",\"operator\":\"<=\",\"threshold\":"
           << kMaximumUnauthorizedWorkPx << ",\"observed\":"
           << report.long_gap_y.unauthorized_scheduled_work_px
           << ",\"pass\":" << report.vertical_single_consumption_oracle
           << "},\n"
           << "    {\"id\":\"O3\",\"metric\":\"both_axes_realized_overshoot_px\",\"operator\":\"<=\",\"threshold\":"
           << kMaximumUnauthorizedWorkPx
           << ",\"observed_x\":" << report.long_gap_x.realized_overshoot_px
           << ",\"observed_y\":" << report.long_gap_y.realized_overshoot_px
           << ",\"pass\":" << report.realized_overshoot_oracle << "}\n"
           << "  ],\n"
           << "  \"overall_pass\": " << report.overall_pass << "\n"
           << "}\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto output =
            controller_native::incident_fixture::output_path_from_args(
                argc,
                argv,
                "ads_freshness_gap_demand_reuse_incident.json");
        const auto report = evaluate();
        write_report(output, report);
        std::cout << "incident=" << kIncidentId
                  << " trigger=" << report.trigger_executed
                  << " counterfactuals=" << report.counterfactuals_valid
                  << " excess_x="
                  << report.long_gap_x.unauthorized_scheduled_work_px
                  << " excess_y="
                  << report.long_gap_y.unauthorized_scheduled_work_px
                  << " overall=" << (report.overall_pass ? "GREEN" : "RED")
                  << '\n';
        if (!report.trigger_executed || !report.counterfactuals_valid) return 3;
        return report.overall_pass ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "incident fixture failed: " << error.what() << '\n';
        return 3;
    }
}
