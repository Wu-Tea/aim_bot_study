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

using controller_native::ControllerVisionSnapshot;
using controller_native::GamepadRuntimeConfig;
using controller_native::NativeGamepadController;
using controller_native::PhysicalGamepadState;

constexpr const char* kIncidentId =
    "ads-incomplete-acquisition-safety-20260825";
constexpr float kNominalMs = 135.0f;
constexpr float kExtensionBudgetMs = 220.0f;
constexpr float kManualX = -0.72f;
constexpr float kManualY = -0.68f;
constexpr float kMinimumManualRetention = 0.95f;
constexpr float kMaximumPostNominalSuppressionMs = 10.0f;
constexpr std::uint64_t kObservationId = 25001;
constexpr std::uint64_t kSelectorGeneration = 2501;

struct TimedSample {
    float requested_elapsed_ms = 0.0f;
    float observed_elapsed_ms = 0.0f;
    float manual_retention = 0.0f;
    float output_x = 0.0f;
    float output_y = 0.0f;
    float ai_x = 0.0f;
    float ai_y = 0.0f;
    bool ads_active = false;
    bool ads_mode = false;
    bool fresh_target = false;
};

struct Report {
    TimedSample nominal;
    TimedSample just_extended;
    TimedSample old_total_deadline;
    TimedSample extension_end;
    TimedSample long_hold;
    float longest_post_nominal_suppression_ms = 0.0f;
    int fresh_target_frames = 0;
    bool neutral_ai_continues = false;
    bool settle_completes = false;
    bool settled_handoff_retains_carried_manual = false;
    bool release_restores_manual = false;
    bool trigger_executed = false;
    bool counterfactuals_valid = false;
    bool overall_pass = false;
};

controller_native::incident_fixture::TargetSpec target_spec() {
    controller_native::incident_fixture::TargetSpec target;
    target.observation_id = kObservationId;
    target.selector_generation = kSelectorGeneration;
    target.has_enemy_cue = true;
    target.enemy_identity_confirmed = true;
    return target;
}

GamepadRuntimeConfig incident_config() {
    auto config = controller_native::incident_fixture::base_config(
        1000.0f, 240.0f);
    config.ai_aim.ads_snap_window_ms = static_cast<int>(kNominalMs);
    config.ai_aim.ads_completion_radius_px = 8.0f;
    config.ai_aim.ads_completion_fresh_frames = 1000;
    // This value is the extra completion budget after the 135 ms nominal
    // phase. The current known-bad runtime incorrectly treats it as the total
    // admitted-acquisition lifetime.
    config.ai_aim.ads_extension_budget_ms = kExtensionBudgetMs;
    config.ai_aim.body_lock_activation_box_px = 150.0f;
    config.ai_aim.body_lock_max_ai_force = 0.60f;
    config.ai_aim.body_lock_max_ai_force_y = 0.66f;
    config.ai_aim.visual_authority_enabled = true;
    return config;
}

PhysicalGamepadState held_manual() {
    return controller_native::incident_fixture::ads_input(
        kManualX, kManualY);
}

float retention(
    const controller_native::GamepadOutputState& output) noexcept {
    const float denominator = kManualX * kManualX + kManualY * kManualY;
    return denominator > 0.0f
        ? (output.right_x * kManualX + output.right_y * kManualY) /
            denominator
        : 1.0f;
}

TimedSample sample_from(
    NativeGamepadController& controller,
    const controller_native::GamepadOutputState& output,
    float requested_elapsed_ms) {
    const auto& plan = controller.last_target_plan();
    const auto& components = controller.last_output_components();
    TimedSample sample;
    sample.requested_elapsed_ms = requested_elapsed_ms;
    sample.observed_elapsed_ms = plan.acquisition_elapsed_ms;
    sample.manual_retention = retention(output);
    sample.output_x = output.right_x;
    sample.output_y = output.right_y;
    sample.ai_x = components.ai_aim_stick.x;
    sample.ai_y = components.ai_aim_stick.y;
    sample.ads_active = plan.ads_acquisition_active;
    sample.ads_mode =
        plan.mode == pipeline_contract::ControlMode::AdsAcquire;
    sample.fresh_target = plan.target_id != 0;
    return sample;
}

bool close_to(float value, float target, float tolerance) noexcept {
    return std::fabs(value - target) <= tolerance;
}

Report evaluate() {
    Report report;
    const auto target = target_spec();
    double now = 100.0;
    NativeGamepadController controller(incident_config(), &now);

    // The user's gesture starts before Vision owns a target. It therefore
    // remains an acquisition gesture unless the ADS lifecycle explicitly
    // bounds its exclusive phase.
    (void)controller.build_output(held_manual());
    now += 0.005;
    const double admitted_at = now;
    std::uint64_t frame_id = 1;
    int current_suppression_ticks = 0;
    int longest_suppression_ticks = 0;

    for (int tick = 0; tick <= 1500; ++tick) {
        const float elapsed_ms = static_cast<float>(
            (now - admitted_at) * 1000.0);
        if (tick % 5 == 0) {
            controller.submit_vision_snapshot(
                controller_native::incident_fixture::observed_snapshot(
                    target, frame_id++, now, 80.0f, -60.0f,
                    frame_id == 2));
            ++report.fresh_target_frames;
        }
        const auto output = controller.build_output(held_manual());
        const auto sample = sample_from(controller, output, elapsed_ms);

        if (elapsed_ms >= kNominalMs + 1.0f) {
            if (sample.manual_retention < kMinimumManualRetention) {
                ++current_suppression_ticks;
                longest_suppression_ticks = std::max(
                    longest_suppression_ticks,
                    current_suppression_ticks);
            } else {
                current_suppression_ticks = 0;
            }
        }

        if (close_to(elapsed_ms, 134.0f, 0.51f)) {
            report.nominal = sample;
        }
        if (close_to(elapsed_ms, 136.0f, 0.51f)) {
            report.just_extended = sample;
        }
        if (close_to(elapsed_ms, 221.0f, 0.51f)) {
            report.old_total_deadline = sample;
        }
        if (close_to(
                elapsed_ms,
                kNominalMs + kExtensionBudgetMs + 1.0f,
                0.51f)) {
            report.extension_end = sample;
        }
        if (close_to(elapsed_ms, 1500.0f, 0.51f)) {
            report.long_hold = sample;
        }
        now += 0.001;
    }
    report.longest_post_nominal_suppression_ms =
        static_cast<float>(longest_suppression_ticks);

    // Neutral input must not cancel the unfinished positioning job. The AI
    // keeps trying on the same target after the user's stick is released.
    controller.submit_vision_snapshot(
        controller_native::incident_fixture::observed_snapshot(
            target, frame_id++, now, 80.0f, -60.0f));
    const auto neutral_output = controller.build_output(
        controller_native::incident_fixture::ads_input());
    const auto& neutral_plan = controller.last_target_plan();
    report.neutral_ai_continues =
        neutral_plan.ads_acquisition_active &&
        neutral_plan.mode == pipeline_contract::ControlMode::AdsAcquire &&
        std::hypot(neutral_output.right_x, neutral_output.right_y) >= 0.05f;

    // Physical ADS release is always the strongest counterfactual: the same
    // manual vector must pass through immediately.
    PhysicalGamepadState released;
    released.connected = true;
    released.right_x = kManualX;
    released.right_y = kManualY;
    const auto released_output = controller.build_output(released);
    report.release_restores_manual =
        std::fabs(released_output.right_x - kManualX) <= 1.0e-5f &&
        std::fabs(released_output.right_y - kManualY) <= 1.0e-5f;

    // A real arrival still completes early; removing timer-as-success must not
    // turn every ADS into a permanent acquisition.
    auto settle_config = incident_config();
    settle_config.ai_aim.ads_completion_fresh_frames = 2;
    double settle_now = 200.0;
    NativeGamepadController settled(settle_config, &settle_now);
    (void)settled.build_output(
        controller_native::incident_fixture::ads_input());
    settle_now += 0.005;
    settled.submit_vision_snapshot(
        controller_native::incident_fixture::observed_snapshot(
            target, 9001, settle_now, 0.0f, 0.0f, true));
    (void)settled.build_output(
        controller_native::incident_fixture::ads_input());
    settle_now += 0.006;
    settled.submit_vision_snapshot(
        controller_native::incident_fixture::observed_snapshot(
            target, 9002, settle_now, 0.0f, 0.0f));
    (void)settled.build_output(
        controller_native::incident_fixture::ads_input());
    const auto& settled_plan = settled.last_target_plan();
    report.settle_completes =
        settled_plan.mode == pipeline_contract::ControlMode::BodyLockFollow &&
        !settled_plan.ads_acquisition_active &&
        settled_plan.acquisition_terminal_reason ==
            pipeline_contract::AdsDecisionReason::Settled;

    // Real-session residual: the acquisition gesture can still be physically
    // held when a genuine settle hands control to BodyLock. A subsequent
    // target move may not reverse that carried user axis.
    double handoff_now = 300.0;
    NativeGamepadController handoff(settle_config, &handoff_now);
    (void)handoff.build_output(held_manual());
    handoff_now += 0.005;
    handoff.submit_vision_snapshot(
        controller_native::incident_fixture::observed_snapshot(
            target, 9101, handoff_now, 0.0f, 0.0f, true));
    (void)handoff.build_output(held_manual());
    handoff_now += 0.006;
    handoff.submit_vision_snapshot(
        controller_native::incident_fixture::observed_snapshot(
            target, 9102, handoff_now, 0.0f, 0.0f));
    (void)handoff.build_output(held_manual());
    handoff_now += 0.006;
    handoff.submit_vision_snapshot(
        controller_native::incident_fixture::observed_snapshot(
            target, 9103, handoff_now, 80.0f, -60.0f));
    const auto handoff_output = handoff.build_output(held_manual());
    const auto& handoff_plan = handoff.last_target_plan();
    report.settled_handoff_retains_carried_manual =
        handoff_plan.mode ==
            pipeline_contract::ControlMode::BodyLockFollow &&
        retention(handoff_output) >= kMinimumManualRetention;

    const bool nominal_exclusive =
        report.nominal.ads_active && report.nominal.ads_mode &&
        report.nominal.fresh_target &&
        report.nominal.manual_retention <= 0.25f &&
        report.nominal.ai_x > 0.0f && report.nominal.ai_y > 0.0f;
    report.trigger_executed =
        nominal_exclusive && report.fresh_target_frames >= 250 &&
        report.old_total_deadline.fresh_target &&
        report.long_hold.fresh_target;
    report.counterfactuals_valid =
        report.settle_completes && report.release_restores_manual;
    report.overall_pass =
        report.trigger_executed && report.counterfactuals_valid &&
        report.just_extended.ads_active && report.just_extended.ads_mode &&
        report.just_extended.manual_retention >= kMinimumManualRetention &&
        report.old_total_deadline.ads_active &&
        report.old_total_deadline.ads_mode &&
        report.old_total_deadline.manual_retention >=
            kMinimumManualRetention &&
        report.extension_end.ads_active && report.extension_end.ads_mode &&
        report.extension_end.manual_retention >= kMinimumManualRetention &&
        report.long_hold.ads_active && report.long_hold.ads_mode &&
        report.long_hold.manual_retention >= kMinimumManualRetention &&
        report.longest_post_nominal_suppression_ms <=
            kMaximumPostNominalSuppressionMs &&
        report.neutral_ai_continues &&
        report.settled_handoff_retains_carried_manual;
    return report;
}

void write_sample(
    std::ostream& stream,
    const char* name,
    const TimedSample& value,
    bool comma) {
    stream << "    \"" << name << "\": {"
           << "\"requested_elapsed_ms\":" << value.requested_elapsed_ms
           << ",\"observed_elapsed_ms\":" << value.observed_elapsed_ms
           << ",\"manual_retention\":" << value.manual_retention
           << ",\"output\":{" << "\"x\":" << value.output_x
           << ",\"y\":" << value.output_y << "}"
           << ",\"ai\":{" << "\"x\":" << value.ai_x
           << ",\"y\":" << value.ai_y << "}"
           << ",\"ads_active\":" << value.ads_active
           << ",\"ads_mode\":" << value.ads_mode
           << ",\"fresh_target\":" << value.fresh_target << "}"
           << (comma ? "," : "") << "\n";
}

void write_report(const std::filesystem::path& output, const Report& report) {
    auto stream = controller_native::incident_fixture::open_report(output);
    stream << std::boolalpha << std::fixed << std::setprecision(6)
           << "{\n"
           << "  \"schema_version\": 1,\n"
           << "  \"incident_id\": \"" << kIncidentId << "\",\n"
           << "  \"symptom\": \"unfinished ADS was terminated at 220 ms or kept suppressing the player's carried input\",\n"
           << "  \"covariates\": {\"nominal_ms\":" << kNominalMs
           << ",\"extension_budget_ms\":" << kExtensionBudgetMs
           << ",\"fresh_target_frames\":" << report.fresh_target_frames
           << ",\"manual\":{" << "\"x\":" << kManualX
           << ",\"y\":" << kManualY << "}},\n"
           << "  \"samples\": {\n";
    write_sample(stream, "nominal_134ms", report.nominal, true);
    write_sample(stream, "just_extended_136ms", report.just_extended, true);
    write_sample(stream, "old_total_deadline_221ms",
                 report.old_total_deadline, true);
    write_sample(stream, "extension_end_356ms", report.extension_end, true);
    write_sample(stream, "long_hold_1500ms", report.long_hold, false);
    stream << "  },\n"
           << "  \"metrics\": {\"longest_post_nominal_suppression_ms\":"
           << report.longest_post_nominal_suppression_ms << "},\n"
           << "  \"trigger_executed\": " << report.trigger_executed
           << ",\n"
           << "  \"counterfactuals_valid\": "
           << report.counterfactuals_valid << ",\n"
           << "  \"oracles\": {\n"
           << "    \"unfinished_active_at_221ms\": "
           << (report.old_total_deadline.ads_active &&
               report.old_total_deadline.ads_mode) << ",\n"
           << "    \"manual_safe_after_nominal\": "
           << (report.just_extended.manual_retention >=
               kMinimumManualRetention) << ",\n"
           << "    \"unfinished_active_after_extension_budget\": "
           << (report.extension_end.ads_active &&
               report.extension_end.ads_mode) << ",\n"
           << "    \"long_hold_manual_safe\": "
           << (report.long_hold.manual_retention >=
               kMinimumManualRetention) << ",\n"
           << "    \"post_nominal_suppression_bounded\": "
           << (report.longest_post_nominal_suppression_ms <=
               kMaximumPostNominalSuppressionMs) << ",\n"
           << "    \"neutral_ai_continues\": "
           << report.neutral_ai_continues << ",\n"
           << "    \"real_settle_completes\": "
           << report.settle_completes << ",\n"
           << "    \"settled_handoff_retains_carried_manual\": "
           << report.settled_handoff_retains_carried_manual << ",\n"
           << "    \"ads_release_restores_manual\": "
           << report.release_restores_manual << "\n"
           << "  },\n"
           << "  \"overall_pass\": " << report.overall_pass << "\n"
           << "}\n";
}

}  // namespace

int run_ads_incomplete_acquisition_safety_incident_regression(
    int argc,
    char** argv) {
    try {
        const auto output =
            controller_native::incident_fixture::output_path_from_args(
                argc,
                argv,
                "ads_incomplete_acquisition_safety_incident.json");
        const auto report = evaluate();
        write_report(output, report);
        std::cout << "incident=" << kIncidentId
                  << " active_221=" << report.old_total_deadline.ads_active
                  << " retention_136="
                  << report.just_extended.manual_retention
                  << " longest_suppression_ms="
                  << report.longest_post_nominal_suppression_ms
                  << " overall=" << (report.overall_pass ? "GREEN" : "RED")
                  << '\n';
        return report.overall_pass ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "incident fixture failed: " << error.what() << '\n';
        return 2;
    }
}

void register_ads_incomplete_acquisition_safety_incident_regression(
    native_test::Registry& registry) {
    registry.add_incident_entry(
        "BaseAds",
        "incident_ads_incomplete_acquisition_safety",
        "ads_incomplete_acquisition_safety_incident.json",
        run_ads_incomplete_acquisition_safety_incident_regression);
}
