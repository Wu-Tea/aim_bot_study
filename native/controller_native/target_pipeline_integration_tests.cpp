#include "native_gamepad_controller.h"
#include "vector_intent_fuser.h"

#include "../runtime_app/vision_controller_adapter.h"
#include "../runtime_app/gate2_5_live_shadow.h"
#include "../vision_native/include/vision_native/ego_motion_observer.h"
#include "../vision_native/include/vision_native/types.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using controller_native::ControllerVisionSnapshot;
using controller_native::GamepadOutputState;
using controller_native::GamepadRuntimeConfig;
using controller_native::NativeGamepadController;
using controller_native::PhysicalGamepadState;

GamepadRuntimeConfig config();
PhysicalGamepadState aiming(float left_x);

std::string g_ads_release_incident_report_path;
std::string g_ads_cue_hold_incident_report_path;
std::string g_iron_sight_incident_report_path;
std::string g_helpful_manual_overdrive_report_path;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

ControllerVisionSnapshot target(
    std::uint64_t frame_id,
    double now,
    float dx = 80.0f,
    float dy = -40.0f,
    std::uint64_t observation_id = 42) {
    ControllerVisionSnapshot snapshot;
    snapshot.frame_updated = true;
    snapshot.selector_identity_protocol = true;
    snapshot.frame_id = frame_id;
    snapshot.capture_time_seconds = now;
    snapshot.ready_time_seconds = now;
    const auto present_ns = static_cast<std::uint64_t>(
        now * 1'000'000'000.0);
    snapshot.actuator_effect_present_qpc = present_ns;
    snapshot.actuator_effect_present_qpc_frequency = 1'000'000'000ull;
    snapshot.actuator_effect_present_steady_ns = present_ns;
    snapshot.actuator_effect_present_calibration_id = 1;
    snapshot.actuator_effect_present_calibration_uncertainty_ns = 100;
    snapshot.actuator_effect_present_time_seconds = now;
    snapshot.actuator_effect_present_raw_available = true;
    snapshot.actuator_effect_present_steady_available = true;
    snapshot.actuator_effect_present_time_valid = true;
    snapshot.selected_observation_id = observation_id;
    snapshot.state.has_target = true;
    snapshot.state.aim_authority = true;
    snapshot.state.fire_authority = true;
    snapshot.state.dx = dx;
    snapshot.state.dy = dy;
    snapshot.state.screen_center_x = 320.0f;
    snapshot.state.screen_center_y = 256.0f;
    snapshot.state.target_x = 320.0f + dx;
    snapshot.state.target_y = 256.0f + dy;
    snapshot.state.target_tier = "observed_strong";
    snapshot.state.observed_at_seconds = now;

    tracking_native::TrackerDetection detection;
    detection.id = observation_id;
    detection.body_box_px = {snapshot.state.target_x - 35.0f,
                             snapshot.state.target_y - 70.0f,
                             70.0f,
                             180.0f};
    detection.aim_point_px = {snapshot.state.target_x, snapshot.state.target_y};
    detection.has_aim_point = true;
    detection.confidence = 0.95f;
    detection.target_tier = "observed_strong";
    snapshot.tracker_detections.push_back(detection);
    return snapshot;
}

ControllerVisionSnapshot fire_target(std::uint64_t frame_id, double now) {
    auto snapshot = target(frame_id, now, 0.0f, 0.0f);
    snapshot.state.auto_fire_requested = true;
    return snapshot;
}

ControllerVisionSnapshot sized_target(
    std::uint64_t frame_id,
    double now,
    float dx,
    float dy,
    float body_height_px,
    std::uint64_t observation_id) {
    auto snapshot = target(frame_id, now, dx, dy, observation_id);
    pipeline_contract::VisionCandidateSnapshot candidate;
    candidate.id = observation_id;
    candidate.valid = true;
    candidate.has_aim_point = true;
    candidate.aim_point_px = {
        snapshot.state.target_x, snapshot.state.target_y};
    constexpr float kAimHeightRatio = 0.365f;
    const float body_width_px = body_height_px * 0.40f;
    candidate.body_box_px = {
        snapshot.state.target_x - body_width_px * 0.5f,
        snapshot.state.target_y - body_height_px * kAimHeightRatio,
        body_width_px,
        body_height_px};
    candidate.confidence = 0.95f;
    snapshot.candidates.push_back(candidate);
    return snapshot;
}

ControllerVisionSnapshot empty_fresh_snapshot(
    std::uint64_t frame_id,
    double now) {
    ControllerVisionSnapshot snapshot;
    snapshot.frame_updated = true;
    snapshot.selector_identity_protocol = true;
    snapshot.frame_id = frame_id;
    snapshot.capture_time_seconds = now;
    snapshot.ready_time_seconds = now;
    snapshot.state.screen_center_x = 320.0f;
    snapshot.state.screen_center_y = 256.0f;
    return snapshot;
}

vision_native::VisionResult production_vision_frame(
    std::uint64_t frame_id,
    std::uint64_t captured_at_ns,
    float target_x,
    std::uint64_t selector_target_generation = 0,
    bool selector_target_changed = false) {
    vision_native::VisionResult result;
    result.selector_identity_protocol = true;
    result.selector_target_generation = selector_target_generation;
    result.selector_target_changed = selector_target_changed;
    result.frame_updated = true;
    result.frame_id = frame_id;
    result.captured_at_ns = captured_at_ns;
    result.result_at_ns = captured_at_ns + 2'000'000;
    result.has_target = true;
    result.has_selected_detection = true;
    result.selected_detection_index = 0;
    result.target_x = target_x;
    result.target_y = 256.0f;
    result.dx = target_x - 320.0f;
    result.dy = 0.0f;
    result.screen_center_x = 320.0f;
    result.screen_center_y = 256.0f;
    result.has_body_box = true;
    result.body_x1 = target_x - 28.0f;
    result.body_y1 = 180.0f;
    result.body_x2 = target_x + 28.0f;
    result.body_y2 = 320.0f;
    result.aim_authority = true;
    result.fire_authority = true;
    vision_native::Detection detection;
    detection.x1 = target_x - 28.0f;
    detection.y1 = 180.0f;
    detection.x2 = target_x + 28.0f;
    detection.y2 = 320.0f;
    detection.conf = 0.95f;
    result.detections.push_back(detection);
    return result;
}

vision_native::VisionResult production_cue_hold_frame(
    std::uint64_t frame_id,
    std::uint64_t captured_at_ns,
    float target_x,
    std::uint64_t selector_target_generation) {
    vision_native::VisionResult result;
    result.selector_identity_protocol = true;
    result.selector_target_generation = selector_target_generation;
    result.frame_updated = true;
    result.frame_id = frame_id;
    result.captured_at_ns = captured_at_ns;
    result.result_at_ns = captured_at_ns + 2'000'000;
    result.has_target = true;
    result.has_selected_detection = false;
    result.target_x = target_x;
    result.target_y = 256.0f;
    result.dx = target_x - 320.0f;
    result.dy = 0.0f;
    result.screen_center_x = 320.0f;
    result.screen_center_y = 256.0f;
    result.has_body_box = true;
    result.body_x1 = target_x - 28.0f;
    result.body_y1 = 180.0f;
    result.body_x2 = target_x + 28.0f;
    result.body_y2 = 320.0f;
    result.target_source = "cue_hold";
    result.target_tier = "cue_hold";
    result.aim_authority = true;
    result.fire_authority = false;
    result.auto_fire = false;
    result.target_confidence = 0.95f;
    return result;
}

vision_native::VisionResult production_cue_hold_frame_xy(
    std::uint64_t frame_id,
    std::uint64_t captured_at_ns,
    float target_x,
    float target_y,
    std::uint64_t selector_target_generation) {
    auto result = production_cue_hold_frame(
        frame_id, captured_at_ns, target_x, selector_target_generation);
    const float y_shift = target_y - 256.0f;
    result.target_y = target_y;
    result.dy = y_shift;
    result.body_y1 += y_shift;
    result.body_y2 += y_shift;
    return result;
}

struct AdsCueHoldIncidentResult {
    std::uint64_t established_target_id = 0;
    std::uint64_t cue_target_id = 0;
    float cue_aim_x = 0.0f;
    float cue_error_x = 0.0f;
    float cue_aim_authority = 0.0f;
    bool cue_fire_authority = false;
    bool cue_fire_requested = false;
    bool same_target = false;
    bool cue_geometry_applied = false;
    bool replay_preserved_geometry = false;
    bool cue_only_rejected = false;
    bool wrong_generation_rejected = false;
    bool ads_release_rejected = false;
};

AdsCueHoldIncidentResult run_ads_cue_hold_incident_fixture() {
    constexpr std::uint64_t kGeneration = 17;
    constexpr float kPersonX = 400.0f;
    constexpr float kCueX = 370.0f;
    auto controller_config = config();
    controller_config.ai_aim.cue_hold_body_lock_force_scale = 0.35f;

    double now = 75.000;
    NativeGamepadController controller(controller_config, [&now] { return now; });
    const auto physical = aiming(0.0f);
    const auto submit = [&](const vision_native::VisionResult& result) {
        controller.submit_vision_snapshot(runtime_app::adapt_vision_result(result));
        (void)controller.build_output(physical);
    };
    submit(production_vision_frame(
        1, static_cast<std::uint64_t>(now * 1'000'000'000.0),
        kPersonX, kGeneration, false));
    const auto established = controller.last_target_plan();

    now += 0.005;
    submit(production_cue_hold_frame(
        2, static_cast<std::uint64_t>(now * 1'000'000'000.0),
        kCueX, kGeneration));
    const auto cue = controller.last_target_plan();

    now += 0.003;
    (void)controller.build_output(physical);
    const auto replay = controller.last_target_plan();

    AdsCueHoldIncidentResult result;
    result.established_target_id = established.target_id;
    result.cue_target_id = cue.target_id;
    result.cue_aim_x = cue.aim_px.x;
    result.cue_error_x = cue.error_px.x;
    result.cue_aim_authority = cue.aim_authority;
    result.cue_fire_authority = cue.fire_authority;
    result.cue_fire_requested = cue.fire_requested;
    result.same_target = established.target_id != 0 &&
        cue.target_id == established.target_id;
    result.cue_geometry_applied =
        std::fabs(cue.aim_px.x - kCueX) <= 0.5f &&
        std::fabs(cue.error_px.x - (kCueX - 320.0f)) <= 0.5f &&
        cue.source_observation_id == 0 && cue.aim_authority > 0.0f &&
        cue.aim_authority <= 0.36f && !cue.fire_authority &&
        !cue.fire_requested;
    result.replay_preserved_geometry = replay.target_id == cue.target_id &&
        std::fabs(replay.aim_px.x - kCueX) <= 0.5f &&
        replay.aim_authority > 0.0f && !replay.fire_authority &&
        !replay.fire_requested;

    double cue_only_now = 76.000;
    NativeGamepadController cue_only(
        controller_config, [&cue_only_now] { return cue_only_now; });
    cue_only.submit_vision_snapshot(runtime_app::adapt_vision_result(
        production_cue_hold_frame(
            1, static_cast<std::uint64_t>(cue_only_now * 1'000'000'000.0),
            kCueX, kGeneration)));
    (void)cue_only.build_output(physical);
    result.cue_only_rejected =
        cue_only.last_target_plan().target_id == 0 &&
        cue_only.last_target_plan().aim_authority == 0.0f;

    double wrong_generation_now = 77.000;
    NativeGamepadController wrong_generation(
        controller_config, [&wrong_generation_now] { return wrong_generation_now; });
    wrong_generation.submit_vision_snapshot(runtime_app::adapt_vision_result(
        production_vision_frame(
            1,
            static_cast<std::uint64_t>(
                wrong_generation_now * 1'000'000'000.0),
            kPersonX, kGeneration, false)));
    (void)wrong_generation.build_output(physical);
    wrong_generation_now += 0.005;
    wrong_generation.submit_vision_snapshot(runtime_app::adapt_vision_result(
        production_cue_hold_frame(
            2,
            static_cast<std::uint64_t>(
                wrong_generation_now * 1'000'000'000.0),
            kCueX, kGeneration + 1)));
    (void)wrong_generation.build_output(physical);
    const auto wrong_generation_plan = wrong_generation.last_target_plan();
    result.wrong_generation_rejected =
        std::fabs(wrong_generation_plan.aim_px.x - kPersonX) <= 0.5f &&
        wrong_generation_plan.source_observation_id == 0;

    double released_now = 78.000;
    NativeGamepadController released(
        controller_config, [&released_now] { return released_now; });
    released.submit_vision_snapshot(runtime_app::adapt_vision_result(
        production_vision_frame(
            1, static_cast<std::uint64_t>(released_now * 1'000'000'000.0),
            kPersonX, kGeneration, false)));
    auto released_physical = physical;
    (void)released.build_output(released_physical);
    released_physical.left_trigger = 0.0f;
    for (unsigned int sample = 0;
         sample < controller_native::kAimLeftTriggerIdleDebounceSamples;
         ++sample) {
        released_now += 0.001;
        (void)released.build_output(released_physical);
    }
    released_now += 0.005;
    released.submit_vision_snapshot(runtime_app::adapt_vision_result(
        production_cue_hold_frame(
            2, static_cast<std::uint64_t>(released_now * 1'000'000'000.0),
            kCueX, kGeneration)));
    (void)released.build_output(released_physical);
    result.ads_release_rejected =
        released.last_target_plan().mode == pipeline_contract::ControlMode::Manual &&
        released.last_target_plan().aim_authority == 0.0f;
    return result;
}

void write_ads_cue_hold_incident_report(
    const AdsCueHoldIncidentResult& result,
    bool passed) {
    if (g_ads_cue_hold_incident_report_path.empty()) return;
    std::ofstream output(
        g_ads_cue_hold_incident_report_path, std::ios::out | std::ios::trunc);
    require(output.is_open(), "could not open ADS cue-hold incident report");
    output << "{\n"
           << "  \"schema_version\": 1,\n"
           << "  \"incident_id\": \"ads-cue-hold-not-steering\",\n"
           << "  \"established_target_id\": "
           << result.established_target_id << ",\n"
           << "  \"cue_target_id\": " << result.cue_target_id << ",\n"
           << "  \"cue_aim_x\": " << result.cue_aim_x << ",\n"
           << "  \"cue_error_x\": " << result.cue_error_x << ",\n"
           << "  \"cue_aim_authority\": " << result.cue_aim_authority << ",\n"
           << "  \"cue_fire_authority\": "
           << (result.cue_fire_authority ? "true" : "false") << ",\n"
           << "  \"cue_fire_requested\": "
           << (result.cue_fire_requested ? "true" : "false") << ",\n"
           << "  \"same_target\": "
           << (result.same_target ? "true" : "false") << ",\n"
           << "  \"cue_geometry_applied\": "
           << (result.cue_geometry_applied ? "true" : "false") << ",\n"
           << "  \"replay_preserved_geometry\": "
           << (result.replay_preserved_geometry ? "true" : "false") << ",\n"
           << "  \"cue_only_rejected\": "
           << (result.cue_only_rejected ? "true" : "false") << ",\n"
           << "  \"wrong_generation_rejected\": "
           << (result.wrong_generation_rejected ? "true" : "false") << ",\n"
           << "  \"ads_release_rejected\": "
           << (result.ads_release_rejected ? "true" : "false") << ",\n"
           << "  \"passed\": " << (passed ? "true" : "false") << "\n"
           << "}\n";
}

void test_same_generation_cue_holds_existing_ads_target() {
    const auto result = run_ads_cue_hold_incident_fixture();
    const bool passed = result.same_target && result.cue_geometry_applied &&
        result.replay_preserved_geometry && result.cue_only_rejected &&
        result.wrong_generation_rejected && result.ads_release_rejected;
    write_ads_cue_hold_incident_report(result, passed);
    require(result.same_target,
            "cue continuation replaced the established person target");
    require(result.cue_geometry_applied,
            "same-generation cue did not steer the existing ADS target with bounded aim-only authority");
    require(result.replay_preserved_geometry,
            "controller replay did not preserve the accepted cue geometry");
    require(result.cue_only_rejected,
            "cue-only frame acquired a target without prior person ownership");
    require(result.wrong_generation_rejected,
            "cue crossed the selector generation identity boundary");
    require(result.ads_release_rejected,
            "cue retained aim authority after physical ADS release");
}

void test_same_generation_cue_updates_both_axes_without_learning_ui_velocity() {
    constexpr std::uint64_t kGeneration = 23;
    double now = 79.000;
    auto controller_config = config();
    controller_config.ai_aim.cue_hold_body_lock_force_scale = 0.35f;
    NativeGamepadController controller(controller_config, [&now] { return now; });
    const auto physical = aiming(0.0f);

    controller.submit_vision_snapshot(runtime_app::adapt_vision_result(
        production_vision_frame(
            1, static_cast<std::uint64_t>(now * 1'000'000'000.0),
            400.0f, kGeneration, false)));
    (void)controller.build_output(physical);
    const auto observed = controller.last_target_plan();

    now += 0.005;
    controller.submit_vision_snapshot(runtime_app::adapt_vision_result(
        production_cue_hold_frame_xy(
            2, static_cast<std::uint64_t>(now * 1'000'000'000.0),
            370.0f, 230.0f, kGeneration)));
    (void)controller.build_output(physical);
    const auto cue = controller.last_target_plan();

    require(cue.target_id == observed.target_id &&
                std::fabs((cue.aim_px.x - observed.aim_px.x) + 30.0f) <= 0.5f &&
                std::fabs((cue.aim_px.y - observed.aim_px.y) + 26.0f) <= 0.5f,
            "same-generation cue did not apply its two-axis marker displacement");
    require(std::fabs(cue.velocity_px_per_sec.x) <= 0.001f &&
                std::fabs(cue.velocity_px_per_sec.y) <= 0.001f,
            "cue UI displacement leaked into learned person velocity");
    require(cue.cue_continuation && cue.source_observation_id == 0 &&
                cue.aim_authority > 0.0f && cue.aim_authority <= 0.36f &&
                !cue.fire_authority && !cue.fire_requested,
            "two-axis cue continuation lost its bounded aim-only contract");
}

void test_production_selector_replacement_has_one_change_tick_then_recovers() {
    double now = 70.000;
    NativeGamepadController controller(config(), [&now] { return now; });
    controller.set_benchmark_intent_fusion_mode(
        controller_native::BenchmarkIntentFusionMode::CausalVector);
    const auto physical = aiming(0.0f);

    const auto submit_frame = [&](std::uint64_t frame_id,
                                   double capture_time,
                                   float target_x,
                                   std::uint64_t generation,
                                   bool changed) {
        now = capture_time;
        controller.submit_vision_snapshot(runtime_app::adapt_vision_result(
            production_vision_frame(
                frame_id,
                static_cast<std::uint64_t>(capture_time * 1'000'000'000.0),
                target_x,
                generation,
                changed)));
        (void)controller.build_output(physical);
    };

    submit_frame(1, 70.000, 380.0f, 1, false);
    const auto first_plan = controller.last_target_plan();
    const auto first_trace = controller.last_acquisition_trace();
    require(first_plan.ads_plan_admitted &&
                first_plan.target_acquisition_id != 0 &&
                first_plan.selector_target_generation == 1,
            "production selector fixture must establish one admitted acquisition");
    require(first_trace.valid && first_trace.plan_admitted &&
                first_trace.selector_target_generation == 1 &&
                !first_trace.selector_target_changed,
            "initial source frame telemetry lost selector/admission identity");

    submit_frame(2, 70.020, 365.0f, 1, false);
    const auto continuity_plan = controller.last_target_plan();
    require(continuity_plan.target_id == first_plan.target_id &&
                continuity_plan.target_acquisition_id ==
                    first_plan.target_acquisition_id,
            "same selector generation must keep one target/acquisition identity");

    // Simulate the latest-only mailbox skipping selector_target_changed;
    // generation 2 remains the durable replacement signal.
    submit_frame(3, 70.040, 250.0f, 2, false);
    const auto replacement_plan = controller.last_target_plan();
    const auto replacement_trace = controller.last_acquisition_trace();
    require(replacement_plan.selector_target_changed &&
                replacement_plan.selector_target_generation == 2 &&
                replacement_plan.target_id != first_plan.target_id,
            "confirmed selector replacement must be visible exactly at the new source frame");
    require(!replacement_plan.ads_plan_admitted &&
                replacement_plan.source_decision_available &&
                replacement_plan.source_decision_outcome ==
                    pipeline_contract::SourceDecisionOutcome::AcceptedContinuation,
            "replacement must not re-admit or re-arm the physical ADS epoch");
    require(replacement_plan.physical_ads_epoch == first_plan.physical_ads_epoch &&
                replacement_plan.target_acquisition_id ==
                    first_plan.target_acquisition_id &&
                replacement_plan.ads_acquisition_begin_ns ==
                    first_plan.ads_acquisition_begin_ns &&
                replacement_plan.acquisition_elapsed_ms >
                    continuity_plan.acquisition_elapsed_ms,
            "replacement reset the physical ADS acquisition clock");
    require(replacement_trace.valid &&
                replacement_trace.selector_target_generation == 2 &&
                replacement_trace.selector_target_changed &&
                !replacement_trace.plan_admitted &&
                replacement_trace.target_acquisition_id ==
                    first_trace.target_acquisition_id &&
                replacement_trace.physical_ads_epoch ==
                    first_trace.physical_ads_epoch,
            "replacement source telemetry did not preserve its join keys");

    // Prime the fuser with the pre-replacement plan. The real production plan
    // then proves that the replacement is one explicit TargetChanged
    // admission tick, rather than a sticky mode or repeated fallback.
    controller_native::VectorIntentFuser fuser;
    controller_native::VectorIntentFusionInput fusion_input{};
    fusion_input.manual_stick = {0.08f, 0.0f};
    fusion_input.shaped_ai_stick = {0.18f, 0.0f};
    fusion_input.fresh_single_target_observation = true;
    fusion_input.plan = continuity_plan;
    const auto continuity_fusion = fuser.update(fusion_input, 0.001f);
    require(continuity_fusion.reason ==
                controller_native::FusionFallbackReason::None,
            "same-target production continuation must use the normal fuser path");

    fusion_input.plan = replacement_plan;
    const auto replacement_fusion = fuser.update(fusion_input, 0.001f);
    require(replacement_fusion.reason ==
                controller_native::FusionFallbackReason::TargetChanged &&
                replacement_fusion.fallback,
            "replacement must produce one explicit TargetChanged fuser admission tick");

    submit_frame(4, 70.045, 325.0f, 2, false);
    const auto recovered_plan = controller.last_target_plan();
    const auto recovered_trace = controller.last_acquisition_trace();
    require(!recovered_plan.selector_target_changed &&
                recovered_plan.selector_target_generation == 2 &&
                recovered_plan.target_id == replacement_plan.target_id &&
                recovered_plan.target_acquisition_id ==
                    replacement_plan.target_acquisition_id &&
                recovered_plan.physical_ads_epoch ==
                    replacement_plan.physical_ads_epoch &&
                recovered_plan.acquisition_elapsed_ms >
                    replacement_plan.acquisition_elapsed_ms,
            "next same-target frame did not restore continuous acquisition state");
    require(recovered_trace.valid &&
                !recovered_trace.selector_target_changed &&
                !recovered_trace.plan_admitted &&
                recovered_trace.target_acquisition_id ==
                    replacement_trace.target_acquisition_id &&
                recovered_trace.physical_ads_epoch ==
                    replacement_trace.physical_ads_epoch,
            "next source telemetry repeated the replacement admission event");

    fusion_input.plan = recovered_plan;
    const auto recovered_fusion = fuser.update(fusion_input, 0.001f);
    require(recovered_fusion.reason ==
                controller_native::FusionFallbackReason::None &&
                !recovered_fusion.fallback &&
                std::isfinite(recovered_fusion.fused_stick.x) &&
                std::isfinite(recovered_fusion.fused_stick.y),
            "same-target frame after replacement did not restore one final-output path");
}

void test_production_frame_local_observation_ids_do_not_switch_one_target() {
    double now = 40.000;
    NativeGamepadController controller(config(), [&now] { return now; });
    const auto physical = aiming(0.0f);

    const auto first_snapshot = runtime_app::adapt_vision_result(
        production_vision_frame(1, 40'000'000'000ull, 380.0f));
    controller.submit_vision_snapshot(first_snapshot);
    (void)controller.build_output(physical);
    const auto first_target_id = controller.last_target_plan().target_id;
    const auto acquisition_id = controller.last_target_plan().target_acquisition_id;
    require(first_target_id != 0 && acquisition_id != 0,
            "production-like frame must establish a target acquisition");

    now = 40.135;
    const auto second_snapshot = runtime_app::adapt_vision_result(
        production_vision_frame(2, 40'135'000'000ull, 330.0f));
    require(first_snapshot.selected_observation_id !=
                second_snapshot.selected_observation_id,
            "production adapter must expose frame-local observation ids");
    controller.submit_vision_snapshot(second_snapshot);
    (void)controller.build_output(physical);
    const auto& plan = controller.last_target_plan();
    require(plan.source_observation_id == second_snapshot.selected_observation_id,
            "plan source observation must remain frame-matched");
    require(plan.target_id == first_target_id,
            "frame-local observation ids must not replace persistent coordinator identity");
    require(plan.target_acquisition_id == acquisition_id,
            "same spatial target must retain one acquisition clock");
    require(plan.ads_acquisition_state ==
                pipeline_contract::AdsAcquisitionState::AcquiringExtended,
            "same target with continued closing must enter Extended despite a new frame id");

    now = 40.150;
    (void)controller.build_output(physical);
    require(controller.last_target_plan().ads_acquisition_state ==
                pipeline_contract::AdsAcquisitionState::AcquiringExtended,
            "Extended must persist on a controller replay tick without new Vision");
    now = 40.165;
    (void)controller.build_output(physical);
    require(controller.last_target_plan().ads_acquisition_state ==
                pipeline_contract::AdsAcquisitionState::AcquiringExtended,
            "Extended must remain stable across consecutive replay ticks");
}

void test_fuser_feedback_controls_manual_escape_without_magnitude_shortcut() {
    double now = 41.000;
    NativeGamepadController controller(config(), [&now] { return now; });
    controller.set_benchmark_intent_fusion_mode(
        controller_native::BenchmarkIntentFusionMode::CausalVector);
    auto physical = aiming(0.0f);
    controller.submit_vision_snapshot(target(1, now, 60.0f, 0.0f));
    (void)controller.build_output(physical);
    now = 41.135;
    controller.submit_vision_snapshot(target(2, now, 35.0f, 0.0f));
    (void)controller.build_output(physical);
    require(controller.last_target_plan().ads_acquisition_state ==
                pipeline_contract::AdsAcquisitionState::AcquiringExtended,
            "end-to-end escape fixture must establish Extended first");

    for (const float manual_x : {0.10f, 0.50f}) {
        physical.right_x = manual_x;
        now += 0.001;
        (void)controller.build_output(physical);
        require(controller.last_target_plan().ads_acquisition_state ==
                    pipeline_contract::AdsAcquisitionState::AcquiringExtended,
                "same-direction manual input must not end Extended");
    }

    physical.right_x = -1.0f;
    now += 0.001;
    (void)controller.build_output(physical);
    now += 0.001;
    (void)controller.build_output(physical);
    require(controller.last_target_plan().ads_acquisition_state ==
                pipeline_contract::AdsAcquisitionState::AcquiringExtended,
            "fuser escape feedback is intentionally consumed on the next tick");
    now += 0.001;
    (void)controller.build_output(physical);
    require(controller.last_target_plan().ads_acquisition_state ==
                pipeline_contract::AdsAcquisitionState::Completed &&
                controller.last_target_plan().ads_decision_reason ==
                    pipeline_contract::AdsDecisionReason::ManualEscape,
            "fuser-confirmed opposing escape must complete Extended state=" +
                std::to_string(static_cast<int>(
                    controller.last_target_plan().ads_acquisition_state)) +
                " reason=" + std::to_string(static_cast<int>(
                    controller.last_target_plan().ads_decision_reason)) +
                " ai=" + std::to_string(
                    controller.last_output_components().ai_aim_stick.x) +
                " escape=" + std::to_string(
                    controller.last_output_components().intent_fusion_manual_escape) +
                " target=" + std::to_string(
                    controller.last_target_plan().target_id) +
                " mode=" + std::to_string(static_cast<int>(
                    controller.last_target_plan().mode)) +
                " lifecycle=" + std::to_string(static_cast<int>(
                    controller.last_target_plan().lifecycle)) +
                " fallback=" + std::to_string(
                    controller.last_output_components().intent_fusion_fallback) +
                " candidate=" + std::to_string(
                    controller.last_output_components().intent_fusion_candidate) +
                " manual=" + std::to_string(
                    controller.last_output_components().manual_stick.x));

    now = 42.000;
    NativeGamepadController orthogonal(config(), [&now] { return now; });
    orthogonal.set_benchmark_intent_fusion_mode(
        controller_native::BenchmarkIntentFusionMode::CausalVector);
    auto orthogonal_physical = aiming(0.0f);
    orthogonal.submit_vision_snapshot(target(1, now, 60.0f, 0.0f));
    (void)orthogonal.build_output(orthogonal_physical);
    now = 42.135;
    orthogonal.submit_vision_snapshot(target(2, now, 35.0f, 0.0f));
    (void)orthogonal.build_output(orthogonal_physical);
    orthogonal_physical.right_y = 0.90f;
    now += 0.001;
    (void)orthogonal.build_output(orthogonal_physical);
    require(orthogonal.last_target_plan().ads_acquisition_state ==
                pipeline_contract::AdsAcquisitionState::AcquiringExtended,
            "orthogonal useful manual input must not be classified as escape");
}

GamepadRuntimeConfig config() {
    GamepadRuntimeConfig value;
    value.recoil.enabled = false;
    value.aim_assist_dynamics.enabled = true;
    value.ai_aim.max_pixels = 100.0f;
    value.ai_aim.max_ai_force = 1.0f;
    value.ai_aim.max_ai_force_y = 1.0f;
    return value;
}

PhysicalGamepadState aiming(float left_x = 0.0f) {
    PhysicalGamepadState state;
    state.connected = true;
    state.left_trigger = 1.0f;
    state.left_x = left_x;
    return state;
}

std::uint8_t shadow_pattern(int x, int y) {
    const int value = (x * 17) + (y * 29) + ((x * y) % 37) * 5;
    return static_cast<std::uint8_t>(value & 0xff);
}

std::array<std::uint8_t, vision_native::kEgoMotionPixelCount>
make_shadow_frame(int dx, int dy) {
    std::array<std::uint8_t, vision_native::kEgoMotionPixelCount> pixels{};
    for (int y = 0; y < vision_native::kEgoMotionFrameHeight; ++y) {
        for (int x = 0; x < vision_native::kEgoMotionFrameWidth; ++x) {
            const int source_x = std::clamp(
                x - dx, 0, vision_native::kEgoMotionFrameWidth - 1);
            const int source_y = std::clamp(
                y - dy, 0, vision_native::kEgoMotionFrameHeight - 1);
            pixels[static_cast<std::size_t>(y) *
                       vision_native::kEgoMotionFrameWidth + x] =
                shadow_pattern(source_x, source_y);
        }
    }
    return pixels;
}

vision_native::EgoMotionFrameView shadow_view(
    std::uint64_t frame_id,
    const std::array<std::uint8_t, vision_native::kEgoMotionPixelCount>& pixels) {
    vision_native::EgoMotionFrameView value;
    value.frame_id = frame_id;
    value.source_present_qpc = frame_id * 100;
    value.source_present_qpc_frequency = 1'000'000;
    value.source_present_steady_ns = frame_id * 1'000'000;
    value.source_present_calibration_id = 1;
    value.source_present_steady_available = true;
    value.captured_at_ns = frame_id * 1'000;
    value.result_at_ns = frame_id * 1'100;
    value.width = vision_native::kEgoMotionFrameWidth;
    value.height = vision_native::kEgoMotionFrameHeight;
    value.row_pitch = vision_native::kEgoMotionFrameWidth;
    value.gray = pixels.data();
    return value;
}

std::uint32_t float_bits(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

bool same_gamepad_output_bitwise(
    const GamepadOutputState& lhs,
    const GamepadOutputState& rhs) {
    return float_bits(lhs.left_x) == float_bits(rhs.left_x) &&
        float_bits(lhs.left_y) == float_bits(rhs.left_y) &&
        float_bits(lhs.right_x) == float_bits(rhs.right_x) &&
        float_bits(lhs.right_y) == float_bits(rhs.right_y) &&
        float_bits(lhs.left_trigger) == float_bits(rhs.left_trigger) &&
        float_bits(lhs.right_trigger) == float_bits(rhs.right_trigger) &&
        lhs.rb == rhs.rb && lhs.lb == rhs.lb && lhs.a == rhs.a &&
        lhs.b == rhs.b && lhs.x == rhs.x && lhs.y == rhs.y &&
        lhs.back == rhs.back && lhs.guide == rhs.guide &&
        lhs.start == rhs.start && lhs.left_thumb == rhs.left_thumb &&
        lhs.right_thumb == rhs.right_thumb && lhs.dpad_up == rhs.dpad_up &&
        lhs.dpad_down == rhs.dpad_down && lhs.dpad_left == rhs.dpad_left &&
        lhs.dpad_right == rhs.dpad_right;
}

std::vector<GamepadOutputState> run_controller_output_sequence(
    bool with_ego_shadow,
    std::uint64_t* submitted_shadow_frames,
    std::uint64_t* processed_shadow_pairs,
    std::uint64_t* taken_shadow_results) {
    double now = 24.0;
    NativeGamepadController controller(config(), [&now] { return now; });
    const auto physical = aiming(0.015f);
    std::unique_ptr<vision_native::EgoMotionObserver> observer;
    if (with_ego_shadow) {
        observer = std::make_unique<vision_native::EgoMotionObserver>();
    }
    const auto first_shadow_frame = make_shadow_frame(0, 0);
    const auto second_shadow_frame = make_shadow_frame(4, -3);
    const auto third_shadow_frame = make_shadow_frame(-2, 2);
    const std::array<const std::array<
        std::uint8_t, vision_native::kEgoMotionPixelCount>*, 3> shadow_frames{{
        &first_shadow_frame, &second_shadow_frame, &third_shadow_frame}};
    std::vector<GamepadOutputState> outputs;
    outputs.reserve(8);
    std::uint64_t taken_results = 0;

    for (std::uint64_t frame = 1; frame <= 8; ++frame) {
        now = 24.0 + static_cast<double>(frame - 1) * 0.010;
        controller.submit_vision_snapshot(target(
            frame, now, 80.0f - static_cast<float>(frame) * 4.0f,
            -32.0f + static_cast<float>(frame) * 1.5f, 240 + frame));

        if (with_ego_shadow) {
            const auto& pixels = *shadow_frames[(frame - 1) % shadow_frames.size()];
            require(observer->submit_frame(shadow_view(frame, pixels)),
                    "shadow A/B submit must accept the source frame");
        }

        outputs.push_back(controller.build_output(physical));

        if (with_ego_shadow) {
            vision_native::EgoMotionShadowResult ignored;
            if (observer->take_latest_result(&ignored)) {
                ++taken_results;
            }
            if (frame == 1) {
                // Give the latest-only worker a bounded opportunity to make
                // frame 1 the predecessor before frame 2 replaces the
                // pending slot.  This is wall-clock test synchronization;
                // the injected controller clock above is unchanged.
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }

    if (with_ego_shadow) {
        // The controller clock remains the injected deterministic value.  A
        // bounded wall-clock poll only waits for the independent observer
        // worker/mailbox and cannot alter any controller timestamp.
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(250);
        while ((taken_results == 0 || observer->stats().pairs_processed == 0) &&
               std::chrono::steady_clock::now() < deadline) {
            vision_native::EgoMotionShadowResult result;
            if (observer->take_latest_result(&result)) {
                ++taken_results;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    const auto stats = with_ego_shadow
        ? observer->stats() : vision_native::EgoMotionObserverStats{};
    if (submitted_shadow_frames != nullptr) {
        *submitted_shadow_frames = stats.frames_submitted;
    }
    if (processed_shadow_pairs != nullptr) {
        *processed_shadow_pairs = stats.pairs_processed;
    }
    if (taken_shadow_results != nullptr) {
        *taken_shadow_results = taken_results;
    }
    return outputs;
}

void test_ego_motion_shadow_does_not_change_final_output_sequence() {
    std::uint64_t submitted_shadow_frames = 0;
    std::uint64_t processed_shadow_pairs = 0;
    std::uint64_t taken_shadow_results = 0;
    const auto shadow_disabled = run_controller_output_sequence(
        false, nullptr, nullptr, nullptr);
    const auto shadow_enabled = run_controller_output_sequence(
        true, &submitted_shadow_frames, &processed_shadow_pairs,
        &taken_shadow_results);
    require(shadow_disabled.size() == shadow_enabled.size(),
            "shadow A/B output sequences have different lengths");
    require(submitted_shadow_frames == shadow_enabled.size(),
            "shadow A/B did not submit every source frame");
    require(processed_shadow_pairs >= 1,
            "shadow A/B did not process an observer pair");
    require(taken_shadow_results >= 1,
            "shadow A/B did not take an observer result from the mailbox");
    for (std::size_t index = 0; index < shadow_disabled.size(); ++index) {
        require(same_gamepad_output_bitwise(
                    shadow_disabled[index], shadow_enabled[index]),
                "ego shadow changed a final GamepadOutputState sample");
    }
}

runtime_app::Gate25ObservationInput make_gate25_controller_observation(
    std::uint64_t frame_id,
    std::uint64_t present_ns,
    float anchor_x,
    float anchor_y,
    std::uint64_t controller_tick_id) {
    runtime_app::Gate25ObservationInput input;
    input.source_frame_id = frame_id;
    input.source_observation_id = frame_id * 10ull + 7ull;
    input.persistent_target_id = 77;
    input.selector_target_generation = 7;
    input.physical_ads_epoch = 3;
    input.target_acquisition_id = 9;
    input.viewport_sequence = 1;
    input.viewport_source_frame_id = frame_id;
    input.accumulated_frames = 1;
    input.source_present_qpc = present_ns;
    input.source_present_qpc_frequency = 1'000'000'000ull;
    input.source_present_steady_ns = present_ns;
    input.source_present_calibration_id = 1;
    input.source_present_calibration_uncertainty_ns = 100;
    input.source_present_available = true;
    input.source_present_steady_available = true;
    input.captured_at_ns = present_ns + 1'000'000ull;
    input.result_at_ns = present_ns + 2'000'000ull;
    input.controller_consume_ns = present_ns + 3'000'000ull;
    input.decision_ns = present_ns + 4'000'000ull;
    input.controller_tick_id = controller_tick_id;
    input.response_delay_ns = 20'000'000ull;
    input.response_delay_valid = true;
    input.response_delay_source =
        runtime_app::Gate25ResponseDelaySource::ConfiguredHypothesis;
    input.target_anchor_screen_x = 320.0f + anchor_x;
    input.target_anchor_screen_y = 256.0f + anchor_y;
    input.stable_body_width = 70.0f;
    input.stable_body_height = 180.0f;
    input.reliability = 0.95f;
    input.target_confidence = 0.95f;
    input.motion_anchor_score = 1.0f;
    input.viewport_width = 640;
    input.viewport_height = 512;
    input.lifecycle = static_cast<std::uint8_t>(
        pipeline_contract::TargetLifecycle::Observed);
    input.motion = static_cast<std::uint8_t>(
        pipeline_contract::TargetMotion::Steady);
    input.mode = static_cast<std::uint8_t>(
        pipeline_contract::ControlMode::BodyLockFollow);
    input.response_label_ms = 260;
    input.response_profile_provenance_valid = true;
    input.response_model_available = true;
    input.game_profile_identity_available = true;
    input.backend_epoch = 1;
    input.backend_known = true;
    input.output_enabled = true;
    input.selector_identity_protocol = true;
    input.fresh_observed = true;
    input.strong_observation = true;
    input.stable_coordinates_valid = true;
    return input;
}

runtime_app::Gate25DeliverySample make_gate25_delivery_sample(
    std::uint64_t sample_seq,
    std::uint64_t applied_at_ns,
    const GamepadOutputState& output) {
    runtime_app::Gate25DeliverySample sample;
    sample.sample_seq = sample_seq;
    sample.applied_at_ns = applied_at_ns;
    sample.backend_epoch = 1;
    sample.final_right = {output.right_x, output.right_y};
    sample.manual = sample.final_right;
    sample.output_delivered = true;
    sample.output_enabled = true;
    return sample;
}

struct GateControllerSequence {
    std::vector<GamepadOutputState> outputs;
    std::uint64_t gate_invocations = 0;
    std::uint64_t gate_pairs = 0;
    std::uint64_t gate_duplicate_rejections = 0;
    bool gate_last_effect_decided = false;
    bool gate_last_effect_valid = false;
};

GateControllerSequence run_gate25_controller_sequence(bool with_gate) {
    double now = 0.970;
    auto controller_config = config();
    controller_config.tracker.causal_memory_enabled = false;
    NativeGamepadController controller(
        controller_config, [&now] { return now; });
    const auto physical = aiming();
    runtime_app::Gate25LiveShadow gate;
    runtime_app::Gate25DeliveryView delivery_view;
    GateControllerSequence result;
    std::uint64_t sample_seq = 0;

    for (int tick = 0; tick < 90; ++tick) {
        now = 0.970 + static_cast<double>(tick) * 0.001;
        std::uint64_t frame_id = 0;
        float anchor_x = 0.0f;
        float anchor_y = 0.0f;
        if (tick == 50) {
            frame_id = 1;
            anchor_x = 80.0f;
            anchor_y = -40.0f;
            controller.submit_vision_snapshot(target(
                frame_id, now, anchor_x, anchor_y, 42));
        } else if (tick == 60) {
            frame_id = 2;
            anchor_x = 80.0f;
            anchor_y = -40.0f;
            controller.submit_vision_snapshot(target(
                frame_id, now, anchor_x, anchor_y, 42));
        } else if (tick == 70) {
            frame_id = 3;
            anchor_x = 70.0f;
            anchor_y = -40.0f;
            controller.submit_vision_snapshot(target(
                frame_id, now, anchor_x, anchor_y, 42));
        }

        const auto output = controller.build_output(physical);
        result.outputs.push_back(output);
        const auto applied_ns = static_cast<std::uint64_t>(
            (now + 0.0001) * 1'000'000'000.0);
        ++sample_seq;
        require(delivery_view.push(make_gate25_delivery_sample(
                    sample_seq, applied_ns, output)),
                "Gate2.5 controller seam delivery view overflowed");
        controller.report_output_delivery(
            true, true, now + 0.0001, 1);

        if (with_gate && frame_id != 0) {
            const auto present_ns = static_cast<std::uint64_t>(
                now * 1'000'000'000.0);
            gate.observe(make_gate25_controller_observation(
                             frame_id, present_ns, anchor_x, anchor_y,
                             static_cast<std::uint64_t>(tick)),
                         delivery_view);
        }
    }

    runtime_app::Gate25AggregateSnapshot summary;
    if (with_gate) {
        // Exercise an actual rejected publication as well as the two
        // compatible source pairs.  This is still observer-only; the
        // controller/delivery sequence above is unchanged.
        gate.observe(make_gate25_controller_observation(
                         3, 1'040'000'000ull, 70.0f, -40.0f, 91),
                     delivery_view);
        require(gate.flush_summary(summary),
                "Gate2.5 controller seam did not flush an aggregate");
        result.gate_invocations = gate.invocation_count();
        result.gate_pairs = summary.compatible_pairs;
        result.gate_duplicate_rejections = summary.duplicate;
        // A compatible pair is itself a source-frame decision even when the
        // observed effect is below the provisional noise/effect gate and the
        // per-effect reason remains None.  Keep that distinction explicit so
        // the A/B test cannot pass by merely invoking the observer.
        result.gate_last_effect_decided = summary.compatible_pairs > 0 ||
            gate.last_effect().valid ||
            gate.last_effect().reason != runtime_app::Gate25Reason::None;
        result.gate_last_effect_valid = gate.last_effect().valid;
    }
    return result;
}

void test_gate25_observer_interleaving_preserves_controller_outputs() {
    const auto gate_off = run_gate25_controller_sequence(false);
    const auto gate_on = run_gate25_controller_sequence(true);
    require(gate_off.outputs.size() == gate_on.outputs.size(),
            "Gate2.5 output A/B sequence lengths differ");
    require(gate_on.gate_invocations >= 3,
            "Gate2.5 observer was not invoked for each fresh endpoint");
    require(gate_on.gate_pairs >= 1,
            "Gate2.5 observer did not form a compatible pair");
    require(gate_on.gate_duplicate_rejections >= 1,
            "Gate2.5 observer seam did not exercise a rejected publication");
    require(gate_on.gate_last_effect_decided,
            "Gate2.5 observer produced no valid/rejected pair decision");
    for (std::size_t index = 0; index < gate_off.outputs.size(); ++index) {
        require(same_gamepad_output_bitwise(
                    gate_off.outputs[index], gate_on.outputs[index]),
                "Gate2.5 observer changed a final GamepadOutputState sample");
    }
    std::cout << "[Gate2.5 PASS] controller+observer interleaving output identity"
              << " invocations=" << gate_on.gate_invocations
              << " pairs=" << gate_on.gate_pairs
              << " duplicate_rejections=" << gate_on.gate_duplicate_rejections
              << " last_effect_valid=" << gate_on.gate_last_effect_valid
              << "\n";
}

struct W5CopyClockRun {
    controller_native::CausalMotionPhaseEstimate estimate{};
    std::vector<GamepadOutputState> outputs;
};

W5CopyClockRun run_w5_copy_complete_counterfactual(
    double copy_complete_offset_ms,
    double present_shift_ms,
    float manual_x) {
    double now = 9.970;
    auto shadow_config = config();
    shadow_config.aim_assist_dynamics.enabled = false;
    shadow_config.tracker.causal_memory_enabled = true;
    shadow_config.tracker.causal_memory_response_delay_ms = 20.0f;
    shadow_config.tracker.causal_memory_horizon_ms = 200.0f;
    NativeGamepadController controller(
        shadow_config, [&now] { return now; });
    auto neutral = aiming();
    (void)controller.build_output(neutral);
    controller.report_output_delivery(true, true, now + 0.0001, 1);

    auto physical = aiming();
    physical.right_x = manual_x;
    W5CopyClockRun result;
    for (std::uint64_t frame = 1; frame <= 4; ++frame) {
        const double present = 10.000 +
            static_cast<double>(frame - 1) * 0.010 +
            (frame == 4 ? present_shift_ms / 1000.0 : 0.0);
        const double decision = 10.020 +
            static_cast<double>(frame - 1) * 0.010;
        // Target-first production ignores manual_x once authority exists, so
        // use a nonzero selected-target error to create an actual delivered T
        // for the source-present positive control.
        auto snapshot = target(frame, present, 80.0f, 0.0f, 42);
        // This is intentionally the legacy copy-complete field. The
        // independent source-present provenance above remains unchanged.
        snapshot.capture_time_seconds =
            present + copy_complete_offset_ms / 1000.0;
        snapshot.ready_time_seconds = decision;
        now = decision;
        controller.submit_vision_snapshot(snapshot);
        result.outputs.push_back(controller.build_output(physical));
        controller.report_output_delivery(
            true, true, decision + 0.0001, 1);
    }
    result.estimate = controller.last_causal_memory_estimate();
    return result;
}

void require_same_w5_copy_clock_estimate(
    const controller_native::CausalMotionPhaseEstimate& expected,
    const controller_native::CausalMotionPhaseEstimate& actual,
    const std::string& label) {
    require(expected.status == actual.status &&
                expected.realized_status == actual.realized_status &&
                expected.realized_valid == actual.realized_valid &&
                expected.pending_valid == actual.pending_valid &&
                expected.valid == actual.valid,
            label + " changed W5 status/validity");
    require(expected.previous_source_frame_id == actual.previous_source_frame_id &&
                expected.previous_source_observation_id ==
                    actual.previous_source_observation_id &&
                expected.source_frame_id == actual.source_frame_id &&
                expected.source_observation_id == actual.source_observation_id &&
                expected.previous_present_steady_ns ==
                    actual.previous_present_steady_ns &&
                expected.current_present_steady_ns ==
                    actual.current_present_steady_ns,
            label + " changed source-present provenance");
    const auto close = [](float lhs, float rhs) {
        return std::fabs(lhs - rhs) <= 1.0e-5f;
    };
    require(close(expected.realized_px.x, actual.realized_px.x) &&
                close(expected.realized_px.y, actual.realized_px.y) &&
                close(expected.in_flight_px.x, actual.in_flight_px.x) &&
                close(expected.in_flight_px.y, actual.in_flight_px.y) &&
                close(expected.scheduled_px.x, actual.scheduled_px.x) &&
                close(expected.scheduled_px.y, actual.scheduled_px.y) &&
                close(expected.pending_total_px.x, actual.pending_total_px.x) &&
                close(expected.pending_total_px.y, actual.pending_total_px.y),
            label + " changed source-present anchored motion estimate");
}

void test_w5_source_present_is_invariant_to_copy_complete_offset() {
    const std::array<double, 4> offsets{{0.0, 1.0, 3.0, 6.0}};
    const auto baseline = run_w5_copy_complete_counterfactual(0.0, 0.0, 0.20);
    require(baseline.estimate.valid && baseline.estimate.realized_valid &&
                baseline.estimate.pending_valid,
            "copy-complete baseline W5 estimate is not usable");
    for (const double offset : offsets) {
        const auto candidate = run_w5_copy_complete_counterfactual(
            offset, 0.0, 0.20);
        require_same_w5_copy_clock_estimate(
            baseline.estimate, candidate.estimate,
            "copy-complete offset " + std::to_string(offset) + "ms");
        require(candidate.outputs.size() == baseline.outputs.size(),
                "copy-complete counterfactual output lengths differ");
        for (std::size_t index = 0; index < baseline.outputs.size(); ++index) {
            require(same_gamepad_output_bitwise(
                        baseline.outputs[index], candidate.outputs[index]),
                    "copy-complete offset changed a shadow final output");
        }
    }

    const auto present_shifted = run_w5_copy_complete_counterfactual(
        0.0, 6.0, 0.20);
    require(present_shifted.estimate.valid &&
                present_shifted.estimate.realized_valid &&
                present_shifted.estimate.pending_valid,
            "source-present positive control W5 estimate is not usable");
    const auto delta = [](float lhs, float rhs) {
        return std::fabs(lhs - rhs);
    };
    const float realized_delta_x = delta(
        present_shifted.estimate.realized_px.x,
        baseline.estimate.realized_px.x);
    const float realized_delta_y = delta(
        present_shifted.estimate.realized_px.y,
        baseline.estimate.realized_px.y);
    const float in_flight_delta_x = delta(
        present_shifted.estimate.in_flight_px.x,
        baseline.estimate.in_flight_px.x);
    const float in_flight_delta_y = delta(
        present_shifted.estimate.in_flight_px.y,
        baseline.estimate.in_flight_px.y);
    const float scheduled_delta_x = delta(
        present_shifted.estimate.scheduled_px.x,
        baseline.estimate.scheduled_px.x);
    const float scheduled_delta_y = delta(
        present_shifted.estimate.scheduled_px.y,
        baseline.estimate.scheduled_px.y);
    const float pending_delta_x = delta(
        present_shifted.estimate.pending_total_px.x,
        baseline.estimate.pending_total_px.x);
    const float pending_delta_y = delta(
        present_shifted.estimate.pending_total_px.y,
        baseline.estimate.pending_total_px.y);
    const bool motion_changed =
        realized_delta_x > 1.0e-5f || realized_delta_y > 1.0e-5f ||
        in_flight_delta_x > 1.0e-5f || in_flight_delta_y > 1.0e-5f ||
        scheduled_delta_x > 1.0e-5f || scheduled_delta_y > 1.0e-5f ||
        pending_delta_x > 1.0e-5f || pending_delta_y > 1.0e-5f;
    require(motion_changed,
            "source-present positive control did not change the W5 interval");
    std::cout << "[W5 PASS] source-present W5 estimate independent of copy offset"
              << " offsets=0,1,3,6ms positive_present_shift=6ms"
              << " motion_changed=1"
              << " deltas(realized=" << realized_delta_x << ","
              << realized_delta_y << " in_flight=" << in_flight_delta_x
              << "," << in_flight_delta_y << " scheduled="
              << scheduled_delta_x << "," << scheduled_delta_y
              << " pending=" << pending_delta_x << "," << pending_delta_y
              << ")\n";
}

float warm_ads_force(float left_x) {
    double now = 10.0;
    NativeGamepadController controller(config(), [&now] { return now; });
    auto physical = aiming(left_x);
    for (std::uint64_t frame = 1; frame <= 3; ++frame) {
        controller.submit_vision_snapshot(target(frame, now));
        controller.build_output(physical);
        now += 0.010;
    }
    return std::fabs(controller.last_output_components().ai_aim_stick.x);
}

void test_ads_is_bounded_and_drift_is_ignored() {
    const float neutral = warm_ads_force(0.0f);
    const float drift = warm_ads_force(-0.0118f);
    require(neutral > 0.05f && neutral <= 1.0f, "ADS did not produce bounded assist");
    require(std::fabs(neutral - drift) <= 0.01f,
            "left-stick drift incorrectly weakened ADS owner hold");
}

void test_production_causal_memory_consumes_only_confirmed_delivery() {
    double now = 15.0;
    auto enabled_config = config();
    enabled_config.tracker.causal_memory_enabled = true;
    NativeGamepadController enabled(enabled_config, [&now] { return now; });
    const auto physical = aiming();

    now = 14.970;
    (void)enabled.build_output(physical);
    enabled.report_output_delivery(true, true, 14.9701, 1);
    now = 15.000;
    enabled.submit_vision_snapshot(target(1, now, 80.0f, -40.0f));
    (void)enabled.build_output(physical);
    enabled.report_output_delivery(true, true, 15.0001, 1);
    now = 15.001;
    enabled.submit_vision_snapshot(target(2, now, 80.0f, -40.0f));
    (void)enabled.build_output(physical);
    require(enabled.last_output_components().pending_motion_valid,
            "confirmed delivery must expose pending final-T motion");

    enabled.report_output_delivery(false, true, 15.0011, 1);
    now = 15.002;
    enabled.submit_vision_snapshot(target(3, now, 80.0f, -40.0f));
    (void)enabled.build_output(physical);
    require(!enabled.last_output_components().pending_motion_valid,
            "failed delivery must fail open instead of reusing old P");
    require(std::string(enabled.last_output_components().memory_status) !=
                "applied",
            "failed delivery must have an observable non-applied status");

    auto disabled_config = enabled_config;
    disabled_config.tracker.causal_memory_enabled = false;
    NativeGamepadController disabled(disabled_config, [&now] { return now; });
    disabled.submit_vision_snapshot(target(1, now, 80.0f, -40.0f));
    (void)disabled.build_output(physical);
    require(!disabled.last_output_components().pending_motion_valid &&
                !disabled.last_output_components().memory_applied,
            "disabled causal memory must preserve the raw path");
}

void test_vision_gap_uses_smooth_short_continuity() {
    double now = 20.0;
    NativeGamepadController controller(config(), [&now] { return now; });
    auto physical = aiming();
    controller.submit_vision_snapshot(target(1, now));
    controller.build_output(physical);
    now += 0.010;
    controller.submit_vision_snapshot(target(2, now));
    controller.build_output(physical);

    now += 0.010;
    ControllerVisionSnapshot miss;
    miss.frame_updated = true;
    miss.selector_identity_protocol = true;
    miss.frame_id = 3;
    miss.capture_time_seconds = now;
    miss.ready_time_seconds = now;
    miss.state.screen_center_x = 320.0f;
    miss.state.screen_center_y = 256.0f;
    controller.submit_vision_snapshot(miss);

    float previous = 2.0f;
    float first = -1.0f;
    float last = 0.0f;
    int continuity_ticks = 0;
    int sign_reversals = 0;
    float previous_signed = 0.0f;
    for (int tick = 0; tick < 240; ++tick) {
        now += 0.001;
        controller.build_output(physical);
        const float signed_force = controller.last_output_components().ai_aim_stick.x;
        const float force = std::fabs(signed_force);
        if (first < 0.0f) first = force;
        last = force;
        require(force <= previous + 0.001f, "continuity assist rose during occlusion");
        if (previous_signed * signed_force < 0.0f) ++sign_reversals;
        previous = force;
        previous_signed = signed_force;
        if (controller.last_frame_vision_state().assist_authority_state ==
            pipeline_contract::AssistAuthorityState::Continuity) {
            ++continuity_ticks;
        }
    }
    require(continuity_ticks > 0, "brief occlusion did not enter continuity hold");
    require(first > last, "continuity hold did not decay");
    require(sign_reversals == 0, "continuity hold reversed direction");
}

void test_opposing_manual_intent_yields_without_braking_bodylock() {
    double now = 30.0;
    NativeGamepadController controller(config(), [&now] { return now; });
    controller.set_benchmark_intent_fusion_mode(
        controller_native::BenchmarkIntentFusionMode::CausalVector);
    auto physical = aiming();
    for (std::uint64_t frame = 1; frame <= 4; ++frame) {
        controller.submit_vision_snapshot(target(frame, now, 24.0f, 0.0f));
        controller.build_output(physical);
        now += 0.010;
    }
    physical.right_x = -0.9f;
    int conflict_ticks = 0;
    for (int tick = 0; tick < 20; ++tick) {
        now += 0.001;
        const auto delivered = controller.build_output(physical);
        if (delivered.right_x > 0.02f) {
            ++conflict_ticks;
        }
    }
    require(conflict_ticks < 16, "assist fought strong opposing manual intent too long");
}

void test_benchmark_mix_override_updates_delivered_feedback() {
    double now = 32.0;
    NativeGamepadController controller(config(), [&now] { return now; });
    controller.set_benchmark_mix_transform(
        [](float manual_x, float manual_y, float, float,
           const controller_native::NativeControllerOutputComponents&) {
            return pipeline_contract::Vec2f{
                manual_x * 0.5f, manual_y * 0.5f};
        });
    auto physical = aiming();
    physical.right_x = 0.20f;
    physical.right_y = -0.12f;

    const auto output = controller.build_output(physical);

    require(std::fabs(output.right_x - 0.10f) < 1e-6f,
            "override must replace delivered X");
    require(std::fabs(output.right_y + 0.06f) < 1e-6f,
            "override must replace delivered Y");
    require(std::fabs(
                controller.last_output_components().before_recoil_stick.x -
                output.right_x) < 1e-6f,
            "feedback components must record overridden X");
    require(std::fabs(
                controller.last_tracker_motion_output().right_x -
                output.right_x) < 1e-6f,
            "tracker feedback must match delivered output");
}

void test_benchmark_vector_fusion_is_one_reported_pipeline_stage() {
    double now = 34.0;
    NativeGamepadController controller(config(), [&now] { return now; });
    controller.set_benchmark_intent_fusion_mode(
        controller_native::BenchmarkIntentFusionMode::CausalVector);
    auto physical = aiming();
    physical.right_x = 0.20f;
    physical.right_y = -0.12f;

    const auto output = controller.build_output(physical);
    const auto& components = controller.last_output_components();
    require(components.intent_fusion_mode == "continuous_vector",
            "benchmark vector mode must be identified in output diagnostics");
    require(components.intent_fusion_fallback,
            "no-target vector mode must report its manual fallback");
    require(std::fabs(output.right_x - physical.right_x) < 1e-6f &&
            std::fabs(output.right_y - physical.right_y) < 1e-6f,
            "vector fallback must apply physical manual input exactly once");
}

float vector_mode_shaped_assist_for(float manual_x) {
    double now = 34.5;
    NativeGamepadController controller(config(), [&now] { return now; });
    controller.set_benchmark_intent_fusion_mode(
        controller_native::BenchmarkIntentFusionMode::CausalVector);
    auto physical = aiming();
    physical.right_x = manual_x;
    for (std::uint64_t frame = 1; frame <= 8; ++frame) {
        controller.submit_vision_snapshot(target(frame, now, 80.0f, 0.0f));
        controller.build_output(physical);
        now += 0.010;
    }
    return std::fabs(
        controller.last_output_components().shaped_assist_stick.x);
}

void test_vector_mode_generates_ai_before_manual_arbitration() {
    const float neutral_ai = vector_mode_shaped_assist_for(0.0f);
    const float opposing_ai = vector_mode_shaped_assist_for(-0.32f);
    require(neutral_ai > 0.05f,
            "fixture must produce material unopposed AI assistance");
    require(opposing_ai >= neutral_ai * 0.95f,
            "vector mode must not weaken AI before its single fusion point");
}

void test_causal_memory_applies_pending_without_geometry_corruption() {
    double now = 16.0;
    auto disabled_config = config();
    disabled_config.tracker.causal_memory_enabled = false;
    disabled_config.tracker.causal_memory_enabled = false;
    auto shadow_config = disabled_config;
    shadow_config.tracker.causal_memory_enabled = true;
    shadow_config.tracker.causal_memory_response_delay_ms = 20.0f;
    shadow_config.tracker.causal_memory_horizon_ms = 200.0f;
    NativeGamepadController disabled(disabled_config, [&now] { return now; });
    NativeGamepadController shadow(shadow_config, [&now] { return now; });
    const auto physical = aiming();

    for (int tick = 0; tick < 40; ++tick) {
        now = 16.0 + tick * 0.001;
        if (tick % 10 == 0) {
            auto snapshot = target(
                static_cast<std::uint64_t>(tick / 10 + 1),
                now - 0.004,
                80.0f - tick * 0.25f,
                -40.0f);
            snapshot.ready_time_seconds = now;
            disabled.submit_vision_snapshot(snapshot);
            shadow.submit_vision_snapshot(snapshot);
        }
        const auto disabled_output = disabled.build_output(physical);
        const auto shadow_output = shadow.build_output(physical);
        // Once P is valid this is intentionally a behavior-changing
        // controller test: R=D-P is the new production residual.  Output
        // identity for P=0/disabled is covered by the dedicated A/B test.
        (void)disabled_output;
        (void)shadow_output;
        disabled.report_output_delivery(true, true, now + 0.0001, 1);
        shadow.report_output_delivery(true, true, now + 0.0001, 1);
    }
    const auto& estimate = shadow.last_causal_memory_estimate();
    require(estimate.valid && estimate.pending_valid,
            "W5 shadow did not reconstruct pending delivered motion");
    require(shadow.last_target_plan().error_px.x !=
                disabled.last_target_plan().error_px.x ||
            shadow.last_target_plan().error_px.y !=
                disabled.last_target_plan().error_px.y,
            "valid pending P must change the controller residual");
    require(std::fabs(shadow.last_frame_vision_state().screen_center_x -
                          disabled.last_frame_vision_state().screen_center_x) <
                1.0e-3f &&
                std::fabs(shadow.last_frame_vision_state().screen_center_y -
                          disabled.last_frame_vision_state().screen_center_y) <
                1.0e-3f,
            "R must not alter the observed geometry state");
}

void test_causal_memory_applies_signed_pending_to_plan() {
    double now = 16.0;
    auto controller_config = config();
    controller_config.tracker.causal_memory_enabled = true;
    NativeGamepadController controller(
        controller_config, [&now] { return now; });
    controller.set_benchmark_intent_fusion_mode(
        controller_native::BenchmarkIntentFusionMode::CausalVector);
    auto physical = aiming();
    physical.right_x = 0.35f;

    // Establish a real actuator anchor before the observation.  The pending
    // interval must not invent a zero state before the first delivery.
    now = 15.970;
    (void)controller.build_output(physical);
    controller.report_output_delivery(true, true, 15.9701, 1);

    now = 16.000;
    controller.submit_vision_snapshot(target(1, now, 80.0f, -40.0f));
    (void)controller.build_output(physical);
    controller.report_output_delivery(true, true, 16.0001, 1);
    require(std::fabs(controller.last_output_components().final_stick.x) >
                1.0e-4f ||
                std::fabs(controller.last_output_components().final_stick.y) >
                1.0e-4f,
            "R fixture must deliver a nonzero final command");

    // A fresh observation with the same selected-target D gives the ledger a
    // current interval containing the just-delivered, not-yet-visible work.
    now = 16.001;
    controller.submit_vision_snapshot(target(2, now, 80.0f, -40.0f));
    (void)controller.build_output(physical);
    const auto& estimate = controller.last_causal_memory_estimate();
    require(estimate.valid && estimate.pending_valid,
            "R fixture must expose a valid pending estimate");
    require(std::fabs(estimate.pending_total_px.x) > 1.0e-4f ||
                std::fabs(estimate.pending_total_px.y) > 1.0e-4f,
            "R fixture must expose nonzero pending final-T work");

    const auto& plan = controller.last_target_plan();
    const pipeline_contract::Vec2f expected_r =
        controller_native::remaining_work_after_delivery(
            {80.0f, -40.0f}, estimate.pending_total_px);
    require(std::fabs(plan.error_px.x - expected_r.x) < 1.0e-3f &&
                std::fabs(plan.error_px.y - expected_r.y) < 1.0e-3f,
            "production plan must consume signed R=D-P");
    require(std::fabs(controller.last_frame_vision_state().screen_center_x -
                          320.0f) < 1.0e-3f &&
                std::fabs(controller.last_frame_vision_state().screen_center_y -
                          256.0f) < 1.0e-3f,
            "R must not be fed back into screen-center geometry");
}

void test_causal_memory_refreshes_pending_on_each_decision_and_epoch_boundary() {
    double now = 16.0;
    auto shadow_config = config();
    shadow_config.tracker.causal_memory_enabled = true;

    // With no selected target, the memory seam must not alter the physical
    // passthrough.  This also proves the non-target path is not an implicit
    // second output owner.
    double manual_now = 19.0;
    NativeGamepadController manual_only(
        shadow_config, [&manual_now] { return manual_now; });
    auto manual_input = aiming();
    manual_input.left_x = 0.18f;
    manual_input.left_y = -0.11f;
    manual_input.right_x = 0.37f;
    manual_input.right_y = -0.22f;
    manual_input.right_trigger = 0.41f;
    manual_input.rb = true;
    manual_input.a = true;
    const auto manual_output = manual_only.build_output(manual_input);
    require(
        same_gamepad_output_bitwise(
            manual_output,
            controller_native::output_from_physical_input(manual_input)),
        "no-target manual final output must remain a bitwise passthrough");

    NativeGamepadController shadow(shadow_config, [&now] { return now; });
    const auto physical = aiming();

    // Establish a known global actuator state before the capture.  Without
    // this explicit successful neutral anchor, the 20ms in-flight prefix is
    // intentionally IncompleteHistory rather than an invented zero hold.
    now = 15.970;
    (void)shadow.build_output(physical);
    shadow.report_output_delivery(true, true, now + 0.0001, 1);
    now = 16.0;
    shadow.submit_vision_snapshot(target(1, now, 80.0f, -40.0f));
    (void)shadow.build_output(physical);
    shadow.report_output_delivery(true, true, now + 0.0001, 1);

    std::array<float, 6> pending_x{};
    for (std::size_t index = 0; index < pending_x.size(); ++index) {
        now = 16.001 + static_cast<double>(index) * 0.001;
        (void)shadow.build_output(physical);
        const auto& estimate = shadow.last_causal_memory_estimate();
        require(estimate.pending_valid && estimate.valid,
                "same-capture decision must expose refreshed pending work");
        if (index == 0) {
            require(estimate.pending_response_confidence_valid,
                    "controller causal trace must expose pending confidence validity");
        }
        pending_x[index] = estimate.pending_total_px.x;
        shadow.report_output_delivery(
            true, true, now + 0.0001, 1);
    }
    for (std::size_t index = 1; index < pending_x.size(); ++index) {
        require(pending_x[index] > pending_x[index - 1] + 1.0e-5f,
                "successful deliveries between captures must grow pending");
    }

    now += 0.001;
    shadow.report_output_delivery(false, true, now + 0.0001, 1);
    now += 0.001;
    (void)shadow.build_output(physical);
    require(shadow.last_causal_memory_estimate().status ==
                controller_native::CausalMotionLedgerStatus::BackendStateUnknown,
            "failed delivery must make the shadow backend epoch unknown");
    shadow.report_output_delivery(true, true, now + 0.0001, 1);
    now += 0.001;
    (void)shadow.build_output(physical);
    require(shadow.last_causal_memory_estimate().status ==
                controller_native::CausalMotionLedgerStatus::BackendStateUnknown,
            "same reconnect epoch must not silently recover after failure");
    shadow.report_output_delivery(true, true, now + 0.0001, 2);
    // A new device epoch clears the sticky backend identity, but its first
    // non-neutral sample cannot retroactively cover the old capture.
    now += 0.001;
    (void)shadow.build_output(physical);
    require(shadow.last_causal_memory_estimate().status !=
                controller_native::CausalMotionLedgerStatus::BackendStateUnknown &&
                shadow.last_causal_memory_estimate().status ==
                    controller_native::CausalMotionLedgerStatus::IncompleteHistory &&
                !shadow.last_causal_memory_estimate().pending_valid,
            "new epoch must recover identity before history coverage");
    // After a complete post-epoch delay window and a fresh capture, pending
    // becomes numerically available.
    now += 0.020;
    shadow.submit_vision_snapshot(target(2, now, 80.0f, -40.0f));
    (void)shadow.build_output(physical);
    require(shadow.last_causal_memory_estimate().pending_valid,
            "new reconnect epoch must re-establish pending shadow history");

    now = 18.0;
    NativeGamepadController disabled(shadow_config, [&now] { return now; });
    disabled.submit_vision_snapshot(target(1, now, 60.0f, -20.0f));
    (void)disabled.build_output(physical);
    disabled.report_output_delivery(true, true, now + 0.0001, 3);
    now += 0.001;
    disabled.report_output_delivery(true, false, now + 0.0001, 3);
    now += 0.001;
    (void)disabled.build_output(physical);
    require(disabled.last_causal_memory_estimate().status ==
                controller_native::CausalMotionLedgerStatus::BackendStateUnknown,
            "disabled output must remain unknown on same reconnect epoch");
    disabled.report_output_delivery(true, true, now + 0.0001, 3);
    now += 0.001;
    (void)disabled.build_output(physical);
    require(disabled.last_causal_memory_estimate().status ==
                controller_native::CausalMotionLedgerStatus::BackendStateUnknown,
            "same-device delivery must not recover disabled output history");

    double carry_now = 20.0;
    NativeGamepadController carry(shadow_config, [&carry_now] { return carry_now; });
    carry_now = 19.970;
    (void)carry.build_output(aiming());
    carry.report_output_delivery(true, true, carry_now + 0.0001, 5);
    carry_now = 20.0;
    auto manual_before_acquisition = aiming();
    manual_before_acquisition.right_x = 0.90f;
    const auto manual_final = carry.build_output(manual_before_acquisition);
    require(
        same_gamepad_output_bitwise(
            manual_final,
            controller_native::output_from_physical_input(
                manual_before_acquisition)),
        "pre-acquisition pure manual final output must remain unchanged");
    carry.report_output_delivery(true, true, carry_now + 0.0001, 5);
    carry_now = 20.010;
    carry.submit_vision_snapshot(target(1, carry_now, 70.0f, 0.0f));
    (void)carry.build_output(physical);
    require(carry.last_causal_memory_estimate().pending_valid &&
                carry.last_causal_memory_estimate().pending_total_px.x > 3.0f,
            "target admission must retain targetless manual carry-in");
    const auto carry_expected_r =
        controller_native::remaining_work_after_delivery(
            {70.0f, 0.0f},
            carry.last_causal_memory_estimate().pending_total_px);
    require(
        std::fabs(carry.last_target_plan().error_px.x -
                  carry_expected_r.x) < 1.0e-3f &&
            std::fabs(carry.last_target_plan().error_px.y -
                      carry_expected_r.y) < 1.0e-3f,
        "first target observation must apply signed R to manual carry-in");
    std::cout << "[W5 PASS PhaseB] controller_global_history_per_decision_refresh"
              << " epoch_recovery_and_carry_in\n";
}

void test_causal_memory_controller_60k_output_and_performance() {
    constexpr std::uint64_t kTicks = 60'000;
    struct RunResult {
        double elapsed_ms = 0.0;
        std::uint64_t valid_estimates = 0;
        std::vector<GamepadOutputState> outputs;
    };

    const auto run = [&](bool shadow_enabled) {
        double now = 200.0;
        auto controller_config = config();
        controller_config.tracker.causal_memory_enabled = shadow_enabled;
        controller_config.tracker.causal_memory_response_delay_ms = 20.0f;
        controller_config.tracker.causal_memory_horizon_ms = 200.0f;
        NativeGamepadController controller(
            controller_config, [&now] { return now; });
        const auto physical = aiming();

        // Establish a known actuator state outside the timed loop.  The
        // target publication cadence is deterministic and identical for both
        // paths; the only changed input is the shadow flag.
        now = 199.970;
        (void)controller.build_output(physical);
        controller.report_output_delivery(true, true, 199.9701, 1);

        RunResult result;
        result.outputs.reserve(static_cast<std::size_t>(kTicks));
        const auto start = std::chrono::steady_clock::now();
        for (std::uint64_t tick = 0; tick < kTicks; ++tick) {
            now = 200.000 + static_cast<double>(tick) * 0.001;
            if ((tick % 10u) == 0u) {
                controller.submit_vision_snapshot(target(
                    tick / 10u + 1u, now, 0.0f, 0.0f));
            }
            result.outputs.push_back(controller.build_output(physical));
            if (shadow_enabled &&
                controller.last_causal_memory_estimate().pending_valid &&
                controller.last_causal_memory_estimate().valid) {
                ++result.valid_estimates;
            }
            controller.report_output_delivery(true, true, now + 0.0001, 1);
        }
        result.elapsed_ms = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - start).count()) /
            1'000'000.0;
        return result;
    };

    std::array<double, 3> off_ms{};
    std::array<double, 3> on_ms{};
    for (std::size_t pass = 0; pass < 3; ++pass) {
        RunResult off;
        RunResult on;
        if ((pass & 1u) == 0u) {
            off = run(false);
            on = run(true);
        } else {
            on = run(true);
            off = run(false);
        }
        require(on.valid_estimates >= kTicks - 100,
                "controller-level shadow path did not exercise valid pending queries");
        require(off.outputs.size() == on.outputs.size(),
                "controller A/B output sequence lengths differ");
        for (std::size_t index = 0; index < off.outputs.size(); ++index) {
            require(same_gamepad_output_bitwise(
                        off.outputs[index], on.outputs[index]),
                    "causal shadow changed a final GamepadOutputState sample");
        }
        off_ms[pass] = off.elapsed_ms;
        on_ms[pass] = on.elapsed_ms;
        const double delta_ms = on_ms[pass] - off_ms[pass];
        const double delta_us_per_tick =
            delta_ms * 1000.0 / static_cast<double>(kTicks);
        std::cout << "causal_controller_60k_pair=" << pass
                  << " off_ms=" << off_ms[pass]
                  << " on_ms=" << on_ms[pass]
                  << " delta_ms=" << delta_ms
                  << " delta_us_per_tick=" << delta_us_per_tick
                  << " valid_estimates=" << on.valid_estimates << "\n";
    }
}

void test_gate2_capture_state_is_latest_only_and_horizon_explicit() {
    auto shadow_config = config();
    shadow_config.tracker.causal_memory_enabled = true;
    shadow_config.tracker.causal_memory_response_delay_ms = 20.0f;
    shadow_config.tracker.causal_memory_horizon_ms = 200.0f;
    const auto physical = aiming();

    double now = 10.0;
    NativeGamepadController controller(
        shadow_config, [&now] { return now; });

    // Establish an explicit global neutral anchor.  This is necessary for a
    // controller-level capture test: no source/target lifecycle is allowed
    // to invent the actuator state before the first successful report.
    now = 9.970;
    (void)controller.build_output(physical);
    controller.report_output_delivery(true, true, 9.9701, 11);

    const auto run_frame = [&](std::uint64_t frame_id,
                               double capture_time,
                               double decision_time) {
        now = decision_time;
        controller.submit_vision_snapshot(
            target(frame_id, capture_time, 80.0f, -40.0f));
        (void)controller.build_output(physical);
        controller.report_output_delivery(true, true, decision_time + 0.0001, 11);
    };

    // First source frame establishes the current capture but has no previous
    // compatible pair yet.
    run_frame(1, 10.000, 10.000);
    const auto first = controller.last_causal_memory_estimate();
    require(!first.realized_valid,
            "first capture must not manufacture a realized pair");
    require(first.pending_valid,
            "first anchored capture must expose pending history");
    const float first_pending = first.pending_total_px.x;

    // Duplicate frame id/timestamp and a different id with the same source
    // timestamp must not advance the capture pair twice.  Successful reports
    // still belong to the same decision history and therefore grow pending.
    run_frame(1, 10.000, 10.001);
    const auto duplicate = controller.last_causal_memory_estimate();
    require(!duplicate.realized_valid && duplicate.pending_valid &&
                duplicate.pending_total_px.x > first_pending + 1.0e-5f,
            "duplicate source frame must not advance capture but must retain delivery history");
    run_frame(2, 10.000, 10.002);
    const auto same_timestamp = controller.last_causal_memory_estimate();
    require(!same_timestamp.realized_valid && same_timestamp.pending_valid &&
                same_timestamp.pending_total_px.x >
                    duplicate.pending_total_px.x + 1.0e-5f,
            "new frame id at the same source timestamp must not double-consume capture");

    // Out-of-order and explicitly stale source samples are consumed as
    // controller inputs, but cannot move the latest-only causal capture
    // backwards or create a realized interval.
    run_frame(3, 9.995, 10.003);
    const auto out_of_order = controller.last_causal_memory_estimate();
    require(!out_of_order.realized_valid,
            "out-of-order source time must not create a realized pair");
    auto stale = target(4, 9.990, 80.0f, -40.0f);
    stale.frame_updated = false;
    stale.state.fresh_observation = false;
    now = 10.004;
    controller.submit_vision_snapshot(stale);
    (void)controller.build_output(physical);
    controller.report_output_delivery(true, true, 10.0041, 11);
    const auto stale_estimate = controller.last_causal_memory_estimate();
    require(!stale_estimate.realized_valid,
            "stale source input must not create a realized pair");

    // A strictly newer capture recovers the pair exactly once.
    run_frame(5, 10.010, 10.010);
    const auto recovered = controller.last_causal_memory_estimate();
    require(recovered.realized_valid && recovered.pending_valid,
            "strictly newer source capture must recover realized and pending state");
    require(std::abs(recovered.realized_px.x) < 1.0e-5f &&
                std::abs(recovered.realized_px.y) < 1.0e-5f,
            "recovery realized interval must remain anchored before the 10.000 capture");
    require(controller.last_target_plan().source_frame_id == 5,
            "fresh recovery frame must remain the source-frame owner");

    // Exercise the controller-level horizon contract with a fresh instance
    // per gap.  Gaps at/below the configured horizon remain explicit samples;
    // a larger gap must report HorizonExceeded rather than silently scoring a
    // zero interval.
    const std::array<double, 7> gaps_ms{{4.0, 6.0, 11.0, 25.0, 60.0,
                                         150.0, 210.0}};
    for (std::size_t index = 0; index < gaps_ms.size(); ++index) {
        double gap_now = 50.0;
        NativeGamepadController gap_controller(
            shadow_config, [&gap_now] { return gap_now; });
        gap_now = 49.970;
        (void)gap_controller.build_output(physical);
        gap_controller.report_output_delivery(true, true, 49.9701, 21);
        gap_now = 50.000;
        gap_controller.submit_vision_snapshot(
            target(100 + index, gap_now, 40.0f, -12.0f));
        (void)gap_controller.build_output(physical);
        gap_controller.report_output_delivery(true, true, 50.0001, 21);
        gap_now = 50.000 + gaps_ms[index] / 1000.0;
        gap_controller.submit_vision_snapshot(
            target(200 + index, gap_now, 40.0f, -12.0f));
        (void)gap_controller.build_output(physical);
        const auto estimate = gap_controller.last_causal_memory_estimate();
        if (gaps_ms[index] > shadow_config.tracker.causal_memory_horizon_ms) {
            require(estimate.realized_status ==
                        controller_native::CausalMotionLedgerStatus::HorizonExceeded,
                    "capture gap beyond horizon must make realized status explicit");
        } else {
            require(estimate.realized_valid &&
                        estimate.realized_status ==
                            controller_native::CausalMotionLedgerStatus::Valid,
                    "capture gap within horizon must have valid realized history");
        }
    }

    // The controller currently derives dimensions from screen-center fields,
    // but has no safe pixel-space reconciliation contract for a viewport
    // change. Keep this as an explicit model-gap fixture; it must not invent a
    // coordinate transform or be promoted to a PASS by this test.
    auto viewport_change = target(6, 10.020, 40.0f, -12.0f);
    viewport_change.state.screen_center_x = 640.0f;
    viewport_change.state.screen_center_y = 360.0f;
    now = 10.020;
    controller.submit_vision_snapshot(viewport_change);
    (void)controller.build_output(physical);
    const auto viewport_estimate = controller.last_causal_memory_estimate();
    const std::array<std::string, 2> capture_paths{{
        "artifacts/benchmarks/w5-causal-memory-gate2-20260808/GATE2_CONTROLLER_CAPTURE.json",
        "../../../artifacts/benchmarks/w5-causal-memory-gate2-20260808/GATE2_CONTROLLER_CAPTURE.json"}};
    bool capture_written = false;
    for (const auto& capture_path : capture_paths) {
        std::ofstream output(capture_path, std::ios::binary);
        if (!output.good()) continue;
        output << "{\n"
               << "  \"viewport_changed\":true,\n"
               << "  \"realized_valid\":"
               << (viewport_estimate.realized_valid ? "true" : "false") << ",\n"
               << "  \"pending_valid\":"
               << (viewport_estimate.pending_valid ? "true" : "false") << ",\n"
               << "  \"status\":"
               << static_cast<int>(viewport_estimate.status) << ",\n"
               << "  \"realized_status\":"
               << static_cast<int>(viewport_estimate.realized_status) << ",\n"
               << "  \"pending_total_px\":{\"x\":"
               << viewport_estimate.pending_total_px.x << ",\"y\":"
               << viewport_estimate.pending_total_px.y << "}\n"
               << "}\n";
        capture_written = true;
        break;
    }
    require(capture_written,
            "viewport fixture must serialize the actual W5 estimate");
    std::cout << "[W5 Gate2 MODEL GAP] viewport/frame-size change has no"
              << " proven pixel reconciliation; actual_realized_valid="
              << (viewport_estimate.realized_valid ? 1 : 0)
              << " actual_pending_valid="
              << (viewport_estimate.pending_valid ? 1 : 0)
              << "; no guessed transform\n";
    std::cout << "[W5 Gate2 PASS] controller_capture_latest_only_stale_recovery"
              << " gaps=4,6,11,25,60,150,210 horizon=explicit\n";
}

void test_remaining_work_drops_and_rebases_after_long_control_gap() {
    double now = 16.0;
    auto controller_config = config();
    controller_config.tracker.causal_memory_enabled = true;
    controller_config.ai_aim.ads_snap_window_ms = 300;
    controller_config.ai_aim.target_max_age_ms = 500.0f;
    controller_config.ai_aim.target_projection_max_age_ms = 500.0f;
    NativeGamepadController controller(
        controller_config, [&now] { return now; });
    const auto physical = aiming();

    controller.submit_vision_snapshot(target(1, now, 80.0f, 0.0f));
    (void)controller.build_output(physical);
    controller.report_output_delivery(true, true, now + 0.000001, 1);

    now += 0.120;
    (void)controller.build_output(physical);
    require(
        controller.last_target_plan().target_id != 0,
        "fixture must retain target identity across the accounting-gap test");
    require(
        !controller.last_target_plan().remaining_work_valid,
        "overlong control gap must discard Remaining instead of releasing stale work");

    require(
        !controller.last_target_plan().remaining_work_valid,
        "long-gap rebase must not restore stale Remaining authority");
}

void test_acquisition_trace_joins_source_observation_across_controller_stages() {
    double now = 18.0;
    NativeGamepadController controller(config(), [&now] { return now; });
    const auto physical = aiming();
    controller.submit_vision_snapshot(
        target(91, now, 40.0f, -12.0f, 9001));
    (void)controller.build_output(physical);

    const auto& plan = controller.last_target_plan();
    const auto& vision_state = controller.last_frame_vision_state();
    const auto& trace = controller.last_acquisition_trace();
    require(plan.source_frame_id == 91,
            "acquisition plan lost the source frame join key");
    require(plan.source_observation_id == 9001,
            "acquisition plan lost the selected source observation key");
    require(plan.target_id != plan.source_observation_id &&
                plan.target_id != 0,
            "fixture must keep persistent target identity distinct from source observation");
    require(vision_state.selected_observation_id ==
                plan.source_observation_id,
            "controller sample must report plan.source_observation_id");
    require(trace.valid && trace.source_frame_id == 91 &&
                trace.source_observation_id == 9001 &&
                trace.persistent_target_id == plan.target_id &&
                trace.target_acquisition_id != 0,
            "acquisition trace did not preserve all source/target join keys");
    require(trace.has_first_requested_ai &&
                trace.has_first_shaped_ai &&
                trace.has_first_fused_output,
            "acquisition trace missed a controller stage");
}

void test_ads_strong_cooperative_mix_uses_non_additive_final_envelope() {
    double now = 34.7;
    auto controller_config = config();
    controller_config.ai_aim.ads_snap_window_ms = 300;
    controller_config.ai_aim.ads_completion_fresh_frames = 1000;
    NativeGamepadController controller(
        controller_config, [&now] { return now; });
    controller.set_benchmark_intent_fusion_mode(
        controller_native::BenchmarkIntentFusionMode::CausalVector);
    auto physical = aiming();
    physical.right_x = 0.80f;

    controller_native::GamepadOutputState output{};
    for (std::uint64_t frame = 1; frame <= 20; ++frame) {
        controller.submit_vision_snapshot(
            sized_target(frame, now, 35.0f, 0.0f, 70.0f, 710));
        output = controller.build_output(physical);
        now += 0.010;
    }

    const auto& components = controller.last_output_components();
    require(controller.last_ai_aim_mode() == "ads_snap",
            "strong-mix ADS fixture left acquisition mode");
    require(components.shaped_assist_stick.x > 0.05f,
            "strong-mix ADS fixture did not generate material AI");
    require(components.intent_fusion_predictive_envelope_applied,
            "ADS strong mix did not enter the single final-output envelope");
    require(components.intent_fusion_fresh_final_radial <=
                components.intent_fusion_fresh_strongest_valid_radial + 0.0001f,
            "ADS strong mix exceeded the strongest validated radial proposal");
    require(components.intent_fusion_fresh_final_radial <=
                components.intent_fusion_fresh_permitted_radial + 0.0001f,
            "ADS strong mix exceeded its stopping permission");
    require(output.right_x < physical.right_x - 0.10f,
            "ADS strong mix still behaved like additive manual + AI force");
}

void test_bodylock_strong_mix_uses_non_additive_final_envelope_at_both_ranges() {
    auto run_fixture = [](float body_height_px,
                          std::uint64_t observation_id) {
        double now = 34.9;
        auto controller_config = config();
        controller_config.ai_aim.ads_completion_fresh_frames = 1;
        controller_config.ai_aim.ads_completion_radius_px = 8.0f;
        controller_config.ai_aim.body_lock_confidence_frames = 1;
        NativeGamepadController controller(
            controller_config, [&now] { return now; });
        controller.set_benchmark_intent_fusion_mode(
            controller_native::BenchmarkIntentFusionMode::CausalVector);
        auto physical = aiming();
        physical.right_x = 0.80f;

        controller.submit_vision_snapshot(sized_target(
            1, now, 2.0f, 0.0f, body_height_px, observation_id));
        (void)controller.build_output(physical);
        for (std::uint64_t frame = 2; frame <= 20; ++frame) {
            now += 0.010;
            controller.submit_vision_snapshot(sized_target(
                frame, now, 35.0f, 0.0f,
                body_height_px, observation_id));
            (void)controller.build_output(physical);
        }
        return std::pair{
            controller.last_target_plan(),
            controller.last_output_components()};
    };

    const auto [near_plan, near_components] = run_fixture(180.0f, 711);
    require(near_plan.mode == pipeline_contract::ControlMode::BodyLockFollow,
            "near strong-mix fixture did not enter BodyLock");
    require(near_plan.normalized_size >= 0.24f,
            "near strong-mix fixture did not enter the size policy scope");
    require(near_components.shaped_assist_stick.x > 0.05f,
            "near strong-mix fixture did not generate material AI");
    require(near_components.intent_fusion_predictive_envelope_applied,
            "near BodyLock did not enter the single final-output envelope");
    require(near_components.intent_fusion_fresh_final_radial <=
                near_components.intent_fusion_fresh_strongest_valid_radial + 0.0001f,
            "near BodyLock exceeded the strongest validated radial proposal");
    require(near_components.intent_fusion_fresh_final_radial <=
                near_components.intent_fusion_fresh_permitted_radial + 0.0001f,
            "near BodyLock exceeded its stopping permission");

    const auto [far_plan, far_components] = run_fixture(60.0f, 712);
    require(far_plan.mode == pipeline_contract::ControlMode::BodyLockFollow,
            "far comparison fixture did not enter BodyLock");
    require(far_plan.normalized_size <= 0.13f,
            "far comparison fixture accidentally entered near-target scope");
    require(far_components.shaped_assist_stick.x > 0.05f,
            "far comparison fixture did not generate material AI");
    require(far_components.intent_fusion_predictive_envelope_applied,
            "far BodyLock did not enter the single final-output envelope");
    require(far_components.intent_fusion_fresh_final_radial <=
                far_components.intent_fusion_fresh_strongest_valid_radial + 0.0001f,
            "far BodyLock exceeded the strongest validated radial proposal");
    require(far_components.intent_fusion_fresh_final_radial <=
                far_components.intent_fusion_fresh_permitted_radial + 0.0001f,
            "far BodyLock exceeded its stopping permission");
}

void test_only_worsening_wrong_way_axis_stops_suppressing_assist() {
    double now = 35.0;
    NativeGamepadController controller(config(), [&now] { return now; });
    // This fixture asserts the retired legacy-axis arbitration contract.  The
    // production-equivalent default is CausalVectorBaseline; keep the old
    // behavior explicit so this test cannot silently select its semantics.
    controller.set_benchmark_intent_fusion_mode(
        controller_native::BenchmarkIntentFusionMode::LegacyAxis);
    auto physical = aiming();
    physical.right_x = -0.30f;
    physical.right_y = 0.20f;

    bool intervened_x = false;
    bool intervened_y = false;
    float minimum_retention_x = 1.0f;
    float minimum_retention_y = 1.0f;
    for (std::uint64_t frame = 1; frame <= 6; ++frame) {
        const float growing_x_error = 10.0f + static_cast<float>(frame) * 3.0f;
        controller.submit_vision_snapshot(
            target(frame, now, growing_x_error, -20.0f));
        controller.build_output(physical);
        const auto& components = controller.last_output_components();
        intervened_x = intervened_x || components.axis_intent_intervention.x > 0.5f;
        intervened_y = intervened_y || components.axis_intent_intervention.y > 0.5f;
        minimum_retention_x = std::min(
            minimum_retention_x, components.axis_manual_retention.x);
        minimum_retention_y = std::min(
            minimum_retention_y, components.axis_manual_retention.y);
        now += 0.010;
    }

    require(intervened_x,
            "worsening wrong-way X input did not stop suppressing assist");
    require(!intervened_y,
            "helpful/non-worsening Y input was incorrectly overridden");
    require(minimum_retention_x < 0.90f && minimum_retention_x >= 0.65f,
            "confirmed wrong X must attenuate only to the configured floor");
    require(std::fabs(minimum_retention_y - 1.0f) < 0.0001f,
            "wrong X must preserve the complete Y-axis input");
}

void test_ads_and_bodylock_share_one_resolved_target_geometry() {
    double now = 40.0;
    auto controller_config = config();
    controller_config.tracker.aim_height_ratio = 0.365f;
    controller_config.ai_aim.body_lock_confidence_frames = 2;
    controller_config.ai_aim.body_lock_box_tolerance_px = 8.0f;
    NativeGamepadController controller(controller_config, [&now] { return now; });
    const auto physical = aiming();
    bool saw_ads = false;
    bool saw_bodylock = false;
    constexpr float expected_y = 180.0f + 200.0f * 0.365f;

    for (std::uint64_t frame = 1; frame <= 16; ++frame) {
        auto snapshot = target(frame, now, 2.0f, -3.0f);
        pipeline_contract::VisionCandidateSnapshot candidate;
        candidate.id = 42;
        candidate.valid = true;
        candidate.has_aim_point = true;
        candidate.aim_point_px = {322.0f, 253.0f};
        candidate.body_box_px = {282.0f, 180.0f, 80.0f, 200.0f};
        candidate.confidence = 0.95f;
        snapshot.candidates.push_back(candidate);
        controller.submit_vision_snapshot(snapshot);
        controller.build_output(physical);
        saw_ads = saw_ads || controller.last_ai_aim_mode() == "ads_snap";
        saw_bodylock = saw_bodylock || controller.last_ai_aim_mode() == "body_lock";
        require(
            std::fabs(controller.last_frame_vision_state().target_y - expected_y) < 0.001f,
            "ADS/BodyLock applied inconsistent or repeated target geometry");
        now += 0.010;
    }
    require(saw_ads, "geometry handoff never entered ADS");
    require(saw_bodylock, "geometry handoff never entered BodyLock");

    now += 0.001;
    controller.build_output(physical);
    require(
        std::fabs(controller.last_frame_vision_state().target_y - expected_y) < 0.001f,
        "coasting applied target height ratio a second time");
}

void test_held_ads_target_change_does_not_rearm_snap() {
    double now = 60.0;
    auto controller_config = config();
    controller_config.ai_aim.ads_snap_window_ms = 40;
    controller_config.ai_aim.ads_completion_fresh_frames = 1;
    controller_config.ai_aim.ads_completion_radius_px = 8.0f;
    controller_config.ai_aim.body_lock_box_tolerance_px = 8.0f;
    NativeGamepadController controller(controller_config, [&now] { return now; });
    auto physical = aiming();

    controller.submit_vision_snapshot(target(1, now, 2.0f, 0.0f, 101));
    controller.build_output(physical);
    require(controller.last_ai_aim_mode() == "body_lock",
            "close opening target must settle into BodyLock");

    now += 0.060;
    controller.submit_vision_snapshot(target(2, now, 60.0f, 0.0f, 202));
    controller.build_output(physical);
    require(controller.last_ai_aim_mode() == "body_lock",
            "new target while ADS is held must not rearm ADS snap");

    now += 0.005;
    physical.left_trigger = 0.72f;
    controller.build_output(physical);
    require(controller.last_ai_aim_mode() == "body_lock",
            "a partial LT axis drop must not create a false release");

    now += 0.001;
    physical.left_trigger = 0.0f;
    controller.build_output(physical);
    require(controller.last_ai_aim_mode() == "body_lock",
            "a one-tick LT zero must be treated as input dropout");

    for (unsigned int sample = 1;
         sample <
             controller_native::kAimLeftTriggerIdleDebounceSamples;
         ++sample) {
        now += 0.001;
        controller.build_output(physical);
    }
    require(controller.last_ai_aim_mode() == "manual",
            "a sustained LT release must return to manual mode");

    now += 0.001;
    physical.left_trigger = 1.0f;
    controller.submit_vision_snapshot(target(3, now, 60.0f, 0.0f, 202));
    controller.build_output(physical);
    require(controller.last_ai_aim_mode() == "ads_snap",
            "a deliberate debounced release must rearm snap immediately");
}

struct AdsReleaseTransitionResult {
    bool one_tick_dropout_preserved = false;
    bool dropout_recovery_stayed_bodylock = false;
    unsigned int release_samples_to_manual = 0;
    GamepadOutputState last_ai_owned_output{};
    GamepadOutputState first_manual_output{};
};

AdsReleaseTransitionResult run_ads_release_transition_fixture() {
    constexpr unsigned int kMeasurementSamples = 32;

    auto controller_config = config();
    controller_config.ai_aim.ads_snap_window_ms = 40;
    controller_config.ai_aim.ads_completion_fresh_frames = 1;
    controller_config.ai_aim.ads_completion_radius_px = 8.0f;
    controller_config.ai_aim.body_lock_box_tolerance_px = 8.0f;

    double counterfactual_now = 70.0;
    NativeGamepadController counterfactual(
        controller_config, [&counterfactual_now] { return counterfactual_now; });
    auto counterfactual_physical = aiming();
    counterfactual_physical.right_x = 0.75f;
    counterfactual_physical.right_y = -0.35f;
    counterfactual.submit_vision_snapshot(
        target(1, counterfactual_now, -2.0f, 2.0f, 701));
    (void)counterfactual.build_output(counterfactual_physical);
    require(counterfactual.last_ai_aim_mode() == "body_lock",
            "ADS release fixture did not establish BodyLock");

    counterfactual_now += 0.001;
    counterfactual_physical.left_trigger = 0.0f;
    (void)counterfactual.build_output(counterfactual_physical);
    AdsReleaseTransitionResult result;
    result.one_tick_dropout_preserved =
        counterfactual.last_ai_aim_mode() == "body_lock";

    counterfactual_now += 0.001;
    counterfactual_physical.left_trigger = 1.0f;
    (void)counterfactual.build_output(counterfactual_physical);
    result.dropout_recovery_stayed_bodylock =
        counterfactual.last_ai_aim_mode() == "body_lock";

    double release_now = 71.0;
    NativeGamepadController release(
        controller_config, [&release_now] { return release_now; });
    auto release_physical = aiming();
    release_physical.right_x = 0.75f;
    release_physical.right_y = -0.35f;
    release.submit_vision_snapshot(target(1, release_now, -2.0f, 2.0f, 711));
    result.last_ai_owned_output = release.build_output(release_physical);
    require(release.last_ai_aim_mode() == "body_lock",
            "sustained ADS release fixture did not establish BodyLock");

    release_physical.left_trigger = 0.0f;
    for (unsigned int sample = 1; sample <= kMeasurementSamples; ++sample) {
        release_now += 0.001;
        const auto output = release.build_output(release_physical);
        if (release.last_ai_aim_mode() == "manual") {
            result.release_samples_to_manual = sample;
            result.first_manual_output = output;
            break;
        }
        result.last_ai_owned_output = output;
    }
    return result;
}

void write_ads_release_transition_report(
    const AdsReleaseTransitionResult& result,
    bool passed) {
    if (g_ads_release_incident_report_path.empty()) return;
    std::ofstream output(
        g_ads_release_incident_report_path, std::ios::out | std::ios::trunc);
    require(output.is_open(), "could not open ADS release incident report");
    const float output_step_x =
        result.first_manual_output.right_x - result.last_ai_owned_output.right_x;
    const float output_step_y =
        result.first_manual_output.right_y - result.last_ai_owned_output.right_y;
    output << "{\n"
           << "  \"schema_version\": 1,\n"
           << "  \"incident_id\": \"ads-release-ai-tail\",\n"
           << "  \"trigger_bodylock_established\": true,\n"
           << "  \"one_tick_dropout_preserved\": "
           << (result.one_tick_dropout_preserved ? "true" : "false") << ",\n"
           << "  \"dropout_recovery_stayed_bodylock\": "
           << (result.dropout_recovery_stayed_bodylock ? "true" : "false")
           << ",\n"
           << "  \"release_samples_to_manual\": "
           << result.release_samples_to_manual << ",\n"
           << "  \"release_latency_ms_at_1khz\": "
           << result.release_samples_to_manual << ",\n"
           << "  \"last_ai_owned_output\": ["
           << result.last_ai_owned_output.right_x << ", "
           << result.last_ai_owned_output.right_y << "],\n"
           << "  \"first_manual_output\": ["
           << result.first_manual_output.right_x << ", "
           << result.first_manual_output.right_y << "],\n"
           << "  \"output_step_at_manual\": [" << output_step_x << ", "
           << output_step_y << "],\n"
           << "  \"oracle_release_within_3_samples\": "
           << (result.release_samples_to_manual > 0 &&
                       result.release_samples_to_manual <= 3
                   ? "true"
                   : "false")
           << ",\n"
           << "  \"passed\": " << (passed ? "true" : "false") << "\n"
           << "}\n";
}

void test_ads_release_unloads_ai_within_three_control_samples() {
    const auto result = run_ads_release_transition_fixture();
    const bool release_fast_enough = result.release_samples_to_manual > 0 &&
        result.release_samples_to_manual <= 3;
    const bool passed = result.one_tick_dropout_preserved &&
        result.dropout_recovery_stayed_bodylock && release_fast_enough;
    write_ads_release_transition_report(result, passed);
    require(result.one_tick_dropout_preserved,
            "one-tick LT dropout incorrectly ended the ADS epoch");
    require(result.dropout_recovery_stayed_bodylock,
            "one-tick LT dropout incorrectly rearmed ADS snap");
    require(release_fast_enough,
            "physical LT release retained AI ownership for more than 3ms");
}

void test_target_first_does_not_carry_manual_into_target_output() {
    auto make_controller = [](double* now) {
        return NativeGamepadController(config(), [now] { return *now; });
    };

    // The no-target leg is a bitwise passthrough control and deliberately
    // leaves a strong physical M in the fuser's old continuity state.
    const std::array<float, 3> manual_values{-0.80f, 0.0f, 0.80f};
    std::array<GamepadOutputState, manual_values.size()> first_target{};
    std::array<GamepadOutputState, manual_values.size()> steady_target{};
    for (std::size_t index = 0; index < manual_values.size(); ++index) {
        double now = 52.0;
        auto controller = make_controller(&now);
        auto manual = aiming();
        manual.right_x = manual_values[index];
        manual.right_y = 0.0f;
        const auto passthrough = controller.build_output(manual);
        const auto expected = controller_native::output_from_physical_input(manual);
        require(same_gamepad_output_bitwise(passthrough, expected),
                "target-first no-target control must remain a bitwise passthrough");
        const auto& no_target_components = controller.last_output_components();
        require(std::string(no_target_components.manual_authority_mode) ==
                    "no_target_passthrough" &&
                    std::fabs(no_target_components.target_final_stick.x -
                              manual.right_x) < 0.0001f &&
                    std::fabs(no_target_components.target_final_stick.y -
                              manual.right_y) < 0.0001f,
                "no-target target-first diagnostics must expose M passthrough");

        now += 0.010;
        controller.submit_vision_snapshot(target(1, now, 60.0f, -24.0f, 752));
        first_target[index] = controller.build_output(manual);
        require(std::isfinite(first_target[index].right_x) &&
                    std::isfinite(first_target[index].right_y),
                "target-first first target output must be finite");

        now += 0.010;
        controller.submit_vision_snapshot(target(2, now, 60.0f, -24.0f, 752));
        steady_target[index] = controller.build_output(manual);
        const auto& components = controller.last_output_components();
        require(std::string(components.manual_authority_mode) ==
                    "single_target_authoritative" &&
                    components.intent_fusion_mode == "target_first_final" &&
                    components.intent_fusion_manual_weight == 0.0f &&
                    components.intent_fusion_ai_weight == 1.0f,
                "target-first authority must not expose legacy allocation weights");
        require(std::isfinite(components.target_final_stick.x) &&
                    std::isfinite(components.target_final_stick.y) &&
                    std::isfinite(components.ai_correction_stick.x) &&
                    std::isfinite(components.ai_correction_stick.y) &&
                    std::fabs((components.manual_stick.x +
                               components.ai_correction_stick.x) -
                              components.target_final_stick.x) < 0.0001f &&
                    std::fabs((components.manual_stick.y +
                               components.ai_correction_stick.y) -
                              components.target_final_stick.y) < 0.0001f,
                "target-first diagnostics must satisfy A=T-M and M+A=T on both axes");
    }

    require(std::fabs(first_target[1].right_x - first_target[0].right_x) <
                0.0001f &&
                std::fabs(first_target[1].right_y - first_target[0].right_y) <
                0.0001f,
            "opposing manual must not change target-first T on entry");
    require(std::fabs(steady_target[1].right_x - steady_target[0].right_x) <
                0.0001f &&
                std::fabs(steady_target[1].right_y - steady_target[0].right_y) <
                0.0001f,
            "opposing manual must not change target-first T after entry");
    const auto require_bounded_aligned_headroom = [](const GamepadOutputState& base,
                                                     const GamepadOutputState& aligned) {
        const float base_magnitude = std::hypot(base.right_x, base.right_y);
        const float aligned_magnitude = std::hypot(aligned.right_x, aligned.right_y);
        require(base_magnitude > 0.01f && aligned_magnitude > 0.01f,
                "aligned manual headroom oracle needs material target output");
        const float scale = aligned_magnitude / base_magnitude;
        const float cosine =
            (base.right_x * aligned.right_x + base.right_y * aligned.right_y) /
            (base_magnitude * aligned_magnitude);
        require(scale >= 1.10f && scale <= 1.20f && cosine > 0.999f,
                "aligned manual must add only bounded target-direction headroom");
    };
    require_bounded_aligned_headroom(first_target[1], first_target[2]);
    require_bounded_aligned_headroom(steady_target[1], steady_target[2]);
    require(std::hypot(first_target[0].right_x, first_target[0].right_y) >
                0.01f,
            "target-first entry oracle must observe a material nonzero T");
    require(std::fabs(first_target[0].right_x - manual_values[0]) > 0.05f,
            "target-first entry must not return the held manual X as T");

    // A durable selector replacement must also compute the new target proposal
    // on that same tick; a strong M from the prior owner cannot re-enter via
    // the fuser's previous-output or TargetChanged fallback state.
    double replacement_now = 53.0;
    NativeGamepadController replacement(config(), [&replacement_now] {
        return replacement_now;
    });
    auto neutral = aiming();
    replacement.submit_vision_snapshot(runtime_app::adapt_vision_result(
        production_vision_frame(1, 53'000'000'000ull, 380.0f, 1, false)));
    (void)replacement.build_output(neutral);
    replacement_now += 0.020;
    replacement.submit_vision_snapshot(runtime_app::adapt_vision_result(
        production_vision_frame(2, 53'020'000'000ull, 380.0f, 1, false)));
    (void)replacement.build_output(neutral);

    auto strong_manual = aiming();
    strong_manual.right_x = -0.90f;
    replacement_now += 0.020;
    replacement.submit_vision_snapshot(runtime_app::adapt_vision_result(
        production_vision_frame(3, 53'040'000'000ull, 250.0f, 2, false)));
    const auto replacement_output = replacement.build_output(strong_manual);
    require(replacement.last_target_plan().selector_target_generation == 2 &&
                replacement.last_target_plan().target_id != 0,
            "replacement control must consume the durable selector generation");
    require(std::hypot(replacement_output.right_x, replacement_output.right_y) >
                0.01f,
            "target replacement oracle must observe a material nonzero T");
    require(std::fabs(replacement_output.right_x - strong_manual.right_x) > 0.05f,
            "target replacement must not return the prior strong manual input");
    require(replacement.last_output_components().intent_fusion_manual_escape == false,
            "target-first replacement must not admit raw manual escape");
}

struct HelpfulManualOverdriveFixtureResult {
    GamepadOutputState neutral{};
    GamepadOutputState aligned{};
    GamepadOutputState opposing{};
    GamepadOutputState aligned_fresh{};
    float aligned_scale = 0.0f;
    float opposing_scale = 0.0f;
    float direction_cosine = 0.0f;
    bool target_owned = false;
    bool diagnostic_identity = false;
    std::uint32_t held_candidate_count = 0;
    int held_lifecycle = 0;
    bool held_cue_continuation = false;
    float held_observation_age_ms = 0.0f;
};

HelpfulManualOverdriveFixtureResult run_helpful_manual_overdrive_fixture() {
    struct Sample {
        GamepadOutputState output{};
        GamepadOutputState fresh_output{};
        bool target_owned = false;
        bool diagnostic_identity = false;
        std::uint32_t candidate_count = 0;
        int lifecycle = 0;
        bool cue_continuation = false;
        float observation_age_ms = 0.0f;
    };
    const auto run = [](float manual_x) {
        double now = 54.0;
        NativeGamepadController controller(config(), [&now] { return now; });
        auto physical = aiming();
        physical.right_x = manual_x;
        physical.right_y = 0.0f;
        (void)controller.build_output(physical);
        now += 0.010;
        controller.submit_vision_snapshot(target(1, now, 60.0f, 0.0f, 900));
        (void)controller.build_output(physical);
        now += 0.010;
        controller.submit_vision_snapshot(target(2, now, 60.0f, 0.0f, 900));
        const auto fresh_output = controller.build_output(physical);
        // The bounded headroom must persist between Vision publications at a
        // faster controller rate; gating on only the updated-frame tick would
        // alternate base/overdriven T and create a new force ripple.
        now += 0.001;
        const auto output = controller.build_output(physical);
        const auto& components = controller.last_output_components();
        Sample sample;
        sample.output = output;
        sample.fresh_output = fresh_output;
        const auto& plan = controller.last_target_plan();
        sample.candidate_count = plan.ads_candidate_count;
        sample.lifecycle = static_cast<int>(plan.lifecycle);
        sample.cue_continuation = plan.cue_continuation;
        sample.observation_age_ms = plan.observation_age_ms;
        sample.target_owned =
            std::string(components.manual_authority_mode) ==
                "single_target_authoritative" &&
            components.intent_fusion_mode == "target_first_final";
        sample.diagnostic_identity =
            std::fabs((components.manual_stick.x +
                       components.ai_correction_stick.x) -
                      components.target_final_stick.x) < 0.0001f &&
            std::fabs((components.manual_stick.y +
                       components.ai_correction_stick.y) -
                      components.target_final_stick.y) < 0.0001f;
        return sample;
    };

    const auto neutral = run(0.0f);
    const auto aligned = run(0.80f);
    const auto opposing = run(-0.80f);
    HelpfulManualOverdriveFixtureResult result;
    result.neutral = neutral.output;
    result.aligned = aligned.output;
    result.opposing = opposing.output;
    result.aligned_fresh = aligned.fresh_output;
    result.held_candidate_count = aligned.candidate_count;
    result.held_lifecycle = aligned.lifecycle;
    result.held_cue_continuation = aligned.cue_continuation;
    result.held_observation_age_ms = aligned.observation_age_ms;
    const float neutral_magnitude = std::hypot(
        result.neutral.right_x, result.neutral.right_y);
    const float aligned_magnitude = std::hypot(
        result.aligned.right_x, result.aligned.right_y);
    const float opposing_magnitude = std::hypot(
        result.opposing.right_x, result.opposing.right_y);
    if (neutral_magnitude > 1.0e-5f) {
        result.aligned_scale = aligned_magnitude / neutral_magnitude;
        result.opposing_scale = opposing_magnitude / neutral_magnitude;
    }
    if (neutral_magnitude > 1.0e-5f && aligned_magnitude > 1.0e-5f) {
        result.direction_cosine =
            (result.neutral.right_x * result.aligned.right_x +
             result.neutral.right_y * result.aligned.right_y) /
            (neutral_magnitude * aligned_magnitude);
    }
    result.target_owned = neutral.target_owned && aligned.target_owned &&
        opposing.target_owned;
    result.diagnostic_identity = neutral.diagnostic_identity &&
        aligned.diagnostic_identity && opposing.diagnostic_identity;
    return result;
}

void write_helpful_manual_overdrive_report(
    const HelpfulManualOverdriveFixtureResult& result,
    bool passed) {
    if (g_helpful_manual_overdrive_report_path.empty()) return;
    std::ofstream output(
        g_helpful_manual_overdrive_report_path,
        std::ios::out | std::ios::trunc);
    output << "{\n"
           << "  \"fixture\": \"fresh-single-target-helpful-manual-overdrive\",\n"
           << "  \"trigger\": \"target-first final T with strong aligned physical manual\",\n"
           << "  \"neutral_x\": " << result.neutral.right_x << ",\n"
           << "  \"aligned_x\": " << result.aligned.right_x << ",\n"
           << "  \"aligned_fresh_x\": " << result.aligned_fresh.right_x << ",\n"
           << "  \"opposing_x\": " << result.opposing.right_x << ",\n"
           << "  \"aligned_scale\": " << result.aligned_scale << ",\n"
           << "  \"opposing_scale\": " << result.opposing_scale << ",\n"
           << "  \"direction_cosine\": " << result.direction_cosine << ",\n"
           << "  \"held_candidate_count\": " << result.held_candidate_count << ",\n"
           << "  \"held_lifecycle\": " << result.held_lifecycle << ",\n"
           << "  \"held_cue_continuation\": "
           << (result.held_cue_continuation ? "true" : "false") << ",\n"
           << "  \"held_observation_age_ms\": "
           << result.held_observation_age_ms << ",\n"
           << "  \"target_owned\": " << (result.target_owned ? "true" : "false") << ",\n"
           << "  \"diagnostic_identity\": "
           << (result.diagnostic_identity ? "true" : "false") << ",\n"
           << "  \"passed\": " << (passed ? "true" : "false") << "\n"
           << "}\n";
}

void test_helpful_manual_adds_only_bounded_target_direction_headroom() {
    const auto result = run_helpful_manual_overdrive_fixture();
    const bool passed = result.target_owned && result.diagnostic_identity &&
        result.aligned_scale >= 1.10f && result.aligned_scale <= 1.20f &&
        std::fabs(result.opposing_scale - 1.0f) < 0.01f &&
        result.direction_cosine > 0.999f;
    write_helpful_manual_overdrive_report(result, passed);
    require(result.target_owned,
            "helpful-manual fixture did not reach fresh single-target authority");
    require(result.diagnostic_identity,
            "helpful-manual fixture broke the diagnostic A=T-M identity");
    require(result.aligned_scale >= 1.10f && result.aligned_scale <= 1.20f,
            "aligned manual did not unlock the requested 1.10x..1.20x target headroom");
    require(std::fabs(result.opposing_scale - 1.0f) < 0.01f,
            "opposing manual changed the authoritative target output");
    require(result.direction_cosine > 0.999f,
            "helpful manual changed target direction instead of only its magnitude");
}

void test_same_track_geometry_shift_keeps_final_output_bounded() {
    double now = 62.0;
    auto controller_config = config();
    controller_config.ai_aim.ads_snap_window_ms = 40;
    controller_config.ai_aim.ads_completion_fresh_frames = 1;
    controller_config.ai_aim.ads_completion_radius_px = 8.0f;
    controller_config.ai_aim.body_lock_box_tolerance_px = 8.0f;
    NativeGamepadController controller(controller_config, [&now] { return now; });
    controller.set_benchmark_intent_fusion_mode(
        controller_native::BenchmarkIntentFusionMode::CausalVector);
    const auto physical = aiming();

    controller.submit_vision_snapshot(target(1, now, 2.0f, 0.0f, 301));
    auto output = controller.build_output(physical);
    require(controller.last_ai_aim_mode() == "body_lock",
            "same-track jump fixture must first settle into BodyLock");
    const auto target_id = controller.last_target_plan().target_id;

    for (std::uint64_t frame = 2; frame <= 3; ++frame) {
        now += 0.010;
        const float dx = frame == 2 ? 12.0f : 24.0f;
        controller.submit_vision_snapshot(target(frame, now, dx, 0.0f, 301));
        output = controller.build_output(physical);
    }
    require(std::fabs(output.right_x) > 0.03f,
            "same-track jump fixture must establish material BodyLock output");
    const auto before_plan = controller.last_target_plan();
    const auto before_output = output;

    now += 0.010;
    controller.submit_vision_snapshot(target(4, now, 94.0f, 0.0f, 301));
    output = controller.build_output(physical);
    const auto after_plan = controller.last_target_plan();
    const float output_delta = std::hypot(
        output.right_x - before_output.right_x,
        output.right_y - before_output.right_y);

    require(after_plan.target_id == target_id,
            "a body-geometry shift must not invent a new canonical target");
    require(after_plan.error_px.x - before_plan.error_px.x >= 60.0f,
            "fixture must deliver the approximately 70px fresh Vision shift");
    require(controller.last_ai_aim_mode() == "body_lock",
            "same-track geometry must remain a BodyLock observation");
    require(output_delta <= 0.0805f,
            "same-track geometry shift bypassed the final vector slew envelope");
}

void test_held_ads_new_track_after_gap_starts_from_zero_ai() {
    double now = 64.0;
    auto controller_config = config();
    controller_config.ai_aim.ads_snap_window_ms = 40;
    controller_config.ai_aim.ads_completion_fresh_frames = 1;
    controller_config.ai_aim.ads_completion_radius_px = 8.0f;
    controller_config.ai_aim.body_lock_box_tolerance_px = 8.0f;
    NativeGamepadController controller(controller_config, [&now] { return now; });
    controller.set_benchmark_intent_fusion_mode(
        controller_native::BenchmarkIntentFusionMode::CausalVector);
    const auto physical = aiming();

    controller.submit_vision_snapshot(target(1, now, -2.0f, 0.0f, 401));
    auto output = controller.build_output(physical);
    for (std::uint64_t frame = 2; frame <= 5; ++frame) {
        now += 0.010;
        controller.submit_vision_snapshot(target(frame, now, -45.0f, 0.0f, 401));
        output = controller.build_output(physical);
    }
    require(output.right_x < -0.05f,
            "new-track fixture must establish old-target BodyLock ownership");
    const auto old_target_id = controller.last_target_plan().target_id;

    now += 0.010;
    ControllerVisionSnapshot miss;
    miss.frame_updated = true;
    miss.selector_identity_protocol = true;
    miss.frame_id = 6;
    miss.capture_time_seconds = now;
    miss.ready_time_seconds = now;
    miss.state.screen_center_x = 320.0f;
    miss.state.screen_center_y = 256.0f;
    controller.submit_vision_snapshot(miss);
    (void)controller.build_output(physical);

    // Expire the bounded identity hold without releasing LT. The next selected
    // person is 95px from the last observed point (-45 -> +50).
    now += 0.190;
    output = controller.build_output(physical);
    require(controller.last_target_plan().target_id == 0,
            "identity hold must expire before the replacement target arrives");
    require(std::hypot(output.right_x, output.right_y) <= 0.001f,
            "expired target must release to the manual/zero-AI baseline");

    now += 0.001;
    controller.submit_vision_snapshot(target(7, now, 50.0f, 0.0f, 402));
    output = controller.build_output(physical);
    const auto replacement_plan = controller.last_target_plan();
    const auto first_components = controller.last_output_components();
    require(replacement_plan.target_id != 0 &&
                replacement_plan.target_id != old_target_id,
            "replacement observation must receive a new canonical target id");
    require(controller.last_ai_aim_mode() == "body_lock",
            "held LT must not rearm ADS snap for the replacement target");
    require(std::fabs(first_components.requested_assist_stick.x) > 0.05f,
            "fixture must generate a material fresh-target BodyLock request");
    require(std::hypot(output.right_x, output.right_y) <= 0.001f,
            "replacement target must spend its first tick at manual/zero AI; output=" +
                std::to_string(output.right_x) + "," +
                std::to_string(output.right_y) +
                " fallback=" +
                std::to_string(first_components.intent_fusion_fallback));
    require(first_components.intent_fusion_fallback &&
                first_components.intent_fusion_ai_weight <= 0.001f,
            "replacement target must pass through the target-change admission gate");

    now += 0.001;
    output = controller.build_output(physical);
    require(output.right_x > 0.001f && output.right_x <= 0.0805f,
            "replacement BodyLock must re-enter gradually after the zero-AI tick");
}

void test_close_lateral_runner_keeps_fresh_bodylock_authority() {
    double now = 66.0;
    auto controller_config = config();
    controller_config.ai_aim.ads_snap_window_ms = 40;
    controller_config.ai_aim.ads_completion_fresh_frames = 1;
    controller_config.ai_aim.ads_completion_radius_px = 8.0f;
    controller_config.ai_aim.body_lock_box_tolerance_px = 8.0f;
    NativeGamepadController controller(controller_config, [&now] { return now; });
    controller.set_benchmark_intent_fusion_mode(
        controller_native::BenchmarkIntentFusionMode::CausalVector);
    const auto physical = aiming();

    controller.submit_vision_snapshot(target(1, now, 2.0f, 0.0f, 501));
    auto output = controller.build_output(physical);
    const auto target_id = controller.last_target_plan().target_id;
    float previous_output_x = output.right_x;

    for (std::uint64_t frame = 2; frame <= 6; ++frame) {
        now += 0.010;
        const float dx = 2.0f + 16.0f * static_cast<float>(frame - 1);
        controller.submit_vision_snapshot(target(frame, now, dx, 0.0f, 501));
        output = controller.build_output(physical);
        const auto plan = controller.last_target_plan();
        const auto components = controller.last_output_components();
        require(plan.target_id == target_id,
                "legitimate lateral runner must retain canonical ownership");
        require(controller.last_ai_aim_mode() == "body_lock",
                "legitimate lateral runner must remain in BodyLock");
        require(plan.error_px.x >= dx - 1.0f,
                "fresh runner position must remain authoritative");
        require(!components.intent_fusion_fallback,
                "ordinary same-track motion must not trip target admission");
        require(output.right_x + 0.001f >= previous_output_x,
                "ordinary lateral tracking must not become lazy or reverse");
        previous_output_x = output.right_x;
    }
    require(output.right_x >= 0.20f,
            "ordinary close lateral motion must retain material follow authority");
}

void test_same_target_reacquire_preserves_pipeline_continuity() {
    double now = 67.0;
    auto controller_config = config();
    controller_config.ai_aim.ads_completion_fresh_frames = 1;
    controller_config.ai_aim.ads_completion_radius_px = 8.0f;
    NativeGamepadController controller(controller_config, [&now] { return now; });
    controller.set_benchmark_intent_fusion_mode(
        controller_native::BenchmarkIntentFusionMode::CausalVector);
    const auto physical = aiming();

    for (std::uint64_t frame_id = 1; frame_id <= 8; ++frame_id) {
        controller.submit_vision_snapshot(
            target(frame_id, now, 80.0f, 0.0f, 601));
        (void)controller.build_output(physical);
        now += 0.010;
    }
    require(
        std::fabs(controller.last_output_components().shaped_assist_stick.x) >
            0.20f,
        "reacquire fixture must establish material shaped AI");

    ControllerVisionSnapshot miss;
    miss.frame_updated = true;
    miss.selector_identity_protocol = true;
    miss.frame_id = 9;
    miss.capture_time_seconds = now;
    miss.ready_time_seconds = now;
    miss.state.screen_center_x = 320.0f;
    miss.state.screen_center_y = 256.0f;
    controller.submit_vision_snapshot(miss);
    (void)controller.build_output(physical);
    require(
        controller.last_target_plan().lifecycle ==
            pipeline_contract::TargetLifecycle::Coasting,
        "empty fresh snapshot must enter the real Coasting lifecycle");

    now += 0.001;
    controller.submit_vision_snapshot(
        target(10, now, 80.0f, 0.0f, 601));
    const auto output = controller.build_output(physical);
    const auto& components = controller.last_output_components();
    require(
        controller.last_target_plan().lifecycle ==
            pipeline_contract::TargetLifecycle::Reacquiring,
        "same source after an empty miss must enter Reacquiring");
    require(
        std::fabs(components.requested_assist_stick.x) > 0.20f &&
            std::fabs(components.shaped_assist_stick.x) > 0.20f,
        "reacquire pipeline must retain requested and shaped AI evidence");
    require(
        !components.intent_fusion_fallback,
        "same-target Reacquiring must not take the fuser manual fallback");
    require(
        components.intent_fusion_ai_weight > 0.99f,
        "same-target Reacquiring must retain the fused AI proposal");
    require(
        std::fabs(components.post_ai_stick.x) >=
            std::fabs(components.shaped_assist_stick.x) - 0.081f,
        "same-target Reacquiring must not unload then reassert hidden AI");
    require(
        std::fabs(components.post_ai_stick.x - output.right_x) < 0.001f &&
            std::fabs(components.before_recoil_stick.x - output.right_x) <
                0.001f,
        "pipeline fixture must expose fused/post-output before recoil");
}

void test_cover_retreat_retires_pipeline_actuation_without_identity_loss() {
    double now = 69.0;
    auto controller_config = config();
    controller_config.ai_aim.ads_completion_fresh_frames = 1;
    controller_config.ai_aim.ads_completion_radius_px = 8.0f;
    controller_config.ai_aim.target_max_age_ms = 180.0f;
    controller_config.ai_aim.target_projection_max_age_ms = 180.0f;
    NativeGamepadController controller(controller_config, [&now] { return now; });
    controller.set_benchmark_intent_fusion_mode(
        controller_native::BenchmarkIntentFusionMode::CausalVector);
    const auto physical = aiming();

    for (std::uint64_t frame_id = 1; frame_id <= 8; ++frame_id) {
        controller.submit_vision_snapshot(
            target(frame_id, now, 80.0f, 0.0f, 701));
        (void)controller.build_output(physical);
        now += 0.010;
    }
    const auto target_id = controller.last_target_plan().target_id;
    const auto initial_plan = controller.last_target_plan();
    const auto initial_components = controller.last_output_components();
    require(
        controller.last_target_plan().aim_authority > 0.70f &&
            std::fabs(initial_components.requested_assist_stick.x) > 0.20f &&
            std::fabs(initial_components.shaped_assist_stick.x) > 0.20f,
        "cover-retreat pipeline must establish material AI before the miss");

    now += 0.0025;
    controller.submit_vision_snapshot(empty_fresh_snapshot(9, now));
    (void)controller.build_output(physical);
    const auto grace_plan = controller.last_target_plan();
    const auto grace_components = controller.last_output_components();
    require(
        grace_plan.lifecycle == pipeline_contract::TargetLifecycle::Coasting,
        "fresh empty frame must enter the real Coasting pipeline");
    require(
        grace_plan.target_id == target_id && grace_plan.aim_authority > 0.70f,
        "12.5ms miss must preserve identity and near-full actuation");
    require(
        std::fabs(grace_components.shaped_assist_stick.x) <=
            std::fabs(initial_components.shaped_assist_stick.x) + 0.001f,
        "Coasting shaper must not blind-rise during the grace frame");

    now += 0.0125;
    controller.submit_vision_snapshot(empty_fresh_snapshot(10, now));
    (void)controller.build_output(physical);
    now += 0.0250;
    controller.submit_vision_snapshot(empty_fresh_snapshot(11, now));
    (void)controller.build_output(physical);
    now += 0.0160;
    controller.submit_vision_snapshot(empty_fresh_snapshot(12, now));
    const auto output = controller.build_output(physical);
    const auto release_plan = controller.last_target_plan();
    const auto release_components = controller.last_output_components();
    require(
        release_plan.lifecycle == pipeline_contract::TargetLifecycle::Coasting,
        "actuation release must not retire the identity before hold_ms");
    require(
        release_plan.target_id == target_id,
        "actuation release must preserve canonical identity");
    const float expected_reliability = initial_plan.reliability *
        std::clamp(1.0f - release_plan.observation_age_ms / 180.0f,
                   0.0f, 1.0f);
    require(
        std::fabs(release_plan.reliability - expected_reliability) < 0.02f,
        "actuation release must preserve the independent 180ms reliability lease: initial=" +
            std::to_string(initial_plan.reliability) +
            " final=" + std::to_string(release_plan.reliability) +
            " age=" + std::to_string(release_plan.observation_age_ms) +
            " expected=" + std::to_string(expected_reliability));
    require(
        release_plan.aim_authority < 0.05f,
        "Coasting actuation must be retired by the 65ms candidate release");
    require(
        std::fabs(release_components.requested_assist_stick.x) <=
            std::fabs(grace_components.requested_assist_stick.x) + 0.001f &&
            std::fabs(release_components.shaped_assist_stick.x) <=
            std::fabs(grace_components.shaped_assist_stick.x) + 0.001f,
        "retiring Coasting authority must not leave a growing AI request");
    require(
        release_components.intent_fusion_ai_weight <=
            grace_components.intent_fusion_ai_weight + 0.001f &&
            std::fabs(release_components.post_ai_stick.x) <=
            std::fabs(grace_components.post_ai_stick.x) + 0.081f,
        "fused/post-output must follow the retiring Coasting proposal");
    require(
        std::fabs(output.right_x) <=
            std::fabs(grace_components.post_ai_stick.x) + 0.081f,
        "final output must not preserve a material old-direction pull after release");
}

void test_36ms_moving_occlusion_preserves_pipeline_tracking() {
    double now = 70.0;
    auto controller_config = config();
    controller_config.ai_aim.ads_completion_fresh_frames = 1;
    controller_config.ai_aim.ads_completion_radius_px = 8.0f;
    NativeGamepadController controller(controller_config, [&now] { return now; });
    controller.set_benchmark_intent_fusion_mode(
        controller_native::BenchmarkIntentFusionMode::CausalVector);
    const auto physical = aiming();

    for (std::uint64_t frame_id = 1; frame_id <= 6; ++frame_id) {
        controller.submit_vision_snapshot(
            target(frame_id, now, 20.0f + 4.0f * static_cast<float>(frame_id - 1),
                   0.0f, 702));
        (void)controller.build_output(physical);
        now += 0.010;
    }
    const auto target_id = controller.last_target_plan().target_id;

    // The sixth observation is at 70.050. Submit a fresh empty frame at
    // 70.086 so the occlusion itself is 36ms and frame ids advance.
    now -= 0.004;
    controller.submit_vision_snapshot(empty_fresh_snapshot(7, now));
    const auto occlusion_output = controller.build_output(physical);
    const auto occlusion_plan = controller.last_target_plan();
    require(
        occlusion_plan.lifecycle == pipeline_contract::TargetLifecycle::Coasting &&
            occlusion_plan.target_id == target_id,
        "36ms moving occlusion must remain on the same Coasting owner");
    require(
        occlusion_plan.mode != pipeline_contract::ControlMode::Manual,
        "36ms moving occlusion must not false-stop the assisted mode");

    now += 0.001;
    controller.submit_vision_snapshot(target(8, now, 44.0f, 0.0f, 702));
    const auto output = controller.build_output(physical);
    const auto reacquired_plan = controller.last_target_plan();
    const auto components = controller.last_output_components();
    require(
        reacquired_plan.lifecycle == pipeline_contract::TargetLifecycle::Reacquiring &&
            reacquired_plan.target_id == target_id,
        "36ms moving occlusion must reacquire the same canonical target");
    require(
        std::fabs(reacquired_plan.error_px.x - 44.0f) < 1.0f,
        "fresh moving-target position must remain authoritative after occlusion");
    require(
        !components.intent_fusion_fallback &&
            std::fabs(components.requested_assist_stick.x) > 0.01f &&
            std::fabs(components.shaped_assist_stick.x) > 0.01f &&
            std::fabs(components.post_ai_stick.x) > 0.01f &&
            std::fabs(output.right_x) > 0.01f,
        "36ms same-target reacquire must not false-stop or unload the pipeline");
    require(
        output.right_x * occlusion_output.right_x >= -0.001f,
        "36ms moving occlusion must not create a material direction reversal");
}

void test_firing_fresh_cross_center_reverses_bodylock_request() {
    double now = 68.0;
    auto controller_config = config();
    controller_config.ai_aim.ads_completion_fresh_frames = 1;
    controller_config.ai_aim.ads_completion_radius_px = 8.0f;
    controller_config.ai_aim.body_lock_confidence_frames = 1;
    NativeGamepadController controller(controller_config, [&now] { return now; });
    controller.set_benchmark_intent_fusion_mode(
        controller_native::BenchmarkIntentFusionMode::CausalVector);
    auto physical = aiming();
    physical.right_trigger = 1.0f;

    const float observations[] = {2.0f, 11.0f, -8.0f, -16.0f, -28.0f};
    for (std::uint64_t index = 0; index < 5; ++index) {
        controller.submit_vision_snapshot(
            target(index + 1, now, observations[index], 0.0f, 602));
        (void)controller.build_output(physical);
        now += 0.010;
    }

    require(
        controller.last_target_plan().error_px.x < 0.0f,
        "fresh firing observation must cross the coordinator error center");
    require(
        controller.last_output_components().requested_assist_stick.x < 0.0f,
        "BodyLock request must follow the fresh firing position across center");
}

void test_bodylock_countersteer_has_no_escape_threshold_impulse() {
    double now = 31.0;
    auto controller_config = config();
    controller_config.ai_aim.ads_completion_fresh_frames = 1;
    controller_config.ai_aim.ads_completion_radius_px = 40.0f;
    controller_config.ai_aim.body_lock_confidence_frames = 1;
    NativeGamepadController controller(
        controller_config, [&now] { return now; });
    controller.set_benchmark_intent_fusion_mode(
        controller_native::BenchmarkIntentFusionMode::CausalVector);
    auto physical = aiming();

    for (std::uint64_t frame = 1; frame <= 4; ++frame) {
        controller.submit_vision_snapshot(
            target(frame, now, 24.0f, 0.0f));
        (void)controller.build_output(physical);
        now += 0.010;
    }
    require(controller.last_ai_aim_mode() == "body_lock",
            "precondition: countersteer fixture did not enter BodyLock");

    const float manual_sequence[] = {
        -0.30f, -0.36f, -0.42f, -0.46f, -0.42f, -0.36f, -0.30f};
    float previous_output = controller.build_output(physical).right_x;
    float maximum_threshold_jump = 0.0f;
    float previous_effective_ai = 2.0f;
    for (std::size_t index = 0;
         index < sizeof(manual_sequence) / sizeof(manual_sequence[0]);
         ++index) {
        now += 0.001;
        physical.right_x = manual_sequence[index];
        controller.submit_vision_snapshot(target(
            5 + static_cast<std::uint64_t>(index), now, 24.0f, 0.0f));
        const float output = controller.build_output(physical).right_x;
        if (index >= 3) {
            maximum_threshold_jump = std::max(
                maximum_threshold_jump,
                std::fabs(output - previous_output));
        }
        const float effective_ai = output - physical.right_x;
        if (index <= 3) {
            require(
                effective_ai <= previous_effective_ai + 0.025f,
                "AI authority reasserted while opposing manual commitment increased");
            previous_effective_ai = effective_ai;
        }
        previous_output = output;
    }
    require(maximum_threshold_jump <= 0.12f,
            "crossing the manual-escape threshold created a BodyLock output impulse");
}

void test_bodylock_capture_alignment_has_no_remaining_authority() {
    double now = 17.0;
    auto controller_config = config();
    controller_config.tracker.causal_memory_enabled = true;
    controller_config.ai_aim.ads_completion_fresh_frames = 1;
    controller_config.ai_aim.ads_completion_radius_px = 8.0f;
    controller_config.ai_aim.body_lock_confidence_frames = 1;
    NativeGamepadController controller(
        controller_config, [&now] { return now; });
    const auto physical = aiming();

    controller.submit_vision_snapshot(target(1, now, 2.0f, 0.0f));
    (void)controller.build_output(physical);
    require(
        controller.last_ai_aim_mode() == "body_lock",
        "precondition: close target did not settle into BodyLock");

    controller.report_output_delivery(true, true, now + 0.000001, 1);
    now += 0.010;
    controller.submit_vision_snapshot(target(2, now - 0.006, 2.0f, 0.0f));
    (void)controller.build_output(physical);
    require(
        !controller.last_target_plan().remaining_work_valid,
        "fresh delayed Vision may align coordinates but must not re-arm Remaining");

    now += 0.001;
    (void)controller.build_output(physical);
    require(
        !controller.last_target_plan().remaining_work_valid,
        "BodyLock Remaining authority must stay off between Vision frames");
}

void test_lt_rising_edge_preserves_same_tick_manual_rb() {
    double now = 69.0;
    auto controller_config = config();
    controller_config.auto_fire.require_aim_ready = true;
    controller_config.auto_fire.max_source_age_ms = 1000.0f;
    controller_config.ai_aim.target_max_age_ms = 1000.0f;
    controller_config.ai_aim.auto_fire_ready_frames = 2;
    NativeGamepadController controller(
        controller_config, [&now] { return now; });

    PhysicalGamepadState physical;
    physical.connected = true;
    require(!controller.build_output(physical).rb,
            "precondition: neutral non-ADS tick unexpectedly fired");

    now += 0.001;
    controller.submit_vision_snapshot(fire_target(1, now));
    physical.left_trigger = 1.0f;
    physical.rb = true;
    const auto first_ads_output = controller.build_output(physical);

    require(first_ads_output.left_trigger >= 0.999f,
            "LT rising edge was not forwarded on the first ADS tick");
    require(first_ads_output.rb,
            "manual RB was not forwarded on the same tick as the LT rising edge");
    require(controller.last_output_components().fire_button,
            "final output components lost manual fire on the first ADS tick");
    require(controller.last_output_components().auto_fire_block_reason ==
                "manual_fire",
            "first-ADS manual RB did not take the manual-fire path");

    double counterfactual_now = 70.0;
    NativeGamepadController counterfactual(
        controller_config, [&counterfactual_now] { return counterfactual_now; });
    PhysicalGamepadState counterfactual_physical;
    counterfactual_physical.connected = true;
    (void)counterfactual.build_output(counterfactual_physical);
    counterfactual_now += 0.001;
    counterfactual.submit_vision_snapshot(
        fire_target(1, counterfactual_now));
    counterfactual_physical.left_trigger = 1.0f;
    const auto protected_output =
        counterfactual.build_output(counterfactual_physical);
    require(!protected_output.rb,
            "counterfactual: first-frame AutoFire readiness protection did not engage");
}

void test_physical_fire_is_never_cleared_by_autofire() {
    double now = 70.0;
    auto controller_config = config();
    controller_config.auto_fire.require_aim_ready = false;
    controller_config.auto_fire.max_source_age_ms = 1000.0f;
    controller_config.auto_fire.pulse_width_ms = 30.0f;
    controller_config.auto_fire.pulse_period_ms = 100.0f;
    NativeGamepadController controller(controller_config, [&now] { return now; });
    auto physical = aiming();
    auto snapshot = target(1, now, 0.0f, 0.0f);
    snapshot.state.auto_fire_requested = true;
    controller.submit_vision_snapshot(snapshot);
    auto output = controller.build_output(physical);
    require(output.rb, "precondition: synthetic RB pulse did not start");

    now += 0.001;
    physical.rb = true;
    output = controller.build_output(physical);
    require(output.rb, "physical RB was cleared during synthetic takeover");

    now += 0.001;
    physical.rb = false;
    controller.build_output(physical);
    now += 0.001;
    physical.right_trigger = 1.0f;
    output = controller.build_output(physical);
    require(output.right_trigger >= 0.999f,
            "physical RT was cleared during synthetic takeover guard");

    double wait_now = 80.0;
    NativeGamepadController wait_controller(
        controller_config, [&wait_now] { return wait_now; });
    physical = aiming();
    snapshot = target(1, wait_now, 0.0f, 0.0f);
    snapshot.state.auto_fire_requested = true;
    wait_controller.submit_vision_snapshot(snapshot);
    require(wait_controller.build_output(physical).rb,
            "precondition: cadence-wait pulse did not start");
    wait_now += 0.031;
    wait_controller.build_output(physical);
    wait_now += 0.001;
    physical.rb = true;
    output = wait_controller.build_output(physical);
    require(output.rb, "physical RB was cleared during cadence wait");
}

void test_100hz_vision_1000hz_control_emits_stable_fire_cadence() {
    double now = 90.0;
    auto controller_config = config();
    controller_config.auto_fire.require_aim_ready = false;
    controller_config.auto_fire.max_source_age_ms = 50.0f;
    controller_config.auto_fire.pulse_width_ms = 30.0f;
    controller_config.auto_fire.pulse_period_ms = 100.0f;
    NativeGamepadController controller(controller_config, [&now] { return now; });
    const auto physical = aiming();

    bool previous = false;
    int current_width = 0;
    int minimum_width = 1000;
    std::vector<int> start_ticks;
    for (int tick = 0; tick < 1000; ++tick) {
        now = 90.0 + tick * 0.001;
        if (tick % 10 == 0) {
            controller.submit_vision_snapshot(
                fire_target(static_cast<std::uint64_t>(tick / 10 + 1), now));
        }
        const bool pressed = controller.build_output(physical).rb;
        if (tick % 10 != 0) {
            require(!controller.last_frame_vision_state().fresh_observation,
                    "no-publication tick was mislabeled as a fresh Vision frame");
            require(
                controller.last_frame_vision_state().current_observed_target_present,
                "no-publication tick lost current observed target continuity");
        }
        if (pressed && !previous) start_ticks.push_back(tick);
        if (pressed) ++current_width;
        if (!pressed && previous) {
            minimum_width = std::min(minimum_width, current_width);
            current_width = 0;
        }
        previous = pressed;
    }
    require(start_ticks.size() == 10,
            "100Hz/1000Hz chain must emit exactly ten pulse starts");
    require(minimum_width >= 30,
            "Vision publication gaps shortened a pulse below 30ms");
    for (std::size_t index = 1; index < start_ticks.size(); ++index) {
        require(std::abs(start_ticks[index] - start_ticks[index - 1] - 100) <= 1,
                "pulse period drifted beyond one controller tick");
    }

    double miss_now = 100.0;
    NativeGamepadController miss_controller(
        controller_config, [&miss_now] { return miss_now; });
    bool pressed_before_miss = false;
    for (int tick = 0; tick <= 515; ++tick) {
        miss_now = 100.0 + tick * 0.001;
        if (tick % 10 == 0) {
            miss_controller.submit_vision_snapshot(
                fire_target(static_cast<std::uint64_t>(tick / 10 + 1), miss_now));
        }
        if (tick == 515) {
            ControllerVisionSnapshot miss;
            miss.frame_updated = true;
            miss.selector_identity_protocol = true;
            miss.frame_id = 1000;
            miss.capture_time_seconds = miss_now;
            miss.ready_time_seconds = miss_now;
            miss.state.screen_center_x = 320.0f;
            miss.state.screen_center_y = 256.0f;
            miss_controller.submit_vision_snapshot(miss);
        }
        const bool pressed = miss_controller.build_output(physical).rb;
        if (tick == 514) pressed_before_miss = pressed;
        if (tick == 515) {
            require(pressed_before_miss, "precondition: miss did not interrupt an active pulse");
            require(!pressed, "fresh processed miss must revoke fire on the same tick");
        }
    }

    for (const auto* tier : {"cue_hold", "weak"}) {
        double tier_now = 110.0;
        NativeGamepadController tier_controller(
            controller_config, [&tier_now] { return tier_now; });
        auto snapshot = fire_target(1, tier_now);
        snapshot.state.target_tier = tier;
        snapshot.state.fire_authority = std::string(tier) != "weak";
        tier_controller.submit_vision_snapshot(snapshot);
        require(!tier_controller.build_output(physical).rb,
                "cue/weak target must remain aim-only");
    }
}

struct IronSightControlIncidentResult {
    float neutral_firing_y = 0.0f;
    float downward_manual_firing_y = 0.0f;
    float undercompensated_recoil_first_y = 0.0f;
    float undercompensated_recoil_last_y = 0.0f;
    float overcompensated_recoil_first_y = 0.0f;
    float overcompensated_recoil_last_y = 0.0f;
    bool target_owned = false;
    bool downward_manual_changed_final_target = false;
    bool undercompensated_recoil_increased = false;
    bool overcompensated_recoil_decreased = false;
};

IronSightControlIncidentResult run_iron_sight_control_incident() {
    auto recoil_config = config();
    recoil_config.aim_assist_dynamics.enabled = false;
    recoil_config.recoil.enabled = true;
    recoil_config.recoil.profile_playback_enabled = false;
    recoil_config.recoil.profile_directory.clear();
    recoil_config.recoil.recognizer_state_path.clear();
    recoil_config.recoil.feedback_amount = 0.20f;

    const auto run_manual_case = [&](float manual_y) {
        double now = 120.000;
        NativeGamepadController controller(recoil_config, [&now] { return now; });
        auto physical = aiming();
        physical.right_trigger = 1.0f;
        physical.right_y = manual_y;
        for (std::uint64_t frame = 1; frame <= 3; ++frame) {
            now = 120.000 + static_cast<double>(frame - 1) * 0.005;
            controller.submit_vision_snapshot(target(frame, now, 0.0f, 0.0f, 900));
            (void)controller.build_output(physical);
        }
        return std::make_pair(
            controller.last_output_components().final_stick.y,
            std::string(controller.last_output_components().manual_authority_mode) ==
                "single_target_authoritative");
    };

    const auto run_recoil_trend = [&](float error_step_y) {
        double now = 121.000;
        NativeGamepadController controller(recoil_config, [&now] { return now; });
        auto physical = aiming();
        physical.right_trigger = 1.0f;
        float first_recoil_y = 0.0f;
        float last_recoil_y = 0.0f;
        for (std::uint64_t frame = 1; frame <= 12; ++frame) {
            now = 121.000 + static_cast<double>(frame - 1) * 0.005;
            const float error_y = error_step_y * static_cast<float>(frame - 1);
            controller.submit_vision_snapshot(target(frame, now, 0.0f, error_y, 901));
            (void)controller.build_output(physical);
            const float recoil_y = controller.last_output_components().recoil_stick.y;
            if (frame == 2) first_recoil_y = recoil_y;
            last_recoil_y = recoil_y;
        }
        return std::make_pair(first_recoil_y, last_recoil_y);
    };

    IronSightControlIncidentResult result;
    const auto neutral = run_manual_case(0.0f);
    const auto downward = run_manual_case(-0.35f);
    const auto undercompensated = run_recoil_trend(1.5f);
    const auto overcompensated = run_recoil_trend(-1.5f);
    result.neutral_firing_y = neutral.first;
    result.downward_manual_firing_y = downward.first;
    result.undercompensated_recoil_first_y = undercompensated.first;
    result.undercompensated_recoil_last_y = undercompensated.second;
    result.overcompensated_recoil_first_y = overcompensated.first;
    result.overcompensated_recoil_last_y = overcompensated.second;
    result.target_owned = neutral.second && downward.second;
    result.downward_manual_changed_final_target =
        result.downward_manual_firing_y <= result.neutral_firing_y - 0.05f;
    result.undercompensated_recoil_increased =
        result.undercompensated_recoil_last_y <=
        result.undercompensated_recoil_first_y - 0.02f;
    result.overcompensated_recoil_decreased =
        result.overcompensated_recoil_last_y >=
        result.overcompensated_recoil_first_y + 0.02f;
    return result;
}

void write_iron_sight_incident_report(
    const IronSightControlIncidentResult& result,
    bool passed) {
    if (g_iron_sight_incident_report_path.empty()) return;
    std::ofstream output(
        g_iron_sight_incident_report_path, std::ios::out | std::ios::trunc);
    require(output.is_open(), "could not open iron-sight incident report");
    output << "{\n"
           << "  \"schema_version\": 1,\n"
           << "  \"incident_id\": \"iron-sight-recoil-manual-cancel-20260809\",\n"
           << "  \"passed\": " << (passed ? "true" : "false") << ",\n"
           << "  \"target_owned\": " << (result.target_owned ? "true" : "false") << ",\n"
           << "  \"neutral_firing_y\": " << result.neutral_firing_y << ",\n"
           << "  \"downward_manual_firing_y\": "
           << result.downward_manual_firing_y << ",\n"
           << "  \"undercompensated_recoil_first_y\": "
           << result.undercompensated_recoil_first_y << ",\n"
           << "  \"undercompensated_recoil_last_y\": "
           << result.undercompensated_recoil_last_y << ",\n"
           << "  \"overcompensated_recoil_first_y\": "
           << result.overcompensated_recoil_first_y << ",\n"
           << "  \"overcompensated_recoil_last_y\": "
           << result.overcompensated_recoil_last_y << ",\n"
           << "  \"downward_manual_changed_final_target\": "
           << (result.downward_manual_changed_final_target ? "true" : "false") << ",\n"
           << "  \"undercompensated_recoil_increased\": "
           << (result.undercompensated_recoil_increased ? "true" : "false") << ",\n"
           << "  \"overcompensated_recoil_decreased\": "
           << (result.overcompensated_recoil_decreased ? "true" : "false") << "\n"
           << "}\n";
}

void test_iron_sight_recoil_and_manual_incident_regression() {
    const auto result = run_iron_sight_control_incident();
    const bool passed = result.target_owned &&
        result.downward_manual_changed_final_target &&
        result.undercompensated_recoil_increased &&
        result.overcompensated_recoil_decreased;
    write_iron_sight_incident_report(result, passed);
    require(result.target_owned,
            "iron-sight regression did not reach single-target authority");
    require(result.downward_manual_changed_final_target,
            "firing downward manual did not change the target-first vertical output");
    require(result.undercompensated_recoil_increased,
            "positive firing residual did not increase recoil compensation");
    require(result.overcompensated_recoil_decreased,
            "negative firing residual did not reduce recoil compensation");
}

}  // namespace

int main(int argc, char** argv) {
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::string(argv[index]) == "--ads-release-incident-report") {
            g_ads_release_incident_report_path = argv[++index];
        } else if (std::string(argv[index]) ==
                   "--ads-cue-hold-incident-report") {
            g_ads_cue_hold_incident_report_path = argv[++index];
        } else if (std::string(argv[index]) ==
                   "--iron-sight-incident-report") {
            g_iron_sight_incident_report_path = argv[++index];
        } else if (std::string(argv[index]) ==
                   "--helpful-manual-overdrive-report") {
            g_helpful_manual_overdrive_report_path = argv[++index];
        }
    }
    try {
        test_ads_release_unloads_ai_within_three_control_samples();
        test_same_generation_cue_holds_existing_ads_target();
        test_same_generation_cue_updates_both_axes_without_learning_ui_velocity();
        test_ads_is_bounded_and_drift_is_ignored();
        test_production_causal_memory_consumes_only_confirmed_delivery();
        test_causal_memory_applies_pending_without_geometry_corruption();
        test_causal_memory_applies_signed_pending_to_plan();
        test_causal_memory_refreshes_pending_on_each_decision_and_epoch_boundary();
        test_causal_memory_controller_60k_output_and_performance();
        test_gate2_capture_state_is_latest_only_and_horizon_explicit();
        test_remaining_work_drops_and_rebases_after_long_control_gap();
        test_bodylock_capture_alignment_has_no_remaining_authority();
        test_vision_gap_uses_smooth_short_continuity();
        test_opposing_manual_intent_yields_without_braking_bodylock();
        test_bodylock_countersteer_has_no_escape_threshold_impulse();
        test_benchmark_mix_override_updates_delivered_feedback();
        test_benchmark_vector_fusion_is_one_reported_pipeline_stage();
        test_production_frame_local_observation_ids_do_not_switch_one_target();
        test_production_selector_replacement_has_one_change_tick_then_recovers();
        test_fuser_feedback_controls_manual_escape_without_magnitude_shortcut();
        test_vector_mode_generates_ai_before_manual_arbitration();
        test_acquisition_trace_joins_source_observation_across_controller_stages();
        test_ego_motion_shadow_does_not_change_final_output_sequence();
        test_gate25_observer_interleaving_preserves_controller_outputs();
        test_w5_source_present_is_invariant_to_copy_complete_offset();
        test_ads_strong_cooperative_mix_uses_non_additive_final_envelope();
        test_bodylock_strong_mix_uses_non_additive_final_envelope_at_both_ranges();
        test_only_worsening_wrong_way_axis_stops_suppressing_assist();
        test_helpful_manual_adds_only_bounded_target_direction_headroom();
        test_target_first_does_not_carry_manual_into_target_output();
        test_ads_and_bodylock_share_one_resolved_target_geometry();
        test_held_ads_target_change_does_not_rearm_snap();
        test_same_track_geometry_shift_keeps_final_output_bounded();
        test_held_ads_new_track_after_gap_starts_from_zero_ai();
        test_close_lateral_runner_keeps_fresh_bodylock_authority();
        test_firing_fresh_cross_center_reverses_bodylock_request();
        test_same_target_reacquire_preserves_pipeline_continuity();
        test_cover_retreat_retires_pipeline_actuation_without_identity_loss();
        test_36ms_moving_occlusion_preserves_pipeline_tracking();
        test_lt_rising_edge_preserves_same_tick_manual_rb();
        test_physical_fire_is_never_cleared_by_autofire();
        test_100hz_vision_1000hz_control_emits_stable_fire_cadence();
        test_iron_sight_recoil_and_manual_incident_regression();
        std::cout << "[TargetPipelineIntegrationTests] PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[TargetPipelineIntegrationTests][FAIL] " << error.what() << '\n';
        return 1;
    }
}
