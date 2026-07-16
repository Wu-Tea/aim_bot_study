#include "aim_assist_dynamics.h"
#include "ads_carry_brake_policy.h"
#include "ai_aim.h"
#include "bodylock_policy.h"
#include "bodylock_lifecycle.h"
#include "controller_tick_context.h"
#include "native_gamepad_controller.h"
#include "output_mixer.h"
#include "recoil_compensation.h"
#include "recoil_profile.h"
#include "target_tracker.h"
#include "weapon_recognizer.h"
#include "../common_native/authority_types.h"
#include "../common_native/screen_geometry.h"
#include "../common_native/stick_types.h"
#include "../common_native/time_types.h"
#include "../tracking_native/legacy_projection_tracker.h"
#include "../tracking_native/tracker_authority.h"
#include "../tracking_native/tracker_backend.h"
#include "../runtime_app/vision_controller_adapter.h"

#include <cmath>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

void require_true(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_near(float actual, float expected, float tolerance, const std::string& message) {
    if (std::fabs(actual - expected) > tolerance) {
        std::ostringstream out;
        out << message << " expected=" << expected << " actual=" << actual;
        throw std::runtime_error(out.str());
    }
}

controller_native::PhysicalGamepadState aiming_physical_state() {
    controller_native::PhysicalGamepadState physical;
    physical.connected = true;
    physical.left_trigger = 1.0f;
    return physical;
}

double now_seconds() {
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::uint64_t now_ns() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

void submit_adapted_vision_result(
    controller_native::NativeGamepadController& controller,
    const vision_native::VisionResult& result) {
    controller.submit_vision_snapshot(runtime_app::adapt_vision_result(result));
}

std::filesystem::path make_temp_test_dir(const std::string& label) {
    const auto stamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("cod_native_controller_" + label + "_" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    return path;
}

void write_text_file(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("failed to write test file: " + path.string());
    }
    output << text;
}

class ScopedEnvVar {
public:
    ScopedEnvVar(std::string name, std::string value)
        : name_(std::move(name)) {
        const char* existing = std::getenv(name_.c_str());
        if (existing != nullptr) {
            previous_ = existing;
        }
        _putenv_s(name_.c_str(), value.c_str());
    }

    ~ScopedEnvVar() {
        _putenv_s(name_.c_str(), previous_.has_value() ? previous_->c_str() : "");
    }

private:
    std::string name_;
    std::optional<std::string> previous_;
};

std::string recoil_profile_json(
    const std::string& profile_id,
    const std::string& weapon_id,
    const std::string& aim_mode,
    float confidence,
    float y_sample) {
    std::ostringstream out;
    out
        << "{\n"
        << "  \"profile_id\": \"" << profile_id << "\",\n"
        << "  \"canonical_weapon_id\": \"" << weapon_id << "\",\n"
        << "  \"game\": \"cod22\",\n"
        << "  \"stance\": \"standing\",\n"
        << "  \"aim_mode\": \"" << aim_mode << "\",\n"
        << "  \"confidence\": " << confidence << ",\n"
        << "  \"sample_interval_ms\": 10,\n"
        << "  \"duration_ms\": 20,\n"
        << "  \"initial_delay_ms\": 0,\n"
        << "  \"samples_x\": [0.0, 0.0, 0.0],\n"
        << "  \"samples_y\": [0.0, " << y_sample << ", " << (y_sample * 2.0f) << "]\n"
        << "}\n";
    return out.str();
}

void test_common_native_types_compile() {
    common_native::TimeSeconds now{10.0};
    common_native::TimeSeconds then{9.5};
    const auto dt = now - then;
    require_near(
        static_cast<float>(common_native::duration_ms(dt)),
        500.0f,
        0.01f,
        "time wrapper should compute milliseconds");

    common_native::Vec2f point{12.0f, -4.0f};
    common_native::Box2f box{10.0f, 20.0f, 30.0f, 40.0f};
    common_native::ScreenSize screen{384.0f, 352.0f};
    require_near(point.x + box.w + screen.height, 394.0f, 0.001f, "screen geometry types should store values");

    common_native::StickComponents components;
    components.manual = {0.10f, 0.20f};
    components.assist = {0.30f, 0.40f};
    components.recoil = {-0.05f, -0.15f};
    components.final_output = {0.35f, 0.45f};
    require_near(components.final_output.x, 0.35f, 0.0001f, "stick components should store final x");

    const auto assist = common_native::AssistAuthority::AimObserved;
    const auto fire = common_native::FireAuthority::ObservedOnly;
    const auto target_authority = common_native::TargetAuthorityState::StrongAssist;
    require_true(
        assist == common_native::AssistAuthority::AimObserved,
        "assist authority enum should compare");
    require_true(
        fire == common_native::FireAuthority::ObservedOnly,
        "fire authority enum should compare");
    require_true(
        target_authority == common_native::TargetAuthorityState::StrongAssist,
        "target authority state enum should compare");
}

controller_native::BodyLockMotionObservation body_lock_motion_box(
    float center_x,
    double now_seconds_value,
    bool strong_observation = true) {
    controller_native::BodyLockMotionObservation observation;
    observation.strong_observation = strong_observation;
    observation.has_body_box = true;
    observation.body_x1 = center_x - 10.0f;
    observation.body_x2 = center_x + 10.0f;
    observation.body_y1 = 90.0f;
    observation.body_y2 = 130.0f;
    observation.now_seconds = now_seconds_value;
    return observation;
}

void test_bodylock_motion_policy_leads_after_consistent_direction() {
    controller_native::GamepadAiAimConfig config;
    config.body_lock_upper_body_ratio = 0.50f;
    config.body_lock_lead_frames = 3;
    config.body_lock_lead_seconds = 0.05f;
    config.body_lock_lead_max_px = 8.0f;
    controller_native::BodyLockMotionPolicy policy(config);

    policy.observe(body_lock_motion_box(100.0f, 10.0));
    policy.observe(body_lock_motion_box(110.0f, 10.1));
    policy.observe(body_lock_motion_box(120.0f, 10.2));

    require_true(policy.has_sustained_motion(), "consistent body motion should become sustained");
    const common_native::Vec2f lead = policy.lead_delta();
    require_near(lead.x, 5.0f, 0.001f, "bodylock sustained motion should lead horizontally");
    require_near(lead.y, 0.0f, 0.001f, "bodylock sustained motion should not invent vertical lead");
}

void test_bodylock_motion_policy_clears_lead_on_direction_reversal() {
    controller_native::GamepadAiAimConfig config;
    config.body_lock_upper_body_ratio = 0.50f;
    config.body_lock_lead_frames = 3;
    config.body_lock_lead_seconds = 0.05f;
    config.body_lock_lead_max_px = 8.0f;
    controller_native::BodyLockMotionPolicy policy(config);

    policy.observe(body_lock_motion_box(100.0f, 20.0));
    policy.observe(body_lock_motion_box(110.0f, 20.1));
    policy.observe(body_lock_motion_box(120.0f, 20.2));
    require_true(policy.has_sustained_motion(), "test setup should build sustained motion");

    policy.observe(body_lock_motion_box(110.0f, 20.3));
    require_true(!policy.has_sustained_motion(), "direction reversal should clear sustained lead");
    const common_native::Vec2f lead = policy.lead_delta();
    require_near(lead.x, 0.0f, 0.001f, "direction reversal should clear horizontal lead");
}

void test_bodylock_motion_policy_ignores_weak_observations() {
    controller_native::GamepadAiAimConfig config;
    config.body_lock_lead_frames = 3;
    config.body_lock_lead_seconds = 0.05f;
    config.body_lock_lead_max_px = 8.0f;
    controller_native::BodyLockMotionPolicy policy(config);

    policy.observe(body_lock_motion_box(100.0f, 30.0));
    policy.observe(body_lock_motion_box(110.0f, 30.1));
    policy.observe(body_lock_motion_box(120.0f, 30.2, false));

    require_true(!policy.has_sustained_motion(), "weak observation should not sustain motion lead");
    const common_native::Vec2f lead = policy.lead_delta();
    require_near(lead.x, 0.0f, 0.001f, "weak observation should clear lead");
}

std::string recoil_profile_xy_json(
    const std::string& profile_id,
    const std::string& weapon_id,
    const std::string& aim_mode,
    float confidence,
    float x_sample,
    float y_sample) {
    std::ostringstream out;
    out
        << "{\n"
        << "  \"profile_id\": \"" << profile_id << "\",\n"
        << "  \"canonical_weapon_id\": \"" << weapon_id << "\",\n"
        << "  \"game\": \"cod22\",\n"
        << "  \"stance\": \"standing\",\n"
        << "  \"aim_mode\": \"" << aim_mode << "\",\n"
        << "  \"confidence\": " << confidence << ",\n"
        << "  \"sample_interval_ms\": 10,\n"
        << "  \"duration_ms\": 20,\n"
        << "  \"initial_delay_ms\": 0,\n"
        << "  \"samples_x\": [0.0, " << x_sample << ", " << (x_sample * 2.0f) << "],\n"
        << "  \"samples_y\": [0.0, " << y_sample << ", " << (y_sample * 2.0f) << "]\n"
        << "}\n";
    return out.str();
}

std::string recognizer_state_json(
    const std::string& weapon_id,
    const std::string& profile_id_hint) {
    std::ostringstream out;
    out
        << "{\n"
        << "  \"type\": \"current_weapon\",\n"
        << "  \"game\": \"cod22\",\n"
        << "  \"canonical_weapon_id\": \"" << weapon_id << "\",\n"
        << "  \"confidence\": 0.91,\n"
        << "  \"source\": \"test\",\n"
        << "  \"timestamp\": \"2026-06-06T12:00:00Z\",\n"
        << "  \"degraded\": false,\n"
        << "  \"matched_name\": null,\n"
        << "  \"profile_ids\": [\"" << profile_id_hint << "\"]\n"
        << "}\n";
    return out.str();
}

void test_tracker_authority_classifies_target_tiers() {
    const tracking_native::TargetAuthorityDecision strong =
        tracking_native::classify_target_authority(true, true, true, "strong");
    require_true(
        strong.tier_class == tracking_native::TargetTierClass::StrongObserved,
        "strong target should classify as strong observed");
    require_true(strong.is_strong_aim_target, "strong target should be strong aim target");
    require_true(
        strong.assist_authority == common_native::AssistAuthority::AimObserved,
        "strong target should get observed assist authority");
    require_true(
        strong.target_authority_state == common_native::TargetAuthorityState::StrongAssist,
        "strong target should map to strong assist state");
    require_true(
        strong.fire_authority == common_native::FireAuthority::ObservedOnly,
        "strong target with fire flag should get observed fire authority");

    const tracking_native::TargetAuthorityDecision weak =
        tracking_native::classify_target_authority(true, true, true, "weak_observed");
    require_true(
        weak.tier_class == tracking_native::TargetTierClass::WeakContinuity,
        "weak_observed should classify as weak continuity");
    require_true(!weak.is_strong_aim_target, "weak target should not be strong aim target");
    require_true(
        weak.assist_authority == common_native::AssistAuthority::AimCoast,
        "weak target should get coast assist authority");
    require_true(
        weak.target_authority_state == common_native::TargetAuthorityState::WeakAssist,
        "weak target should map to weak assist state");
    require_true(
        weak.fire_authority == common_native::FireAuthority::None,
        "weak target should not get fire authority");

    const tracking_native::TargetAuthorityDecision projected =
        tracking_native::classify_target_authority(true, true, true, "projected");
    require_true(
        projected.tier_class == tracking_native::TargetTierClass::Projected,
        "projected target should classify as projected");
    require_true(
        projected.target_authority_state == common_native::TargetAuthorityState::TrackOnly,
        "projected target should map to track-only state");
    require_true(
        projected.fire_authority == common_native::FireAuthority::None,
        "projected target should not get fire authority");

    const tracking_native::TargetAuthorityDecision observed_without_aim =
        tracking_native::classify_target_authority(true, false, true, "strong");
    require_true(
        observed_without_aim.target_authority_state == common_native::TargetAuthorityState::TrackOnly,
        "observed target without aim authority should map to track-only state");
    require_true(
        observed_without_aim.assist_authority == common_native::AssistAuthority::None,
        "track-only target should not grant assist authority");

    const tracking_native::TargetAuthorityDecision unknown =
        tracking_native::classify_target_authority(true, true, true, "new_detector_tier");
    require_true(
        unknown.tier_class == tracking_native::TargetTierClass::StrongObserved,
        "unknown non-empty target tier should preserve legacy strong behavior");
    require_true(
        unknown.target_authority_state == common_native::TargetAuthorityState::StrongAssist,
        "unknown non-empty target tier should preserve legacy strong assist behavior");

    const tracking_native::TargetAuthorityDecision lost =
        tracking_native::classify_target_authority(false, true, true, "none");
    require_true(
        lost.target_authority_state == common_native::TargetAuthorityState::Reject,
        "lost target should map to reject state");
}

void test_adapter_downgrades_color_checked_wide_low_detection_without_enemy_evidence() {
    vision_native::VisionResult result;
    result.frame_updated = true;
    result.frame_id = 42;
    result.result_at_ns = now_ns();

    vision_native::Detection detection;
    detection.x1 = 260.0f;
    detection.y1 = 240.0f;
    detection.x2 = 380.0f;
    detection.y2 = 300.0f;
    detection.conf = 0.92f;
    detection.color_classified = true;
    detection.has_cue_point = false;
    detection.color_bonus = 0.0f;
    result.detections.push_back(detection);

    const controller_native::ControllerVisionSnapshot snapshot =
        runtime_app::adapt_vision_result(result);

    require_true(snapshot.tracker_detections.size() == 1u, "adapter should keep valid detection");
    require_true(
        snapshot.tracker_detections.front().target_tier == "associated_weak",
        "adapter should downgrade color-checked wide-low no-cue detections for tracker");
}

void test_auto_fire_requires_fire_authority() {
    controller_native::GamepadRuntimeConfig config;
    config.auto_fire_output = "RB";
    config.auto_fire.require_aim_ready = false;
    controller_native::NativeGamepadController controller(config);

    controller_native::NativeControllerVisionState blocked;
    blocked.has_target = true;
    blocked.auto_fire_requested = true;
    blocked.aim_authority = true;
    blocked.fire_authority = false;
    blocked.target_tier = "cue_hold";
    blocked.observed_at_seconds = now_seconds();
    controller.submit_vision_state(blocked);

    controller_native::GamepadOutputState output = controller.build_output(aiming_physical_state());
    require_true(!output.rb, "auto-fire must stay blocked without fire_authority");
    controller_native::NativeAutoFireCounters counters = controller.auto_fire_counters();
    require_true(counters.requested == 1, "blocked auto-fire should count one request");
    require_true(counters.blocked == 1, "blocked auto-fire should count one block");

    blocked.fire_authority = true;
    blocked.target_tier = "cue_hold";
    blocked.observed_at_seconds = now_seconds();
    controller.submit_vision_state(blocked);
    output = controller.build_output(aiming_physical_state());
    require_true(!output.rb, "auto-fire must stay blocked for cue-only targets");

    blocked.target_tier = "projected";
    blocked.observed_at_seconds = now_seconds();
    controller.submit_vision_state(blocked);
    output = controller.build_output(aiming_physical_state());
    require_true(!output.rb, "auto-fire must stay blocked for projected targets");

    blocked.fire_authority = true;
    blocked.target_tier = "strong";
    blocked.observed_at_seconds = now_seconds();
    controller.submit_vision_state(blocked);
    output = controller.build_output(aiming_physical_state());
    require_true(output.rb, "auto-fire should press RB when target has fire_authority");
}

void test_auto_fire_blocks_stale_source() {
    controller_native::GamepadRuntimeConfig config;
    config.auto_fire_output = "RB";
    controller_native::NativeGamepadController controller(config);

    controller_native::NativeControllerVisionState stale;
    stale.has_target = true;
    stale.auto_fire_requested = true;
    stale.aim_authority = true;
    stale.fire_authority = true;
    stale.target_tier = "strong";
    stale.observed_at_seconds = now_seconds() - 1.0;
    controller.submit_vision_state(stale);

    controller_native::GamepadOutputState output = controller.build_output(aiming_physical_state());
    require_true(!output.rb, "auto-fire must stay blocked when source is stale");
}

void test_controller_passes_extended_buttons_and_dpad_through() {
    controller_native::NativeGamepadController controller;
    controller_native::PhysicalGamepadState physical = aiming_physical_state();
    physical.dpad_up = true;
    physical.dpad_left = true;
    physical.back = true;
    physical.start = true;
    physical.left_thumb = true;
    physical.right_thumb = true;

    const controller_native::GamepadOutputState output = controller.build_output(physical);
    require_true(output.dpad_up, "dpad up should pass through");
    require_true(output.dpad_left, "dpad left should pass through");
    require_true(output.back, "back should pass through");
    require_true(output.start, "start should pass through");
    require_true(output.left_thumb, "left thumb should pass through");
    require_true(output.right_thumb, "right thumb should pass through");
}

void test_controller_records_pipeline_stage_traces() {
    controller_native::GamepadRuntimeConfig config;
    config.auto_fire.require_aim_ready = false;
    config.recoil.enabled = false;
    controller_native::NativeGamepadController controller(config);

    controller_native::NativeControllerVisionState state;
    state.has_target = true;
    state.auto_fire_requested = true;
    state.aim_authority = true;
    state.fire_authority = true;
    state.target_tier = "strong";
    state.dx = 4.0f;
    state.dy = 4.0f;
    state.observed_at_seconds = now_seconds();
    controller.submit_vision_state(state);

    controller_native::GamepadOutputState output =
        controller.build_output(aiming_physical_state());
    const std::vector<controller_native::NativeControllerStageTrace>& traces =
        controller.last_pipeline_traces();

    require_true(traces.size() == 3, "controller should trace the three slim runtime stages");
    require_true(
        traces[0].stage_name == "target_plan_aim",
        "first trace should be the unified target-plan aim stage");
    require_true(traces[1].stage_name == "auto_fire", "second trace should be auto_fire");
    require_true(traces[2].stage_name == "recoil", "third trace should be recoil");
    require_true(
        std::fabs(traces[0].delta_right_y) > 0.0001f,
        "ai_aim trace should record its right-stick Y delta");
    require_true(
        !traces[1].before_auto_fire_active && traces[1].after_auto_fire_active,
        "auto_fire trace should expose activation state transition");
    require_near(
        traces.back().after_right_y,
        output.right_y,
        0.0001f,
        "last trace should end at final output right_y");
}

void test_controller_pipeline_records_recoil_as_final_independent_component() {
    controller_native::GamepadRuntimeConfig config;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.enabled = true;
    config.recoil.feedback_amount = 0.22f;
    config.recoil.profile_directory.clear();
    config.recoil.recognizer_state_path.clear();
    controller_native::NativeGamepadController controller(config);

    controller_native::PhysicalGamepadState physical = aiming_physical_state();
    physical.right_trigger = 1.0f;
    const controller_native::GamepadOutputState output = controller.build_output(physical);

    const std::vector<controller_native::NativeControllerStageTrace>& traces =
        controller.last_pipeline_traces();
    const controller_native::NativeControllerOutputComponents& components =
        controller.last_output_components();
    require_true(traces.size() == 3, "controller should trace all slim runtime stages with recoil enabled");
    require_true(traces[2].stage_name == "recoil", "recoil should remain the final output stage");
    require_true(
        components.recoil_stick.y < -0.20f,
        "recoil component should capture fallback down-pull separately");
    require_near(
        traces[2].delta_right_y,
        components.recoil_stick.y,
        0.0001f,
        "recoil stage trace should match the captured recoil component");
    require_near(
        components.final_stick.y,
        output.right_y,
        0.0001f,
        "final component should match controller output after recoil");
    require_near(
        controller.last_tracker_motion_output().right_y,
        components.final_stick.y,
        0.0001f,
        "tracker should receive final camera motion while recoil remains separately attributed");
}

void test_controller_tick_context_carries_tracker_snapshot_and_output_components() {
    controller_native::NativeControllerTickContext context;
    context.physical = aiming_physical_state();
    context.vision.has_target = true;
    context.vision.aim_authority = true;
    context.vision.dx = 12.0f;
    context.tracker_snapshot.has_target = true;
    context.tracker_snapshot.aim_error_px = {10.0f, -2.0f};
    context.now = {42.0};
    context.dt = {0.008};
    context.aiming = true;
    context.manual_fire_pressed = true;
    context.auto_fire_active = false;
    context.output_components.manual_stick = {0.20f, -0.10f};
    context.output_components.final_stick = {0.30f, -0.15f};

    require_true(context.aiming, "tick context should carry ADS state");
    require_true(
        context.tracker_snapshot.has_target,
        "tick context should carry current tracker snapshot");
    require_near(
        context.output_components.final_stick.x,
        0.30f,
        0.001f,
        "tick context should carry output components for later tracker samples");
}

void test_auto_fire_manual_takeover_releases_output_briefly() {
    controller_native::GamepadRuntimeConfig config;
    config.auto_fire_output = "RB";
    config.auto_fire.require_aim_ready = false;
    controller_native::NativeGamepadController controller(config);

    controller_native::NativeControllerVisionState state;
    state.has_target = true;
    state.auto_fire_requested = true;
    state.aim_authority = true;
    state.fire_authority = true;
    state.target_tier = "strong";
    state.observed_at_seconds = now_seconds();
    controller.submit_vision_state(state);

    controller_native::PhysicalGamepadState physical = aiming_physical_state();
    controller_native::GamepadOutputState output = controller.build_output(physical);
    require_true(output.rb, "auto-fire should start before manual takeover");

    physical.rb = true;
    output = controller.build_output(physical);
    require_true(!output.rb, "manual takeover should briefly release fire output");
}

void test_auto_fire_requires_aim_ready_settle_frames() {
    controller_native::GamepadRuntimeConfig config;
    config.auto_fire_output = "RB";
    config.auto_fire.require_aim_ready = true;
    config.auto_fire.max_source_age_ms = 0.0f;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.max_ai_force = 1.0f;
    config.ai_aim.max_ai_force_y = 1.0f;
    config.ai_aim.target_max_age_ms = 0.0f;
    config.ai_aim.auto_fire_ready_error_px = 10.0f;
    config.ai_aim.auto_fire_ready_frames = 2;
    config.ai_aim.auto_fire_ready_min_ads_ms = 0.0f;
    config.ai_aim.auto_fire_ready_max_ai_stick = 6000.0f;
    config.recoil.enabled = false;
    controller_native::NativeGamepadController controller(config);

    vision_native::VisionResult target;
    target.frame_updated = true;
    target.has_target = true;
    target.auto_fire = true;
    target.aim_authority = true;
    target.fire_authority = true;
    target.dx = 5.0f;
    target.dy = 0.0f;
    target.target_tier = "strong";
    target.result_at_ns = now_ns();
    submit_adapted_vision_result(controller, target);

    controller_native::GamepadOutputState first = controller.build_output(aiming_physical_state());
    require_true(!first.rb, "auto-fire should wait for first settled frame");

    target.result_at_ns = now_ns();
    submit_adapted_vision_result(controller, target);
    controller_native::GamepadOutputState second = controller.build_output(aiming_physical_state());
    require_true(second.rb, "auto-fire should start after required settled frames");
}

void test_auto_fire_uses_80hz_vision_frames_at_1000hz_controller_rate() {
    double now = 90.000;
    controller_native::GamepadRuntimeConfig config;
    config.auto_fire_output = "RB";
    config.auto_fire.fire_output = "RB";
    config.auto_fire.require_aim_ready = true;
    config.auto_fire.max_source_age_ms = 50.0f;
    config.ai_aim.target_max_age_ms = 96.0f;
    config.ai_aim.target_projection_max_age_ms = 96.0f;
    config.ai_aim.auto_fire_ready_error_px = 4.0f;
    config.ai_aim.auto_fire_ready_frames = 2;
    config.ai_aim.auto_fire_ready_min_ads_ms = 0.0f;
    config.ai_aim.auto_fire_ready_max_ai_stick = 6000.0f;
    config.recoil.enabled = false;
    controller_native::NativeGamepadController controller(config, [&now]() { return now; });

    const auto observed = [](std::uint64_t frame, std::uint64_t observation, double at) {
        controller_native::ControllerVisionSnapshot snapshot;
        snapshot.frame_updated = true;
        snapshot.selector_identity_protocol = true;
        snapshot.frame_id = frame;
        snapshot.capture_time_seconds = at;
        snapshot.ready_time_seconds = at;
        snapshot.selected_observation_id = observation;
        snapshot.state.has_target = true;
        snapshot.state.aim_authority = true;
        snapshot.state.fire_authority = true;
        snapshot.state.auto_fire_requested = true;
        snapshot.state.screen_center_x = 320.0f;
        snapshot.state.screen_center_y = 256.0f;
        snapshot.state.target_x = 320.0f;
        snapshot.state.target_y = 256.0f;
        snapshot.state.dx = 0.0f;
        snapshot.state.dy = 0.0f;
        snapshot.state.target_tier = "observed_strong";
        snapshot.state.observed_at_seconds = at;
        tracking_native::TrackerDetection detection;
        detection.id = observation;
        detection.body_box_px = {285.0f, 196.0f, 70.0f, 140.0f};
        detection.aim_point_px = {320.0f, 256.0f};
        detection.has_aim_point = true;
        detection.confidence = 0.95f;
        detection.target_tier = "observed_strong";
        snapshot.tracker_detections.push_back(detection);
        return snapshot;
    };

    controller.build_output(aiming_physical_state());
    controller.submit_vision_snapshot(observed(900, 9001, now));
    for (int tick = 0; tick < 12; ++tick) {
        const auto output = controller.build_output(aiming_physical_state());
        require_true(!output.rb, "tentative first vision frame must not fire");
        now += 0.001;
    }

    controller.submit_vision_snapshot(observed(901, 9011, now));
    for (int tick = 0; tick < 12; ++tick) {
        const auto output = controller.build_output(aiming_physical_state());
        require_true(
            !output.rb,
            "repeated 1000Hz ticks from one fire-authoritative frame must not satisfy readiness");
        now += 0.001;
    }

    controller.submit_vision_snapshot(observed(902, 9021, now));
    const auto fired = controller.build_output(aiming_physical_state());
    require_true(fired.rb, "second unique fire-authoritative vision frame should press RB");
    require_true(controller.auto_fire_counters().allowed >= 1,
                 "live-rate integration must record an allowed auto-fire decision");
    require_true(controller.last_output_components().auto_fire_requested,
                 "controller components should expose the auto-fire request");
    require_true(controller.last_output_components().auto_fire_aim_ready,
                 "controller components should expose settled readiness");
    require_true(controller.last_output_components().auto_fire_allowed,
                 "controller components should expose pre-takeover permission");
    require_true(controller.last_output_components().auto_fire_active,
                 "controller components should expose active synthetic fire");
    require_true(controller.last_output_components().fire_button,
                 "controller components should expose the final fire button");
    require_true(controller.last_output_components().auto_fire_block_reason == "none",
                 "allowed auto-fire should expose no block reason");
    std::cerr << "[TrackerAutofireBenchmark] autofire vision_hz=83 controller_hz=1000"
              << " first_active_sequence=902 allowed="
              << controller.auto_fire_counters().allowed << '\n';
}

void test_scope_occlusion_owner_hold_releases_and_reacquires_smoothly() {
    double now = 91.000;
    controller_native::GamepadRuntimeConfig config;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.max_ai_force = 1.0f;
    config.ai_aim.max_ai_force_y = 1.0f;
    config.ai_aim.ads_snap_max_ai_force = 1.0f;
    config.ai_aim.ads_snap_max_ai_force_y = 1.0f;
    config.ai_aim.ads_snap_window_ms = 100;
    config.ai_aim.deadzone_inner = 0.0f;
    config.ai_aim.deadzone_outer = 0.0f;
    config.ai_aim.x_deadzone_outer = 0.0f;
    config.ai_aim.piecewise_mid_pixels = 0.0f;
    config.ai_aim.piecewise_mid_pixels_y = 0.0f;
    config.ai_aim.target_max_age_ms = 96.0f;
    config.ai_aim.target_projection_max_age_ms = 96.0f;
    config.aim_assist_dynamics.enabled = true;
    config.recoil.enabled = false;
    controller_native::NativeGamepadController controller(config, [&now]() { return now; });

    const auto observed = [](std::uint64_t frame,
                             std::uint64_t observation,
                             double at,
                             float target_x) {
        controller_native::ControllerVisionSnapshot snapshot;
        snapshot.frame_updated = true;
        snapshot.selector_identity_protocol = true;
        snapshot.frame_id = frame;
        snapshot.capture_time_seconds = at;
        snapshot.ready_time_seconds = at;
        snapshot.selected_observation_id = observation;
        snapshot.state.has_target = true;
        snapshot.state.aim_authority = true;
        snapshot.state.fire_authority = true;
        snapshot.state.screen_center_x = 320.0f;
        snapshot.state.screen_center_y = 256.0f;
        snapshot.state.target_x = target_x;
        snapshot.state.target_y = 256.0f;
        snapshot.state.dx = target_x - 320.0f;
        snapshot.state.dy = 0.0f;
        snapshot.state.target_tier = "observed_strong";
        snapshot.state.observed_at_seconds = at;
        tracking_native::TrackerDetection detection;
        detection.id = observation;
        detection.body_box_px = {target_x - 35.0f, 196.0f, 70.0f, 140.0f};
        detection.aim_point_px = {target_x, 256.0f};
        detection.has_aim_point = true;
        detection.confidence = 0.95f;
        detection.target_tier = "observed_strong";
        snapshot.tracker_detections.push_back(detection);
        return snapshot;
    };

    controller.build_output(aiming_physical_state());
    controller.submit_vision_snapshot(observed(910, 9101, now, 400.0f));
    for (int tick = 0; tick < 12; ++tick) {
        controller.build_output(aiming_physical_state());
        now += 0.001;
    }
    controller.submit_vision_snapshot(observed(911, 9111, now, 400.0f));
    float previous = controller.build_output(aiming_physical_state()).right_x;
    require_true(previous > 0.08f, "test setup should establish rightward ADS assist");

    for (int tick = 0; tick < 12; ++tick) {
        now += 0.001;
        previous = controller.build_output(aiming_physical_state()).right_x;
    }
    const float release_start = previous;
    controller.submit_vision_snapshot(observed(912, 9121, now, 200.0f));
    float held = controller.build_output(aiming_physical_state()).right_x;
    float max_tick_delta = std::fabs(held - previous);
    int reverse_ticks = held < -0.001f ? 1 : 0;
    require_true(held >= -0.001f,
                 "opposite-side competitor must never reverse owner assist during occlusion");
    require_true(std::fabs(held - previous) <= 0.055f,
                 "owner hold should release ADS assist through the existing bounded envelope");
    previous = held;

    for (int tick = 0; tick < 12; ++tick) {
        now += 0.001;
        const float current = controller.build_output(aiming_physical_state()).right_x;
        require_true(current >= -0.001f,
                     "identity-only hold must not emit opposite-side assist");
        require_true(std::fabs(current - previous) <= 0.055f,
                     "identity-only release must remain bounded per controller tick");
        max_tick_delta = std::max(max_tick_delta, std::fabs(current - previous));
        if (current < -0.001f) {
            ++reverse_ticks;
        }
        previous = current;
    }

    controller.submit_vision_snapshot(observed(913, 9131, now, 400.0f));
    const float reacquired = controller.build_output(aiming_physical_state()).right_x;
    require_true(reacquired >= -0.001f,
                 "owner reacquisition must preserve the established direction");
    require_true(std::fabs(reacquired - previous) <= 0.055f,
                 "owner reacquisition should use the same bounded envelope");
    max_tick_delta = std::max(max_tick_delta, std::fabs(reacquired - previous));
    if (reacquired < -0.001f) {
        ++reverse_ticks;
    }
    std::cerr << "[TrackerAutofireBenchmark] owner_hold release_start="
              << release_start << " gap_first=" << held
              << " max_tick_delta=" << max_tick_delta
              << " reverse_ticks=" << reverse_ticks
              << " track=" << controller.last_frame_vision_state().selected_track_id
              << '\n';
}

void test_auto_fire_aim_ready_gate_can_be_disabled() {
    controller_native::GamepadRuntimeConfig config;
    config.auto_fire_output = "RB";
    config.auto_fire.require_aim_ready = false;
    config.auto_fire.max_source_age_ms = 0.0f;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.target_max_age_ms = 0.0f;
    config.ai_aim.auto_fire_ready_error_px = 1.0f;
    config.ai_aim.auto_fire_ready_frames = 3;
    config.recoil.enabled = false;
    controller_native::NativeGamepadController controller(config);

    vision_native::VisionResult target;
    target.frame_updated = true;
    target.has_target = true;
    target.auto_fire = true;
    target.aim_authority = true;
    target.fire_authority = true;
    target.dx = 50.0f;
    target.dy = 0.0f;
    target.target_tier = "strong";
    target.result_at_ns = now_ns();
    submit_adapted_vision_result(controller, target);

    controller_native::GamepadOutputState output = controller.build_output(aiming_physical_state());
    require_true(output.rb, "disabled aim-ready gate should keep legacy first-frame fire");
}

void test_auto_fire_ready_allows_manual_right_stick_when_fire_zone_is_hit() {
    controller_native::GamepadRuntimeConfig config;
    config.auto_fire_output = "RB";
    config.auto_fire.require_aim_ready = true;
    config.auto_fire.max_source_age_ms = 0.0f;
    config.ai_aim.target_max_age_ms = 0.0f;
    config.ai_aim.auto_fire_ready_error_px = 4.0f;
    config.ai_aim.auto_fire_ready_frames = 1;
    config.ai_aim.auto_fire_ready_min_ads_ms = 0.0f;
    config.ai_aim.auto_fire_ready_max_ai_stick = 6000.0f;
    config.ai_aim.body_lock_box_tolerance_px = 20.0f;
    config.ai_aim.body_lock_activation_box_px = 160.0f;
    config.ai_aim.body_lock_upper_body_ratio = 0.40f;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.enabled = false;
    controller_native::NativeGamepadController controller(config);

    vision_native::VisionResult target;
    target.frame_updated = true;
    target.has_target = true;
    target.auto_fire = true;
    target.aim_authority = true;
    target.fire_authority = true;
    target.dx = 0.0f;
    target.dy = 0.0f;
    target.screen_center_x = 320.0f;
    target.screen_center_y = 256.0f;
    target.has_body_box = true;
    target.body_x1 = 290.0f;
    target.body_y1 = 200.0f;
    target.body_x2 = 350.0f;
    target.body_y2 = 340.0f;
    target.target_tier = "strong";
    target.result_at_ns = now_ns();
    submit_adapted_vision_result(controller, target);

    controller_native::PhysicalGamepadState physical = aiming_physical_state();
    physical.right_x = 0.35f;
    const controller_native::GamepadOutputState output = controller.build_output(physical);
    require_true(
        output.rb,
        "auto-fire should not treat manual right-stick tracking as unsettled AI pull when fire-zone is hit");
}

void test_no_update_vision_result_preserves_latest_target() {
    controller_native::GamepadRuntimeConfig config;
    config.tracker_backend = tracking_native::TrackerBackendKind::LegacyProjection;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.max_ai_force = 1.0f;
    config.ai_aim.max_ai_force_y = 1.0f;
    config.ai_aim.target_max_age_ms = 0.0f;
    controller_native::NativeGamepadController controller(config);

    vision_native::VisionResult target;
    target.frame_updated = true;
    target.has_target = true;
    target.aim_authority = true;
    target.fire_authority = true;
    target.dx = 50.0f;
    target.dy = 0.0f;
    target.target_tier = "strong";
    target.result_at_ns = now_ns();
    submit_adapted_vision_result(controller, target);

    controller_native::GamepadOutputState output = controller.build_output(aiming_physical_state());
    require_true(output.right_x > 0.40f, "fresh target should produce right-stick assist");

    vision_native::VisionResult no_update;
    no_update.frame_updated = false;
    no_update.has_target = false;
    no_update.result_at_ns = now_ns();
    submit_adapted_vision_result(controller, no_update);

    output = controller.build_output(aiming_physical_state());
    require_true(output.right_x > 0.40f, "no-update poll must not clear latest target");

    vision_native::VisionResult valid_no_target;
    valid_no_target.frame_updated = true;
    valid_no_target.has_target = false;
    valid_no_target.result_at_ns = now_ns();
    submit_adapted_vision_result(controller, valid_no_target);

    output = controller.build_output(aiming_physical_state());
    require_near(output.right_x, 0.0f, 0.001f, "processed no-target frame should clear assist");
}

void test_controller_ads_resume_waits_for_fresh_vision() {
    controller_native::GamepadRuntimeConfig config;
    config.ai_aim.target_max_age_ms = 500.0f;
    config.ai_aim.ads_snap_window_ms = 120;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.deadzone_inner = 0.0f;
    config.ai_aim.deadzone_outer = 0.0f;
    config.ai_aim.x_deadzone_outer = 0.0f;
    config.ai_aim.ads_snap_max_ai_force = 1.0f;
    config.ai_aim.ads_snap_max_ai_force_y = 1.0f;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.enabled = false;

    double now = 80.0;
    controller_native::NativeGamepadController controller(
        config,
        [&now]() { return now; });

    controller_native::NativeControllerVisionState target;
    target.has_target = true;
    target.aim_authority = true;
    target.fire_authority = true;
    target.target_tier = "strong";
    target.dx = 50.0f;
    target.dy = 0.0f;
    target.observed_at_seconds = now;
    controller.submit_vision_state(target);

    controller_native::GamepadOutputState output =
        controller.build_output(aiming_physical_state());
    require_true(output.right_x > 0.10f, "fresh ADS target should assist before ADS is released");

    now = 80.020;
    controller_native::PhysicalGamepadState released;
    released.connected = true;
    output = controller.build_output(released);
    require_near(output.right_x, 0.0f, 0.001f, "released ADS should pass idle right stick");

    now = 80.040;
    output = controller.build_output(aiming_physical_state());
    require_near(
        output.right_x,
        0.0f,
        0.001f,
        "ADS resume should wait for fresh vision instead of pulling the previous target on the first frame");

    target.observed_at_seconds = now;
    controller.submit_vision_state(target);
    output = controller.build_output(aiming_physical_state());
    require_true(output.right_x > 0.10f, "fresh vision after ADS resume should restore assist");
}

void test_controller_accepts_controller_vision_snapshot_without_vision_result() {
    controller_native::GamepadRuntimeConfig config;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.max_ai_force = 1.0f;
    config.ai_aim.max_ai_force_y = 1.0f;
    config.ai_aim.target_max_age_ms = 0.0f;
    controller_native::NativeGamepadController controller(config);

    controller_native::ControllerVisionSnapshot snapshot;
    snapshot.frame_updated = true;
    snapshot.selector_identity_protocol = true;
    snapshot.frame_id = 101;
    const double snapshot_time = now_seconds();
    snapshot.capture_time_seconds = snapshot_time;
    snapshot.ready_time_seconds = snapshot_time;
    snapshot.selected_observation_id = 77;
    snapshot.state.has_target = true;
    snapshot.state.aim_authority = true;
    snapshot.state.fire_authority = true;
    snapshot.state.dx = 50.0f;
    snapshot.state.dy = 0.0f;
    snapshot.state.screen_center_x = 320.0f;
    snapshot.state.screen_center_y = 256.0f;
    snapshot.state.target_tier = "strong";

    tracking_native::TrackerDetection detection;
    detection.id = 77;
    detection.body_box_px = {330.0f, 200.0f, 80.0f, 140.0f};
    detection.aim_point_px = {370.0f, 256.0f};
    detection.has_aim_point = true;
    detection.confidence = 0.90f;
    detection.target_tier = "observed_strong";
    snapshot.tracker_detections.push_back(detection);

    controller.submit_vision_snapshot(snapshot);
    snapshot.frame_id = 102;
    snapshot.capture_time_seconds = snapshot_time + 0.010;
    snapshot.ready_time_seconds = snapshot_time + 0.010;
    snapshot.selected_observation_id = 78;
    snapshot.tracker_detections.front().id = 78;
    controller.submit_vision_snapshot(snapshot);
    controller_native::GamepadOutputState output =
        controller.build_output(aiming_physical_state());
    require_true(output.right_x > 0.40f, "controller snapshot should produce right-stick assist");

    controller_native::ControllerVisionSnapshot no_update;
    no_update.frame_updated = false;
    no_update.state.has_target = false;
    controller.submit_vision_snapshot(no_update);

    output = controller.build_output(aiming_physical_state());
    require_true(output.right_x > 0.40f, "no-update controller snapshot must not clear latest target");
}

void test_controller_ads_drops_tracker_only_vertical_snap_after_processed_miss() {
    double now = 60.000;
    controller_native::GamepadRuntimeConfig config;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.max_ai_force = 1.0f;
    config.ai_aim.max_ai_force_y = 1.0f;
    config.ai_aim.target_max_age_ms = 96.0f;
    config.ai_aim.target_projection_max_age_ms = 96.0f;
    config.aim_assist_dynamics.enabled = false;
    controller_native::NativeGamepadController controller(config, [&now]() { return now; });

    auto observed = [](std::uint64_t frame, std::uint64_t observation, double at) {
        controller_native::ControllerVisionSnapshot snapshot;
        snapshot.frame_updated = true;
        snapshot.selector_identity_protocol = true;
        snapshot.frame_id = frame;
        snapshot.capture_time_seconds = at;
        snapshot.ready_time_seconds = at;
        snapshot.selected_observation_id = observation;
        snapshot.state.has_target = true;
        snapshot.state.aim_authority = true;
        snapshot.state.fire_authority = true;
        snapshot.state.dx = 0.0f;
        snapshot.state.dy = -100.0f;
        snapshot.state.screen_center_x = 320.0f;
        snapshot.state.screen_center_y = 256.0f;
        snapshot.state.target_x = 320.0f;
        snapshot.state.target_y = 156.0f;
        snapshot.state.target_tier = "observed_strong";
        snapshot.state.observed_at_seconds = at;
        tracking_native::TrackerDetection detection;
        detection.id = observation;
        detection.body_box_px = {285.0f, 110.0f, 70.0f, 180.0f};
        detection.aim_point_px = {320.0f, 156.0f};
        detection.has_aim_point = true;
        detection.confidence = 0.95f;
        detection.target_tier = "observed_strong";
        snapshot.tracker_detections.push_back(detection);
        return snapshot;
    };

    controller.submit_vision_snapshot(observed(600, 6001, now));
    now += 0.010;
    controller.submit_vision_snapshot(observed(601, 6011, now));
    controller.build_output(aiming_physical_state());

    now += 0.010;
    controller_native::ControllerVisionSnapshot miss;
    miss.frame_updated = true;
    miss.selector_identity_protocol = true;
    miss.frame_id = 602;
    miss.capture_time_seconds = now;
    miss.ready_time_seconds = now;
    miss.state.screen_center_x = 320.0f;
    miss.state.screen_center_y = 256.0f;
    controller.submit_vision_snapshot(miss);

    controller_native::PhysicalGamepadState physical = aiming_physical_state();
    physical.right_y = 0.22f;
    const controller_native::GamepadOutputState output = controller.build_output(physical);
    const auto& frame = controller.last_frame_vision_state();
    require_true(frame.has_target && !frame.aim_authority,
                 "processed miss should retain identity without blind aim authority");
    require_true(
        frame.assist_authority_state == pipeline_contract::AssistAuthorityState::TrackOnly,
        "processed miss should expose identity-only tracker continuity");
    require_true(!frame.current_observed_target_present,
                 "processed miss must revoke current observed target presence");
    require_near(controller.last_output_components().ai_aim_stick.y, 0.0f, 0.001f,
                 "ADS must not emit vertical AI snap from tracker-only continuity");
    require_near(output.right_y, physical.right_y, 0.001f,
                 "tracker-only ADS miss must preserve manual vertical input");
}

void test_controller_bodylock_coasts_then_releases_at_1000hz() {
    double now = 61.000;
    controller_native::GamepadRuntimeConfig config;
    config.ai_aim.ads_snap_window_ms = 0;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.deadzone_inner = 0.0f;
    config.ai_aim.deadzone_outer = 0.0f;
    config.ai_aim.x_deadzone_outer = 0.0f;
    config.ai_aim.piecewise_mid_pixels = 0.0f;
    config.ai_aim.piecewise_mid_pixels_y = 0.0f;
    config.ai_aim.body_lock_max_ai_force = 1.0f;
    config.ai_aim.body_lock_max_ai_force_y = 0.0f;
    config.ai_aim.body_lock_confidence_frames = 1;
    config.ai_aim.body_lock_upper_body_ratio = 0.50f;
    config.ai_aim.target_max_age_ms = 96.0f;
    config.ai_aim.target_projection_max_age_ms = 96.0f;
    config.aim_assist_dynamics.enabled = true;
    config.recoil.enabled = false;
    controller_native::NativeGamepadController controller(
        config,
        [&now]() { return now; });

    const auto observed = [](std::uint64_t frame, std::uint64_t observation, double at) {
        controller_native::ControllerVisionSnapshot snapshot;
        snapshot.frame_updated = true;
        snapshot.selector_identity_protocol = true;
        snapshot.frame_id = frame;
        snapshot.capture_time_seconds = at;
        snapshot.ready_time_seconds = at;
        snapshot.selected_observation_id = observation;
        snapshot.state.has_target = true;
        snapshot.state.aim_authority = true;
        snapshot.state.fire_authority = true;
        snapshot.state.dx = 36.0f;
        snapshot.state.dy = 0.0f;
        snapshot.state.screen_center_x = 320.0f;
        snapshot.state.screen_center_y = 256.0f;
        snapshot.state.target_x = 356.0f;
        snapshot.state.target_y = 256.0f;
        snapshot.state.has_body_box = true;
        snapshot.state.body_x1 = 316.0f;
        snapshot.state.body_x2 = 396.0f;
        snapshot.state.body_y1 = 216.0f;
        snapshot.state.body_y2 = 296.0f;
        snapshot.state.target_tier = "observed_strong";
        snapshot.state.observed_at_seconds = at;
        tracking_native::TrackerDetection detection;
        detection.id = observation;
        detection.body_box_px = {316.0f, 216.0f, 80.0f, 80.0f};
        detection.aim_point_px = {356.0f, 256.0f};
        detection.has_aim_point = true;
        detection.confidence = 0.95f;
        detection.target_tier = "observed_strong";
        snapshot.tracker_detections.push_back(detection);
        return snapshot;
    };

    controller.build_output(aiming_physical_state());
    for (int frame = 0; frame < 4; ++frame) {
        now += 0.010;
        controller.submit_vision_snapshot(observed(
            700 + static_cast<std::uint64_t>(frame),
            7001 + static_cast<std::uint64_t>(frame),
            now));
        controller.build_output(aiming_physical_state());
    }
    const common_native::Vec2f tracked_assist =
        controller.last_output_components().before_recoil_stick;
    require_true(
        std::hypot(tracked_assist.x, tracked_assist.y) > 0.10f,
        "test setup should establish material delivered BodyLock assist");
    require_true(
        controller.last_ai_aim_mode() == "body_lock",
        "test setup should enter BodyLock before the evidence gap");

    now += 0.001;
    controller_native::ControllerVisionSnapshot miss;
    miss.frame_updated = true;
    miss.selector_identity_protocol = true;
    miss.frame_id = 704;
    miss.capture_time_seconds = now;
    miss.ready_time_seconds = now;
    miss.state.screen_center_x = 320.0f;
    miss.state.screen_center_y = 256.0f;
    controller.submit_vision_snapshot(miss);

    common_native::Vec2f previous = tracked_assist;
    double coast_started_at = 0.0;
    double release_started_at = 0.0;
    float first_coast_magnitude = 0.0f;
    float max_tick_delta = 0.0f;
    for (int tick = 0; tick < 145; ++tick) {
        controller.build_output(aiming_physical_state());
        const auto& components = controller.last_output_components();
        const common_native::Vec2f current = components.before_recoil_stick;
        max_tick_delta = std::max(
            max_tick_delta,
            std::hypot(current.x - previous.x, current.y - previous.y));
        previous = current;
        const std::string lifecycle = components.bodylock_lifecycle;
        if (lifecycle == "coast") {
            if (coast_started_at == 0.0) {
                coast_started_at = now;
                first_coast_magnitude = std::hypot(current.x, current.y);
            }
            require_true(
                controller.last_ai_aim_mode() == "body_lock",
                "ADS-only stages must remain bypassed while lifecycle owns BodyLock coast");
        } else if (coast_started_at > 0.0 && release_started_at == 0.0) {
            release_started_at = now;
        }
        now += 0.001;
    }

    require_true(coast_started_at > 0.0, "processed miss should enter same-track coast");
    require_true(
        first_coast_magnitude > 0.05f && first_coast_magnitude <=
            std::hypot(tracked_assist.x, tracked_assist.y),
        "coast should preserve bounded delivered AI without inventing a new plan");
    require_true(release_started_at > coast_started_at, "coast expiry should enter release");
    require_true(
        release_started_at - coast_started_at <= 0.097,
        "same-track coast must not exceed 96 ms");
    require_true(
        max_tick_delta <= 0.07f,
        "coast and release must stay inside the 1000 Hz delivered envelope bound");
    require_true(
        std::hypot(previous.x, previous.y) <= 0.001f,
        "expired identity must release delivered BodyLock AI to zero within 40 ms");
}

void test_target_tracker_projects_camera_motion_between_vision_frames() {
    controller_native::NativeTargetTrackerConfig config;
    config.reticle_speed_px_per_sec = 1000.0f;
    config.max_projection_age_ms = 100.0f;
    controller_native::NativeGamepadTargetTracker tracker(config);

    controller_native::NativeTargetTrackerObservation observation;
    observation.has_target = true;
    observation.dx = 50.0f;
    observation.dy = 0.0f;
    observation.target_tier = "strong";
    observation.observed_at_seconds = 10.0;
    tracker.update_observation(observation);
    tracker.record_output(0.50f, 0.0f, 0.020);

    const auto projection = tracker.project(10.020);
    require_true(projection.has_value(), "tracker should project a fresh target");
    require_near(projection->dx, 40.0f, 0.001f, "projected dx should subtract camera motion");
}

void test_legacy_projection_tracker_matches_native_project_output() {
    controller_native::NativeTargetTrackerConfig config;
    config.reticle_speed_px_per_sec = 1000.0f;
    config.max_projection_age_ms = 100.0f;

    controller_native::NativeGamepadTargetTracker direct(config);
    tracking_native::LegacyProjectionTracker adapter(config);

    controller_native::NativeTargetTrackerObservation direct_observation;
    direct_observation.has_target = true;
    direct_observation.dx = 50.0f;
    direct_observation.dy = 4.0f;
    direct_observation.target_tier = "strong";
    direct_observation.observed_at_seconds = 10.0;
    direct.update_observation(direct_observation);

    tracking_native::TrackerObservation adapter_observation;
    adapter_observation.has_target = true;
    adapter_observation.aim_error_px = {50.0f, 4.0f};
    adapter_observation.target_tier = "strong";
    adapter_observation.capture_time = {10.0};
    adapter.ingest(adapter_observation);

    direct.record_output(0.50f, -0.25f, 0.020);

    tracking_native::TrackerControlSample sample;
    sample.apply_time = {10.020};
    sample.dt = {0.020};
    sample.sticks.final_output = {0.50f, -0.25f};
    adapter.push_control_sample(sample);

    const auto direct_projection = direct.project(10.020);
    const auto adapter_snapshot = adapter.query({10.020});

    require_true(direct_projection.has_value(), "direct tracker should project a fresh target");
    require_true(adapter_snapshot.has_target, "legacy adapter should project a fresh target");
    require_true(
        adapter_snapshot.source == tracking_native::TrackerSnapshotSource::Projected,
        "legacy adapter should mark projected snapshots");
    require_near(
        adapter_snapshot.aim_error_px.x,
        direct_projection->dx,
        0.001f,
        "legacy adapter dx should match direct tracker projection");
    require_near(
        adapter_snapshot.aim_error_px.y,
        direct_projection->dy,
        0.001f,
        "legacy adapter dy should match direct tracker projection");
    require_true(
        adapter_snapshot.fire_authority == common_native::FireAuthority::None,
        "projected adapter snapshots should not grant fire authority");
}

void test_legacy_projection_tracker_expires_after_max_age() {
    controller_native::NativeTargetTrackerConfig config;
    config.max_projection_age_ms = 5.0f;
    tracking_native::LegacyProjectionTracker adapter(config);

    tracking_native::TrackerObservation observation;
    observation.has_target = true;
    observation.aim_error_px = {50.0f, 0.0f};
    observation.target_tier = "strong";
    observation.capture_time = {10.0};
    adapter.ingest(observation);

    const auto snapshot = adapter.query({10.020});
    require_true(!snapshot.has_target, "legacy adapter projection should expire after max age");
    require_true(
        snapshot.source == tracking_native::TrackerSnapshotSource::Absent,
        "expired adapter projection should be absent");
}

void test_legacy_projection_tracker_empty_observation_clears_snapshot() {
    tracking_native::LegacyProjectionTracker adapter;

    tracking_native::TrackerObservation observation;
    observation.has_target = true;
    observation.aim_error_px = {50.0f, 0.0f};
    observation.target_tier = "strong";
    observation.capture_time = {10.0};
    adapter.ingest(observation);
    require_true(adapter.query({10.001}).has_target, "test setup should create a target");

    tracking_native::TrackerObservation empty_observation;
    empty_observation.has_target = false;
    empty_observation.capture_time = {10.010};
    adapter.ingest(empty_observation);

    const auto snapshot = adapter.query({10.011});
    require_true(!snapshot.has_target, "empty observation should clear legacy adapter snapshot");
}

void test_tracker_backend_config_defaults_to_fps_reference() {
    controller_native::GamepadRuntimeConfig config;
    require_true(
        config.tracker_backend == tracking_native::TrackerBackendKind::FpsReference,
        "default tracker backend should use fps_reference");
}

void test_runtime_config_defaults_recoil_recognizer_state_path() {
    ScopedEnvVar clear_enable_recoil("ENABLE_RECOIL_RUNTIME", "");
    ScopedEnvVar clear_recoil_enabled("RECOIL_ENABLED", "");
    ScopedEnvVar clear_state_path("RECOIL_RECOGNIZER_STATE_PATH", "");
    ScopedEnvVar clear_profile_dir("RECOIL_PROFILE_DIR", "");

    const std::filesystem::path root = make_temp_test_dir("default_recoil_state_path");
    const std::filesystem::path config_path = root / "config.toml";
    write_text_file(config_path, "");

    const controller_native::RuntimeConfig config =
        controller_native::load_runtime_config(config_path);
    require_true(config.gamepad.recoil.enabled, "recoil should be enabled by default");
    require_true(
        config.gamepad.recoil.recognizer_state_path ==
            "artifacts/recoil_app/current_weapon.json",
        "direct native runtime config should default to current_weapon recoil state");
    require_true(
        config.gamepad.recoil.profile_directory == "artifacts/recoil_profiles",
        "direct native runtime config should default to recoil profile directory");
}

void test_runtime_config_env_can_disable_recoil_runtime() {
    ScopedEnvVar disable_recoil("ENABLE_RECOIL_RUNTIME", "0");
    ScopedEnvVar clear_recoil_enabled("RECOIL_ENABLED", "");

    const std::filesystem::path root = make_temp_test_dir("disable_recoil_runtime");
    const std::filesystem::path config_path = root / "config.toml";
    write_text_file(config_path, "");

    const controller_native::RuntimeConfig config =
        controller_native::load_runtime_config(config_path);
    require_true(
        !config.gamepad.recoil.enabled,
        "ENABLE_RECOIL_RUNTIME=0 should disable native recoil even with a default state path");
}

void test_runtime_config_rejects_unknown_tracker_backend() {
    const std::filesystem::path root = make_temp_test_dir("unknown_tracker_backend");
    const std::filesystem::path config_path = root / "config.toml";
    write_text_file(
        config_path,
        "[runtime.gamepad]\ntracker_backend = \"unknown_backend\"\n");

    bool threw = false;
    try {
        (void)controller_native::load_runtime_config(config_path);
    } catch (const std::runtime_error& exc) {
        threw = std::string(exc.what()).find("tracker_backend") != std::string::npos;
    }
    require_true(threw, "unknown tracker backend should fail config load clearly");
}

void test_runtime_config_parses_ads_snap_fov_scale() {
    const std::filesystem::path root = make_temp_test_dir("ads_snap_fov_scale");
    const std::filesystem::path config_path = root / "config.toml";
    write_text_file(
        config_path,
        "[gamepad.ai_aim]\nads_snap_fov_scale = 0.68\nads_snap_fov_transition_ms = 130\n");

    const controller_native::RuntimeConfig config =
        controller_native::load_runtime_config(config_path);
    require_near(
        config.gamepad.ai_aim.ads_snap_fov_scale,
        0.68f,
        0.001f,
        "runtime config should parse ADS snap FOV scale");
    require_near(
        config.gamepad.ai_aim.ads_snap_fov_transition_ms,
        130.0f,
        0.001f,
        "runtime config should parse ADS snap FOV transition");
}

void test_experimental_kalman_tracker_backend_can_be_constructed() {
    controller_native::NativeTargetTrackerConfig config;
    std::unique_ptr<tracking_native::TrackerBackend> backend =
        tracking_native::create_tracker_backend(
            tracking_native::TrackerBackendKind::KalmanExperimental,
            config);
    require_true(static_cast<bool>(backend), "experimental Kalman backend should construct");
    require_true(
        !backend->query({10.0}).has_target,
        "empty experimental Kalman backend should have no target");
}

void test_fps_reference_tracker_coasts_after_processed_miss_without_fire_authority() {
    controller_native::NativeTargetTrackerConfig config;
    config.max_projection_age_ms = 160.0f;
    std::unique_ptr<tracking_native::TrackerBackend> backend =
        tracking_native::create_tracker_backend(
            tracking_native::TrackerBackendKind::FpsReference,
            config);

    tracking_native::TrackerObservation first;
    first.frame_id = 1;
    first.has_target = true;
    first.aim_error_px = {50.0f, -16.0f};
    first.target_tier = "observed_strong";
    first.capture_time = {10.000};
    first.ready_time = {10.006};
    first.screen_center_px = {320.0f, 256.0f};
    first.detections.push_back({
        101,
        {340.0f, 176.0f, 60.0f, 160.0f},
        {370.0f, 240.0f},
        true,
        0.92f,
        0,
        "observed_strong"});
    backend->ingest(first);

    tracking_native::TrackerObservation second = first;
    second.frame_id = 2;
    second.capture_time = {10.010};
    second.ready_time = {10.016};
    second.aim_error_px = {54.0f, -16.0f};
    second.detections[0].id = 201;
    second.detections[0].body_box_px.x += 4.0f;
    second.detections[0].aim_point_px.x += 4.0f;
    backend->ingest(second);

    const tracking_native::TrackerSnapshot observed = backend->query({10.017});
    require_true(observed.has_target, "confirmed fps_reference track should be selectable");
    require_true(
        observed.assist_authority == common_native::AssistAuthority::AimObserved,
        "fresh confirmed fps_reference track should expose observed assist authority");

    tracking_native::TrackerObservation miss;
    miss.frame_id = 3;
    miss.capture_time = {10.020};
    miss.ready_time = {10.026};
    miss.screen_center_px = {320.0f, 256.0f};
    backend->ingest(miss);

    const tracking_native::TrackerSnapshot coast = backend->query({10.032});
    require_true(coast.has_target, "fps_reference should coast a confirmed target after one miss");
    require_true(
        coast.assist_authority == common_native::AssistAuthority::AimCoast,
        "coasted fps_reference target should keep aim assist authority");
    require_true(
        coast.fire_authority == common_native::FireAuthority::None,
        "coasted fps_reference target must not grant fire authority");
}

void test_legacy_tracker_backend_factory_matches_legacy_projection() {
    controller_native::NativeTargetTrackerConfig config;
    config.reticle_speed_px_per_sec = 1000.0f;
    config.max_projection_age_ms = 100.0f;
    tracking_native::LegacyProjectionTracker legacy(config);
    std::unique_ptr<tracking_native::TrackerBackend> backend =
        tracking_native::create_tracker_backend(
            tracking_native::TrackerBackendKind::LegacyProjection,
            config);

    tracking_native::TrackerObservation observation;
    observation.has_target = true;
    observation.aim_error_px = {50.0f, 4.0f};
    observation.target_tier = "strong";
    observation.capture_time = {10.0};
    legacy.ingest(observation);
    backend->ingest(observation);

    tracking_native::TrackerControlSample sample;
    sample.apply_time = {10.020};
    sample.dt = {0.020};
    sample.sticks.final_output = {0.50f, -0.25f};
    legacy.push_control_sample(sample);
    backend->push_control_sample(sample);

    const tracking_native::TrackerSnapshot legacy_snapshot = legacy.query({10.020});
    const tracking_native::TrackerSnapshot backend_snapshot = backend->query({10.020});
    require_true(legacy_snapshot.has_target, "legacy setup should project a target");
    require_true(backend_snapshot.has_target, "legacy backend should project a target");
    require_near(
        backend_snapshot.aim_error_px.x,
        legacy_snapshot.aim_error_px.x,
        0.001f,
        "legacy backend dx should match legacy projection");
    require_near(
        backend_snapshot.aim_error_px.y,
        legacy_snapshot.aim_error_px.y,
        0.001f,
        "legacy backend dy should match legacy projection");
}

void test_controller_projects_target_during_no_update_ticks() {
    controller_native::GamepadRuntimeConfig config;
    config.tracker_backend = tracking_native::TrackerBackendKind::LegacyProjection;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.max_ai_force = 1.0f;
    config.ai_aim.max_ai_force_y = 1.0f;
    config.ai_aim.piecewise_mid_pixels = 0.0f;
    config.ai_aim.piecewise_mid_pixels_y = 0.0f;
    config.ai_aim.ads_snap_time_to_go_gain = 0.0f;
    config.ai_aim.target_max_age_ms = 500.0f;
    config.ai_aim.target_projection_max_age_ms = 500.0f;
    config.ai_aim.target_projection_reticle_speed_px_per_sec = 1000.0f;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.enabled = false;
    controller_native::NativeGamepadController controller(config);

    vision_native::VisionResult target;
    target.frame_updated = true;
    target.has_target = true;
    target.aim_authority = true;
    target.fire_authority = true;
    target.dx = 50.0f;
    target.dy = 0.0f;
    target.target_tier = "strong";
    target.result_at_ns = now_ns();
    submit_adapted_vision_result(controller, target);

    controller_native::GamepadOutputState first = controller.build_output(aiming_physical_state());
    require_true(first.right_x > 0.40f, "fresh target should produce assist before projection");

    vision_native::VisionResult no_update;
    no_update.frame_updated = false;
    no_update.has_target = false;
    submit_adapted_vision_result(controller, no_update);

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    controller_native::GamepadOutputState second = controller.build_output(aiming_physical_state());
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    controller_native::GamepadOutputState third = controller.build_output(aiming_physical_state());
    require_true(
        third.right_x < second.right_x - 0.03f,
        "controller should use projected target error after output moves the reticle");
}

void test_controller_expires_projection_before_aim_target_age() {
    controller_native::GamepadRuntimeConfig config;
    config.tracker_backend = tracking_native::TrackerBackendKind::LegacyProjection;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.max_ai_force = 1.0f;
    config.ai_aim.max_ai_force_y = 1.0f;
    config.ai_aim.piecewise_mid_pixels = 0.0f;
    config.ai_aim.piecewise_mid_pixels_y = 0.0f;
    config.ai_aim.ads_snap_time_to_go_gain = 0.0f;
    config.ai_aim.target_max_age_ms = 500.0f;
    config.ai_aim.target_projection_max_age_ms = 5.0f;
    config.ai_aim.target_projection_reticle_speed_px_per_sec = 1000.0f;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.enabled = false;
    controller_native::NativeGamepadController controller(config);

    vision_native::VisionResult target;
    target.frame_updated = true;
    target.has_target = true;
    target.aim_authority = true;
    target.fire_authority = true;
    target.dx = 50.0f;
    target.dy = 0.0f;
    target.target_tier = "strong";
    target.result_at_ns = now_ns();
    submit_adapted_vision_result(controller, target);

    const controller_native::GamepadOutputState first =
        controller.build_output(aiming_physical_state());
    require_true(first.right_x > 0.40f, "fresh target should produce assist");

    vision_native::VisionResult no_update;
    no_update.frame_updated = false;
    no_update.has_target = false;
    submit_adapted_vision_result(controller, no_update);

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const controller_native::GamepadOutputState second =
        controller.build_output(aiming_physical_state());
    require_true(
        std::fabs(second.right_x) < 0.001f,
        "expired projection should clear stale target instead of reusing old vision state");
}

void test_fps_reference_controller_clears_no_update_after_projection_ttl() {
    controller_native::GamepadRuntimeConfig config;
    config.tracker_backend = tracking_native::TrackerBackendKind::FpsReference;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.max_ai_force = 1.0f;
    config.ai_aim.max_ai_force_y = 1.0f;
    config.ai_aim.piecewise_mid_pixels = 0.0f;
    config.ai_aim.piecewise_mid_pixels_y = 0.0f;
    config.ai_aim.ads_snap_time_to_go_gain = 0.0f;
    config.ai_aim.target_max_age_ms = 500.0f;
    config.ai_aim.target_projection_max_age_ms = 20.0f;
    config.ai_aim.target_projection_reticle_speed_px_per_sec = 1000.0f;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.enabled = false;
    controller_native::NativeGamepadController controller(config);

    const std::uint64_t base_ns = now_ns();
    auto make_result = [&](std::uint64_t frame_id, std::uint64_t offset_ns) {
        vision_native::VisionResult target;
        target.frame_updated = true;
        target.frame_id = frame_id;
        target.has_target = true;
        target.aim_authority = true;
        target.fire_authority = true;
        target.dx = 50.0f;
        target.dy = 0.0f;
        target.screen_center_x = 320.0f;
        target.screen_center_y = 256.0f;
        target.target_x = 370.0f;
        target.target_y = 256.0f;
        target.has_body_box = true;
        target.body_x1 = 340.0f;
        target.body_y1 = 180.0f;
        target.body_x2 = 400.0f;
        target.body_y2 = 320.0f;
        target.target_tier = "strong";
        target.captured_at_ns = base_ns + offset_ns;
        target.result_at_ns = target.captured_at_ns + 1'000'000ull;

        vision_native::Detection detection;
        detection.x1 = target.body_x1;
        detection.y1 = target.body_y1;
        detection.x2 = target.body_x2;
        detection.y2 = target.body_y2;
        detection.conf = 0.95f;
        detection.class_id = 1;
        target.detections.push_back(detection);
        return target;
    };

    submit_adapted_vision_result(controller, make_result(1, 0));
    submit_adapted_vision_result(controller, make_result(2, 5'000'000ull));
    const controller_native::GamepadOutputState fresh =
        controller.build_output(aiming_physical_state());
    const auto fresh_state = controller.last_frame_vision_state();
    std::ostringstream fresh_message;
    fresh_message << "fresh fps_reference target should produce assist"
                  << " output=" << fresh.right_x
                  << " target=" << fresh_state.has_target
                  << " aim=" << fresh_state.aim_authority
                  << " tier=" << fresh_state.target_tier
                  << " projection=" << fresh_state.has_tracker_projection;
    require_true(fresh.right_x > 0.30f, fresh_message.str());

    vision_native::VisionResult no_update;
    no_update.frame_updated = false;
    submit_adapted_vision_result(controller, no_update);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const controller_native::GamepadOutputState expired =
        controller.build_output(aiming_physical_state());
    require_true(
        std::fabs(expired.right_x) < 0.001f,
        "fps_reference no-update projection should be destroyed after projection TTL");
}

void test_fps_reference_controller_ignores_unauthorized_raw_detections() {
    controller_native::GamepadRuntimeConfig config;
    config.tracker_backend = tracking_native::TrackerBackendKind::FpsReference;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.max_ai_force = 1.0f;
    config.ai_aim.max_ai_force_y = 1.0f;
    config.ai_aim.piecewise_mid_pixels = 0.0f;
    config.ai_aim.piecewise_mid_pixels_y = 0.0f;
    config.ai_aim.ads_snap_time_to_go_gain = 0.0f;
    config.ai_aim.target_max_age_ms = 500.0f;
    config.ai_aim.target_projection_max_age_ms = 200.0f;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.enabled = false;
    controller_native::NativeGamepadController controller(config);

    const std::uint64_t base_ns = now_ns();
    auto make_rejected_result = [&](std::uint64_t frame_id, std::uint64_t offset_ns) {
        vision_native::VisionResult result;
        result.frame_updated = true;
        result.frame_id = frame_id;
        result.has_target = false;
        result.aim_authority = false;
        result.fire_authority = false;
        result.screen_center_x = 320.0f;
        result.screen_center_y = 360.0f;
        result.target_tier = "none";
        result.captured_at_ns = base_ns + offset_ns;
        result.result_at_ns = result.captured_at_ns + 1'000'000ull;

        vision_native::Detection detection;
        detection.x1 = 290.0f;
        detection.y1 = 300.0f;
        detection.x2 = 350.0f;
        detection.y2 = 500.0f;
        detection.conf = 0.95f;
        detection.class_id = 1;
        result.detections.push_back(detection);
        result.boxes_seen = 1;
        return result;
    };

    submit_adapted_vision_result(controller, make_rejected_result(1, 0));
    submit_adapted_vision_result(controller, make_rejected_result(2, 5'000'000ull));
    const controller_native::GamepadOutputState output =
        controller.build_output(aiming_physical_state());
    require_near(
        output.right_x,
        0.0f,
        0.001f,
        "unauthorized raw detections should not create horizontal aim assist");
    require_near(
        output.right_y,
        0.0f,
        0.001f,
        "unauthorized raw detections should not create vertical aim assist");
    require_near(
        controller.last_output_components().ai_aim_stick.y,
        0.0f,
        0.001f,
        "tracker must not promote selector-rejected raw detections into body-lock");
}

void test_controller_prefers_fresh_vision_over_tracker_projection() {
    controller_native::GamepadRuntimeConfig config;
    config.ai_aim.max_pixels = 500.0f;
    config.ai_aim.max_ai_force = 1.0f;
    config.ai_aim.max_ai_force_y = 1.0f;
    config.ai_aim.piecewise_mid_pixels = 0.0f;
    config.ai_aim.piecewise_mid_pixels_y = 0.0f;
    config.ai_aim.ads_snap_time_to_go_gain = 0.0f;
    config.ai_aim.ads_snap_window_ms = 0;
    config.ai_aim.target_max_age_ms = 500.0f;
    config.ai_aim.target_projection_max_age_ms = 500.0f;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.enabled = false;
    controller_native::NativeGamepadController controller(config);

    const std::uint64_t base_ns = now_ns();
    auto make_result = [&](std::uint64_t frame_id, float dx, std::uint64_t offset_ns) {
        vision_native::VisionResult result;
        result.frame_updated = true;
        result.frame_id = frame_id;
        result.has_target = true;
        result.aim_authority = true;
        result.fire_authority = true;
        result.dx = dx;
        result.dy = 0.0f;
        result.screen_center_x = 320.0f;
        result.screen_center_y = 256.0f;
        result.target_x = result.screen_center_x + dx;
        result.target_y = result.screen_center_y;
        result.has_body_box = true;
        result.body_x1 = result.target_x - 24.0f;
        result.body_y1 = 176.0f;
        result.body_x2 = result.target_x + 24.0f;
        result.body_y2 = 336.0f;
        result.target_tier = "strong";
        result.captured_at_ns = base_ns + offset_ns;
        result.result_at_ns = result.captured_at_ns + 1'000'000ull;

        vision_native::Detection detection;
        detection.x1 = result.body_x1;
        detection.y1 = result.body_y1;
        detection.x2 = result.body_x2;
        detection.y2 = result.body_y2;
        detection.conf = 0.92f;
        detection.class_id = 1;
        result.detections.push_back(detection);
        return result;
    };

    submit_adapted_vision_result(controller, make_result(1, 250.0f, 0));
    submit_adapted_vision_result(controller, make_result(2, 250.0f, 10'000'000ull));
    submit_adapted_vision_result(controller, make_result(3, -250.0f, 20'000'000ull));

    const controller_native::GamepadOutputState output =
        controller.build_output(aiming_physical_state());
    require_true(
        output.right_x < -0.35f,
        "fresh vision target should drive aim even when tracker still prefers an older confirmed track");
}

void test_controller_tracker_records_component_aware_final_motion() {
    const std::filesystem::path root = make_temp_test_dir("tracker_component_aware_final_motion");
    const std::filesystem::path profile_dir = root / "profiles";
    const std::filesystem::path state_path = root / "latest-state.json";
    std::filesystem::create_directories(profile_dir);
    write_text_file(
        profile_dir / "profile-cod22-m4-ads-standing-side.json",
        recoil_profile_xy_json(
            "profile-cod22-m4-ads-standing-side",
            "cod22-m4",
            "ads",
            0.95f,
            10.0f,
            0.0f));
    write_text_file(
        state_path,
        recognizer_state_json("cod22-m4", "profile-cod22-m4-ads-standing-side"));

    controller_native::GamepadRuntimeConfig config;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.max_ai_force = 1.0f;
    config.ai_aim.max_ai_force_y = 1.0f;
    config.ai_aim.piecewise_mid_pixels = 0.0f;
    config.ai_aim.piecewise_mid_pixels_y = 0.0f;
    config.ai_aim.ads_snap_window_ms = 0;
    config.ai_aim.target_max_age_ms = 500.0f;
    config.ai_aim.target_projection_max_age_ms = 500.0f;
    config.ai_aim.target_projection_reticle_speed_px_per_sec = 3000.0f;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.enabled = true;
    config.recoil.selection_log_enabled = false;
    config.recoil.profile_directory = profile_dir.string();
    config.recoil.recognizer_state_path = state_path.string();
    config.recoil.profile_amount = 1.0f;
    config.recoil.profile_x_amount = 1.0f;
    config.recoil.profile_velocity_reference_ms = 10.0f;
    config.recoil.profile_despike_enabled = false;
    config.recoil.piecewise_mid_pixels_y = 10.0f;
    config.recoil.piecewise_max_pixels_y = 20.0f;
    config.recoil.piecewise_mid_ratio_y = 0.50f;
    controller_native::NativeGamepadController controller(config);

    controller_native::NativeControllerVisionState target;
    target.has_target = true;
    target.aim_authority = true;
    target.fire_authority = true;
    target.dx = 0.0f;
    target.dy = 0.0f;
    target.target_tier = "strong";
    target.observed_at_seconds = now_seconds();
    controller.submit_vision_state(target);

    controller_native::PhysicalGamepadState firing = aiming_physical_state();
    firing.right_x = 0.30f;
    firing.right_trigger = 1.0f;
    controller.build_output(firing);
    std::this_thread::sleep_for(std::chrono::milliseconds(12));
    const controller_native::GamepadOutputState firing_output = controller.build_output(firing);
    require_true(
        firing_output.right_x < -0.10f,
        "test setup should produce opposing recoil compensation actual=" +
            std::to_string(firing_output.right_x));
    require_near(
        controller.last_tracker_motion_output().right_x,
        firing_output.right_x,
        0.001f,
        "tracker should record final camera motion for ego projection");
    require_near(
        controller.last_output_components().manual_stick.x,
        0.30f,
        0.001f,
        "controller components should preserve manual stick separately from tracker final motion");
    require_true(
        controller.last_output_components().recoil_stick.x < -0.40f,
        "controller components should expose recoil separately instead of filtering it from tracker");
}

void test_controller_recoil_ignores_tracker_only_projected_targets() {
    controller_native::GamepadRuntimeConfig config;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.max_ai_force = 1.0f;
    config.ai_aim.max_ai_force_y = 1.0f;
    config.ai_aim.piecewise_mid_pixels = 0.0f;
    config.ai_aim.piecewise_mid_pixels_y = 0.0f;
    config.ai_aim.ads_snap_window_ms = 0;
    config.ai_aim.target_max_age_ms = 500.0f;
    config.ai_aim.target_projection_max_age_ms = 500.0f;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.enabled = true;
    config.recoil.feedback_amount = 0.24f;
    config.recoil.profile_amount = 1.0f;
    config.recoil.profile_x_amount = 1.0f;
    controller_native::NativeGamepadController controller(config);

    const std::uint64_t base_ns = now_ns();
    vision_native::VisionResult target;
    target.frame_updated = true;
    target.frame_id = 1;
    target.has_target = true;
    target.aim_authority = true;
    target.fire_authority = true;
    target.dx = 0.0f;
    target.dy = -50.0f;
    target.screen_center_x = 320.0f;
    target.screen_center_y = 256.0f;
    target.target_x = 320.0f;
    target.target_y = 206.0f;
    target.has_body_box = true;
    target.body_x1 = 290.0f;
    target.body_y1 = 130.0f;
    target.body_x2 = 350.0f;
    target.body_y2 = 290.0f;
    target.target_tier = "observed_strong";
    target.captured_at_ns = base_ns;
    target.result_at_ns = base_ns + 1'000'000ull;
    submit_adapted_vision_result(controller, target);

    target.frame_id = 2;
    target.captured_at_ns = base_ns + 10'000'000ull;
    target.result_at_ns = base_ns + 11'000'000ull;
    submit_adapted_vision_result(controller, target);

    vision_native::VisionResult miss;
    miss.frame_updated = true;
    miss.frame_id = 3;
    miss.has_target = false;
    miss.screen_center_x = 320.0f;
    miss.screen_center_y = 256.0f;
    miss.target_x = 320.0f;
    miss.target_y = 256.0f;
    miss.captured_at_ns = base_ns + 20'000'000ull;
    miss.result_at_ns = base_ns + 21'000'000ull;
    submit_adapted_vision_result(controller, miss);

    controller_native::PhysicalGamepadState firing = aiming_physical_state();
    firing.right_trigger = 1.0f;
    (void)controller.build_output(firing);

    require_true(
        controller.last_output_components().recoil_stick.y < -0.20f,
        "recoil should keep fallback down-pull when only tracker-projected target remains");
}


void test_controller_tracker_final_motion_is_not_config_toggled() {
    const std::filesystem::path root = make_temp_test_dir("tracker_final_motion_no_toggle");
    const std::filesystem::path profile_dir = root / "profiles";
    const std::filesystem::path state_path = root / "latest-state.json";
    std::filesystem::create_directories(profile_dir);
    write_text_file(
        profile_dir / "profile-cod22-m4-ads-standing-side.json",
        recoil_profile_xy_json(
            "profile-cod22-m4-ads-standing-side",
            "cod22-m4",
            "ads",
            0.95f,
            10.0f,
            0.0f));
    write_text_file(
        state_path,
        recognizer_state_json("cod22-m4", "profile-cod22-m4-ads-standing-side"));

    controller_native::GamepadRuntimeConfig config;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.max_ai_force = 1.0f;
    config.ai_aim.max_ai_force_y = 1.0f;
    config.ai_aim.piecewise_mid_pixels = 0.0f;
    config.ai_aim.piecewise_mid_pixels_y = 0.0f;
    config.ai_aim.ads_snap_window_ms = 0;
    config.ai_aim.target_max_age_ms = 500.0f;
    config.ai_aim.target_projection_max_age_ms = 500.0f;
    config.ai_aim.target_projection_reticle_speed_px_per_sec = 3000.0f;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.enabled = true;
    config.recoil.selection_log_enabled = false;
    config.recoil.profile_directory = profile_dir.string();
    config.recoil.recognizer_state_path = state_path.string();
    config.recoil.profile_amount = 1.0f;
    config.recoil.profile_x_amount = 1.0f;
    config.recoil.profile_velocity_reference_ms = 10.0f;
    config.recoil.profile_despike_enabled = false;
    config.recoil.piecewise_mid_pixels_y = 10.0f;
    config.recoil.piecewise_max_pixels_y = 20.0f;
    config.recoil.piecewise_mid_ratio_y = 0.50f;
    controller_native::NativeGamepadController controller(config);

    controller_native::NativeControllerVisionState target;
    target.has_target = true;
    target.aim_authority = true;
    target.fire_authority = true;
    target.dx = 0.0f;
    target.dy = 0.0f;
    target.target_tier = "strong";
    target.observed_at_seconds = now_seconds();
    controller.submit_vision_state(target);

    controller_native::PhysicalGamepadState firing = aiming_physical_state();
    firing.right_x = 0.30f;
    firing.right_trigger = 1.0f;
    controller.build_output(firing);
    std::this_thread::sleep_for(std::chrono::milliseconds(12));
    const controller_native::GamepadOutputState firing_output = controller.build_output(firing);
    require_true(
        firing_output.right_x < -0.10f,
        "test setup should produce opposing recoil compensation");
    require_near(
        controller.last_tracker_motion_output().right_x,
        firing_output.right_x,
        0.001f,
        "tracker final motion should include recoil without a recoil-specific toggle");
}

void test_controller_output_components_capture_recoil_after_tracker_sample() {
    const std::filesystem::path root = make_temp_test_dir("controller_output_components");
    const std::filesystem::path profile_dir = root / "profiles";
    const std::filesystem::path state_path = root / "latest-state.json";
    std::filesystem::create_directories(profile_dir);
    write_text_file(
        profile_dir / "profile-cod22-m4-ads-standing-side.json",
        recoil_profile_xy_json(
            "profile-cod22-m4-ads-standing-side",
            "cod22-m4",
            "ads",
            0.95f,
            10.0f,
            0.0f));
    write_text_file(
        state_path,
        recognizer_state_json("cod22-m4", "profile-cod22-m4-ads-standing-side"));

    controller_native::GamepadRuntimeConfig config;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.max_ai_force = 1.0f;
    config.ai_aim.max_ai_force_y = 1.0f;
    config.ai_aim.piecewise_mid_pixels = 0.0f;
    config.ai_aim.piecewise_mid_pixels_y = 0.0f;
    config.ai_aim.ads_snap_window_ms = 0;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.enabled = true;
    config.recoil.selection_log_enabled = false;
    config.recoil.profile_directory = profile_dir.string();
    config.recoil.recognizer_state_path = state_path.string();
    config.recoil.profile_amount = 1.0f;
    config.recoil.profile_x_amount = 1.0f;
    config.recoil.profile_velocity_reference_ms = 10.0f;
    config.recoil.profile_despike_enabled = false;
    config.recoil.piecewise_mid_pixels_y = 10.0f;
    config.recoil.piecewise_max_pixels_y = 20.0f;
    config.recoil.piecewise_mid_ratio_y = 0.50f;
    controller_native::NativeGamepadController controller(config);

    controller_native::NativeControllerVisionState target;
    target.has_target = true;
    target.aim_authority = true;
    target.fire_authority = true;
    target.dx = 0.0f;
    target.dy = 0.0f;
    target.target_tier = "strong";
    target.observed_at_seconds = now_seconds();
    controller.submit_vision_state(target);

    controller_native::PhysicalGamepadState firing = aiming_physical_state();
    firing.right_x = 0.30f;
    firing.right_trigger = 1.0f;
    controller.build_output(firing);
    std::this_thread::sleep_for(std::chrono::milliseconds(12));
    const controller_native::GamepadOutputState firing_output = controller.build_output(firing);
    const controller_native::NativeControllerOutputComponents& components =
        controller.last_output_components();

    require_near(
        components.manual_stick.x,
        0.30f,
        0.001f,
        "output components should retain physical manual stick");
    require_near(
        components.final_stick.x,
        firing_output.right_x,
        0.001f,
        "output components should retain final stick");
    require_true(
        components.recoil_stick.x < -0.40f,
        "output components should expose recoil contribution separately");
}

void test_controller_projects_body_box_during_no_update_ticks() {
    controller_native::GamepadRuntimeConfig config;
    config.tracker_backend = tracking_native::TrackerBackendKind::LegacyProjection;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.max_ai_force = 1.0f;
    config.ai_aim.max_ai_force_y = 1.0f;
    config.ai_aim.piecewise_mid_pixels = 0.0f;
    config.ai_aim.piecewise_mid_pixels_y = 0.0f;
    config.ai_aim.ads_snap_window_ms = 0;
    config.ai_aim.target_max_age_ms = 500.0f;
    config.ai_aim.target_projection_max_age_ms = 500.0f;
    config.ai_aim.target_projection_reticle_speed_px_per_sec = 1000.0f;
    config.ai_aim.body_lock_max_ai_force = 1.0f;
    config.ai_aim.body_lock_max_ai_force_y = 0.0f;
    config.ai_aim.body_lock_box_tolerance_px = 80.0f;
    config.ai_aim.body_lock_activation_box_px = 240.0f;
    config.ai_aim.body_lock_upper_body_ratio = 0.50f;
    config.ai_aim.body_lock_smoothing = 0.0f;
    config.ai_aim.body_lock_lateral_motion_min_speed_px_per_sec = 1000000.0f;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.enabled = false;
    controller_native::NativeGamepadController controller(config);

    vision_native::VisionResult target;
    target.frame_updated = true;
    target.has_target = true;
    target.aim_authority = true;
    target.fire_authority = true;
    target.dx = 40.0f;
    target.dy = 0.0f;
    target.screen_center_x = 320.0f;
    target.screen_center_y = 256.0f;
    target.has_body_box = true;
    target.body_x1 = 330.0f;
    target.body_y1 = 196.0f;
    target.body_x2 = 390.0f;
    target.body_y2 = 316.0f;
    target.target_tier = "strong";
    target.result_at_ns = now_ns();
    submit_adapted_vision_result(controller, target);

    const controller_native::GamepadOutputState first =
        controller.build_output(aiming_physical_state());
    require_true(first.right_x > 0.30f, "fresh body-lock box should produce lateral assist");

    vision_native::VisionResult no_update;
    no_update.frame_updated = false;
    no_update.has_target = false;
    submit_adapted_vision_result(controller, no_update);

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const controller_native::GamepadOutputState second =
        controller.build_output(aiming_physical_state());
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const controller_native::GamepadOutputState third =
        controller.build_output(aiming_physical_state());
    require_true(
        third.right_x < second.right_x - 0.03f,
        "controller should project the body-lock box between no-update ticks");
}

void test_ai_aim_scales_weak_and_cue_targets() {
    controller_native::GamepadAiAimConfig config;
    config.max_pixels = 100.0f;
    config.max_ai_force = 1.0f;
    config.max_ai_force_y = 1.0f;
    config.piecewise_mid_pixels = 0.0f;
    config.piecewise_mid_pixels_y = 0.0f;
    config.weak_target_body_lock_force_scale = 0.50f;
    config.cue_hold_body_lock_force_scale = 0.25f;
    controller_native::NativeAiAim ai_aim(config);

    controller_native::NativeAiAimInput input;
    input.aiming = true;
    input.has_target = true;
    input.aim_authority = true;
    input.dx = 50.0f;
    input.dy = 0.0f;
    input.observed_at_seconds = 10.0;
    input.now_seconds = 10.01;

    input.target_tier = "strong";
    require_near(ai_aim.compute(input).assist_x, 0.50f, 0.001f, "strong assist");

    input.target_tier = "associated_weak";
    require_near(ai_aim.compute(input).assist_x, 0.25f, 0.001f, "weak assist scale");

    input.target_tier = "weak_observed";
    require_near(ai_aim.compute(input).assist_x, 0.25f, 0.001f, "weak observed assist scale");

    input.target_tier = "cue_hold";
    require_near(ai_aim.compute(input).assist_x, 0.125f, 0.001f, "cue assist scale");
}

void test_ai_aim_body_lock_preserves_selected_target_y() {
    controller_native::GamepadAiAimConfig config;
    config.max_pixels = 100.0f;
    config.max_ai_force = 1.0f;
    config.max_ai_force_y = 1.0f;
    config.body_lock_max_ai_force = 1.0f;
    config.body_lock_max_ai_force_y = 1.0f;
    config.body_lock_box_tolerance_px = 20.0f;
    config.body_lock_activation_box_px = 180.0f;
    config.body_lock_upper_body_ratio = 0.40f;
    config.body_lock_smoothing = 0.0f;
    config.piecewise_mid_pixels_y = 0.0f;
    controller_native::NativeAiAim ai_aim(config);

    controller_native::NativeAiAimInput input;
    input.aiming = true;
    input.has_target = true;
    input.aim_authority = true;
    input.ads_snap_active = false;
    input.dx = 90.0f;
    input.dy = 0.0f;
    input.target_y = 280.0f;
    input.target_tier = "strong";
    input.observed_at_seconds = 10.0;
    input.now_seconds = 10.01;
    input.screen_center_x = 320.0f;
    input.screen_center_y = 256.0f;
    input.has_body_box = true;
    input.body_x1 = 120.0f;
    input.body_y1 = 120.0f;
    input.body_x2 = 520.0f;
    input.body_y2 = 320.0f;

    const controller_native::NativeAiAimOutput output = ai_aim.compute(input);
    require_true(output.has_assist, "body-lock target should produce assist");
    require_near(
        output.assist_x,
        0.0f,
        0.001f,
        "body-lock should use upper-body x, not the generic target dx");
    require_near(
        output.assist_y,
        -0.18f,
        0.001f,
        "body-lock should preserve selector target y instead of recomputing from box height");
}

void test_body_lock_suppresses_harmful_manual_input_after_confidence_builds() {
    controller_native::GamepadAiAimConfig config;
    config.max_pixels = 100.0f;
    config.target_max_age_ms = 0.0f;
    config.body_lock_max_ai_force = 1.0f;
    config.body_lock_max_ai_force_y = 1.0f;
    config.body_lock_box_tolerance_px = 20.0f;
    config.body_lock_activation_box_px = 180.0f;
    config.body_lock_confidence_frames = 4;
    config.body_lock_confidence_min_strong = 0.65f;
    config.body_lock_opposing_suppression_max = 0.90f;
    controller_native::NativeAiAim ai_aim(config);

    controller_native::NativeAiAimInput input;
    input.aiming = true;
    input.has_target = true;
    input.aim_authority = true;
    input.ads_snap_active = false;
    input.target_tier = "strong";
    input.observed_at_seconds = 10.0;
    input.screen_center_x = 320.0f;
    input.screen_center_y = 256.0f;
    input.has_body_box = true;
    input.body_x1 = 315.0f;
    input.body_y1 = 210.0f;
    input.body_x2 = 385.0f;
    input.body_y2 = 330.0f;

    for (int index = 0; index < 4; ++index) {
        input.now_seconds = 10.00 + (static_cast<double>(index) * 0.02);
        input.manual_right_x = 0.0f;
        ai_aim.compute(input);
    }

    input.now_seconds = 10.08;
    input.manual_right_x = -0.36f;
    const controller_native::NativeAiAimOutput output = ai_aim.compute(input);
    const float final_x = input.manual_right_x + output.assist_x;
    require_true(
        final_x > 0.0f,
        "body-lock should suppress harmful manual input once confidence is high");
    require_true(
        final_x < 0.36f,
        "body-lock suppression should not turn harmful manual input into a full-force snap");
}

void test_body_lock_preserves_strong_manual_escape_input() {
    controller_native::GamepadAiAimConfig config;
    config.max_pixels = 100.0f;
    config.target_max_age_ms = 0.0f;
    config.piecewise_mid_pixels = 0.0f;
    config.piecewise_mid_pixels_y = 0.0f;
    config.deadzone_inner = 0.0f;
    config.deadzone_outer = 1.0f;
    config.x_deadzone_outer = 1.0f;
    config.body_lock_max_ai_force = 0.52f;
    config.body_lock_max_ai_force_y = 0.48f;
    config.body_lock_box_tolerance_px = 20.0f;
    config.body_lock_activation_box_px = 192.0f;
    config.body_lock_confidence_frames = 3;
    config.body_lock_confidence_min_strong = 0.42f;
    config.body_lock_opposing_suppression_max = 0.58f;
    config.body_lock_orthogonal_suppression_max = 0.45f;
    config.body_lock_manual_overlap_scale = 0.60f;
    config.body_lock_near_lock_error_px = 36.0f;
    config.body_lock_smoothing = 0.0f;
    controller_native::NativeAiAim ai_aim(config);

    controller_native::NativeAiAimInput input;
    input.aiming = true;
    input.has_target = true;
    input.aim_authority = true;
    input.ads_snap_active = false;
    input.target_tier = "strong";
    input.observed_at_seconds = 10.0;
    input.screen_center_x = 320.0f;
    input.screen_center_y = 256.0f;
    input.has_body_box = true;
    input.body_x1 = 316.0f;
    input.body_y1 = 170.0f;
    input.body_x2 = 372.0f;
    input.body_y2 = 338.0f;

    for (int index = 0; index < 3; ++index) {
        input.now_seconds = 10.00 + (static_cast<double>(index) * 0.02);
        input.manual_right_x = 0.0f;
        ai_aim.compute(input);
    }

    input.now_seconds = 10.08;
    input.manual_right_x = -0.50f;
    const controller_native::NativeAiAimOutput output = ai_aim.compute(input);
    const float final_x = input.manual_right_x + output.assist_x;
    require_true(
        final_x < 0.0f,
        "strong manual escape should keep its direction instead of being pinned by body-lock");
    require_true(
        std::fabs(final_x) >= std::fabs(input.manual_right_x) * 0.55f,
        "strong manual escape should preserve at least 55% of the commanded stick");
}

void test_body_lock_counts_aligned_manual_input_as_planned_correction_near_lock() {
    controller_native::GamepadAiAimConfig config;
    config.max_pixels = 100.0f;
    config.target_max_age_ms = 0.0f;
    config.piecewise_mid_pixels = 0.0f;
    config.piecewise_mid_pixels_y = 0.0f;
    config.deadzone_inner = 0.0f;
    config.deadzone_outer = 1.0f;
    config.x_deadzone_outer = 1.0f;
    config.body_lock_max_ai_force = 1.0f;
    config.body_lock_max_ai_force_y = 1.0f;
    config.body_lock_box_tolerance_px = 20.0f;
    config.body_lock_activation_box_px = 180.0f;
    config.body_lock_confidence_frames = 1;
    config.body_lock_manual_overlap_scale = 1.0f;
    config.body_lock_near_lock_error_px = 18.0f;
    config.body_lock_smoothing = 0.0f;
    controller_native::NativeAiAim ai_aim(config);

    controller_native::NativeAiAimInput input;
    input.aiming = true;
    input.has_target = true;
    input.aim_authority = true;
    input.ads_snap_active = false;
    input.target_tier = "strong";
    input.observed_at_seconds = 10.0;
    input.now_seconds = 10.01;
    input.screen_center_x = 320.0f;
    input.screen_center_y = 256.0f;
    input.has_body_box = true;
    input.body_x1 = 293.0f;
    input.body_y1 = 208.0f;
    input.body_x2 = 363.0f;
    input.body_y2 = 328.0f;
    input.manual_right_x = 0.04f;

    const controller_native::NativeAiAimOutput output = ai_aim.compute(input);
    const float final_x = input.manual_right_x + output.assist_x;
    require_true(
        final_x < 0.12f,
        "body-lock manual overlap should reduce the old stacked correction");
    require_near(
        final_x,
        0.08f,
        0.002f,
        "the unified manual-priority curve should credit aligned manual without stacking");
}

void test_body_lock_damps_orthogonal_manual_input_near_lock() {
    controller_native::GamepadAiAimConfig config;
    config.max_pixels = 100.0f;
    config.target_max_age_ms = 0.0f;
    config.piecewise_mid_pixels = 0.0f;
    config.piecewise_mid_pixels_y = 0.0f;
    config.deadzone_inner = 0.0f;
    config.deadzone_outer = 1.0f;
    config.x_deadzone_outer = 1.0f;
    config.body_lock_max_ai_force = 1.0f;
    config.body_lock_max_ai_force_y = 1.0f;
    config.body_lock_box_tolerance_px = 20.0f;
    config.body_lock_activation_box_px = 180.0f;
    config.body_lock_confidence_frames = 1;
    config.body_lock_orthogonal_suppression_max = 0.75f;
    config.body_lock_vertical_orthogonal_bias = 1.15f;
    config.body_lock_near_lock_error_px = 18.0f;
    controller_native::NativeAiAim ai_aim(config);

    controller_native::NativeAiAimInput input;
    input.aiming = true;
    input.has_target = true;
    input.aim_authority = true;
    input.ads_snap_active = false;
    input.target_tier = "strong";
    input.observed_at_seconds = 10.0;
    input.now_seconds = 10.01;
    input.screen_center_x = 320.0f;
    input.screen_center_y = 256.0f;
    input.has_body_box = true;
    input.body_x1 = 293.0f;
    input.body_y1 = 208.0f;
    input.body_x2 = 363.0f;
    input.body_y2 = 328.0f;
    input.manual_right_y = 0.24f;

    const controller_native::NativeAiAimOutput output = ai_aim.compute(input);
    const float final_y = input.manual_right_y + output.assist_y;
    require_true(
        final_y < 0.08f,
        "body-lock should damp orthogonal manual input near lock");
    require_true(
        output.assist_y < 0.0f,
        "body-lock should emit a counter-delta for damped orthogonal manual input");
}

void test_body_lock_restores_vertical_tail_help_after_continuous_target_history() {
    controller_native::GamepadAiAimConfig config;
    config.max_pixels = 100.0f;
    config.target_max_age_ms = 0.0f;
    config.piecewise_mid_pixels = 0.0f;
    config.piecewise_mid_pixels_y = 0.0f;
    config.deadzone_inner = 0.0f;
    config.deadzone_outer = 1.0f;
    config.x_deadzone_outer = 1.0f;
    config.body_lock_max_ai_force = 1.0f;
    config.body_lock_max_ai_force_y = 1.0f;
    config.body_lock_box_tolerance_px = 20.0f;
    config.body_lock_activation_box_px = 180.0f;
    config.body_lock_vertical_deadzone_px = 6.0f;
    config.body_lock_vertical_tail_inner_px = 2.0f;
    config.body_lock_vertical_tail_speed_threshold_px_per_sec = 90.0f;
    controller_native::NativeAiAim ai_aim(config);

    controller_native::NativeAiAimInput input;
    input.aiming = true;
    input.has_target = true;
    input.aim_authority = true;
    input.ads_snap_active = false;
    input.target_tier = "strong";
    input.observed_at_seconds = 10.0;
    input.now_seconds = 10.00;
    input.screen_center_x = 320.0f;
    input.screen_center_y = 256.0f;
    input.has_body_box = true;
    input.body_x1 = 290.0f;
    input.body_y1 = 180.0f;
    input.body_x2 = 370.0f;
    input.body_y2 = 360.0f;

    const controller_native::NativeAiAimOutput first = ai_aim.compute(input);
    input.now_seconds = 10.02;
    const controller_native::NativeAiAimOutput second = ai_aim.compute(input);

    require_near(
        first.assist_y,
        0.0f,
        0.001f,
        "body-lock should suppress first-frame small vertical tail help");
    require_true(
        second.assist_y > 0.008f,
        "body-lock should restore some vertical tail help after target history exists, actual=" +
            std::to_string(second.assist_y));
}

void test_body_lock_manual_priority_has_no_commit_or_release_timer() {
    controller_native::GamepadAiAimConfig config;
    config.max_pixels = 100.0f;
    config.target_max_age_ms = 0.0f;
    config.piecewise_mid_pixels = 0.0f;
    config.piecewise_mid_pixels_y = 0.0f;
    config.deadzone_inner = 0.0f;
    config.deadzone_outer = 1.0f;
    config.x_deadzone_outer = 1.0f;
    config.body_lock_max_ai_force = 0.50f;
    config.body_lock_max_ai_force_y = 0.0f;
    config.body_lock_box_tolerance_px = 20.0f;
    config.body_lock_activation_box_px = 180.0f;
    config.body_lock_confidence_frames = 1;
    controller_native::NativeAiAim ai_aim(config);

    controller_native::NativeAiAimInput input;
    input.aiming = true;
    input.has_target = true;
    input.aim_authority = true;
    input.ads_snap_active = false;
    input.target_tier = "strong";
    input.observed_at_seconds = 10.0;
    input.now_seconds = 10.00;
    input.screen_center_x = 320.0f;
    input.screen_center_y = 256.0f;
    input.has_body_box = true;
    input.body_x1 = 310.0f;
    input.body_x2 = 390.0f;
    input.body_y1 = 216.0f;
    input.body_y2 = 296.0f;
    input.manual_right_x = -0.60f;

    const controller_native::NativeAiAimOutput escape = ai_aim.compute(input);
    require_near(
        escape.assist_x,
        0.0f,
        0.001f,
        "strong opposing manual input should receive current-tick priority");
    require_true(
        ai_aim.manual_takeover_active(),
        "manual-priority diagnostics should reflect the current strong escape tick");

    input.now_seconds += 0.001;
    input.manual_right_x = 0.0f;
    const controller_native::NativeAiAimOutput released = ai_aim.compute(input);
    require_true(
        released.assist_x > 0.10f,
        "releasing manual input must restore planning on the next tick without a timer");
    require_true(
        !ai_aim.manual_takeover_active(),
        "manual-priority diagnostics must clear on the next non-escape tick");
}

void test_body_lock_motion_updates_once_per_fresh_sequence() {
    controller_native::GamepadAiAimConfig config;
    config.body_lock_activation_box_px = 180.0f;
    controller_native::BodyLockMotionPolicy motion(config);

    controller_native::BodyLockMotionObservation observation;
    observation.strong_observation = true;
    observation.has_body_box = true;
    observation.body_x1 = 280.0f;
    observation.body_y1 = 180.0f;
    observation.body_x2 = 360.0f;
    observation.body_y2 = 360.0f;
    observation.fresh_observation = true;
    observation.vision_sequence = 1;
    observation.selected_track_id = 42;
    observation.observed_at_seconds = 10.000;
    observation.now_seconds = 10.000;
    motion.observe(observation);

    for (int tick = 1; tick < 10; ++tick) {
        observation.fresh_observation = false;
        observation.now_seconds = 10.000 + (static_cast<double>(tick) * 0.001);
        motion.observe(observation);
    }
    require_true(
        motion.motion_frames() == 1,
        "duplicate controller ticks must not become motion observations");

    observation.fresh_observation = true;
    observation.vision_sequence = 2;
    observation.observed_at_seconds = 10.010;
    observation.now_seconds = 10.010;
    observation.body_x1 += 10.0f;
    observation.body_x2 += 10.0f;
    motion.observe(observation);
    require_true(
        motion.motion_frames() == 2,
        "the next fresh sequence must produce exactly one motion observation");
    require_near(
        motion.velocity_px_per_sec().x,
        1000.0f,
        0.5f,
        "motion velocity must use the 10 ms observation interval");

    observation.now_seconds = 10.011;
    motion.observe(observation);
    require_true(
        motion.motion_frames() == 2,
        "repeated fresh flags with the same sequence must be deduplicated");
    require_near(
        motion.velocity_px_per_sec().x,
        1000.0f,
        0.5f,
        "a duplicate sequence must not overwrite velocity with zero");
}

void test_body_lock_warm_left_reversal_predicts_between_vision_frames() {
    controller_native::GamepadAiAimConfig config;
    config.body_lock_activation_box_px = 180.0f;
    config.body_lock_lead_frames = 2;
    config.body_lock_lead_seconds = 0.04f;
    config.body_lock_lead_max_px = 40.0f;
    controller_native::BodyLockMotionPolicy motion(config);

    controller_native::BodyLockMotionObservation observation;
    observation.strong_observation = true;
    observation.has_body_box = true;
    observation.body_y1 = 180.0f;
    observation.body_y2 = 360.0f;
    observation.fresh_observation = true;
    observation.selected_track_id = 42;

    const auto observe_fresh = [&](std::uint64_t sequence, double time, float center_x, float left_x) {
        observation.vision_sequence = sequence;
        observation.observed_at_seconds = time;
        observation.now_seconds = time;
        observation.body_x1 = center_x - 40.0f;
        observation.body_x2 = center_x + 40.0f;
        observation.left_x = left_x;
        motion.observe(observation);
    };

    observe_fresh(1, 10.000, 320.0f, 0.0f);
    observe_fresh(2, 10.010, 320.0f, 0.0f);
    observe_fresh(3, 10.020, 315.0f, 0.8f);
    observe_fresh(4, 10.030, 310.0f, 0.8f);
    observe_fresh(5, 10.040, 305.0f, 0.8f);
    const float learned_lead = motion.lead_delta().x;
    require_true(
        learned_lead < -5.0f,
        "fresh observations must establish right-strafe relative motion");
    require_true(
        motion.relative_motion_estimate().state ==
            controller_native::RelativeMotionState::Warm,
        "three compatible fresh strafe samples must warm the in-memory mobility prior");

    observation.fresh_observation = false;
    observation.left_x = -0.8f;
    observation.now_seconds = 10.041;
    motion.observe(observation);
    const float predicted_reversal_lead = motion.lead_delta().x;
    require_true(
        predicted_reversal_lead > 5.0f,
        "a warm left-stick reversal must predict relative motion before the next vision frame");
}

void test_body_lock_gain_uses_camera_attributed_tracker_velocity() {
    controller_native::GamepadAiAimConfig config;
    config.body_lock_activation_box_px = 180.0f;
    config.body_lock_lead_frames = 2;
    config.body_lock_lead_seconds = 0.04f;
    config.body_lock_lead_max_px = 40.0f;
    controller_native::BodyLockMotionPolicy motion(config);

    controller_native::BodyLockMotionObservation observation;
    observation.strong_observation = true;
    observation.has_body_box = true;
    observation.body_y1 = 180.0f;
    observation.body_y2 = 360.0f;
    observation.fresh_observation = true;
    observation.selected_track_id = 42;
    observation.has_camera_attributed_velocity = true;

    const auto observe_fresh = [&](std::uint64_t sequence,
                                   double time,
                                   float center_x,
                                   float left_x,
                                   float attributed_velocity_x) {
        observation.vision_sequence = sequence;
        observation.observed_at_seconds = time;
        observation.now_seconds = time;
        observation.body_x1 = center_x - 40.0f;
        observation.body_x2 = center_x + 40.0f;
        observation.left_x = left_x;
        observation.camera_attributed_velocity_x_px_per_sec = attributed_velocity_x;
        motion.observe(observation);
    };

    observe_fresh(1, 15.000, 320.0f, 0.0f, 0.0f);
    observe_fresh(2, 15.010, 320.0f, 0.0f, 0.0f);
    observe_fresh(3, 15.020, 310.0f, 0.8f, -500.0f);
    observe_fresh(4, 15.030, 300.0f, 0.8f, -500.0f);
    observe_fresh(5, 15.040, 290.0f, 0.8f, -500.0f);

    const controller_native::RelativeMotionEstimate estimate =
        motion.relative_motion_estimate();
    require_true(
        estimate.state == controller_native::RelativeMotionState::Warm,
        "camera-attributed samples must still warm the mobility prior");
    require_true(
        estimate.strafe_gain > 2.5f && estimate.strafe_gain < 4.5f,
        "mobility gain must exclude AI-attributed camera displacement");
    require_near(
        estimate.lead_x_px,
        -20.0f,
        0.5f,
        "projected lead must use the camera-attributed relative velocity");
}

void test_body_lock_lead_projects_fresh_observation_latency() {
    controller_native::GamepadAiAimConfig config;
    config.body_lock_activation_box_px = 180.0f;
    config.body_lock_lead_seconds = 0.026f;
    config.body_lock_lead_max_px = 40.0f;
    controller_native::BodyLockMotionPolicy motion(config);

    controller_native::BodyLockMotionObservation observation;
    observation.strong_observation = true;
    observation.has_body_box = true;
    observation.body_x1 = 280.0f;
    observation.body_x2 = 360.0f;
    observation.body_y1 = 180.0f;
    observation.body_y2 = 340.0f;
    observation.selected_track_id = 77;
    observation.fresh_observation = true;
    observation.has_camera_attributed_velocity = true;
    observation.camera_attributed_velocity_x_px_per_sec = -160.0f;

    observation.vision_sequence = 1;
    observation.observed_at_seconds = 30.000;
    observation.now_seconds = 30.030;
    motion.observe(observation);
    observation.vision_sequence = 2;
    observation.observed_at_seconds = 30.010;
    observation.now_seconds = 30.040;
    motion.observe(observation);

    require_near(
        motion.relative_motion_estimate().lead_x_px,
        -8.96f,
        0.10f,
        "relative-motion lead must project the 30 ms observation latency plus its configured horizon");
}

void test_body_lock_cold_relative_motion_responds_within_eighty_ms() {
    controller_native::GamepadAiAimConfig config;
    config.body_lock_activation_box_px = 180.0f;
    config.body_lock_lead_frames = 2;
    config.body_lock_lead_seconds = 0.04f;
    config.body_lock_lead_max_px = 40.0f;
    controller_native::BodyLockMotionPolicy motion(config);

    controller_native::BodyLockMotionObservation observation;
    observation.strong_observation = true;
    observation.has_body_box = true;
    observation.body_y1 = 180.0f;
    observation.body_y2 = 360.0f;
    observation.fresh_observation = true;
    observation.selected_track_id = 42;

    const auto observe_fresh = [&](std::uint64_t sequence, double time, float center_x, float left_x) {
        observation.vision_sequence = sequence;
        observation.observed_at_seconds = time;
        observation.now_seconds = time;
        observation.body_x1 = center_x - 40.0f;
        observation.body_x2 = center_x + 40.0f;
        observation.left_x = left_x;
        motion.observe(observation);
    };

    observe_fresh(1, 20.000, 320.0f, 0.0f);
    observe_fresh(2, 20.010, 320.0f, 0.0f);
    const double onset_at = 20.010;
    observe_fresh(3, 20.020, 315.0f, 0.8f);

    require_true(
        std::fabs(motion.lead_delta().x) > 0.1f,
        "cold relative motion must produce conservative measured feed-forward");
    require_true(
        (20.020 - onset_at) * 1000.0 <= 80.0,
        "cold relative-motion response must begin within 80 ms");
}

void test_body_lock_learns_from_delayed_strafe_acceleration_inside_event_window() {
    controller_native::GamepadAiAimConfig config;
    config.body_lock_activation_box_px = 180.0f;
    controller_native::BodyLockMotionPolicy motion(config);

    controller_native::BodyLockMotionObservation observation;
    observation.strong_observation = true;
    observation.has_body_box = true;
    observation.body_y1 = 180.0f;
    observation.body_y2 = 340.0f;
    observation.selected_track_id = 55;
    observation.fresh_observation = true;
    observation.has_camera_attributed_velocity = true;
    const auto observe = [&](std::uint64_t sequence,
                             double time,
                             float left_x,
                             float relative_velocity_x) {
        observation.vision_sequence = sequence;
        observation.observed_at_seconds = time;
        observation.now_seconds = time;
        observation.left_x = left_x;
        observation.camera_attributed_velocity_x_px_per_sec =
            relative_velocity_x;
        observation.body_x1 = 280.0f;
        observation.body_x2 = 360.0f;
        motion.observe(observation);
    };

    observe(1, 20.000, 0.0f, 0.0f);
    observe(2, 20.010, 0.0f, 0.0f);
    observe(3, 20.020, 0.8f, -3.0f);
    require_true(
        motion.relative_motion_estimate().state ==
            controller_native::RelativeMotionState::Cold,
        "the first low-information acceleration sample should remain cold");
    observe(4, 20.030, 0.8f, -35.0f);
    observe(5, 20.040, 0.8f, -75.0f);
    observe(6, 20.050, 0.8f, -120.0f);

    const controller_native::RelativeMotionEstimate estimate =
        motion.relative_motion_estimate();
    require_true(
        estimate.state == controller_native::RelativeMotionState::Warm,
        "delayed velocity response must warm from the same left-input event within 80 ms");
    require_true(
        estimate.strafe_gain > 0.5f,
        "the delayed acceleration event must produce a useful in-memory mobility gain");
}

void test_body_lock_relative_motion_distinguishes_synchronized_and_opposite_motion() {
    const auto lead_after_next_motion = [](float next_center_x) {
        controller_native::GamepadAiAimConfig config;
        config.body_lock_activation_box_px = 180.0f;
        config.body_lock_lead_frames = 2;
        config.body_lock_lead_seconds = 0.04f;
        config.body_lock_lead_max_px = 40.0f;
        controller_native::BodyLockMotionPolicy motion(config);

        controller_native::BodyLockMotionObservation observation;
        observation.strong_observation = true;
        observation.has_body_box = true;
        observation.body_y1 = 180.0f;
        observation.body_y2 = 360.0f;
        observation.fresh_observation = true;
        observation.selected_track_id = 42;

        const auto observe_fresh = [&](std::uint64_t sequence, double time, float center_x, float left_x) {
            observation.vision_sequence = sequence;
            observation.observed_at_seconds = time;
            observation.now_seconds = time;
            observation.body_x1 = center_x - 40.0f;
            observation.body_x2 = center_x + 40.0f;
            observation.left_x = left_x;
            motion.observe(observation);
        };

        observe_fresh(1, 30.000, 320.0f, 0.0f);
        observe_fresh(2, 30.010, 320.0f, 0.0f);
        observe_fresh(3, 30.020, 315.0f, 0.8f);
        observe_fresh(4, 30.030, 310.0f, 0.8f);
        observe_fresh(5, 30.040, 305.0f, 0.8f);
        observe_fresh(6, 30.050, next_center_x, 0.8f);
        return motion.lead_delta().x;
    };

    const float synchronized_lead = lead_after_next_motion(305.0f);
    const float same_direction_lead = lead_after_next_motion(303.0f);
    const float opposite_direction_lead = lead_after_next_motion(297.0f);

    require_near(
        synchronized_lead,
        0.0f,
        0.02f,
        "synchronized player and target motion must not invent feed-forward");
    require_true(
        std::fabs(opposite_direction_lead) > std::fabs(same_direction_lead),
        "opposite relative motion must produce more feed-forward than same-direction motion");
}

void test_body_lock_rejects_changed_mobility_on_left_reversal_and_reset_clears_prior() {
    controller_native::GamepadAiAimConfig config;
    config.body_lock_activation_box_px = 180.0f;
    config.body_lock_lead_frames = 2;
    config.body_lock_lead_seconds = 0.04f;
    config.body_lock_lead_max_px = 40.0f;
    controller_native::BodyLockMotionPolicy motion(config);

    controller_native::BodyLockMotionObservation observation;
    observation.strong_observation = true;
    observation.has_body_box = true;
    observation.body_y1 = 180.0f;
    observation.body_y2 = 360.0f;
    observation.fresh_observation = true;
    observation.selected_track_id = 42;

    const auto observe_fresh = [&](std::uint64_t sequence, double time, float center_x, float left_x) {
        observation.vision_sequence = sequence;
        observation.observed_at_seconds = time;
        observation.now_seconds = time;
        observation.body_x1 = center_x - 40.0f;
        observation.body_x2 = center_x + 40.0f;
        observation.left_x = left_x;
        motion.observe(observation);
    };

    observe_fresh(1, 40.000, 320.0f, 0.0f);
    observe_fresh(2, 40.010, 320.0f, 0.0f);
    observe_fresh(3, 40.020, 315.0f, 0.8f);
    observe_fresh(4, 40.030, 310.0f, 0.8f);
    observe_fresh(5, 40.040, 305.0f, 0.8f);
    require_true(
        motion.relative_motion_estimate().state ==
            controller_native::RelativeMotionState::Warm,
        "test setup must establish a warm mobility prior");

    observe_fresh(6, 40.050, 306.0f, -0.8f);
    require_true(
        motion.relative_motion_estimate().state ==
            controller_native::RelativeMotionState::Rejected,
        "a high-information left reversal must reject an incompatible mobility prior immediately");

    motion.reset();
    const controller_native::RelativeMotionEstimate reset_estimate =
        motion.relative_motion_estimate();
    require_true(
        reset_estimate.state == controller_native::RelativeMotionState::Cold,
        "controller reset must return the mobility prior to cold");
    require_near(reset_estimate.strafe_gain, 0.0f, 0.0001f, "reset must clear learned gain");
    require_near(reset_estimate.confidence, 0.0f, 0.0001f, "reset must clear gain confidence");
}

void test_body_lock_uses_lateral_motion_when_current_error_is_inside_deadzone() {
    controller_native::GamepadAiAimConfig config;
    config.max_pixels = 100.0f;
    config.target_max_age_ms = 0.0f;
    config.piecewise_mid_pixels = 0.0f;
    config.piecewise_mid_pixels_y = 0.0f;
    config.deadzone_inner = 1.5f;
    config.deadzone_outer = 5.0f;
    config.x_deadzone_outer = 3.0f;
    config.body_lock_max_ai_force = 1.0f;
    config.body_lock_max_ai_force_y = 1.0f;
    config.body_lock_box_tolerance_px = 20.0f;
    config.body_lock_activation_box_px = 180.0f;
    config.body_lock_lateral_motion_min_speed_px_per_sec = 120.0f;
    config.body_lock_lateral_motion_lead_seconds = 0.04f;
    config.body_lock_lateral_motion_lead_window_px = 8.0f;
    config.body_lock_lateral_motion_lead_max_px = 7.0f;
    config.body_lock_smoothing = 0.0f;
    controller_native::NativeAiAim ai_aim(config);

    controller_native::NativeAiAimInput input;
    input.aiming = true;
    input.has_target = true;
    input.aim_authority = true;
    input.ads_snap_active = false;
    input.target_tier = "strong";
    input.observed_at_seconds = 10.0;
    input.now_seconds = 10.00;
    input.screen_center_x = 320.0f;
    input.screen_center_y = 256.0f;
    input.has_body_box = true;
    input.body_x1 = 296.0f;
    input.body_y1 = 180.0f;
    input.body_x2 = 376.0f;
    input.body_y2 = 360.0f;
    ai_aim.compute(input);

    input.now_seconds = 10.10;
    input.body_x1 = 280.8f;
    input.body_x2 = 360.8f;
    const controller_native::NativeAiAimOutput output = ai_aim.compute(input);

    require_true(
        output.assist_x < -0.01f,
        "body-lock should lead against lateral target motion inside the release window");
}

float body_lock_assist_after_motion_sequence(
    controller_native::GamepadAiAimConfig config,
    const std::vector<float>& center_offsets,
    const std::string& target_tier = "strong") {
    controller_native::NativeAiAim ai_aim(config);

    controller_native::NativeAiAimInput input;
    input.aiming = true;
    input.has_target = true;
    input.aim_authority = true;
    input.ads_snap_active = false;
    input.target_tier = target_tier;
    input.observed_at_seconds = 10.0;
    input.screen_center_x = 320.0f;
    input.screen_center_y = 256.0f;
    input.has_body_box = true;
    input.body_y1 = 180.0f;
    input.body_y2 = 360.0f;

    controller_native::NativeAiAimOutput output;
    for (std::size_t index = 0; index < center_offsets.size(); ++index) {
        input.now_seconds = 10.00 + (static_cast<double>(index) * 0.01);
        const float center_x = 320.0f + center_offsets[index];
        input.body_x1 = center_x - 40.0f;
        input.body_x2 = center_x + 40.0f;
        output = ai_aim.compute(input);
    }
    return output.assist_x;
}

void test_body_lock_sustained_motion_lead_increases_follow_after_consistent_strong_history() {
    controller_native::GamepadAiAimConfig baseline_config;
    baseline_config.max_pixels = 100.0f;
    baseline_config.target_max_age_ms = 0.0f;
    baseline_config.piecewise_mid_pixels = 0.0f;
    baseline_config.piecewise_mid_pixels_y = 0.0f;
    baseline_config.deadzone_inner = 0.0f;
    baseline_config.deadzone_outer = 1.0f;
    baseline_config.x_deadzone_outer = 1.0f;
    baseline_config.body_lock_max_ai_force = 1.0f;
    baseline_config.body_lock_max_ai_force_y = 0.0f;
    baseline_config.body_lock_box_tolerance_px = 80.0f;
    baseline_config.body_lock_activation_box_px = 240.0f;
    baseline_config.body_lock_smoothing = 0.0f;
    baseline_config.body_lock_lateral_motion_min_speed_px_per_sec = 40.0f;
    baseline_config.body_lock_lead_frames = 3;
    baseline_config.body_lock_lead_max_px = 10.0f;
    baseline_config.body_lock_lead_seconds = 0.0f;

    controller_native::GamepadAiAimConfig lead_config = baseline_config;
    lead_config.body_lock_lead_seconds = 0.012f;

    const std::vector<float> offsets = {2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
    const float baseline = body_lock_assist_after_motion_sequence(baseline_config, offsets);
    const float lead = body_lock_assist_after_motion_sequence(lead_config, offsets);

    std::ostringstream message;
    message << "sustained strong same-direction motion should increase body-lock follow assist; baseline="
            << baseline << " lead=" << lead;
    require_true(
        lead > baseline + 0.004f,
        message.str());
}

void test_body_lock_sustained_motion_lead_clears_on_direction_reversal() {
    controller_native::GamepadAiAimConfig baseline_config;
    baseline_config.max_pixels = 100.0f;
    baseline_config.target_max_age_ms = 0.0f;
    baseline_config.piecewise_mid_pixels = 0.0f;
    baseline_config.piecewise_mid_pixels_y = 0.0f;
    baseline_config.deadzone_inner = 0.0f;
    baseline_config.deadzone_outer = 1.0f;
    baseline_config.x_deadzone_outer = 1.0f;
    baseline_config.body_lock_max_ai_force = 1.0f;
    baseline_config.body_lock_max_ai_force_y = 0.0f;
    baseline_config.body_lock_box_tolerance_px = 80.0f;
    baseline_config.body_lock_activation_box_px = 240.0f;
    baseline_config.body_lock_smoothing = 0.0f;
    baseline_config.body_lock_lateral_motion_min_speed_px_per_sec = 40.0f;
    baseline_config.body_lock_lead_frames = 3;
    baseline_config.body_lock_lead_max_px = 10.0f;
    baseline_config.body_lock_lead_seconds = 0.0f;

    controller_native::GamepadAiAimConfig lead_config = baseline_config;
    lead_config.body_lock_lead_seconds = 0.012f;

    const std::vector<float> offsets = {2.0f, 3.0f, 4.0f, 5.0f, 4.0f};
    const float baseline = body_lock_assist_after_motion_sequence(baseline_config, offsets);
    const float lead = body_lock_assist_after_motion_sequence(lead_config, offsets);

    require_near(
        lead,
        baseline,
        0.004f,
        "direction reversal should clear sustained-motion lead before it pulls the wrong way");
}

void test_body_lock_sustained_motion_lead_ignores_weak_targets() {
    controller_native::GamepadAiAimConfig baseline_config;
    baseline_config.max_pixels = 100.0f;
    baseline_config.target_max_age_ms = 0.0f;
    baseline_config.piecewise_mid_pixels = 0.0f;
    baseline_config.piecewise_mid_pixels_y = 0.0f;
    baseline_config.deadzone_inner = 0.0f;
    baseline_config.deadzone_outer = 1.0f;
    baseline_config.x_deadzone_outer = 1.0f;
    baseline_config.body_lock_max_ai_force = 1.0f;
    baseline_config.body_lock_max_ai_force_y = 0.0f;
    baseline_config.body_lock_box_tolerance_px = 80.0f;
    baseline_config.body_lock_activation_box_px = 240.0f;
    baseline_config.body_lock_smoothing = 0.0f;
    baseline_config.body_lock_lateral_motion_min_speed_px_per_sec = 40.0f;
    baseline_config.body_lock_lead_frames = 3;
    baseline_config.body_lock_lead_max_px = 10.0f;
    baseline_config.body_lock_lead_seconds = 0.0f;

    controller_native::GamepadAiAimConfig lead_config = baseline_config;
    lead_config.body_lock_lead_seconds = 0.012f;

    const std::vector<float> offsets = {2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
    const float baseline = body_lock_assist_after_motion_sequence(
        baseline_config,
        offsets,
        "associated_weak");
    const float lead = body_lock_assist_after_motion_sequence(
        lead_config,
        offsets,
        "associated_weak");

    require_near(
        lead,
        baseline,
        0.004f,
        "weak targets should not use sustained-motion lead");
}

void test_body_lock_can_disable_release_tail_to_zero_x_axis_inside_window() {
    controller_native::GamepadAiAimConfig config;
    config.max_pixels = 100.0f;
    config.target_max_age_ms = 0.0f;
    config.piecewise_mid_pixels = 0.0f;
    config.piecewise_mid_pixels_y = 0.0f;
    config.deadzone_inner = 1.5f;
    config.deadzone_outer = 5.0f;
    config.x_deadzone_outer = 3.0f;
    config.body_lock_max_ai_force = 1.0f;
    config.body_lock_max_ai_force_y = 1.0f;
    config.body_lock_box_tolerance_px = 20.0f;
    config.body_lock_activation_box_px = 180.0f;
    config.body_lock_release_tail_scale = 0.0f;
    controller_native::NativeAiAim ai_aim(config);

    controller_native::NativeAiAimInput input;
    input.aiming = true;
    input.has_target = true;
    input.aim_authority = true;
    input.ads_snap_active = false;
    input.target_tier = "strong";
    input.observed_at_seconds = 10.0;
    input.now_seconds = 10.00;
    input.screen_center_x = 320.0f;
    input.screen_center_y = 256.0f;
    input.has_body_box = true;
    input.body_x1 = 282.1f;
    input.body_y1 = 156.0f;
    input.body_x2 = 362.1f;
    input.body_y2 = 336.0f;

    const controller_native::NativeAiAimOutput output = ai_aim.compute(input);
    require_near(
        output.assist_x,
        0.0f,
        0.001f,
        "body-lock should clear x assist when release tail scale is disabled");
    require_true(
        output.assist_y > 0.10f,
        "body-lock should keep y assist when only x is inside the release window");
}

float body_lock_tail_output_for_pair(float first_dx, float second_dx) {
    controller_native::GamepadAiAimConfig config;
    config.max_pixels = 100.0f;
    config.target_max_age_ms = 0.0f;
    config.piecewise_mid_pixels = 0.0f;
    config.piecewise_mid_pixels_y = 0.0f;
    config.deadzone_inner = 1.5f;
    config.deadzone_outer = 5.0f;
    config.x_deadzone_outer = 3.0f;
    config.body_lock_max_ai_force = 1.0f;
    config.body_lock_max_ai_force_y = 1.0f;
    config.body_lock_box_tolerance_px = 20.0f;
    config.body_lock_activation_box_px = 180.0f;
    config.body_lock_smoothing = 0.18f;
    config.body_lock_release_tail_scale = 0.20f;
    config.body_lock_lateral_motion_min_speed_px_per_sec = 120.0f;
    config.body_lock_lateral_motion_lead_seconds = 0.0f;
    config.body_lock_lateral_motion_tail_scale = 0.65f;
    controller_native::NativeAiAim ai_aim(config);

    controller_native::NativeAiAimInput input;
    input.aiming = true;
    input.has_target = true;
    input.aim_authority = true;
    input.ads_snap_active = false;
    input.target_tier = "strong";
    input.observed_at_seconds = 10.0;
    input.now_seconds = 10.00;
    input.screen_center_x = 320.0f;
    input.screen_center_y = 256.0f;
    input.has_body_box = true;
    input.body_y1 = 156.0f;
    input.body_y2 = 336.0f;
    input.body_x1 = 280.0f + first_dx;
    input.body_x2 = 360.0f + first_dx;
    ai_aim.compute(input);

    input.now_seconds = 10.10;
    input.body_x1 = 280.0f + second_dx;
    input.body_x2 = 360.0f + second_dx;
    return ai_aim.compute(input).assist_x;
}

void test_body_lock_preserves_more_horizontal_tail_for_moving_target_inside_release_window() {
    const float static_tail = body_lock_tail_output_for_pair(2.1f, 2.1f);
    const float moving_tail = body_lock_tail_output_for_pair(14.1f, 2.1f);

    require_true(static_tail > 0.0f, "static body-lock release tail should be positive");
    require_true(
        moving_tail > static_tail * 1.8f,
        "moving body-lock target should preserve more horizontal tail inside release window");
}

void test_body_lock_clears_release_tail_carry_on_near_zero_x_sign_flip() {
    controller_native::GamepadAiAimConfig config;
    config.max_pixels = 100.0f;
    config.target_max_age_ms = 0.0f;
    config.piecewise_mid_pixels = 0.0f;
    config.piecewise_mid_pixels_y = 0.0f;
    config.deadzone_inner = 1.5f;
    config.deadzone_outer = 5.0f;
    config.x_deadzone_outer = 3.0f;
    config.body_lock_max_ai_force = 1.0f;
    config.body_lock_max_ai_force_y = 1.0f;
    config.body_lock_box_tolerance_px = 20.0f;
    config.body_lock_activation_box_px = 180.0f;
    config.body_lock_smoothing = 0.18f;
    controller_native::NativeAiAim ai_aim(config);

    controller_native::NativeAiAimInput input;
    input.aiming = true;
    input.has_target = true;
    input.aim_authority = true;
    input.ads_snap_active = false;
    input.target_tier = "strong";
    input.observed_at_seconds = 10.0;
    input.now_seconds = 10.00;
    input.screen_center_x = 320.0f;
    input.screen_center_y = 256.0f;
    input.has_body_box = true;
    input.body_y1 = 156.0f;
    input.body_y2 = 336.0f;
    input.body_x1 = 282.6f;
    input.body_x2 = 362.6f;
    ai_aim.compute(input);

    input.now_seconds = 10.10;
    input.body_x1 = 277.9f;
    input.body_x2 = 357.9f;
    const controller_native::NativeAiAimOutput output = ai_aim.compute(input);

    require_true(
        output.assist_x <= 0.0f,
        "body-lock should clear positive x carry when near-zero error flips sign");
    require_true(
        output.assist_x > -0.02f,
        "body-lock zero-cross guard should not snap hard across center");
}

void test_controller_body_lock_short_plan_damps_small_vector_turn_near_lock() {
    controller_native::GamepadRuntimeConfig config;
    config.ai_aim.target_max_age_ms = 0.0f;
    config.ai_aim.piecewise_mid_pixels = 0.0f;
    config.ai_aim.piecewise_mid_pixels_y = 0.0f;
    config.ai_aim.max_ai_force = 0.0f;
    config.ai_aim.max_ai_force_y = 0.0f;
    config.ai_aim.body_lock_max_ai_force = 0.0f;
    config.ai_aim.body_lock_max_ai_force_y = 0.0f;
    config.ai_aim.body_lock_box_tolerance_px = 20.0f;
    config.ai_aim.body_lock_activation_box_px = 180.0f;
    config.ai_aim.body_lock_upper_body_ratio = 0.50f;
    config.ai_aim.body_lock_confidence_frames = 1;
    config.ai_aim.body_lock_manual_escape_input_threshold = 1.0f;
    config.ai_aim.body_lock_near_lock_error_px = 32.0f;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.enabled = false;

    double now = 10.0;
    controller_native::NativeGamepadController controller(
        config,
        [&now]() { return now; });

    const auto submit_body_lock_target = [&]() {
        controller_native::NativeControllerVisionState target;
        target.has_target = true;
        target.aim_authority = true;
        target.fire_authority = true;
        target.target_tier = "strong";
        target.dx = 1.0f;
        target.dy = 0.0f;
        target.screen_center_x = 320.0f;
        target.screen_center_y = 256.0f;
        target.has_body_box = true;
        target.body_x1 = 281.0f;
        target.body_x2 = 361.0f;
        target.body_y1 = 166.0f;
        target.body_y2 = 346.0f;
        target.observed_at_seconds = now;
        controller.submit_vision_state(target);
    };
    auto physical_with_stick = [](float right_x, float right_y) {
        controller_native::PhysicalGamepadState physical = aiming_physical_state();
        physical.right_x = right_x;
        physical.right_y = right_y;
        return physical;
    };

    submit_body_lock_target();
    controller.build_output(physical_with_stick(0.07f, 0.0f));
    const auto first = controller.last_output_components().final_stick;
    require_true(first.x > 0.06f, "test setup should keep the first small body-lock x vector");

    now = 10.001;
    submit_body_lock_target();
    controller.build_output(physical_with_stick(0.0f, 0.07f));
    const auto second = controller.last_output_components().final_stick;
    require_true(
        std::fabs(second.x) <= 0.02f,
        "body-lock short plan should clear stale x carry on a small near-lock vector turn");
    require_true(
        second.y >= 0.03f && second.y <= 0.07f,
        "body-lock short plan should damp, not zero, the current small vector turn for moving targets");
}

void test_controller_body_lock_preserves_manual_after_target_crossing() {
    controller_native::GamepadRuntimeConfig config;
    // This test starts after ADS acquisition has already centered.
    config.ai_aim.ads_snap_window_ms = 0;
    config.ai_aim.target_max_age_ms = 0.0f;
    config.ai_aim.piecewise_mid_pixels = 0.0f;
    config.ai_aim.piecewise_mid_pixels_y = 0.0f;
    config.ai_aim.deadzone_inner = 0.0f;
    config.ai_aim.deadzone_outer = 1.0f;
    config.ai_aim.x_deadzone_outer = 1.0f;
    config.ai_aim.body_lock_smoothing = 0.0f;
    config.ai_aim.body_lock_max_ai_force = 0.42f;
    config.ai_aim.body_lock_opposing_boost_max_ai_force = 0.42f;
    config.ai_aim.body_lock_max_ai_force_y = 0.0f;
    config.ai_aim.body_lock_box_tolerance_px = 20.0f;
    config.ai_aim.body_lock_activation_box_px = 180.0f;
    config.ai_aim.body_lock_upper_body_ratio = 0.50f;
    config.ai_aim.body_lock_confidence_frames = 1;
    config.ai_aim.body_lock_manual_escape_input_threshold = 0.45f;
    config.ai_aim.body_lock_manual_escape_preservation = 0.55f;
    config.ai_aim.body_lock_near_lock_error_px = 32.0f;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.enabled = false;

    double now = 20.0;
    controller_native::NativeGamepadController controller(
        config,
        [&now]() { return now; });

    const auto submit_body_lock_target = [&](float lock_dx) {
        controller_native::NativeControllerVisionState target;
        target.has_target = true;
        target.aim_authority = true;
        target.fire_authority = true;
        target.target_tier = "strong";
        target.dx = lock_dx;
        target.dy = 0.0f;
        target.screen_center_x = 320.0f;
        target.screen_center_y = 256.0f;
        target.has_body_box = true;
        target.body_x1 = 280.0f + lock_dx;
        target.body_x2 = 360.0f + lock_dx;
        target.body_y1 = 216.0f;
        target.body_y2 = 296.0f;
        target.observed_at_seconds = now;
        controller.submit_vision_state(target);
    };
    auto strong_right_pull = []() {
        controller_native::PhysicalGamepadState physical = aiming_physical_state();
        physical.right_x = 0.92f;
        return physical;
    };

    submit_body_lock_target(4.0f);
    controller.build_output(strong_right_pull());

    now = 20.001;
    submit_body_lock_target(-4.0f);
    controller.build_output(strong_right_pull());
    const auto crossed = controller.last_output_components().final_stick;

    now = 20.021;
    submit_body_lock_target(-8.0f);
    controller.build_output(strong_right_pull());
    const auto& components = controller.last_output_components();
    const auto sustained = components.final_stick;

    if (!(sustained.x >= 0.80f)) {
        std::ostringstream out;
        out << "body-lock should preserve user-owned direction after takeover actual="
            << sustained.x << " crossed=" << crossed.x
            << " mode=" << components.aim_mode
            << " requested_ai=" << components.requested_assist_stick.x
            << " shaped_ai=" << components.shaped_assist_stick.x
            << " ads_brake=" << components.ads_brake_stick.x
            << " carry_brake=" << components.ads_carry_brake_stick.x
            << " lifecycle=" << components.bodylock_lifecycle
            << " limit=" << components.assist_limit_reason
            << " takeover=" << controller.body_lock_manual_takeover_active();
        throw std::runtime_error(out.str());
    }
    require_true(
        !components.ads_carry_brake_active,
        "ADS carry brake must not gain authority while body-lock owns tracking");
}

void test_controller_body_lock_preserves_helpful_manual_near_lock_edge() {
    controller_native::GamepadRuntimeConfig config;
    config.ai_aim.target_max_age_ms = 0.0f;
    config.ai_aim.piecewise_mid_pixels = 0.0f;
    config.ai_aim.piecewise_mid_pixels_y = 0.0f;
    config.ai_aim.deadzone_inner = 0.0f;
    config.ai_aim.deadzone_outer = 1.0f;
    config.ai_aim.x_deadzone_outer = 1.0f;
    config.ai_aim.ads_snap_window_ms = 0;
    config.ai_aim.max_ai_force = 0.0f;
    config.ai_aim.max_ai_force_y = 0.0f;
    config.ai_aim.body_lock_smoothing = 0.0f;
    config.ai_aim.body_lock_max_ai_force = 0.42f;
    config.ai_aim.body_lock_opposing_boost_max_ai_force = 0.42f;
    config.ai_aim.body_lock_max_ai_force_y = 0.0f;
    config.ai_aim.body_lock_box_tolerance_px = 20.0f;
    config.ai_aim.body_lock_activation_box_px = 180.0f;
    config.ai_aim.body_lock_upper_body_ratio = 0.50f;
    config.ai_aim.body_lock_confidence_frames = 1;
    config.ai_aim.body_lock_manual_escape_input_threshold = 0.45f;
    config.ai_aim.body_lock_manual_escape_preservation = 0.55f;
    config.ai_aim.body_lock_near_lock_error_px = 52.0f;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.enabled = false;

    double now = 21.0;
    controller_native::NativeGamepadController controller(
        config,
        [&now]() { return now; });

    const auto submit_body_lock_target = [&](float lock_dx) {
        controller_native::NativeControllerVisionState target;
        target.has_target = true;
        target.aim_authority = true;
        target.fire_authority = true;
        target.target_tier = "strong";
        target.dx = lock_dx;
        target.dy = 0.0f;
        target.screen_center_x = 320.0f;
        target.screen_center_y = 256.0f;
        target.has_body_box = true;
        target.body_x1 = 280.0f + lock_dx;
        target.body_x2 = 360.0f + lock_dx;
        target.body_y1 = 216.0f;
        target.body_y2 = 296.0f;
        target.observed_at_seconds = now;
        controller.submit_vision_state(target);
    };
    auto helpful_right_pull = []() {
        controller_native::PhysicalGamepadState physical = aiming_physical_state();
        physical.right_x = 0.50f;
        return physical;
    };

    constexpr float kNearLockDx[] = {50.0f, 49.0f, 51.0f, 50.0f, 48.0f, 50.0f};
    float min_output_x = 1.0f;
    int hard_brake_frames = 0;
    for (float lock_dx : kNearLockDx) {
        submit_body_lock_target(lock_dx);
        controller.build_output(helpful_right_pull());
        const auto output = controller.last_output_components().final_stick;
        min_output_x = std::min(min_output_x, output.x);
        if (output.x < 0.45f) {
            ++hard_brake_frames;
        }
        now += 0.010;
    }

    require_true(
        hard_brake_frames == 0,
        "body-lock should not repeatedly slow helpful manual input around the near-lock edge");
    require_true(
        min_output_x >= 0.45f,
        "body-lock should preserve most of a useful rightward correction near the 50px lock edge");
}

void test_controller_ads_brakes_large_manual_after_target_crossing_without_body_lock() {
    controller_native::GamepadRuntimeConfig config;
    config.ai_aim.target_max_age_ms = 0.0f;
    config.ai_aim.ads_snap_window_ms = 0;
    config.ai_aim.ads_snap_max_ai_force = 0.0f;
    config.ai_aim.ads_snap_max_ai_force_y = 0.0f;
    config.ai_aim.max_ai_force = 0.0f;
    config.ai_aim.max_ai_force_y = 0.0f;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.enabled = false;

    double now = 30.0;
    controller_native::NativeGamepadController controller(
        config,
        [&now]() { return now; });

    const auto submit_target = [&](float dy) {
        controller_native::NativeControllerVisionState target;
        target.has_target = true;
        target.aim_authority = true;
        target.fire_authority = true;
        target.target_tier = "strong";
        target.dx = 96.0f;
        target.dy = dy;
        target.screen_center_x = 320.0f;
        target.screen_center_y = 256.0f;
        target.has_body_box = false;
        target.observed_at_seconds = now;
        controller.submit_vision_state(target);
    };
    auto strong_up_pull = []() {
        controller_native::PhysicalGamepadState physical = aiming_physical_state();
        physical.right_y = 0.82f;
        return physical;
    };

    submit_target(-4.0f);
    controller.build_output(strong_up_pull());

    now = 30.001;
    submit_target(4.0f);
    controller.build_output(strong_up_pull());
    const auto crossed = controller.last_output_components().final_stick;

    require_true(
        crossed.y <= 0.26f,
        "ADS should tightly brake strong manual input that keeps pushing away after target crossing without body lock");

    now = 30.021;
    submit_target(8.0f);
    controller.build_output(strong_up_pull());
    const auto sustained = controller.last_output_components().final_stick;

    require_true(
        sustained.y <= 0.26f,
        "ADS should keep sustained wrong-way manual brake bounded");
}

void test_controller_ads_corrects_small_manual_after_target_crossing_without_body_lock() {
    controller_native::GamepadRuntimeConfig config;
    config.ai_aim.target_max_age_ms = 0.0f;
    config.ai_aim.ads_snap_window_ms = 0;
    config.ai_aim.ads_snap_max_ai_force = 0.0f;
    config.ai_aim.ads_snap_max_ai_force_y = 0.0f;
    config.ai_aim.max_ai_force = 0.0f;
    config.ai_aim.max_ai_force_y = 0.0f;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.enabled = false;

    double now = 31.0;
    controller_native::NativeGamepadController controller(
        config,
        [&now]() { return now; });

    const auto submit_target = [&](float dy) {
        controller_native::NativeControllerVisionState target;
        target.has_target = true;
        target.aim_authority = true;
        target.fire_authority = true;
        target.target_tier = "strong";
        target.dx = 96.0f;
        target.dy = dy;
        target.screen_center_x = 320.0f;
        target.screen_center_y = 256.0f;
        target.has_body_box = false;
        target.observed_at_seconds = now;
        controller.submit_vision_state(target);
    };
    auto small_up_pull = []() {
        controller_native::PhysicalGamepadState physical = aiming_physical_state();
        physical.right_y = 0.15f;
        return physical;
    };

    submit_target(-4.0f);
    controller.build_output(small_up_pull());

    now = 31.001;
    submit_target(4.0f);
    controller.build_output(small_up_pull());
    const auto crossed = controller.last_output_components().final_stick;

    require_true(
        crossed.y < -0.03f && crossed.y >= -0.10f,
        "ADS should turn small wrong-way manual input into a bounded correction after target crossing");

    now = 31.021;
    submit_target(4.0f);
    controller.build_output(small_up_pull());
    const auto sustained = controller.last_output_components().final_stick;

    require_true(
        sustained.y < -0.03f && sustained.y >= -0.10f,
        "ADS should keep a bounded correction during the brake window instead of zeroing output");
}

void test_controller_ads_cross_brake_survives_fresh_stale_sign_flip() {
    controller_native::GamepadRuntimeConfig config;
    config.ai_aim.target_max_age_ms = 0.0f;
    config.ai_aim.ads_snap_window_ms = 0;
    config.ai_aim.ads_snap_max_ai_force = 0.0f;
    config.ai_aim.ads_snap_max_ai_force_y = 0.0f;
    config.ai_aim.max_ai_force = 0.0f;
    config.ai_aim.max_ai_force_y = 0.0f;
    config.ai_aim.body_lock_manual_escape_input_threshold = 0.45f;
    config.ai_aim.body_lock_near_lock_error_px = 32.0f;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.enabled = false;

    double now = 32.0;
    controller_native::NativeGamepadController controller(
        config,
        [&now]() { return now; });

    const auto submit_target = [&](float dx) {
        controller_native::NativeControllerVisionState target;
        target.has_target = true;
        target.aim_authority = true;
        target.fire_authority = true;
        target.target_tier = "strong";
        target.dx = dx;
        target.dy = 0.0f;
        target.screen_center_x = 320.0f;
        target.screen_center_y = 256.0f;
        target.has_body_box = false;
        target.observed_at_seconds = now;
        controller.submit_vision_state(target);
    };
    auto left_pull = []() {
        controller_native::PhysicalGamepadState physical = aiming_physical_state();
        physical.right_x = -0.20f;
        return physical;
    };

    submit_target(-4.0f);
    controller.build_output(left_pull());

    now = 32.001;
    submit_target(4.0f);
    controller.build_output(left_pull());
    const auto crossed = controller.last_output_components().final_stick;
    require_true(
        crossed.x > 0.03f && crossed.x <= 0.14f,
        "ADS x crossing should apply a bounded correction for the wrong-way manual direction");

    now = 32.021;
    submit_target(-18.0f);
    controller.build_output(left_pull());
    const auto stale_sign = controller.last_output_components().final_stick;
    require_true(
        stale_sign.x < -0.03f,
        "fresh-stale vision sign flip should allow the current helpful manual direction instead of hard-zeroing");
}

void test_controller_ads_near_target_brake_allows_bounded_chase_before_crossing() {
    controller_native::GamepadRuntimeConfig config;
    config.ai_aim.target_max_age_ms = 0.0f;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.deadzone_inner = 0.0f;
    config.ai_aim.deadzone_outer = 0.0f;
    config.ai_aim.x_deadzone_outer = 0.0f;
    config.ai_aim.piecewise_mid_pixels = 0.0f;
    config.ai_aim.piecewise_mid_pixels_y = 0.0f;
    config.ai_aim.ads_snap_window_ms = 120;
    config.ai_aim.ads_snap_max_ai_force = 1.0f;
    config.ai_aim.ads_snap_max_ai_force_y = 1.0f;
    config.ai_aim.ads_snap_reticle_speed_px_per_sec = 1500.0f;
    config.ai_aim.target_projection_reticle_speed_px_per_sec = 1500.0f;
    config.ai_aim.ads_snap_time_to_go_min_remaining_ms = 1.0f;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.enabled = false;

    double now = 33.0;
    controller_native::NativeGamepadController controller(
        config,
        [&now]() { return now; });
    controller.build_output(aiming_physical_state());

    now = 33.119;
    controller_native::NativeControllerVisionState target;
    target.has_target = true;
    target.aim_authority = true;
    target.fire_authority = true;
    target.target_tier = "strong";
    target.dx = 6.0f;
    target.dy = 0.0f;
    target.screen_center_x = 320.0f;
    target.screen_center_y = 256.0f;
    target.observed_at_seconds = now;
    controller.submit_vision_state(target);

    const auto output = controller.build_output(aiming_physical_state());
    require_true(
        output.right_x >= 0.45f && output.right_x <= 0.65f,
        "ADS near-target brake should allow bounded chase for moving targets without full-force crossing");
}

void test_controller_ads_near_target_brake_preserves_manual_input() {
    controller_native::GamepadRuntimeConfig config;
    config.ai_aim.target_max_age_ms = 0.0f;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.deadzone_inner = 0.0f;
    config.ai_aim.deadzone_outer = 0.0f;
    config.ai_aim.x_deadzone_outer = 0.0f;
    config.ai_aim.piecewise_mid_pixels = 0.0f;
    config.ai_aim.piecewise_mid_pixels_y = 0.0f;
    config.ai_aim.ads_snap_window_ms = 120;
    config.ai_aim.ads_snap_max_ai_force = 1.0f;
    config.ai_aim.ads_snap_max_ai_force_y = 1.0f;
    config.ai_aim.ads_snap_reticle_speed_px_per_sec = 1500.0f;
    config.ai_aim.target_projection_reticle_speed_px_per_sec = 1500.0f;
    config.ai_aim.ads_snap_time_to_go_min_remaining_ms = 1.0f;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.enabled = false;

    double now = 34.0;
    controller_native::NativeGamepadController controller(
        config,
        [&now]() { return now; });
    controller.build_output(aiming_physical_state());

    now = 34.119;
    controller_native::NativeControllerVisionState target;
    target.has_target = true;
    target.aim_authority = true;
    target.fire_authority = true;
    target.target_tier = "strong";
    target.dx = 6.0f;
    target.dy = 0.0f;
    target.screen_center_x = 320.0f;
    target.screen_center_y = 256.0f;
    target.observed_at_seconds = now;
    controller.submit_vision_state(target);

    controller_native::PhysicalGamepadState physical = aiming_physical_state();
    physical.right_x = 0.35f;
    const auto output = controller.build_output(physical);
    require_true(
        output.right_x >= 0.45f && output.right_x <= 0.65f,
        "ADS near-target brake should preserve helpful manual input while limiting excess assist");
}

void test_controller_ads_near_target_brake_caps_manual_dominated_carry() {
    controller_native::GamepadRuntimeConfig config;
    config.ai_aim.target_max_age_ms = 0.0f;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.deadzone_inner = 0.0f;
    config.ai_aim.deadzone_outer = 0.0f;
    config.ai_aim.x_deadzone_outer = 0.0f;
    config.ai_aim.piecewise_mid_pixels = 0.0f;
    config.ai_aim.piecewise_mid_pixels_y = 0.0f;
    config.ai_aim.ads_snap_window_ms = 120;
    config.ai_aim.ads_snap_max_ai_force = 1.0f;
    config.ai_aim.ads_snap_max_ai_force_y = 1.0f;
    config.ai_aim.ads_snap_reticle_speed_px_per_sec = 1500.0f;
    config.ai_aim.target_projection_reticle_speed_px_per_sec = 1500.0f;
    config.ai_aim.ads_snap_time_to_go_min_remaining_ms = 1.0f;
    config.aim_assist_dynamics.enabled = true;
    config.recoil.enabled = false;

    double now = 34.5;
    controller_native::NativeGamepadController controller(
        config,
        [&now]() { return now; });
    controller.build_output(aiming_physical_state());

    now += 0.010;
    controller_native::NativeControllerVisionState target;
    target.has_target = true;
    target.aim_authority = true;
    target.fire_authority = true;
    target.target_tier = "strong";
    target.dx = 6.0f;
    target.dy = 0.0f;
    target.screen_center_x = 320.0f;
    target.screen_center_y = 256.0f;
    target.observed_at_seconds = now;
    controller.submit_vision_state(target);

    controller_native::PhysicalGamepadState physical = aiming_physical_state();
    physical.right_x = 0.92f;
    const controller_native::GamepadOutputState output = controller.build_output(physical);
    require_true(
        output.right_x >= 0.30f && output.right_x <= 0.65f,
        "fresh ADS near-target budget should cap manual-dominated carry without reversing it");
}

void test_controller_ads_snap_brake_survives_manual_yield_after_confirmed_crossing() {
    controller_native::GamepadRuntimeConfig config;
    config.ai_aim.target_max_age_ms = 0.0f;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.deadzone_inner = 0.0f;
    config.ai_aim.deadzone_outer = 0.0f;
    config.ai_aim.x_deadzone_outer = 0.0f;
    config.ai_aim.piecewise_mid_pixels = 0.0f;
    config.ai_aim.piecewise_mid_pixels_y = 0.0f;
    config.ai_aim.ads_snap_window_ms = 220;
    config.ai_aim.ads_max_acquisition_ms = 220.0f;
    config.ai_aim.ads_snap_max_ai_force = 1.0f;
    config.ai_aim.ads_snap_max_ai_force_y = 1.0f;
    config.ai_aim.ads_snap_time_to_go_gain = 0.0f;
    config.aim_assist_dynamics.enabled = true;
    config.recoil.enabled = false;

    double now = 35.0;
    controller_native::NativeGamepadController controller(
        config,
        [&now]() { return now; });

    const auto submit_target = [&](float dx) {
        controller_native::NativeControllerVisionState target;
        target.has_target = true;
        target.aim_authority = true;
        target.fire_authority = true;
        target.target_tier = "strong";
        target.dx = dx;
        target.dy = 0.0f;
        target.screen_center_x = 320.0f;
        target.screen_center_y = 256.0f;
        target.observed_at_seconds = now;
        controller.submit_vision_state(target);
    };
    auto strong_right_pull = []() {
        controller_native::PhysicalGamepadState physical = aiming_physical_state();
        physical.right_x = 0.92f;
        return physical;
    };

    controller.build_output(aiming_physical_state());
    now += 0.010;
    submit_target(4.0f);
    controller.build_output(strong_right_pull());

    now += 0.010;
    submit_target(2.0f);
    controller.build_output(strong_right_pull());

    now += 0.010;
    submit_target(-4.0f);
    const controller_native::GamepadOutputState crossed =
        controller.build_output(strong_right_pull());
    const controller_native::NativeControllerOutputComponents& components =
        controller.last_output_components();

    require_true(
        components.ai_aim_stick.x < -0.30f,
        "ADS crossing setup should request a material counter-steering brake");
    require_true(
        components.dynamic_adjustment_stick.x < 0.30f,
        "ADS crossing brake must not be completely deleted by the global manual-yield stage");
    require_true(
        crossed.right_x < 0.50f,
        "confirmed ADS crossing should reduce strong manual carry before it becomes overshoot");
}

void test_controller_ads_snap_yields_to_opposing_manual_without_crossing_evidence() {
    controller_native::GamepadRuntimeConfig config;
    config.ai_aim.target_max_age_ms = 0.0f;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.deadzone_inner = 0.0f;
    config.ai_aim.deadzone_outer = 0.0f;
    config.ai_aim.x_deadzone_outer = 0.0f;
    config.ai_aim.piecewise_mid_pixels = 0.0f;
    config.ai_aim.ads_snap_window_ms = 220;
    config.ai_aim.ads_snap_max_ai_force = 1.0f;
    config.ai_aim.ads_snap_time_to_go_gain = 0.0f;
    config.aim_assist_dynamics.enabled = true;
    config.recoil.enabled = false;

    double now = 36.0;
    controller_native::NativeGamepadController controller(
        config,
        [&now]() { return now; });
    controller.build_output(aiming_physical_state());

    now += 0.010;
    controller_native::NativeControllerVisionState target;
    target.has_target = true;
    target.aim_authority = true;
    target.fire_authority = true;
    target.target_tier = "strong";
    target.dx = -40.0f;
    target.dy = 0.0f;
    target.screen_center_x = 320.0f;
    target.screen_center_y = 256.0f;
    target.observed_at_seconds = now;
    controller.submit_vision_state(target);

    controller_native::PhysicalGamepadState physical = aiming_physical_state();
    physical.right_x = 0.92f;
    const controller_native::GamepadOutputState output = controller.build_output(physical);
    require_true(
        output.right_x >= 0.90f,
        "strong manual target-switch intent without crossing evidence must retain control");
}

void test_controller_ads_snap_yields_when_manual_starts_only_after_crossing() {
    controller_native::GamepadRuntimeConfig config;
    config.ai_aim.target_max_age_ms = 0.0f;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.deadzone_inner = 0.0f;
    config.ai_aim.deadzone_outer = 0.0f;
    config.ai_aim.x_deadzone_outer = 0.0f;
    config.ai_aim.piecewise_mid_pixels = 0.0f;
    config.ai_aim.piecewise_mid_pixels_y = 0.0f;
    config.ai_aim.ads_snap_window_ms = 220;
    config.ai_aim.ads_snap_max_ai_force = 1.0f;
    config.ai_aim.ads_snap_time_to_go_gain = 0.0f;
    config.aim_assist_dynamics.enabled = true;
    config.recoil.enabled = false;

    double now = 36.5;
    controller_native::NativeGamepadController controller(
        config,
        [&now]() { return now; });

    const auto submit_target = [&](float dx) {
        controller_native::NativeControllerVisionState target;
        target.has_target = true;
        target.aim_authority = true;
        target.fire_authority = true;
        target.target_tier = "strong";
        target.dx = dx;
        target.dy = 0.0f;
        target.screen_center_x = 320.0f;
        target.screen_center_y = 256.0f;
        target.observed_at_seconds = now;
        controller.submit_vision_state(target);
    };

    controller.build_output(aiming_physical_state());
    now += 0.010;
    submit_target(4.0f);
    controller.build_output(aiming_physical_state());

    now += 0.010;
    submit_target(-4.0f);
    controller_native::PhysicalGamepadState escape = aiming_physical_state();
    escape.right_x = 0.92f;
    const controller_native::GamepadOutputState output = controller.build_output(escape);
    require_true(
        output.right_x >= 0.90f,
        "manual input that begins after a target crossing must be treated as escape intent");
}

void test_controller_ads_snap_crossing_brake_expires_to_manual_control() {
    controller_native::GamepadRuntimeConfig config;
    config.ai_aim.target_max_age_ms = 0.0f;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.deadzone_inner = 0.0f;
    config.ai_aim.deadzone_outer = 0.0f;
    config.ai_aim.x_deadzone_outer = 0.0f;
    config.ai_aim.piecewise_mid_pixels = 0.0f;
    config.ai_aim.piecewise_mid_pixels_y = 0.0f;
    config.ai_aim.ads_snap_window_ms = 220;
    config.ai_aim.ads_max_acquisition_ms = 220.0f;
    config.ai_aim.ads_snap_max_ai_force = 1.0f;
    config.ai_aim.ads_snap_time_to_go_gain = 0.0f;
    config.aim_assist_dynamics.enabled = true;
    config.recoil.enabled = false;

    double now = 37.0;
    controller_native::NativeGamepadController controller(
        config,
        [&now]() { return now; });
    const auto submit_target = [&](float dx) {
        controller_native::NativeControllerVisionState target;
        target.has_target = true;
        target.aim_authority = true;
        target.fire_authority = true;
        target.target_tier = "strong";
        target.dx = dx;
        target.dy = 0.0f;
        target.screen_center_x = 320.0f;
        target.screen_center_y = 256.0f;
        target.observed_at_seconds = now;
        controller.submit_vision_state(target);
    };
    auto pull_right = []() {
        controller_native::PhysicalGamepadState physical = aiming_physical_state();
        physical.right_x = 0.92f;
        return physical;
    };

    controller.build_output(aiming_physical_state());
    now += 0.010;
    submit_target(4.0f);
    controller.build_output(pull_right());
    now += 0.010;
    submit_target(-4.0f);
    controller.build_output(pull_right());

    now += 0.060;
    submit_target(-8.0f);
    const controller_native::GamepadOutputState expired = controller.build_output(pull_right());
    require_true(
        expired.right_x >= 0.90f,
        "ADS crossing brake must expire into full manual control");
}

void test_controller_ads_snap_crossing_brake_is_axis_local() {
    controller_native::GamepadRuntimeConfig config;
    config.ai_aim.target_max_age_ms = 0.0f;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.deadzone_inner = 0.0f;
    config.ai_aim.deadzone_outer = 0.0f;
    config.ai_aim.x_deadzone_outer = 0.0f;
    config.ai_aim.piecewise_mid_pixels = 0.0f;
    config.ai_aim.piecewise_mid_pixels_y = 0.0f;
    config.ai_aim.ads_snap_window_ms = 220;
    config.ai_aim.ads_snap_max_ai_force = 1.0f;
    config.ai_aim.ads_snap_max_ai_force_y = 1.0f;
    config.ai_aim.ads_snap_time_to_go_gain = 0.0f;
    config.aim_assist_dynamics.enabled = true;
    config.recoil.enabled = false;

    double now = 38.0;
    controller_native::NativeGamepadController controller(
        config,
        [&now]() { return now; });
    const auto submit_target = [&](float dx) {
        controller_native::NativeControllerVisionState target;
        target.has_target = true;
        target.aim_authority = true;
        target.fire_authority = true;
        target.target_tier = "strong";
        target.dx = dx;
        target.dy = 40.0f;
        target.screen_center_x = 320.0f;
        target.screen_center_y = 256.0f;
        target.observed_at_seconds = now;
        controller.submit_vision_state(target);
    };
    auto diagonal_pull = []() {
        controller_native::PhysicalGamepadState physical = aiming_physical_state();
        physical.right_x = 0.92f;
        physical.right_y = 0.92f;
        return physical;
    };

    controller.build_output(aiming_physical_state());
    now += 0.010;
    submit_target(4.0f);
    controller.build_output(diagonal_pull());
    now += 0.010;
    submit_target(-4.0f);
    const controller_native::GamepadOutputState crossed = controller.build_output(diagonal_pull());
    require_true(
        crossed.right_x < 0.50f,
        "confirmed x crossing should brake the x carry");
    require_true(
        crossed.right_y >= 0.90f,
        "x crossing brake must not suppress uncrossed y escape intent");
}

void test_controller_output_validation_corrects_wrong_way_after_x_crossing() {
    controller_native::GamepadRuntimeConfig config;
    config.ai_aim.target_max_age_ms = 0.0f;
    config.ai_aim.ads_snap_window_ms = 0;
    config.ai_aim.ads_snap_max_ai_force = 0.0f;
    config.ai_aim.ads_snap_max_ai_force_y = 0.0f;
    config.ai_aim.max_ai_force = 0.0f;
    config.ai_aim.max_ai_force_y = 0.0f;
    config.ai_aim.body_lock_manual_escape_input_threshold = 0.45f;
    config.ai_aim.body_lock_near_lock_error_px = 32.0f;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.enabled = false;

    double now = 40.0;
    controller_native::NativeGamepadController controller(
        config,
        [&now]() { return now; });

    const auto submit_target = [&](float dx) {
        controller_native::NativeControllerVisionState target;
        target.has_target = true;
        target.aim_authority = true;
        target.fire_authority = true;
        target.target_tier = "strong";
        target.dx = dx;
        target.dy = 0.0f;
        target.screen_center_x = 320.0f;
        target.screen_center_y = 256.0f;
        target.has_body_box = false;
        target.observed_at_seconds = now;
        controller.submit_vision_state(target);
    };
    auto strong_right_pull = []() {
        controller_native::PhysicalGamepadState physical = aiming_physical_state();
        physical.right_x = 0.92f;
        return physical;
    };

    submit_target(4.0f);
    controller.build_output(strong_right_pull());

    now = 40.001;
    submit_target(-4.0f);
    controller.build_output(strong_right_pull());
    const auto crossed = controller.last_output_components().final_stick;

    require_true(
        crossed.x < -0.03f && crossed.x >= -0.26f,
        "target-aware output validation should correct a crossed x axis that keeps pushing away");
}

void test_controller_output_validation_caps_only_crossed_axis_on_diagonal() {
    controller_native::GamepadRuntimeConfig config;
    config.ai_aim.target_max_age_ms = 0.0f;
    config.ai_aim.ads_snap_window_ms = 0;
    config.ai_aim.ads_snap_max_ai_force = 0.0f;
    config.ai_aim.ads_snap_max_ai_force_y = 0.0f;
    config.ai_aim.max_ai_force = 0.0f;
    config.ai_aim.max_ai_force_y = 0.0f;
    config.ai_aim.body_lock_manual_escape_input_threshold = 0.45f;
    config.ai_aim.body_lock_near_lock_error_px = 32.0f;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.enabled = false;

    double now = 41.0;
    controller_native::NativeGamepadController controller(
        config,
        [&now]() { return now; });

    const auto submit_target = [&](float dx, float dy) {
        controller_native::NativeControllerVisionState target;
        target.has_target = true;
        target.aim_authority = true;
        target.fire_authority = true;
        target.target_tier = "strong";
        target.dx = dx;
        target.dy = dy;
        target.screen_center_x = 320.0f;
        target.screen_center_y = 256.0f;
        target.has_body_box = false;
        target.observed_at_seconds = now;
        controller.submit_vision_state(target);
    };
    auto diagonal_pull = []() {
        controller_native::PhysicalGamepadState physical = aiming_physical_state();
        physical.right_x = 0.92f;
        physical.right_y = 0.56f;
        return physical;
    };

    submit_target(4.0f, -44.0f);
    controller.build_output(diagonal_pull());

    now = 41.001;
    submit_target(-4.0f, -44.0f);
    controller.build_output(diagonal_pull());
    const auto crossed = controller.last_output_components().final_stick;

    require_true(
        crossed.x < -0.03f && crossed.x >= -0.26f,
        "target-aware output validation should correct only the crossed x axis");
    require_true(
        crossed.y >= 0.35f,
        "target-aware output validation should preserve the still-correct y correction");
}

void test_controller_output_validation_yields_to_manual_correction_with_tracker_reference() {
    controller_native::GamepadRuntimeConfig config;
    config.ai_aim.target_max_age_ms = 0.0f;
    config.ai_aim.ads_snap_window_ms = 0;
    config.ai_aim.ads_snap_max_ai_force = 0.0f;
    config.ai_aim.ads_snap_max_ai_force_y = 0.0f;
    config.ai_aim.max_ai_force = 0.80f;
    config.ai_aim.max_ai_force_y = 0.0f;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.enabled = false;

    double now = 42.0;
    controller_native::NativeGamepadController controller(
        config,
        [&now]() { return now; });

    controller_native::NativeControllerVisionState target;
    target.has_target = true;
    target.aim_authority = true;
    target.fire_authority = true;
    target.target_tier = "strong";
    target.dx = -82.0f;
    target.dy = 0.0f;
    target.screen_center_x = 320.0f;
    target.screen_center_y = 256.0f;
    target.has_body_box = false;
    target.has_tracker_projection = true;
    target.tracker_dx = 18.0f;
    target.tracker_dy = 0.0f;
    target.observed_at_seconds = now;
    controller.submit_vision_state(target);

    controller_native::PhysicalGamepadState physical = aiming_physical_state();
    physical.right_x = 0.34f;
    controller.build_output(physical);
    const auto mixed = controller.last_output_components().final_stick;

    require_true(
        mixed.x >= 0.25f,
        "manual correction backed by tracker reference should not be opposed by stale AI aim");
}

void test_controller_output_validation_does_not_turn_manual_into_assist() {
    controller_native::GamepadRuntimeConfig config;
    config.ai_aim.target_max_age_ms = 80.0f;
    config.ai_aim.ads_snap_window_ms = 0;
    config.ai_aim.ads_snap_max_ai_force = 0.0f;
    config.ai_aim.ads_snap_max_ai_force_y = 0.0f;
    config.ai_aim.max_ai_force = 0.0f;
    config.ai_aim.max_ai_force_y = 0.0f;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.enabled = false;

    double now = 42.5;
    controller_native::NativeGamepadController controller(
        config,
        [&now]() { return now; });

    controller_native::NativeControllerVisionState target;
    target.has_target = true;
    target.aim_authority = true;
    target.fire_authority = true;
    target.target_tier = "strong";
    target.dx = 72.0f;
    target.dy = 0.0f;
    target.screen_center_x = 320.0f;
    target.screen_center_y = 256.0f;
    target.observed_at_seconds = now - 0.070;
    controller.submit_vision_state(target);

    controller_native::PhysicalGamepadState physical = aiming_physical_state();
    physical.right_x = -0.20f;
    controller.build_output(physical);
    const auto output = controller.last_output_components().final_stick;

    require_near(
        output.x,
        physical.right_x,
        0.0001f,
        "validation must not reinterpret manual-only output as stale-target assist");
}

void test_controller_suspicious_target_jump_holds_ai_aim_until_verified() {
    controller_native::GamepadRuntimeConfig config;
    config.tracker_backend = tracking_native::TrackerBackendKind::LegacyProjection;
    config.ai_aim.target_max_age_ms = 500.0f;
    config.ai_aim.target_projection_max_age_ms = 500.0f;
    config.ai_aim.ads_snap_window_ms = 200;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.deadzone_inner = 0.0f;
    config.ai_aim.deadzone_outer = 0.0f;
    config.ai_aim.x_deadzone_outer = 0.0f;
    config.ai_aim.piecewise_mid_pixels = 0.0f;
    config.ai_aim.piecewise_mid_pixels_y = 0.0f;
    config.ai_aim.ads_snap_max_ai_force = 1.0f;
    config.ai_aim.ads_snap_max_ai_force_y = 1.0f;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.enabled = false;

    double now = 50.0;
    controller_native::NativeGamepadController controller(
        config,
        [&now]() { return now; });

    auto submit_target = [&](float dx) {
        controller_native::NativeControllerVisionState target;
        target.has_target = true;
        target.aim_authority = true;
        target.fire_authority = true;
        target.target_tier = "strong";
        target.dx = dx;
        target.dy = 0.0f;
        target.screen_center_x = 320.0f;
        target.screen_center_y = 256.0f;
        target.target_x = target.screen_center_x + dx;
        target.target_y = target.screen_center_y;
        target.has_body_box = true;
        target.body_x1 = target.target_x - 42.0f;
        target.body_x2 = target.target_x + 42.0f;
        target.body_y1 = target.target_y - 72.0f;
        target.body_y2 = target.target_y + 108.0f;
        target.observed_at_seconds = now;
        controller.submit_vision_state(target);
    };

    submit_target(18.0f);
    controller.build_output(aiming_physical_state());

    now = 50.010;
    submit_target(160.0f);
    controller.build_output(aiming_physical_state());
    const auto held = controller.last_output_components().final_stick;
    require_near(
        held.x,
        0.0f,
        0.0001f,
        "single suspicious jump without verified continuity must withhold assist");
}

void test_controller_moderate_stale_jump_coasts_on_tracker_projection() {
    controller_native::GamepadRuntimeConfig config;
    config.tracker_backend = tracking_native::TrackerBackendKind::LegacyProjection;
    config.ai_aim.target_max_age_ms = 500.0f;
    config.ai_aim.target_projection_max_age_ms = 500.0f;
    config.ai_aim.ads_snap_window_ms = 200;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.deadzone_inner = 0.0f;
    config.ai_aim.deadzone_outer = 0.0f;
    config.ai_aim.x_deadzone_outer = 0.0f;
    config.ai_aim.piecewise_mid_pixels = 0.0f;
    config.ai_aim.piecewise_mid_pixels_y = 0.0f;
    config.ai_aim.ads_snap_max_ai_force = 1.0f;
    config.ai_aim.ads_snap_max_ai_force_y = 1.0f;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.enabled = false;

    double now = 54.0;
    controller_native::NativeGamepadController controller(
        config,
        [&now]() { return now; });

    auto submit_target = [&](float dx) {
        controller_native::NativeControllerVisionState target;
        target.has_target = true;
        target.aim_authority = true;
        target.fire_authority = true;
        target.target_tier = "strong";
        target.dx = dx;
        target.dy = 0.0f;
        target.screen_center_x = 320.0f;
        target.screen_center_y = 256.0f;
        target.target_x = target.screen_center_x + dx;
        target.target_y = target.screen_center_y;
        target.has_body_box = true;
        target.body_x1 = target.target_x - 42.0f;
        target.body_x2 = target.target_x + 42.0f;
        target.body_y1 = target.target_y - 72.0f;
        target.body_y2 = target.target_y + 108.0f;
        target.observed_at_seconds = now;
        controller.submit_vision_state(target);
    };

    submit_target(18.0f);
    controller.build_output(aiming_physical_state());

    now = 54.010;
    submit_target(80.0f);
    controller.build_output(aiming_physical_state());
    const auto held = controller.last_output_components().final_stick;

    require_near(
        held.x,
        0.0f,
        0.0001f,
        "moderate unverified jump must withhold assist instead of using raw target");
}

void test_controller_fresh_stale_jump_inside_candidate_threshold_uses_tracker_projection() {
    controller_native::GamepadRuntimeConfig config;
    // Exercise the tracker/bodylock path after ADS acquisition has completed.
    config.tracker_backend = tracking_native::TrackerBackendKind::LegacyProjection;
    config.ai_aim.target_max_age_ms = 500.0f;
    config.ai_aim.target_projection_max_age_ms = 500.0f;
    config.ai_aim.ads_snap_window_ms = 0;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.deadzone_inner = 0.0f;
    config.ai_aim.deadzone_outer = 0.0f;
    config.ai_aim.x_deadzone_outer = 0.0f;
    config.ai_aim.piecewise_mid_pixels = 0.0f;
    config.ai_aim.piecewise_mid_pixels_y = 0.0f;
    config.ai_aim.ads_snap_max_ai_force = 1.0f;
    config.ai_aim.ads_snap_max_ai_force_y = 1.0f;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.enabled = false;

    double now = 56.0;
    controller_native::NativeGamepadController controller(
        config,
        [&now]() { return now; });

    auto submit_target = [&](float dx) {
        controller_native::NativeControllerVisionState target;
        target.has_target = true;
        target.aim_authority = true;
        target.fire_authority = true;
        target.target_tier = "strong";
        target.dx = dx;
        target.dy = 0.0f;
        target.screen_center_x = 320.0f;
        target.screen_center_y = 256.0f;
        target.target_x = target.screen_center_x + dx;
        target.target_y = target.screen_center_y;
        target.has_body_box = true;
        target.body_x1 = target.target_x - 42.0f;
        target.body_x2 = target.target_x + 42.0f;
        target.body_y1 = target.target_y - 72.0f;
        target.body_y2 = target.target_y + 108.0f;
        target.observed_at_seconds = now;
        controller.submit_vision_state(target);
    };

    submit_target(18.0f);
    controller.build_output(aiming_physical_state());

    now = 56.010;
    submit_target(60.0f);
    controller.build_output(aiming_physical_state());
    const auto held = controller.last_output_components().final_stick;

    if (!(held.x > 0.04f && held.x < 0.30f)) {
        std::ostringstream out;
        out << "fresh-timestamp stale jump inside the candidate threshold should stay near tracker projection actual="
            << held.x;
        throw std::runtime_error(out.str());
    }
}

void test_controller_ads_holds_recent_strong_target_when_projection_ages_out() {
    controller_native::GamepadRuntimeConfig config;
    config.tracker_backend = tracking_native::TrackerBackendKind::LegacyProjection;
    config.ai_aim.target_max_age_ms = 80.0f;
    config.ai_aim.target_projection_max_age_ms = 40.0f;
    config.ai_aim.ads_snap_window_ms = 0;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.deadzone_inner = 0.0f;
    config.ai_aim.deadzone_outer = 0.0f;
    config.ai_aim.x_deadzone_outer = 0.0f;
    config.ai_aim.piecewise_mid_pixels = 0.0f;
    config.ai_aim.piecewise_mid_pixels_y = 0.0f;
    config.ai_aim.max_ai_force = 0.0f;
    config.ai_aim.max_ai_force_y = 0.0f;
    config.ai_aim.body_lock_max_ai_force = 1.0f;
    config.ai_aim.body_lock_max_ai_force_y = 1.0f;
    config.ai_aim.body_lock_box_tolerance_px = 80.0f;
    config.ai_aim.body_lock_activation_box_px = 240.0f;
    config.ai_aim.body_lock_confidence_frames = 1;
    config.ai_aim.body_lock_smoothing = 0.0f;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.enabled = false;

    double now = 57.0;
    controller_native::NativeGamepadController controller(
        config,
        [&now]() { return now; });

    controller_native::NativeControllerVisionState target;
    target.has_target = true;
    target.aim_authority = true;
    target.fire_authority = true;
    target.target_tier = "strong";
    target.dx = 24.0f;
    target.dy = 0.0f;
    target.screen_center_x = 320.0f;
    target.screen_center_y = 256.0f;
    target.target_x = target.screen_center_x + target.dx;
    target.target_y = target.screen_center_y;
    target.has_body_box = true;
    target.body_x1 = target.target_x - 42.0f;
    target.body_x2 = target.target_x + 42.0f;
    target.body_y1 = target.target_y - 72.0f;
    target.body_y2 = target.body_y1 + 180.0f;
    target.observed_at_seconds = now;
    controller.submit_vision_state(target);
    controller.build_output(aiming_physical_state());

    now = 57.060;
    controller.build_output(aiming_physical_state());
    const auto held = controller.last_output_components().final_stick;

    require_true(
        controller.last_ai_aim_mode() == "body_lock",
        "ADS should keep a recent strong target usable after projection age alone expires");
    require_true(
        held.x > 0.05f,
        "ADS should keep assisting a recent strong target until target max age expires");
}

void test_controller_accepts_sustained_candidate_after_fresh_samples() {
    controller_native::GamepadRuntimeConfig config;
    config.tracker_backend = tracking_native::TrackerBackendKind::LegacyProjection;
    config.ai_aim.target_max_age_ms = 500.0f;
    config.ai_aim.target_projection_max_age_ms = 500.0f;
    config.ai_aim.ads_snap_window_ms = 200;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.deadzone_inner = 0.0f;
    config.ai_aim.deadzone_outer = 0.0f;
    config.ai_aim.x_deadzone_outer = 0.0f;
    config.ai_aim.piecewise_mid_pixels = 0.0f;
    config.ai_aim.piecewise_mid_pixels_y = 0.0f;
    config.ai_aim.ads_snap_max_ai_force = 1.0f;
    config.ai_aim.ads_snap_max_ai_force_y = 1.0f;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.enabled = false;

    double now = 51.0;
    controller_native::NativeGamepadController controller(
        config,
        [&now]() { return now; });

    auto submit_target = [&](float dx) {
        controller_native::NativeControllerVisionState target;
        target.has_target = true;
        target.aim_authority = true;
        target.fire_authority = true;
        target.target_tier = "strong";
        target.dx = dx;
        target.dy = 0.0f;
        target.screen_center_x = 320.0f;
        target.screen_center_y = 256.0f;
        target.observed_at_seconds = now;
        controller.submit_vision_state(target);
    };

    submit_target(18.0f);
    controller.build_output(aiming_physical_state());

    for (int sample = 0; sample < 5; ++sample) {
        now = 51.020 + (static_cast<double>(sample) * 0.020);
        submit_target(-160.0f);
    }
    controller.build_output(aiming_physical_state());
    const auto accepted = controller.last_output_components().final_stick;

    require_true(
        accepted.x < -0.30f,
        "sustained candidate with consecutive fresh samples should become the aim target");
}

void test_controller_candidate_hold_does_not_reverse_opposing_manual() {
    controller_native::GamepadRuntimeConfig config;
    config.tracker_backend = tracking_native::TrackerBackendKind::LegacyProjection;
    config.ai_aim.target_max_age_ms = 500.0f;
    config.ai_aim.target_projection_max_age_ms = 500.0f;
    config.ai_aim.ads_snap_window_ms = 200;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.deadzone_inner = 0.0f;
    config.ai_aim.deadzone_outer = 0.0f;
    config.ai_aim.x_deadzone_outer = 0.0f;
    config.ai_aim.piecewise_mid_pixels = 0.0f;
    config.ai_aim.piecewise_mid_pixels_y = 0.0f;
    config.ai_aim.ads_snap_max_ai_force = 1.0f;
    config.ai_aim.ads_snap_max_ai_force_y = 1.0f;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.enabled = false;

    double now = 52.0;
    controller_native::NativeGamepadController controller(
        config,
        [&now]() { return now; });

    auto submit_target = [&](float dx) {
        controller_native::NativeControllerVisionState target;
        target.has_target = true;
        target.aim_authority = true;
        target.fire_authority = true;
        target.target_tier = "strong";
        target.dx = dx;
        target.dy = 0.0f;
        target.screen_center_x = 320.0f;
        target.screen_center_y = 256.0f;
        target.observed_at_seconds = now;
        controller.submit_vision_state(target);
    };

    submit_target(18.0f);
    controller.build_output(aiming_physical_state());

    now = 52.010;
    submit_target(160.0f);
    controller_native::PhysicalGamepadState physical = aiming_physical_state();
    physical.right_x = -0.92f;
    controller.build_output(physical);
    const auto held = controller.last_output_components().final_stick;
    require_true(
        held.x < -0.55f,
        "candidate hold may damp authorized assist but must not reverse strong opposing manual input actual=" +
            std::to_string(held.x));
}

void test_controller_ads_candidate_projection_hold_preserves_manual_during_gap() {
    controller_native::GamepadRuntimeConfig config;
    config.tracker_backend = tracking_native::TrackerBackendKind::LegacyProjection;
    config.ai_aim.target_max_age_ms = 80.0f;
    config.ai_aim.target_projection_max_age_ms = 80.0f;
    config.ai_aim.ads_snap_window_ms = 0;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.deadzone_inner = 0.0f;
    config.ai_aim.deadzone_outer = 0.0f;
    config.ai_aim.x_deadzone_outer = 0.0f;
    config.ai_aim.piecewise_mid_pixels = 0.0f;
    config.ai_aim.piecewise_mid_pixels_y = 0.0f;
    config.ai_aim.ads_snap_max_ai_force = 0.0f;
    config.ai_aim.ads_snap_max_ai_force_y = 0.0f;
    config.ai_aim.max_ai_force = 0.0f;
    config.ai_aim.max_ai_force_y = 0.0f;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.enabled = false;

    double now = 52.5;
    controller_native::NativeGamepadController controller(
        config,
        [&now]() { return now; });

    auto submit_target = [&](float dx) {
        controller_native::NativeControllerVisionState target;
        target.has_target = true;
        target.aim_authority = true;
        target.fire_authority = true;
        target.target_tier = "strong";
        target.dx = dx;
        target.dy = 0.0f;
        target.screen_center_x = 320.0f;
        target.screen_center_y = 256.0f;
        target.observed_at_seconds = now;
        controller.submit_vision_state(target);
    };

    submit_target(18.0f);
    controller.build_output(aiming_physical_state());

    now = 52.510;
    submit_target(160.0f);
    controller.build_output(aiming_physical_state());

    now = 52.570;
    controller_native::PhysicalGamepadState physical = aiming_physical_state();
    physical.right_x = -0.92f;
    controller.build_output(physical);
    const auto held = controller.last_output_components().final_stick;

    require_near(
        held.x,
        physical.right_x,
        0.0001f,
        "ADS candidate projection hold must not reinterpret manual input as correction");
}

void test_controller_candidate_projection_hold_uses_projection_time_for_freshness() {
    controller_native::GamepadRuntimeConfig config;
    config.tracker_backend = tracking_native::TrackerBackendKind::LegacyProjection;
    config.ai_aim.target_max_age_ms = 30.0f;
    config.ai_aim.target_projection_max_age_ms = 80.0f;
    config.ai_aim.ads_snap_window_ms = 0;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.deadzone_inner = 0.0f;
    config.ai_aim.deadzone_outer = 0.0f;
    config.ai_aim.x_deadzone_outer = 0.0f;
    config.ai_aim.piecewise_mid_pixels = 0.0f;
    config.ai_aim.piecewise_mid_pixels_y = 0.0f;
    config.ai_aim.ads_snap_max_ai_force = 0.0f;
    config.ai_aim.ads_snap_max_ai_force_y = 0.0f;
    config.ai_aim.max_ai_force = 0.0f;
    config.ai_aim.max_ai_force_y = 0.0f;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.enabled = false;

    double now = 52.8;
    controller_native::NativeGamepadController controller(
        config,
        [&now]() { return now; });

    auto submit_target = [&](float dx) {
        controller_native::NativeControllerVisionState target;
        target.has_target = true;
        target.aim_authority = true;
        target.fire_authority = true;
        target.target_tier = "strong";
        target.dx = dx;
        target.dy = 0.0f;
        target.screen_center_x = 320.0f;
        target.screen_center_y = 256.0f;
        target.observed_at_seconds = now;
        controller.submit_vision_state(target);
    };

    submit_target(18.0f);
    controller.build_output(aiming_physical_state());

    now = 52.810;
    submit_target(160.0f);
    controller.build_output(aiming_physical_state());

    now = 52.845;
    controller_native::PhysicalGamepadState physical = aiming_physical_state();
    physical.right_x = -0.92f;
    controller.build_output(physical);
    const auto held = controller.last_output_components().final_stick;

    require_near(
        held.x,
        physical.right_x,
        0.0001f,
        "active candidate projection hold must preserve manual even while projection is fresh");
}

void test_controller_suspicious_candidate_without_projection_holds_output_until_verified() {
    controller_native::GamepadRuntimeConfig config;
    config.tracker_backend = tracking_native::TrackerBackendKind::LegacyProjection;
    config.ai_aim.target_max_age_ms = 500.0f;
    config.ai_aim.target_projection_max_age_ms = 500.0f;
    config.ai_aim.ads_snap_window_ms = 200;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.deadzone_inner = 0.0f;
    config.ai_aim.deadzone_outer = 0.0f;
    config.ai_aim.x_deadzone_outer = 0.0f;
    config.ai_aim.piecewise_mid_pixels = 0.0f;
    config.ai_aim.piecewise_mid_pixels_y = 0.0f;
    config.ai_aim.ads_snap_max_ai_force = 1.0f;
    config.ai_aim.ads_snap_max_ai_force_y = 1.0f;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.enabled = false;

    double now = 52.95;
    controller_native::NativeGamepadController controller(
        config,
        [&now]() { return now; });

    auto submit_target = [&](float dx, float dy) {
        controller_native::NativeControllerVisionState target;
        target.has_target = true;
        target.aim_authority = true;
        target.fire_authority = true;
        target.target_tier = "strong";
        target.dx = dx;
        target.dy = dy;
        target.screen_center_x = 320.0f;
        target.screen_center_y = 256.0f;
        target.observed_at_seconds = now;
        controller.submit_vision_state(target);
    };

    submit_target(18.0f, 0.0f);
    controller.build_output(aiming_physical_state());

    now = 53.150;
    submit_target(160.0f, -120.0f);
    controller_native::PhysicalGamepadState physical = aiming_physical_state();
    physical.right_x = -0.72f;
    physical.right_y = 0.64f;
    controller.build_output(physical);

    const auto& frame = controller.last_frame_vision_state();
    const auto output = controller.last_output_components().final_stick;
    require_true(
        !frame.has_target && !frame.fire_authority,
        "suspicious no-projection candidate should not become an aim target while unverified");
    require_near(
        output.x,
        physical.right_x,
        0.0001f,
        "suspicious no-projection candidate must preserve manual x while unverified");
    require_near(
        output.y,
        physical.right_y,
        0.0001f,
        "suspicious no-projection candidate must preserve manual y while unverified");
}

void test_controller_accepts_moving_candidate_after_fresh_samples() {
    controller_native::GamepadRuntimeConfig config;
    config.tracker_backend = tracking_native::TrackerBackendKind::LegacyProjection;
    config.ai_aim.target_max_age_ms = 500.0f;
    config.ai_aim.target_projection_max_age_ms = 500.0f;
    config.ai_aim.ads_snap_window_ms = 200;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.deadzone_inner = 0.0f;
    config.ai_aim.deadzone_outer = 0.0f;
    config.ai_aim.x_deadzone_outer = 0.0f;
    config.ai_aim.piecewise_mid_pixels = 0.0f;
    config.ai_aim.piecewise_mid_pixels_y = 0.0f;
    config.ai_aim.ads_snap_max_ai_force = 1.0f;
    config.ai_aim.ads_snap_max_ai_force_y = 1.0f;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.enabled = false;

    double now = 53.0;
    controller_native::NativeGamepadController controller(
        config,
        [&now]() { return now; });

    auto submit_target = [&](float dx) {
        controller_native::NativeControllerVisionState target;
        target.has_target = true;
        target.aim_authority = true;
        target.fire_authority = true;
        target.target_tier = "strong";
        target.dx = dx;
        target.dy = 0.0f;
        target.screen_center_x = 320.0f;
        target.screen_center_y = 256.0f;
        target.observed_at_seconds = now;
        controller.submit_vision_state(target);
    };

    submit_target(18.0f);
    controller.build_output(aiming_physical_state());

    const float moving_candidate_dx[] = {-160.0f, -110.0f, -70.0f, -80.0f, -90.0f};
    for (int sample = 0; sample < 5; ++sample) {
        now = 53.005 + (static_cast<double>(sample) * 0.0175);
        submit_target(moving_candidate_dx[sample]);
    }
    controller.build_output(aiming_physical_state());
    const auto accepted = controller.last_output_components().final_stick;

    require_true(
        accepted.x < -0.20f,
        "moving candidate should verify after sustained fresh samples instead of staying manual-only");
}

void test_controller_verified_candidate_reopens_ads_snap_after_initial_window() {
    controller_native::GamepadRuntimeConfig config;
    config.tracker_backend = tracking_native::TrackerBackendKind::LegacyProjection;
    config.ai_aim.target_max_age_ms = 500.0f;
    config.ai_aim.target_projection_max_age_ms = 500.0f;
    config.ai_aim.ads_snap_window_ms = 120;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.deadzone_inner = 0.0f;
    config.ai_aim.deadzone_outer = 0.0f;
    config.ai_aim.x_deadzone_outer = 0.0f;
    config.ai_aim.piecewise_mid_pixels = 0.0f;
    config.ai_aim.piecewise_mid_pixels_y = 0.0f;
    config.ai_aim.ads_snap_max_ai_force = 1.0f;
    config.ai_aim.ads_snap_max_ai_force_y = 1.0f;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.enabled = false;

    double now = 55.0;
    controller_native::NativeGamepadController controller(
        config,
        [&now]() { return now; });
    controller.build_output(aiming_physical_state());

    auto submit_target = [&](float dx) {
        controller_native::NativeControllerVisionState target;
        target.has_target = true;
        target.aim_authority = true;
        target.fire_authority = true;
        target.target_tier = "strong";
        target.dx = dx;
        target.dy = 0.0f;
        target.screen_center_x = 320.0f;
        target.screen_center_y = 256.0f;
        target.observed_at_seconds = now;
        controller.submit_vision_state(target);
    };

    now = 55.300;
    submit_target(18.0f);
    controller.build_output(aiming_physical_state());

    for (int sample = 0; sample < 5; ++sample) {
        now = 55.305 + (static_cast<double>(sample) * 0.0175);
        submit_target(-110.0f);
    }
    controller.build_output(aiming_physical_state());
    const auto reacquired = controller.last_output_components().final_stick;

    require_true(
        reacquired.x < -0.30f,
        "verified candidate should reopen ADS snap briefly after the initial ADS window");
}

void test_controller_projection_envelope_without_authority_stays_manual() {
    controller_native::GamepadRuntimeConfig config;
    config.tracker_backend = tracking_native::TrackerBackendKind::LegacyProjection;
    config.ai_aim.target_max_age_ms = 500.0f;
    config.ai_aim.target_projection_max_age_ms = 500.0f;
    config.ai_aim.ads_snap_window_ms = 120;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.deadzone_inner = 0.0f;
    config.ai_aim.deadzone_outer = 0.0f;
    config.ai_aim.x_deadzone_outer = 0.0f;
    config.ai_aim.piecewise_mid_pixels = 0.0f;
    config.ai_aim.piecewise_mid_pixels_y = 0.0f;
    config.ai_aim.ads_snap_max_ai_force = 1.0f;
    config.ai_aim.ads_snap_max_ai_force_y = 1.0f;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.enabled = false;

    double now = 56.0;
    controller_native::NativeGamepadController controller(
        config,
        [&now]() { return now; });
    controller.build_output(aiming_physical_state());

    auto submit_target = [&](float dx) {
        controller_native::NativeControllerVisionState target;
        target.has_target = true;
        target.aim_authority = true;
        target.fire_authority = true;
        target.target_tier = "strong";
        target.dx = dx;
        target.dy = 0.0f;
        target.screen_center_x = 320.0f;
        target.screen_center_y = 256.0f;
        target.observed_at_seconds = now;
        controller.submit_vision_state(target);
    };

    now = 56.300;
    submit_target(18.0f);
    controller.build_output(aiming_physical_state());

    now = 56.320;
    submit_target(120.0f);
    controller.build_output(aiming_physical_state());

    now = 56.340;
    submit_target(22.0f);
    controller.build_output(aiming_physical_state());
    const auto reacquired = controller.last_output_components().final_stick;

    require_near(
        reacquired.x,
        0.0f,
        0.0001f,
        "projected candidate without explicit continuity authority must stay manual");
    require_true(
        controller.last_ai_aim_mode() == "manual",
        "projected candidate without authority must not reopen ADS snap");
}

void test_body_lock_clears_vertical_axis_on_near_zero_sign_flip() {
    controller_native::GamepadAiAimConfig config;
    config.max_pixels = 100.0f;
    config.target_max_age_ms = 0.0f;
    config.piecewise_mid_pixels = 0.0f;
    config.piecewise_mid_pixels_y = 0.0f;
    config.deadzone_inner = 1.5f;
    config.deadzone_outer = 5.0f;
    config.x_deadzone_outer = 3.0f;
    config.body_lock_max_ai_force = 1.0f;
    config.body_lock_max_ai_force_y = 1.0f;
    config.body_lock_box_tolerance_px = 20.0f;
    config.body_lock_activation_box_px = 180.0f;
    config.body_lock_smoothing = 0.18f;
    config.body_lock_vertical_deadzone_px = 6.0f;
    controller_native::NativeAiAim ai_aim(config);

    controller_native::NativeAiAimInput input;
    input.aiming = true;
    input.has_target = true;
    input.aim_authority = true;
    input.ads_snap_active = false;
    input.target_tier = "strong";
    input.observed_at_seconds = 10.0;
    input.screen_center_x = 320.0f;
    input.screen_center_y = 256.0f;
    input.has_body_box = true;
    input.body_x1 = 300.0f;
    input.body_x2 = 380.0f;

    input.now_seconds = 10.00;
    input.body_y1 = 179.0f;
    input.body_y2 = 359.0f;
    ai_aim.compute(input);
    input.now_seconds = 10.10;
    ai_aim.compute(input);

    input.now_seconds = 10.20;
    input.body_y1 = 187.0f;
    input.body_y2 = 367.0f;
    const controller_native::NativeAiAimOutput output = ai_aim.compute(input);

    require_near(
        output.assist_y,
        0.0f,
        0.002f,
        "the continuous body-lock soft deadband should keep a y crossing bumpless");
    require_true(output.assist_x != 0.0f, "body-lock should keep x assist while clearing y");
}

void test_body_lock_applies_motion_lead_after_configured_history_frames() {
    controller_native::GamepadAiAimConfig config;
    config.max_pixels = 100.0f;
    config.target_max_age_ms = 0.0f;
    config.piecewise_mid_pixels = 0.0f;
    config.piecewise_mid_pixels_y = 0.0f;
    config.deadzone_inner = 0.0f;
    config.deadzone_outer = 1.0f;
    config.x_deadzone_outer = 1.0f;
    config.body_lock_max_ai_force = 1.0f;
    config.body_lock_max_ai_force_y = 1.0f;
    config.body_lock_box_tolerance_px = 20.0f;
    config.body_lock_activation_box_px = 180.0f;
    config.body_lock_lead_frames = 2;
    config.body_lock_lead_seconds = 0.05f;
    config.body_lock_lead_max_px = 7.0f;
    config.body_lock_vertical_lead_scale = 0.95f;
    config.body_lock_smoothing = 0.0f;
    controller_native::NativeAiAim ai_aim(config);

    controller_native::NativeAiAimInput input;
    input.aiming = true;
    input.has_target = true;
    input.aim_authority = true;
    input.ads_snap_active = false;
    input.target_tier = "strong";
    input.observed_at_seconds = 10.0;
    input.now_seconds = 10.00;
    input.screen_center_x = 320.0f;
    input.screen_center_y = 256.0f;
    input.has_body_box = true;
    input.body_y1 = 184.0f;
    input.body_y2 = 364.0f;
    input.body_x1 = 290.0f;
    input.body_x2 = 370.0f;
    ai_aim.compute(input);

    input.now_seconds = 10.10;
    input.body_x1 = 300.0f;
    input.body_x2 = 380.0f;
    const controller_native::NativeAiAimOutput output = ai_aim.compute(input);

    require_near(
        output.assist_x,
        0.25f,
        0.002f,
        "body-lock should lead sustained horizontal target motion before computing assist");
}

void test_body_lock_confidence_resets_when_body_box_no_longer_matches_target() {
    controller_native::GamepadAiAimConfig config;
    config.max_pixels = 100.0f;
    config.target_max_age_ms = 0.0f;
    config.piecewise_mid_pixels = 0.0f;
    config.piecewise_mid_pixels_y = 0.0f;
    config.deadzone_inner = 0.0f;
    config.deadzone_outer = 1.0f;
    config.x_deadzone_outer = 1.0f;
    config.body_lock_max_ai_force = 1.0f;
    config.body_lock_max_ai_force_y = 1.0f;
    config.body_lock_box_tolerance_px = 20.0f;
    config.body_lock_activation_box_px = 180.0f;
    config.body_lock_confidence_frames = 3;
    config.body_lock_confidence_min_strong = 0.65f;
    config.body_lock_opposing_suppression_max = 1.0f;
    config.body_lock_target_match_iou = 0.90f;
    config.body_lock_target_match_center_px = 20.0f;
    config.body_lock_smoothing = 0.0f;
    controller_native::NativeAiAim ai_aim(config);

    controller_native::NativeAiAimInput input;
    input.aiming = true;
    input.has_target = true;
    input.aim_authority = true;
    input.ads_snap_active = false;
    input.target_tier = "strong";
    input.observed_at_seconds = 10.0;
    input.screen_center_x = 320.0f;
    input.screen_center_y = 256.0f;
    input.has_body_box = true;
    input.body_y1 = 196.0f;
    input.body_y2 = 316.0f;
    input.body_x1 = 288.0f;
    input.body_x2 = 368.0f;

    for (int index = 0; index < 3; ++index) {
        input.now_seconds = 10.00 + (static_cast<double>(index) * 0.02);
        ai_aim.compute(input);
    }

    input.now_seconds = 10.08;
    input.body_x1 = 330.0f;
    input.body_x2 = 410.0f;
    input.manual_right_x = -0.30f;
    const controller_native::NativeAiAimOutput output = ai_aim.compute(input);
    const float final_x = input.manual_right_x + output.assist_x;

    require_near(
        final_x,
        0.20f,
        0.03f,
        "a new body box should reset confidence before the unified curve suppresses moderate manual input");
}

void test_controller_ads_snap_only_runs_inside_ads_window_without_body_lock() {
    controller_native::GamepadRuntimeConfig config;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.max_ai_force = 1.0f;
    config.ai_aim.ads_snap_window_ms = 20;
    config.ai_aim.ads_max_acquisition_ms = 20.0f;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.enabled = false;
    controller_native::NativeGamepadController controller(config);

    vision_native::VisionResult target;
    target.frame_updated = true;
    target.has_target = true;
    target.aim_authority = true;
    target.fire_authority = true;
    target.dx = 50.0f;
    target.dy = 0.0f;
    target.target_tier = "strong";
    target.result_at_ns = now_ns();
    submit_adapted_vision_result(controller, target);

    controller_native::GamepadOutputState first = controller.build_output(aiming_physical_state());
    require_true(first.right_x > 0.40f, "fresh ADS snap window should assist strong targets");

    std::this_thread::sleep_for(std::chrono::milliseconds(35));
    target.result_at_ns = now_ns();
    submit_adapted_vision_result(controller, target);

    controller_native::GamepadOutputState second = controller.build_output(aiming_physical_state());
    require_near(
        second.right_x,
        0.0f,
        0.001f,
        "ADS acquisition should stop after its bounded timeout when body-lock is unavailable");
}

void test_controller_left_thumb_does_not_count_as_aiming() {
    controller_native::GamepadRuntimeConfig config;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.max_ai_force = 1.0f;
    config.ai_aim.ads_snap_window_ms = 120;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.enabled = false;
    controller_native::NativeGamepadController controller(config);

    vision_native::VisionResult target;
    target.frame_updated = true;
    target.has_target = true;
    target.aim_authority = true;
    target.fire_authority = true;
    target.dx = 50.0f;
    target.dy = 0.0f;
    target.target_tier = "strong";
    target.result_at_ns = now_ns();
    submit_adapted_vision_result(controller, target);

    controller_native::PhysicalGamepadState physical;
    physical.connected = true;
    physical.left_thumb = true;
    const controller_native::GamepadOutputState output = controller.build_output(physical);
    require_near(output.right_x, 0.0f, 0.001f, "holding L3 must not count as aiming for aim assist");
}

void test_controller_left_trigger_release_drop_ends_aiming_before_idle() {
    controller_native::GamepadRuntimeConfig config;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.max_ai_force = 1.0f;
    config.ai_aim.ads_snap_window_ms = 120;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.enabled = false;
    controller_native::NativeGamepadController controller(config);

    vision_native::VisionResult target;
    target.frame_updated = true;
    target.has_target = true;
    target.aim_authority = true;
    target.fire_authority = true;
    target.dx = 50.0f;
    target.dy = 0.0f;
    target.target_tier = "strong";
    target.result_at_ns = now_ns();
    submit_adapted_vision_result(controller, target);

    controller_native::PhysicalGamepadState physical;
    physical.connected = true;
    physical.left_trigger = 0.60f;
    controller_native::GamepadOutputState output = controller.build_output(physical);
    require_true(output.right_x > 0.40f, "pressed LT should start aim assist immediately");

    physical.left_trigger = 0.48f;
    output = controller.build_output(physical);
    require_near(
        output.right_x,
        0.0f,
        0.001f,
        "LT release drop should cancel aim assist before the trigger is fully idle");
}

void test_controller_light_left_trigger_press_counts_as_aiming() {
    controller_native::GamepadRuntimeConfig config;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.max_ai_force = 1.0f;
    config.ai_aim.ads_snap_window_ms = 120;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.enabled = false;
    controller_native::NativeGamepadController controller(config);

    vision_native::VisionResult target;
    target.frame_updated = true;
    target.has_target = true;
    target.aim_authority = true;
    target.fire_authority = true;
    target.dx = 50.0f;
    target.dy = 0.0f;
    target.target_tier = "strong";
    target.result_at_ns = now_ns();
    submit_adapted_vision_result(controller, target);

    controller_native::PhysicalGamepadState physical;
    physical.connected = true;
    physical.left_trigger = 0.10f;
    const controller_native::GamepadOutputState output = controller.build_output(physical);
    require_true(output.right_x > 0.40f, "light LT press should count as aiming immediately");
}

void test_auto_fire_ready_uses_body_lock_error_when_body_box_is_active() {
    controller_native::GamepadRuntimeConfig config;
    config.ai_aim.ads_snap_window_ms = 0;
    config.auto_fire_output = "RB";
    config.auto_fire.require_aim_ready = true;
    config.auto_fire.max_source_age_ms = 0.0f;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.target_max_age_ms = 0.0f;
    config.ai_aim.auto_fire_ready_error_px = 10.0f;
    config.ai_aim.auto_fire_ready_frames = 1;
    config.ai_aim.auto_fire_ready_min_ads_ms = 0.0f;
    config.ai_aim.auto_fire_ready_max_ai_stick = 12000.0f;
    config.ai_aim.body_lock_box_tolerance_px = 20.0f;
    config.ai_aim.body_lock_activation_box_px = 180.0f;
    config.ai_aim.body_lock_upper_body_ratio = 0.40f;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.enabled = false;
    controller_native::NativeGamepadController controller(config);

    vision_native::VisionResult target;
    target.frame_updated = true;
    target.has_target = true;
    target.auto_fire = true;
    target.aim_authority = true;
    target.fire_authority = true;
    target.dx = 90.0f;
    target.dy = 0.0f;
    target.screen_center_x = 320.0f;
    target.screen_center_y = 256.0f;
    target.has_body_box = true;
    target.body_x1 = 300.0f;
    target.body_y1 = 176.0f;
    target.body_x2 = 340.0f;
    target.body_y2 = 376.0f;
    target.target_tier = "strong";
    target.result_at_ns = now_ns();
    submit_adapted_vision_result(controller, target);

    const controller_native::GamepadOutputState output = controller.build_output(aiming_physical_state());
    require_true(
        output.rb,
        "auto-fire readiness should use body-lock error when the upper-body point is settled");
}

void test_ads_snap_counts_same_direction_manual_as_planned_correction() {
    controller_native::GamepadAiAimConfig config;
    config.max_pixels = 100.0f;
    config.piecewise_mid_pixels = 0.0f;
    config.ads_snap_max_ai_force = 1.0f;
    controller_native::NativeAiAim ai_aim(config);

    controller_native::NativeAiAimInput input;
    input.aiming = true;
    input.has_target = true;
    input.aim_authority = true;
    input.ads_snap_active = true;
    input.dx = 50.0f;
    input.dy = 0.0f;
    input.target_tier = "strong";
    input.manual_right_x = 0.25f;
    input.observed_at_seconds = 10.0;
    input.now_seconds = 10.01;

    const controller_native::NativeAiAimOutput output = ai_aim.compute(input);
    require_near(
        output.assist_x,
        0.25f,
        0.001f,
        "ADS snap should not stack same-direction manual input beyond planned correction");
}

void test_ads_snap_softens_opposing_manual_instead_of_canceling_snap() {
    controller_native::GamepadAiAimConfig config;
    config.max_pixels = 100.0f;
    config.piecewise_mid_pixels = 0.0f;
    config.ads_snap_max_ai_force = 1.0f;
    config.ads_snap_opposing_manual_suppression_max = 0.35f;
    controller_native::NativeAiAim ai_aim(config);

    controller_native::NativeAiAimInput input;
    input.aiming = true;
    input.has_target = true;
    input.aim_authority = true;
    input.ads_snap_active = true;
    input.ads_snap_progress_ratio = 0.0f;
    input.dx = 50.0f;
    input.dy = 0.0f;
    input.target_tier = "strong";
    input.manual_right_x = -0.50f;
    input.observed_at_seconds = 10.0;
    input.now_seconds = 10.01;

    const controller_native::NativeAiAimOutput output = ai_aim.compute(input);
    const float final_x = input.manual_right_x + output.assist_x;
    require_near(
        final_x,
        0.06125f,
        0.001f,
        "ADS snap should lightly suppress opposing manual input early in the snap window");
}

void test_ads_snap_damps_orthogonal_manual_curve_without_removing_it() {
    controller_native::GamepadAiAimConfig config;
    config.max_pixels = 100.0f;
    config.piecewise_mid_pixels = 0.0f;
    config.deadzone_inner = 0.0f;
    config.deadzone_outer = 1.0f;
    config.x_deadzone_outer = 1.0f;
    config.ads_snap_max_ai_force = 1.0f;
    config.ads_snap_time_to_go_gain = 0.0f;
    config.ads_snap_opposing_manual_suppression_max = 0.50f;
    controller_native::NativeAiAim ai_aim(config);

    controller_native::NativeAiAimInput input;
    input.aiming = true;
    input.has_target = true;
    input.aim_authority = true;
    input.ads_snap_active = true;
    input.ads_snap_progress_ratio = 1.0f;
    input.dx = 50.0f;
    input.dy = 0.0f;
    input.target_tier = "strong";
    input.manual_right_y = 0.50f;
    input.observed_at_seconds = 10.0;
    input.now_seconds = 10.01;

    const controller_native::NativeAiAimOutput output = ai_aim.compute(input);
    const float final_x = input.manual_right_x + output.assist_x;
    const float final_y = input.manual_right_y + output.assist_y;
    require_near(final_x, 0.50f, 0.001f, "ADS snap should keep full planned correction on the snap axis");
    require_near(final_y, 0.30f, 0.001f, "ADS snap should damp but not erase orthogonal manual curve");
}

void test_ads_snap_uses_tracker_projection_as_mixing_reference() {
    controller_native::GamepadAiAimConfig config;
    config.max_pixels = 100.0f;
    config.piecewise_mid_pixels = 0.0f;
    config.deadzone_inner = 0.0f;
    config.deadzone_outer = 1.0f;
    config.x_deadzone_outer = 1.0f;
    config.ads_snap_max_ai_force = 1.0f;
    config.ads_snap_time_to_go_gain = 0.0f;
    config.ads_snap_opposing_manual_suppression_max = 0.50f;
    controller_native::NativeAiAim ai_aim(config);

    controller_native::NativeAiAimInput input;
    input.aiming = true;
    input.has_target = true;
    input.aim_authority = true;
    input.ads_snap_active = true;
    input.ads_snap_progress_ratio = 1.0f;
    input.dx = 50.0f;
    input.dy = 0.0f;
    input.has_mixing_reference = true;
    input.mixing_reference_dx = 50.0f;
    input.mixing_reference_dy = -50.0f;
    input.target_tier = "strong";
    input.manual_right_y = 0.50f;
    input.observed_at_seconds = 10.0;
    input.now_seconds = 10.01;

    const controller_native::NativeAiAimOutput output = ai_aim.compute(input);
    const float final_x = input.manual_right_x + output.assist_x;
    const float final_y = input.manual_right_y + output.assist_y;
    require_near(final_x, 0.50f, 0.001f, "tracker reference should not weaken snap-axis correction");
    require_near(final_y, 0.40f, 0.001f, "tracker reference should preserve more manual input that follows projected target direction");
}

void test_ai_aim_piecewise_mapping_matches_python_midpoint() {
    controller_native::GamepadAiAimConfig config;
    config.piecewise_mid_pixels = 60.0f;
    config.piecewise_max_pixels = 230.0f;
    config.piecewise_mid_ratio = 0.56f;
    config.ads_snap_max_ai_force = 1.0f;
    controller_native::NativeAiAim ai_aim(config);

    controller_native::NativeAiAimInput input;
    input.aiming = true;
    input.has_target = true;
    input.aim_authority = true;
    input.ads_snap_active = true;
    input.dx = 60.0f;
    input.dy = 0.0f;
    input.target_tier = "strong";
    input.observed_at_seconds = 10.0;
    input.now_seconds = 10.01;

    const controller_native::NativeAiAimOutput output = ai_aim.compute(input);
    require_near(
        output.assist_x,
        0.56f,
        0.001f,
        "AI aim should use the configured piecewise midpoint mapping");
}

void test_ads_snap_fov_scale_reduces_ads_pull() {
    controller_native::GamepadAiAimConfig full_scale_config;
    full_scale_config.max_pixels = 100.0f;
    full_scale_config.piecewise_mid_pixels = 0.0f;
    full_scale_config.deadzone_inner = 0.0f;
    full_scale_config.deadzone_outer = 1.0f;
    full_scale_config.x_deadzone_outer = 1.0f;
    full_scale_config.ads_snap_max_ai_force = 1.0f;
    full_scale_config.ads_snap_time_to_go_gain = 0.0f;

    controller_native::GamepadAiAimConfig ads_scaled_config = full_scale_config;
    ads_scaled_config.ads_snap_fov_scale = 0.50f;
    ads_scaled_config.ads_snap_fov_transition_ms = 0.0f;

    controller_native::NativeAiAimInput input;
    input.aiming = true;
    input.has_target = true;
    input.aim_authority = true;
    input.ads_snap_active = true;
    input.dx = 60.0f;
    input.dy = 0.0f;
    input.target_tier = "strong";
    input.observed_at_seconds = 10.0;
    input.now_seconds = 10.01;

    const controller_native::NativeAiAimOutput full_scale_output =
        controller_native::NativeAiAim(full_scale_config).compute(input);
    const controller_native::NativeAiAimOutput ads_scaled_output =
        controller_native::NativeAiAim(ads_scaled_config).compute(input);

    require_near(
        full_scale_output.assist_x,
        0.60f,
        0.001f,
        "baseline ADS snap pull should map unscaled target error");
    require_near(
        ads_scaled_output.assist_x,
        0.30f,
        0.001f,
        "ADS FOV scale should reduce snap pull before pixel-to-stick mapping");
}

void test_ads_snap_fov_scale_transitions_over_configured_window() {
    controller_native::GamepadAiAimConfig config;
    config.max_pixels = 100.0f;
    config.piecewise_mid_pixels = 0.0f;
    config.deadzone_inner = 0.0f;
    config.deadzone_outer = 1.0f;
    config.x_deadzone_outer = 1.0f;
    config.ads_snap_max_ai_force = 1.0f;
    config.ads_snap_time_to_go_gain = 0.0f;
    config.ads_snap_window_ms = 130;
    config.ads_snap_fov_scale = 0.68f;
    config.ads_snap_fov_transition_ms = 130.0f;

    controller_native::NativeAiAimInput input;
    input.aiming = true;
    input.has_target = true;
    input.aim_authority = true;
    input.ads_snap_active = true;
    input.dx = 100.0f;
    input.dy = 0.0f;
    input.target_tier = "strong";
    input.observed_at_seconds = 10.0;
    input.now_seconds = 10.01;

    input.ads_snap_progress_ratio = 0.0f;
    const controller_native::NativeAiAimOutput start_output =
        controller_native::NativeAiAim(config).compute(input);
    input.ads_snap_progress_ratio = 0.5f;
    const controller_native::NativeAiAimOutput mid_output =
        controller_native::NativeAiAim(config).compute(input);
    input.ads_snap_progress_ratio = 1.0f;
    const controller_native::NativeAiAimOutput end_output =
        controller_native::NativeAiAim(config).compute(input);

    require_near(start_output.assist_x, 1.00f, 0.001f, "ADS FOV transition should start at hipfire scale");
    require_near(mid_output.assist_x, 0.84f, 0.001f, "ADS FOV transition should interpolate half way");
    require_near(end_output.assist_x, 0.68f, 0.001f, "ADS FOV transition should end at configured ADS scale");
}

void test_ads_snap_time_to_go_can_override_piecewise_mapping() {
    controller_native::GamepadAiAimConfig config;
    config.piecewise_mid_pixels = 60.0f;
    config.piecewise_max_pixels = 230.0f;
    config.piecewise_mid_ratio = 0.56f;
    config.ads_snap_max_ai_force = 1.0f;
    config.ads_snap_reticle_speed_px_per_sec = 1500.0f;
    config.ads_snap_time_to_go_gain = 1.0f;
    config.ads_snap_time_to_go_min_remaining_ms = 1.0f;
    controller_native::NativeAiAim ai_aim(config);

    controller_native::NativeAiAimInput input;
    input.aiming = true;
    input.has_target = true;
    input.aim_authority = true;
    input.ads_snap_active = true;
    input.ads_snap_remaining_seconds = 0.010f;
    input.dx = 30.0f;
    input.dy = 0.0f;
    input.target_tier = "strong";
    input.observed_at_seconds = 10.0;
    input.now_seconds = 10.01;

    const controller_native::NativeAiAimOutput output = ai_aim.compute(input);
    require_near(
        output.assist_x,
        1.0f,
        0.001f,
        "ADS snap should use time-to-go when it needs more force than piecewise mapping");
}

void test_ads_snap_clamps_vertical_target_delta_before_mapping() {
    controller_native::GamepadAiAimConfig config;
    config.max_pixels = 100.0f;
    config.piecewise_mid_pixels_y = 0.0f;
    config.piecewise_max_pixels_y = 100.0f;
    config.deadzone_inner = 0.0f;
    config.deadzone_outer = 1.0f;
    config.ads_snap_max_ai_force_y = 1.0f;
    config.ads_snap_time_to_go_gain = 0.0f;
    config.ads_snap_max_target_dy_px = 20.0f;
    controller_native::NativeAiAim ai_aim(config);

    controller_native::NativeAiAimInput input;
    input.aiming = true;
    input.has_target = true;
    input.aim_authority = true;
    input.ads_snap_active = true;
    input.dx = 0.0f;
    input.dy = 80.0f;
    input.target_tier = "strong";
    input.observed_at_seconds = 10.0;
    input.now_seconds = 10.01;

    const controller_native::NativeAiAimOutput output = ai_aim.compute(input);
    require_near(
        output.assist_y,
        -0.20f,
        0.001f,
        "ADS snap should clamp vertical target error before mapping to stick");
}

void test_ai_aim_fire_active_does_not_add_ads_snap_force_cap() {
    controller_native::GamepadAiAimConfig config;
    config.max_pixels = 100.0f;
    config.deadzone_inner = 0.0f;
    config.deadzone_outer = 0.0f;
    config.x_deadzone_outer = 0.0f;
    config.piecewise_mid_pixels = 0.0f;
    config.piecewise_mid_pixels_y = 0.0f;
    config.ads_snap_max_ai_force = 4.0f;
    config.ads_snap_max_ai_force_y = 4.0f;
    config.ads_snap_max_target_dy_px = 1000.0f;
    controller_native::NativeAiAim non_firing_ai_aim(config);
    controller_native::NativeAiAim firing_ai_aim(config);

    controller_native::NativeAiAimInput input;
    input.aiming = true;
    input.has_target = true;
    input.aim_authority = true;
    input.ads_snap_active = true;
    input.fire_active = true;
    input.dx = 100.0f;
    input.dy = -100.0f;
    input.target_tier = "strong";
    input.observed_at_seconds = 10.0;
    input.now_seconds = 10.01;

    controller_native::NativeAiAimInput non_firing_input = input;
    non_firing_input.fire_active = false;
    const controller_native::NativeAiAimOutput non_firing_output =
        non_firing_ai_aim.compute(non_firing_input);
    const controller_native::NativeAiAimOutput firing_output = firing_ai_aim.compute(input);
    require_near(
        firing_output.assist_x,
        non_firing_output.assist_x,
        0.001f,
        "fire-active ADS snap x should use the same configured force as non-firing ADS snap");
    require_near(
        firing_output.assist_y,
        non_firing_output.assist_y,
        0.001f,
        "fire-active ADS snap upward y should use the same configured force as non-firing ADS snap");
}

void test_ai_aim_fire_active_caps_downward_ads_snap_vertical_stack() {
    controller_native::GamepadAiAimConfig config;
    config.max_pixels = 100.0f;
    config.deadzone_inner = 0.0f;
    config.deadzone_outer = 0.0f;
    config.x_deadzone_outer = 0.0f;
    config.piecewise_mid_pixels = 0.0f;
    config.piecewise_mid_pixels_y = 0.0f;
    config.ads_snap_max_ai_force = 4.0f;
    config.ads_snap_max_ai_force_y = 4.0f;
    config.ads_snap_max_target_dy_px = 1000.0f;
    controller_native::NativeAiAim ai_aim(config);

    controller_native::NativeAiAimInput input;
    input.aiming = true;
    input.has_target = true;
    input.aim_authority = true;
    input.ads_snap_active = true;
    input.fire_active = true;
    input.dx = 100.0f;
    input.dy = 100.0f;
    input.target_tier = "strong";
    input.observed_at_seconds = 10.0;
    input.now_seconds = 10.01;

    const controller_native::NativeAiAimOutput output = ai_aim.compute(input);
    require_true(output.assist_y < 0.0f, "test setup should request downward ADS snap");
    require_true(
        output.assist_y >= -0.1801f,
        "fire-active downward ADS snap should not stack hard with recoil down-pull");
}

void test_fire_active_projected_target_suppresses_vertical_aim_assist() {
    controller_native::GamepadAiAimConfig config;
    config.max_pixels = 100.0f;
    config.deadzone_inner = 0.0f;
    config.deadzone_outer = 0.0f;
    config.x_deadzone_outer = 0.0f;
    config.piecewise_mid_pixels = 0.0f;
    config.piecewise_mid_pixels_y = 0.0f;
    config.ads_snap_max_ai_force = 2.0f;
    config.ads_snap_max_ai_force_y = 2.0f;
    config.ads_snap_max_target_dy_px = 1000.0f;
    controller_native::NativeAiAim ai_aim(config);

    controller_native::NativeAiAimInput input;
    input.aiming = true;
    input.has_target = true;
    input.aim_authority = true;
    input.ads_snap_active = true;
    input.fire_active = true;
    input.dx = 50.0f;
    input.dy = 50.0f;
    input.target_tier = "projected";
    input.observed_at_seconds = 10.0;
    input.now_seconds = 10.01;

    const controller_native::NativeAiAimOutput output = ai_aim.compute(input);
    require_true(std::fabs(output.assist_x) > 0.01f, "projected target can keep horizontal assist");
    require_near(
        output.assist_y,
        0.0f,
        0.001f,
        "fire-active projected target should not add vertical aim assist on top of recoil");
}

void test_body_lock_fire_active_caps_downward_vertical_stack_without_x_force_cap() {
    controller_native::GamepadAiAimConfig config;
    config.max_pixels = 100.0f;
    config.deadzone_inner = 0.0f;
    config.deadzone_outer = 0.0f;
    config.x_deadzone_outer = 0.0f;
    config.piecewise_mid_pixels = 0.0f;
    config.body_lock_smoothing = 0.0f;
    config.body_lock_box_tolerance_px = 500.0f;
    config.body_lock_activation_box_px = 1000.0f;
    config.body_lock_max_ai_force = 2.0f;
    config.body_lock_max_ai_force_y = 2.0f;
    controller_native::NativeAiAim non_firing_ai_aim(config);
    controller_native::NativeAiAim firing_ai_aim(config);

    controller_native::NativeAiAimInput input;
    input.aiming = true;
    input.has_target = true;
    input.aim_authority = true;
    input.ads_snap_active = false;
    input.fire_active = true;
    input.has_body_box = true;
    input.screen_center_x = 0.0f;
    input.screen_center_y = 0.0f;
    input.body_x1 = 60.0f;
    input.body_x2 = 140.0f;
    input.body_y1 = 260.0f;
    input.body_y2 = 340.0f;
    input.target_tier = "strong";
    input.observed_at_seconds = 10.0;
    input.now_seconds = 10.01;

    controller_native::NativeAiAimInput non_firing_input = input;
    non_firing_input.fire_active = false;
    const controller_native::NativeAiAimOutput non_firing_output =
        non_firing_ai_aim.compute(non_firing_input);
    const controller_native::NativeAiAimOutput firing_output = firing_ai_aim.compute(input);
    require_near(
        firing_output.assist_x,
        non_firing_output.assist_x,
        0.001f,
        "fire-active body-lock x should use the same configured force as non-firing body-lock");
    require_true(
        non_firing_output.assist_y < -0.18f,
        "test setup should produce stronger non-firing downward body-lock");
    require_true(
        firing_output.assist_y >= -0.1801f,
        "fire-active downward body-lock should not stack hard with recoil down-pull");
}

void test_ads_snap_smoothing_interpolates_first_assist_frame() {
    controller_native::GamepadAiAimConfig config;
    config.max_pixels = 100.0f;
    config.piecewise_mid_pixels = 0.0f;
    config.deadzone_inner = 0.0f;
    config.deadzone_outer = 1.0f;
    config.x_deadzone_outer = 1.0f;
    config.ads_snap_max_ai_force = 1.0f;
    config.ads_snap_time_to_go_gain = 0.0f;
    config.ads_snap_smoothing = 0.5f;
    controller_native::NativeAiAim ai_aim(config);

    controller_native::NativeAiAimInput input;
    input.aiming = true;
    input.has_target = true;
    input.aim_authority = true;
    input.ads_snap_active = true;
    input.dx = 50.0f;
    input.dy = 0.0f;
    input.target_tier = "strong";
    input.observed_at_seconds = 10.0;
    input.now_seconds = 10.01;

    const controller_native::NativeAiAimOutput output = ai_aim.compute(input);
    require_near(
        output.assist_x,
        0.25f,
        0.001f,
        "ADS snap smoothing should interpolate from the previous assist state");
}

void test_ai_aim_deadzone_suppresses_tiny_target_error() {
    controller_native::GamepadAiAimConfig config;
    config.deadzone_inner = 1.5f;
    config.deadzone_outer = 5.0f;
    config.x_deadzone_outer = 3.0f;
    config.ads_snap_max_ai_force = 1.0f;
    controller_native::NativeAiAim ai_aim(config);

    controller_native::NativeAiAimInput input;
    input.aiming = true;
    input.has_target = true;
    input.aim_authority = true;
    input.ads_snap_active = true;
    input.dx = 1.0f;
    input.dy = 0.0f;
    input.target_tier = "strong";
    input.observed_at_seconds = 10.0;
    input.now_seconds = 10.01;

    const controller_native::NativeAiAimOutput output = ai_aim.compute(input);
    require_true(!output.has_assist, "AI aim should suppress target error inside the inner deadzone");
}

void test_aim_assist_dynamics_bounds_authorized_steps_and_reversals() {
    controller_native::GamepadAimAssistDynamicsConfig config;
    config.enabled = true;
    controller_native::NativeAimAssistDynamics dynamics(config);

    controller_native::NativeAimAssistDynamicsInput input;
    input.authority = pipeline_contract::AssistAuthorityState::ObservedStrong;
    input.lifecycle = pipeline_contract::BodylockLifecycleState::Tracking;
    input.target_error_px = {12.0f, 8.0f};
    input.dt_seconds = 0.001;
    input.now_seconds = 20.0;
    input.requested_assist = {0.80f, 0.0f};
    controller_native::NativeAimAssistDynamicsOutput output = dynamics.apply(input);
    require_near(output.assist.x, 0.018f, 0.0001f,
                  "near-target assist attack must be bounded per nominal tick");

    input.now_seconds += 0.001;
    input.requested_assist.x = -0.80f;
    output = dynamics.apply(input);
    require_true(output.assist.x >= 0.0f,
                 "assist must decelerate through zero before changing sign");
}

void test_aim_assist_dynamics_preserves_manual_contract_at_authority_boundary() {
    controller_native::GamepadAimAssistDynamicsConfig config;
    config.enabled = true;
    controller_native::NativeAimAssistDynamics dynamics(config);

    controller_native::NativeAimAssistDynamicsInput input;
    input.manual = {0.80f, 0.66f};
    input.requested_assist = {-0.70f, -0.65f};
    input.authority = pipeline_contract::AssistAuthorityState::TrackOnly;
    input.lifecycle = pipeline_contract::BodylockLifecycleState::Yield;
    input.now_seconds = 20.0;
    const controller_native::NativeAimAssistDynamicsOutput output = dynamics.apply(input);
    require_near(output.assist.x, 0.0f, 0.0f,
                 "track-only must emit no residual X assist");
    require_near(output.assist.y, 0.0f, 0.0f,
                 "track-only must emit no residual Y assist");
}

void test_ads_reacquire_uses_existing_delivery_envelope() {
    controller_native::GamepadAimAssistDynamicsConfig config;
    config.enabled = true;
    controller_native::NativeAimAssistDynamics dynamics(config);

    controller_native::NativeAimAssistDynamicsInput input;
    input.authority = pipeline_contract::AssistAuthorityState::TrackOnly;
    input.lifecycle = pipeline_contract::BodylockLifecycleState::Inactive;
    input.dt_seconds = 0.001;
    input.now_seconds = 30.000;
    require_near(
        dynamics.apply(input).assist.x,
        0.0f,
        0.0f,
        "authority loss must release assist before reacquisition");

    input.authority = pipeline_contract::AssistAuthorityState::ObservedStrong;
    input.ads_snap_active = true;
    input.requested_assist = {0.60f, 0.0f};
    input.target_error_px = {50.0f, 0.0f};
    input.selected_track_id = 91;
    input.fresh_observation = true;
    input.vision_sequence = 10;
    input.now_seconds += 0.001;
    const controller_native::NativeAimAssistDynamicsOutput first = dynamics.apply(input);
    require_near(
        first.assist.x,
        0.018f,
        0.0001f,
        "ADS reacquisition must attack through the existing jerk-limited envelope");

    input.fresh_observation = false;
    input.now_seconds += 0.001;
    const controller_native::NativeAimAssistDynamicsOutput second = dynamics.apply(input);
    require_true(
        second.assist.x > first.assist.x &&
            second.assist.x - first.assist.x <= 0.0351f,
        "ADS reacquisition must become useful without a hard output step");
}

void test_body_lock_smoothing_is_owned_by_delivery_envelope() {
    controller_native::GamepadAimAssistDynamicsConfig config;
    config.enabled = true;
    controller_native::NativeAimAssistDynamics dynamics(config, 0.50f);

    controller_native::NativeAimAssistDynamicsInput input;
    input.requested_assist = {0.02f, 0.0f};
    input.authority = pipeline_contract::AssistAuthorityState::ObservedStrong;
    input.lifecycle = pipeline_contract::BodylockLifecycleState::Tracking;
    input.dt_seconds = 0.001;
    input.now_seconds = 20.0;

    const controller_native::NativeAimAssistDynamicsOutput output = dynamics.apply(input);
    require_near(
        output.assist.x,
        0.01f,
        0.0001f,
        "legacy body-lock smoothing must damp the delivery target inside the sole envelope");
}

void test_controller_body_lock_has_one_planner_and_one_delivery_owner() {
    controller_native::GamepadRuntimeConfig config;
    config.ai_aim.ads_snap_window_ms = 0;
    config.ai_aim.target_max_age_ms = 0.0f;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.piecewise_mid_pixels = 0.0f;
    config.ai_aim.piecewise_mid_pixels_y = 0.0f;
    config.ai_aim.deadzone_inner = 0.0f;
    config.ai_aim.deadzone_outer = 1.0f;
    config.ai_aim.x_deadzone_outer = 1.0f;
    config.ai_aim.body_lock_max_ai_force = 0.30f;
    config.ai_aim.body_lock_max_ai_force_y = 0.42f;
    config.ai_aim.body_lock_box_tolerance_px = 20.0f;
    config.ai_aim.body_lock_activation_box_px = 180.0f;
    config.ai_aim.body_lock_upper_body_ratio = 0.50f;
    config.ai_aim.body_lock_confidence_frames = 1;
    config.aim_assist_dynamics.enabled = true;
    config.recoil.enabled = false;

    double now = 15.0;
    controller_native::NativeGamepadController controller(
        config,
        [&now]() { return now; });

    controller_native::NativeControllerVisionState target;
    target.has_target = true;
    target.aim_authority = true;
    target.fire_authority = true;
    target.target_tier = "strong";
    target.dx = 24.0f;
    target.dy = 0.0f;
    target.screen_center_x = 320.0f;
    target.screen_center_y = 256.0f;
    target.has_body_box = true;
    target.body_x1 = 304.0f;
    target.body_x2 = 384.0f;
    target.body_y1 = 216.0f;
    target.body_y2 = 296.0f;
    target.observed_at_seconds = now;
    target.fresh_observation = true;
    target.vision_sequence = 1;
    target.selected_track_id = 7;
    controller.submit_vision_state(target);

    controller.build_output(aiming_physical_state());
    const controller_native::NativeControllerOutputComponents& components =
        controller.last_output_components();

    require_true(components.aim_mode == "body_lock", "test must enter body-lock");
    require_true(
        std::fabs(components.planned_assist_stick.x) > 0.001f,
        "the body-lock planner must own a non-zero desired assist");
    require_true(
        std::fabs(components.dynamic_adjustment_stick.x) > 0.001f,
        "the dynamics envelope must own delivered-assist shaping");
    require_near(
        components.body_lock_short_plan_stick.x,
        0.0f,
        0.0001f,
        "the ADS short-plan stage must not alter body-lock x");
    require_near(
        components.body_lock_short_plan_stick.y,
        0.0f,
        0.0001f,
        "the ADS short-plan stage must not alter body-lock y");
    require_near(
        components.output_validation_stick.x,
        0.0f,
        0.0001f,
        "output validation must not become a third body-lock x owner");
    require_near(
        components.output_validation_stick.y,
        0.0f,
        0.0001f,
        "output validation must not become a third body-lock y owner");
    require_near(
        components.ads_brake_stick.x,
        0.0f,
        0.0001f,
        "ADS near-target brake must not alter body-lock x");
    require_near(
        components.ads_carry_brake_stick.x,
        0.0f,
        0.0001f,
        "ADS carry brake must not alter body-lock x");
}

void test_aim_assist_dynamics_seeds_bodylock_from_post_brake_ads_ai() {
    controller_native::GamepadAimAssistDynamicsConfig config;
    config.enabled = true;
    controller_native::NativeAimAssistDynamics dynamics(config);
    dynamics.observe_pre_recoil_output(
        {0.20f, 0.0f},
        {0.50f, 0.0f},
        true,
        77);

    controller_native::NativeAimAssistDynamicsInput input;
    input.manual = {0.20f, 0.0f};
    input.requested_assist = {0.10f, 0.0f};
    input.authority = pipeline_contract::AssistAuthorityState::ObservedStrong;
    input.lifecycle = pipeline_contract::BodylockLifecycleState::Warm;
    input.target_error_px = {3.0f, 0.0f};
    input.selected_track_id = 77;
    input.dt_seconds = 0.001;
    input.now_seconds = 20.001;

    const controller_native::NativeAimAssistDynamicsOutput output = dynamics.apply(input);
    require_near(
        output.assist.x,
        0.282f,
        0.0001f,
        "first same-track BodyLock tick should decelerate from actual post-brake ADS AI");
}

void test_aim_assist_dynamics_releases_bodylock_monotonically_without_hard_reset() {
    controller_native::GamepadAimAssistDynamicsConfig config;
    config.enabled = true;
    controller_native::NativeAimAssistDynamics dynamics(config);

    controller_native::NativeAimAssistDynamicsInput input;
    input.requested_assist = {0.80f, -0.50f};
    input.authority = pipeline_contract::AssistAuthorityState::ObservedStrong;
    input.lifecycle = pipeline_contract::BodylockLifecycleState::Tracking;
    input.target_error_px = {40.0f, 30.0f};
    input.selected_track_id = 12;
    input.dt_seconds = 0.001;
    input.now_seconds = 40.0;

    controller_native::NativeAimAssistDynamicsOutput output;
    for (int i = 0; i < 32; ++i) {
        input.now_seconds += 0.001;
        output = dynamics.apply(input);
    }
    const common_native::Vec2f before_release = output.assist;
    require_true(
        std::hypot(before_release.x, before_release.y) > 0.50f,
        "test setup should establish material delivered BodyLock AI");

    input.requested_assist = {};
    input.authority = pipeline_contract::AssistAuthorityState::Reject;
    input.lifecycle = pipeline_contract::BodylockLifecycleState::Yield;
    float previous_magnitude = std::hypot(before_release.x, before_release.y);
    float max_tick_delta = 0.0f;
    common_native::Vec2f previous = before_release;
    for (int i = 0; i < 40; ++i) {
        input.now_seconds += 0.001;
        output = dynamics.apply(input);
        const float magnitude = std::hypot(output.assist.x, output.assist.y);
        require_true(
            magnitude <= previous_magnitude + 0.0001f,
            "BodyLock authority release must be monotonic");
        max_tick_delta = std::max(
            max_tick_delta,
            std::hypot(output.assist.x - previous.x, output.assist.y - previous.y));
        previous_magnitude = magnitude;
        previous = output.assist;
        input.lifecycle = pipeline_contract::BodylockLifecycleState::Inactive;
    }
    require_true(
        max_tick_delta <= 0.07f,
        "BodyLock release must stay inside the delivered envelope tick bound");
    require_true(
        previous_magnitude <= 0.001f,
        "BodyLock authority release must reach zero within 40 ms");
}

void test_bodylock_geometry_grace_and_reacquire_are_bumpless() {
    controller_native::BodylockLifecycle lifecycle;
    controller_native::GamepadAimAssistDynamicsConfig dynamics_config;
    dynamics_config.enabled = true;
    controller_native::NativeAimAssistDynamics dynamics(dynamics_config);

    controller_native::BodylockLifecycleInput lifecycle_input;
    lifecycle_input.aiming = true;
    lifecycle_input.bodylock_available = true;
    lifecycle_input.selected_track_id = 33;
    lifecycle_input.authority = pipeline_contract::AssistAuthorityState::ObservedStrong;
    lifecycle_input.authority_reason = pipeline_contract::AssistAuthorityReason::StrongObserved;
    lifecycle_input.now_seconds = 50.0;
    auto lifecycle_decision = lifecycle.update(lifecycle_input);

    controller_native::NativeAimAssistDynamicsInput dynamics_input;
    dynamics_input.requested_assist = {0.55f, 0.0f};
    dynamics_input.authority = lifecycle_input.authority;
    dynamics_input.lifecycle = lifecycle_decision.state;
    dynamics_input.selected_track_id = 33;
    dynamics_input.target_error_px = {30.0f, 0.0f};
    dynamics_input.dt_seconds = 0.001;
    dynamics_input.now_seconds = lifecycle_input.now_seconds;
    controller_native::NativeAimAssistDynamicsOutput output = dynamics.apply(dynamics_input);
    for (int tick = 0; tick < 24; ++tick) {
        lifecycle_input.now_seconds += 0.001;
        lifecycle_decision = lifecycle.update(lifecycle_input);
        dynamics_input.lifecycle = lifecycle_decision.state;
        dynamics_input.now_seconds = lifecycle_input.now_seconds;
        output = dynamics.apply(dynamics_input);
    }
    require_true(output.assist.x > 0.30f, "test setup should establish BodyLock output");

    lifecycle_input.bodylock_available = false;
    dynamics_input.requested_assist = {};
    float previous = output.assist.x;
    float max_delta = 0.0f;
    for (int tick = 0; tick < 39; ++tick) {
        lifecycle_input.now_seconds += 0.001;
        lifecycle_decision = lifecycle.update(lifecycle_input);
        require_true(
            lifecycle_decision.state == pipeline_contract::BodylockLifecycleState::Coast,
            "same-track geometry grace should remain in Coast for 40 ms");
        dynamics_input.authority = lifecycle_input.authority;
        dynamics_input.lifecycle = lifecycle_decision.state;
        dynamics_input.now_seconds = lifecycle_input.now_seconds;
        output = dynamics.apply(dynamics_input);
        max_delta = std::max(max_delta, std::fabs(output.assist.x - previous));
        previous = output.assist.x;
    }
    require_true(
        output.assist.x > 0.05f,
        "geometry grace should retain a bounded nonzero envelope seed");

    lifecycle_input.bodylock_available = true;
    lifecycle_input.now_seconds += 0.001;
    lifecycle_decision = lifecycle.update(lifecycle_input);
    require_true(
        lifecycle_decision.state == pipeline_contract::BodylockLifecycleState::Tracking,
        "valid same-track geometry should resume Tracking without a cold lifecycle");
    dynamics_input.requested_assist = {0.55f, 0.0f};
    dynamics_input.lifecycle = lifecycle_decision.state;
    dynamics_input.now_seconds = lifecycle_input.now_seconds;
    output = dynamics.apply(dynamics_input);
    max_delta = std::max(max_delta, std::fabs(output.assist.x - previous));
    require_true(output.assist.x > 0.05f, "geometry recovery must not restart from zero");

    lifecycle_input.authority = pipeline_contract::AssistAuthorityState::Reject;
    lifecycle_input.authority_reason = pipeline_contract::AssistAuthorityReason::Stale;
    dynamics_input.requested_assist = {};
    for (int tick = 0; tick < 10; ++tick) {
        lifecycle_input.now_seconds += 0.001;
        lifecycle_decision = lifecycle.update(lifecycle_input);
        dynamics_input.authority = lifecycle_input.authority;
        dynamics_input.lifecycle = lifecycle_decision.state;
        dynamics_input.now_seconds = lifecycle_input.now_seconds;
        previous = output.assist.x;
        output = dynamics.apply(dynamics_input);
        max_delta = std::max(max_delta, std::fabs(output.assist.x - previous));
    }

    lifecycle_input.authority = pipeline_contract::AssistAuthorityState::ObservedStrong;
    lifecycle_input.authority_reason = pipeline_contract::AssistAuthorityReason::StrongObserved;
    dynamics_input.requested_assist = {0.55f, 0.0f};
    bool useful_within_60ms = false;
    for (int tick = 0; tick < 60; ++tick) {
        lifecycle_input.now_seconds += 0.001;
        lifecycle_decision = lifecycle.update(lifecycle_input);
        dynamics_input.authority = lifecycle_input.authority;
        dynamics_input.lifecycle = lifecycle_decision.state;
        dynamics_input.now_seconds = lifecycle_input.now_seconds;
        previous = output.assist.x;
        output = dynamics.apply(dynamics_input);
        max_delta = std::max(max_delta, std::fabs(output.assist.x - previous));
        useful_within_60ms = useful_within_60ms || output.assist.x >= 0.20f;
    }
    require_true(useful_within_60ms, "valid reacquisition should restore useful AI within 60 ms");
    require_true(max_delta <= 0.07f, "geometry recovery and reacquisition must be bumpless");
}

void test_bodylock_planner_preserves_motion_feedforward_near_terminal() {
    controller_native::GamepadAiAimConfig config;
    config.max_pixels = 100.0f;
    config.deadzone_inner = 0.0f;
    config.deadzone_outer = 0.0f;
    config.x_deadzone_outer = 0.0f;
    config.piecewise_mid_pixels = 0.0f;
    config.piecewise_mid_pixels_y = 0.0f;
    config.body_lock_max_ai_force = 1.0f;
    config.body_lock_max_ai_force_y = 0.0f;
    config.body_lock_confidence_frames = 1;
    config.body_lock_upper_body_ratio = 0.50f;
    config.body_lock_activation_box_px = 180.0f;
    config.body_lock_release_tail_scale = 1.0f;
    config.body_lock_lead_frames = 2;
    config.body_lock_lead_seconds = 0.050f;
    config.body_lock_lead_max_px = 18.0f;
    config.body_lock_lateral_motion_min_speed_px_per_sec = 10000.0f;
    controller_native::NativeAiAim ai_aim(config);

    controller_native::NativeAiAimInput input;
    input.aiming = true;
    input.has_target = true;
    input.aim_authority = true;
    input.ads_snap_active = false;
    input.target_tier = "strong";
    input.selected_track_id = 91;
    input.bodylock_lifecycle_valid = true;
    input.bodylock_lifecycle = pipeline_contract::BodylockLifecycleState::Tracking;
    input.screen_center_x = 320.0f;
    input.screen_center_y = 256.0f;
    input.has_body_box = true;
    input.body_y1 = 216.0f;
    input.body_y2 = 296.0f;
    input.target_y = 256.0f;
    input.has_camera_attributed_velocity = true;
    input.camera_attributed_velocity_x_px_per_sec = 200.0f;

    controller_native::NativeAiAimOutput output;
    for (int frame = 0; frame < 3; ++frame) {
        const float error_x = 1.0f + static_cast<float>(frame);
        input.body_x1 = 280.0f + error_x;
        input.body_x2 = 360.0f + error_x;
        input.target_x = 320.0f + error_x;
        input.dx = error_x;
        input.vision_sequence = static_cast<std::uint64_t>(frame + 1);
        input.fresh_observation = true;
        input.observed_at_seconds = 30.0 + (static_cast<double>(frame) * 0.010);
        input.now_seconds = input.observed_at_seconds;
        output = ai_aim.compute(input);
    }

    require_true(
        output.position_assist_x > 0.0f,
        "near-terminal BodyLock should retain bounded position feedback");
    require_true(
        output.motion_feedforward_x > 0.04f,
        "terminal position limiting must not delete trusted moving-target feed-forward");
    require_true(
        output.assist_x >= output.motion_feedforward_x,
        "combined BodyLock request should include the surviving motion feed-forward");
}

void test_controller_ads_to_bodylock_handoff_preserves_delivered_envelope() {
    controller_native::GamepadRuntimeConfig config;
    config.ai_aim.target_max_age_ms = 0.0f;
    config.ai_aim.max_pixels = 130.0f;
    config.ai_aim.deadzone_inner = 0.0f;
    config.ai_aim.deadzone_outer = 0.0f;
    config.ai_aim.x_deadzone_outer = 0.0f;
    config.ai_aim.piecewise_mid_pixels = 0.0f;
    config.ai_aim.piecewise_mid_pixels_y = 0.0f;
    config.ai_aim.ads_snap_window_ms = 1000;
    config.ai_aim.ads_max_acquisition_ms = 1000.0f;
    config.ai_aim.ads_completion_radius_px = 8.0f;
    config.ai_aim.ads_completion_fresh_frames = 3;
    config.ai_aim.ads_snap_smoothing = 0.80f;
    config.ai_aim.ads_snap_time_to_go_gain = 0.0f;
    config.ai_aim.body_lock_smoothing = 0.0f;
    config.ai_aim.body_lock_confidence_frames = 1;
    config.ai_aim.body_lock_upper_body_ratio = 0.50f;
    config.ai_aim.body_lock_activation_box_px = 180.0f;
    config.ai_aim.target_projection_reticle_speed_px_per_sec = 1500.0f;
    config.aim_assist_dynamics.enabled = true;
    config.recoil.enabled = false;

    double now = 70.0;
    controller_native::NativeGamepadController controller(
        config,
        [&now]() { return now; });

    const auto submit_stationary_target = [&](float error_x) {
        controller_native::NativeControllerVisionState target;
        target.has_target = true;
        target.aim_authority = true;
        target.fire_authority = true;
        target.target_tier = "strong";
        target.selected_track_id = 77;
        target.dx = error_x;
        target.dy = 0.0f;
        target.target_x = 320.0f + error_x;
        target.target_y = 256.0f;
        target.screen_center_x = 320.0f;
        target.screen_center_y = 256.0f;
        target.has_body_box = true;
        target.body_x1 = 280.0f + error_x;
        target.body_x2 = 360.0f + error_x;
        target.body_y1 = 216.0f;
        target.body_y2 = 296.0f;
        target.observed_at_seconds = now;
        controller.submit_vision_state(target);
    };

    const controller_native::PhysicalGamepadState physical = aiming_physical_state();
    controller.build_output(physical);
    for (int i = 0; i < 16; ++i) {
        now += 0.010;
        submit_stationary_target(60.0f);
        controller.build_output(physical);
    }

    constexpr float kClosingErrors[] = {20.0f, 11.0f, 7.0f, 5.0f, 3.0f};
    std::vector<float> closing_position_feedback;
    float previous_ads_output = 0.0f;
    float first_bodylock_output = 0.0f;
    bool crossed_to_bodylock = false;
    for (float error_x : kClosingErrors) {
        now += 0.010;
        submit_stationary_target(error_x);
        controller.build_output(physical);
        const float pre_recoil_x =
            controller.last_output_components().before_recoil_stick.x;
        closing_position_feedback.push_back(std::fabs(pre_recoil_x));
        if (controller.last_ai_aim_mode() == "body_lock") {
            first_bodylock_output = pre_recoil_x;
            crossed_to_bodylock = true;
            break;
        }
        previous_ads_output = pre_recoil_x;
    }

    for (int i = 0; i < 40 && !crossed_to_bodylock; ++i) {
        now += 0.010;
        submit_stationary_target(3.0f);
        controller.build_output(physical);
        const float pre_recoil_x =
            controller.last_output_components().before_recoil_stick.x;
        closing_position_feedback.push_back(std::fabs(pre_recoil_x));
        if (controller.last_ai_aim_mode() == "body_lock") {
            first_bodylock_output = pre_recoil_x;
            crossed_to_bodylock = true;
        } else {
            previous_ads_output = pre_recoil_x;
        }
    }

    require_true(crossed_to_bodylock, "test setup should complete ADS into BodyLock");
    const float first_bodylock_tick_delta =
        std::fabs(first_bodylock_output - previous_ads_output);
    require_true(
        first_bodylock_tick_delta <= 0.07f,
        "ADS-to-BodyLock must preserve the delivered AI envelope");

    constexpr float kTerminalHorizonSeconds = 0.050f;
    const float max_transition_overshoot_px = std::max(
        0.0f,
        (std::fabs(first_bodylock_output) *
         config.ai_aim.target_projection_reticle_speed_px_per_sec *
         kTerminalHorizonSeconds) -
            3.0f);
    require_true(
        max_transition_overshoot_px <= 2.0f,
        "stationary-target handoff must not create transition overshoot");

    bool position_feedback_is_non_increasing = true;
    for (std::size_t i = 1; i < closing_position_feedback.size(); ++i) {
        if (closing_position_feedback[i] > closing_position_feedback[i - 1] + 0.0001f) {
            position_feedback_is_non_increasing = false;
            break;
        }
    }
    require_true(
        position_feedback_is_non_increasing,
        "closing position feedback must decay until terminal approach is safe");
}



void test_controller_recoil_uses_runtime_ads_or_hipfire_profile_selection() {
    const std::filesystem::path root = make_temp_test_dir("controller_profile_select");
    const std::filesystem::path profile_dir = root / "profiles";
    std::filesystem::create_directories(profile_dir);
    const std::filesystem::path state_path = root / "latest-state.json";

    write_text_file(
        profile_dir / "profile-cod22-m4-ads-standing-high.json",
        recoil_profile_json("profile-cod22-m4-ads-standing-high", "cod22-m4", "ads", 0.95f, 12.0f));
    write_text_file(
        profile_dir / "profile-cod22-m4-hipfire-standing-high.json",
        recoil_profile_json("profile-cod22-m4-hipfire-standing-high", "cod22-m4", "hipfire", 0.95f, 2.0f));
    write_text_file(
        state_path,
        recognizer_state_json("cod22-m4", "profile-cod22-m4-ads-standing-high"));

    controller_native::GamepadRuntimeConfig config;
    config.ai_aim.max_pixels = 100.0f;
    config.recoil.profile_directory = profile_dir.string();
    config.recoil.recognizer_state_path = state_path.string();
    config.recoil.profile_amount = 1.0f;
    config.recoil.profile_x_amount = 1.0f;
    config.recoil.profile_velocity_reference_ms = 10.0f;
    config.recoil.profile_despike_enabled = false;
    config.recoil.piecewise_mid_pixels_y = 10.0f;
    config.recoil.piecewise_max_pixels_y = 20.0f;
    config.recoil.piecewise_mid_ratio_y = 0.50f;

    controller_native::NativeGamepadController ads_controller(config);
    controller_native::PhysicalGamepadState ads_physical = aiming_physical_state();
    ads_physical.right_trigger = 1.0f;
    ads_controller.build_output(ads_physical);
    std::this_thread::sleep_for(std::chrono::milliseconds(12));
    const controller_native::GamepadOutputState ads_output = ads_controller.build_output(ads_physical);

    controller_native::NativeGamepadController hipfire_controller(config);
    controller_native::PhysicalGamepadState hipfire_physical;
    hipfire_physical.connected = true;
    hipfire_physical.right_trigger = 1.0f;
    hipfire_controller.build_output(hipfire_physical);
    std::this_thread::sleep_for(std::chrono::milliseconds(12));
    const controller_native::GamepadOutputState hipfire_output =
        hipfire_controller.build_output(hipfire_physical);

    require_true(
        ads_output.right_y < hipfire_output.right_y - 0.20f,
        "ADS recoil profile should pull harder than hipfire profile");
}

void test_controller_recoil_profile_is_not_suppressed_by_target_direction_state() {
    const std::filesystem::path root = make_temp_test_dir("controller_recoil_target_isolation");
    const std::filesystem::path profile_dir = root / "profiles";
    std::filesystem::create_directories(profile_dir);
    const std::filesystem::path state_path = root / "latest-state.json";

    write_text_file(
        profile_dir / "profile-cod22-m4-ads-standing-high.json",
        recoil_profile_json("profile-cod22-m4-ads-standing-high", "cod22-m4", "ads", 0.95f, 12.0f));
    write_text_file(
        state_path,
        recognizer_state_json("cod22-m4", "profile-cod22-m4-ads-standing-high"));

    controller_native::GamepadRuntimeConfig config;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.max_ai_force = 0.05f;
    config.ai_aim.max_ai_force_y = 0.05f;
    config.ai_aim.target_max_age_ms = 500.0f;
    config.aim_assist_dynamics.enabled = false;
    config.recoil.profile_directory = profile_dir.string();
    config.recoil.recognizer_state_path = state_path.string();
    config.recoil.profile_amount = 1.0f;
    config.recoil.profile_x_amount = 1.0f;
    config.recoil.profile_velocity_reference_ms = 10.0f;
    config.recoil.profile_despike_enabled = false;
    config.recoil.piecewise_mid_pixels_y = 10.0f;
    config.recoil.piecewise_max_pixels_y = 20.0f;
    config.recoil.piecewise_mid_ratio_y = 0.50f;

    controller_native::NativeGamepadController controller(config);
    controller_native::NativeControllerVisionState target;
    target.has_target = true;
    target.aim_authority = true;
    target.fire_authority = true;
    target.dx = 0.0f;
    target.dy = -80.0f;
    target.target_tier = "strong";
    target.observed_at_seconds = now_seconds();
    controller.submit_vision_state(target);

    controller_native::PhysicalGamepadState physical = aiming_physical_state();
    physical.right_trigger = 1.0f;
    controller.build_output(physical);
    std::this_thread::sleep_for(std::chrono::milliseconds(12));
    controller.build_output(physical);

    require_true(
        controller.last_output_components().recoil_stick.y < -0.20f,
        "recoil profile output should not be suppressed by controller target direction state");
}

void test_controller_recoil_uses_fallback_when_no_recognizer_state_is_configured() {
    const std::filesystem::path root = make_temp_test_dir("controller_recoil_no_state");
    const std::filesystem::path profile_dir = root / "profiles";
    std::filesystem::create_directories(profile_dir);

    write_text_file(
        profile_dir / "profile-cod22-m4-hipfire-standing-high.json",
        recoil_profile_json("profile-cod22-m4-hipfire-standing-high", "cod22-m4", "hipfire", 0.95f, 12.0f));

    controller_native::GamepadRuntimeConfig config;
    config.recoil.profile_directory = profile_dir.string();
    config.recoil.recognizer_state_path.clear();
    config.recoil.feedback_amount = 0.15f;
    config.recoil.profile_velocity_reference_ms = 10.0f;
    config.recoil.profile_despike_enabled = false;
    config.recoil.piecewise_mid_pixels_y = 10.0f;
    config.recoil.piecewise_max_pixels_y = 20.0f;
    config.recoil.piecewise_mid_ratio_y = 0.50f;

    controller_native::NativeGamepadController controller(config);
    controller_native::PhysicalGamepadState physical;
    physical.connected = true;
    physical.right_trigger = 1.0f;

    controller.build_output(physical);
    std::this_thread::sleep_for(std::chrono::milliseconds(12));
    const controller_native::GamepadOutputState output = controller.build_output(physical);

    require_near(
        output.right_y,
        -0.15f,
        0.03f,
        "recoil should use fixed fallback when no recognizer state is configured");
}








void test_ads_carry_brake_policy_ignores_inactive_contexts() {
    controller_native::AdsCarryBrakePolicy policy;
    controller_native::AdsCarryBrakeInput input;
    input.output.right_x = 0.82f;
    input.manual_right_x = 0.62f;
    input.target_error_x = -8.0f;
    input.ads_active = true;
    input.ads_acquisition_active = true;
    input.body_lock_active = false;
    input.has_fresh_target = true;
    input.reticle_speed_px_per_sec = 1500.0f;

    const controller_native::GamepadOutputState output = policy.apply(input);
    require_near(
        output.right_x,
        0.82f,
        0.001f,
        "ADS carry brake should ignore non-body-lock contexts");
}

void test_ads_carry_brake_policy_does_not_correct_bodylock_output() {
    controller_native::AdsCarryBrakePolicy policy;
    controller_native::AdsCarryBrakeInput input;
    input.output.right_x = 0.82f;
    input.manual_right_x = 0.62f;
    input.target_error_x = -8.0f;
    input.ads_active = true;
    input.ads_acquisition_active = true;
    input.body_lock_active = true;
    input.has_fresh_target = true;
    input.reticle_speed_px_per_sec = 1500.0f;

    const controller_native::GamepadOutputState output = policy.apply(input);
    require_near(
        output.right_x,
        0.82f,
        0.001f,
        "ADS carry brake must not reverse user-owned body-lock output");
}

void test_ads_carry_brake_policy_does_not_own_bodylock_terminal_limiting() {
    controller_native::AdsCarryBrakePolicy policy;
    controller_native::AdsCarryBrakeInput input;
    input.output.right_x = 0.92f;
    input.manual_right_x = 0.62f;
    input.target_error_x = 10.0f;
    input.ads_active = true;
    input.ads_acquisition_active = true;
    input.body_lock_active = true;
    input.has_fresh_target = true;
    input.reticle_speed_px_per_sec = 1500.0f;

    const controller_native::GamepadOutputState output = policy.apply(input);
    require_near(
        output.right_x,
        0.92f,
        0.001f,
        "ADS carry brake must leave BodyLock terminal limiting to the BodyLock planner");
}

void test_ads_carry_brake_policy_limits_unfresh_ads_acquisition_stack() {
    controller_native::AdsCarryBrakePolicy policy;
    controller_native::AdsCarryBrakeInput input;
    input.output.right_x = 0.92f;
    input.manual_right_x = 0.62f;
    input.target_error_x = 10.0f;
    input.ads_active = true;
    input.ads_acquisition_active = true;
    input.body_lock_active = false;
    input.has_fresh_target = false;
    input.reticle_speed_px_per_sec = 1500.0f;

    const controller_native::GamepadOutputState output = policy.apply(input);
    require_true(
        output.right_x > 0.0f && output.right_x <= 0.45f,
        "ADS acquisition should not keep high same-direction output without fresh target evidence");
}

void test_ads_carry_brake_policy_vector_caps_unfresh_diagonal_stack() {
    controller_native::AdsCarryBrakePolicy policy;
    controller_native::AdsCarryBrakeInput input;
    input.output.right_x = 0.92f;
    input.output.right_y = -0.92f;
    input.manual_right_x = 0.62f;
    input.manual_right_y = -0.62f;
    input.target_error_x = 10.0f;
    input.target_error_y = 10.0f;
    input.ads_active = true;
    input.ads_acquisition_active = true;
    input.has_fresh_target = false;
    input.reticle_speed_px_per_sec = 1500.0f;

    const controller_native::GamepadOutputState output = policy.apply(input);
    require_true(
        std::hypot(output.right_x, output.right_y) <= 0.45f,
        "ADS acquisition should vector-cap diagonal output without fresh target evidence");
    require_true(
        output.right_x > 0.0f && output.right_y < 0.0f,
        "unfresh ADS vector cap should preserve stick direction");
}

void test_ads_carry_brake_policy_does_not_cross_axis_cap_same_direction_output() {
    controller_native::AdsCarryBrakePolicy policy;
    controller_native::AdsCarryBrakeInput input;
    input.output.right_x = 0.92f;
    input.manual_right_y = 0.72f;
    input.target_error_x = 10.0f;
    input.ads_active = true;
    input.ads_acquisition_active = true;
    input.body_lock_active = true;
    input.has_fresh_target = true;
    input.reticle_speed_px_per_sec = 1500.0f;

    const controller_native::GamepadOutputState output = policy.apply(input);
    require_near(
        output.right_x,
        0.92f,
        0.001f,
        "ADS carry brake should not cap X output from strong Y-axis manual input");
}

}  // namespace

int main() {
    try {
        test_common_native_types_compile();
        test_bodylock_motion_policy_leads_after_consistent_direction();
        test_bodylock_motion_policy_clears_lead_on_direction_reversal();
        test_bodylock_motion_policy_ignores_weak_observations();
        test_tracker_authority_classifies_target_tiers();
        test_adapter_downgrades_color_checked_wide_low_detection_without_enemy_evidence();
        test_auto_fire_requires_fire_authority();
        test_auto_fire_blocks_stale_source();
        test_controller_passes_extended_buttons_and_dpad_through();
        test_controller_records_pipeline_stage_traces();
        test_controller_pipeline_records_recoil_as_final_independent_component();
        test_controller_tick_context_carries_tracker_snapshot_and_output_components();
        test_auto_fire_manual_takeover_releases_output_briefly();
        test_auto_fire_requires_aim_ready_settle_frames();
        test_auto_fire_uses_80hz_vision_frames_at_1000hz_controller_rate();
        test_scope_occlusion_owner_hold_releases_and_reacquires_smoothly();
        test_auto_fire_aim_ready_gate_can_be_disabled();
        test_auto_fire_ready_allows_manual_right_stick_when_fire_zone_is_hit();
        test_controller_left_thumb_does_not_count_as_aiming();
        test_controller_left_trigger_release_drop_ends_aiming_before_idle();
        test_controller_light_left_trigger_press_counts_as_aiming();
        test_no_update_vision_result_preserves_latest_target();
        test_controller_ads_resume_waits_for_fresh_vision();
        test_controller_accepts_controller_vision_snapshot_without_vision_result();
        test_controller_ads_drops_tracker_only_vertical_snap_after_processed_miss();
        test_controller_bodylock_coasts_then_releases_at_1000hz();
        test_target_tracker_projects_camera_motion_between_vision_frames();
        test_legacy_projection_tracker_matches_native_project_output();
        test_legacy_projection_tracker_expires_after_max_age();
        test_legacy_projection_tracker_empty_observation_clears_snapshot();
        test_tracker_backend_config_defaults_to_fps_reference();
        test_runtime_config_defaults_recoil_recognizer_state_path();
        test_runtime_config_env_can_disable_recoil_runtime();
        test_runtime_config_rejects_unknown_tracker_backend();
        test_runtime_config_parses_ads_snap_fov_scale();
        test_experimental_kalman_tracker_backend_can_be_constructed();
        test_fps_reference_tracker_coasts_after_processed_miss_without_fire_authority();
        test_legacy_tracker_backend_factory_matches_legacy_projection();
        test_controller_projects_target_during_no_update_ticks();
        test_controller_expires_projection_before_aim_target_age();
        test_fps_reference_controller_clears_no_update_after_projection_ttl();
        test_fps_reference_controller_ignores_unauthorized_raw_detections();
        test_controller_tracker_records_component_aware_final_motion();
        test_controller_recoil_ignores_tracker_only_projected_targets();
        test_controller_tracker_final_motion_is_not_config_toggled();
        test_controller_output_components_capture_recoil_after_tracker_sample();
        test_controller_projects_body_box_during_no_update_ticks();
        test_ai_aim_scales_weak_and_cue_targets();
        test_ai_aim_body_lock_preserves_selected_target_y();
        test_body_lock_suppresses_harmful_manual_input_after_confidence_builds();
        test_body_lock_preserves_strong_manual_escape_input();
        test_body_lock_manual_priority_has_no_commit_or_release_timer();
        test_body_lock_counts_aligned_manual_input_as_planned_correction_near_lock();
        test_body_lock_damps_orthogonal_manual_input_near_lock();
        test_body_lock_restores_vertical_tail_help_after_continuous_target_history();
        test_body_lock_motion_updates_once_per_fresh_sequence();
        test_body_lock_warm_left_reversal_predicts_between_vision_frames();
        test_body_lock_gain_uses_camera_attributed_tracker_velocity();
        test_body_lock_lead_projects_fresh_observation_latency();
        test_body_lock_cold_relative_motion_responds_within_eighty_ms();
        test_body_lock_learns_from_delayed_strafe_acceleration_inside_event_window();
        test_body_lock_relative_motion_distinguishes_synchronized_and_opposite_motion();
        test_body_lock_rejects_changed_mobility_on_left_reversal_and_reset_clears_prior();
        test_body_lock_uses_lateral_motion_when_current_error_is_inside_deadzone();
        test_body_lock_sustained_motion_lead_increases_follow_after_consistent_strong_history();
        test_body_lock_sustained_motion_lead_clears_on_direction_reversal();
        test_body_lock_sustained_motion_lead_ignores_weak_targets();
        test_body_lock_can_disable_release_tail_to_zero_x_axis_inside_window();
        test_body_lock_preserves_more_horizontal_tail_for_moving_target_inside_release_window();
        test_body_lock_clears_release_tail_carry_on_near_zero_x_sign_flip();
        test_controller_body_lock_short_plan_damps_small_vector_turn_near_lock();
        test_controller_body_lock_has_one_planner_and_one_delivery_owner();
        test_controller_body_lock_preserves_manual_after_target_crossing();
        test_controller_body_lock_preserves_helpful_manual_near_lock_edge();
        test_controller_ads_brakes_large_manual_after_target_crossing_without_body_lock();
        test_controller_ads_corrects_small_manual_after_target_crossing_without_body_lock();
        test_controller_ads_cross_brake_survives_fresh_stale_sign_flip();
        test_controller_ads_near_target_brake_allows_bounded_chase_before_crossing();
        test_controller_ads_near_target_brake_preserves_manual_input();
        test_controller_ads_near_target_brake_caps_manual_dominated_carry();
        test_controller_ads_snap_brake_survives_manual_yield_after_confirmed_crossing();
        test_controller_ads_snap_yields_to_opposing_manual_without_crossing_evidence();
        test_controller_ads_snap_yields_when_manual_starts_only_after_crossing();
        test_controller_ads_snap_crossing_brake_expires_to_manual_control();
        test_controller_ads_snap_crossing_brake_is_axis_local();
        test_controller_output_validation_corrects_wrong_way_after_x_crossing();
        test_controller_output_validation_caps_only_crossed_axis_on_diagonal();
        test_controller_output_validation_yields_to_manual_correction_with_tracker_reference();
        test_controller_output_validation_does_not_turn_manual_into_assist();
        test_controller_suspicious_target_jump_holds_ai_aim_until_verified();
        test_controller_moderate_stale_jump_coasts_on_tracker_projection();
        test_controller_fresh_stale_jump_inside_candidate_threshold_uses_tracker_projection();
        test_controller_accepts_sustained_candidate_after_fresh_samples();
        test_controller_candidate_hold_does_not_reverse_opposing_manual();
        test_controller_ads_candidate_projection_hold_preserves_manual_during_gap();
        test_controller_candidate_projection_hold_uses_projection_time_for_freshness();
        test_controller_suspicious_candidate_without_projection_holds_output_until_verified();
        test_controller_accepts_moving_candidate_after_fresh_samples();
        test_controller_verified_candidate_reopens_ads_snap_after_initial_window();
        test_controller_projection_envelope_without_authority_stays_manual();
        test_controller_ads_holds_recent_strong_target_when_projection_ages_out();
        test_body_lock_clears_vertical_axis_on_near_zero_sign_flip();
        test_body_lock_applies_motion_lead_after_configured_history_frames();
        test_body_lock_confidence_resets_when_body_box_no_longer_matches_target();
        test_controller_ads_snap_only_runs_inside_ads_window_without_body_lock();
        test_auto_fire_ready_uses_body_lock_error_when_body_box_is_active();
        test_ads_snap_counts_same_direction_manual_as_planned_correction();
        test_ads_snap_softens_opposing_manual_instead_of_canceling_snap();
        test_ads_snap_damps_orthogonal_manual_curve_without_removing_it();
        test_ads_snap_uses_tracker_projection_as_mixing_reference();
        test_ai_aim_piecewise_mapping_matches_python_midpoint();
        test_ads_snap_fov_scale_reduces_ads_pull();
        test_ads_snap_fov_scale_transitions_over_configured_window();
        test_ads_snap_time_to_go_can_override_piecewise_mapping();
        test_ads_snap_clamps_vertical_target_delta_before_mapping();
        test_ai_aim_fire_active_does_not_add_ads_snap_force_cap();
        test_ai_aim_fire_active_caps_downward_ads_snap_vertical_stack();
        test_fire_active_projected_target_suppresses_vertical_aim_assist();
        test_body_lock_fire_active_caps_downward_vertical_stack_without_x_force_cap();
        test_ads_carry_brake_policy_ignores_inactive_contexts();
        test_ads_carry_brake_policy_does_not_correct_bodylock_output();
        test_ads_carry_brake_policy_does_not_own_bodylock_terminal_limiting();
        test_ads_carry_brake_policy_limits_unfresh_ads_acquisition_stack();
        test_ads_carry_brake_policy_vector_caps_unfresh_diagonal_stack();
        test_ads_carry_brake_policy_does_not_cross_axis_cap_same_direction_output();
        test_ads_snap_smoothing_interpolates_first_assist_frame();
        test_ai_aim_deadzone_suppresses_tiny_target_error();
        test_aim_assist_dynamics_bounds_authorized_steps_and_reversals();
        test_body_lock_smoothing_is_owned_by_delivery_envelope();
        test_aim_assist_dynamics_preserves_manual_contract_at_authority_boundary();
        test_ads_reacquire_uses_existing_delivery_envelope();
        test_aim_assist_dynamics_seeds_bodylock_from_post_brake_ads_ai();
        test_aim_assist_dynamics_releases_bodylock_monotonically_without_hard_reset();
        test_bodylock_geometry_grace_and_reacquire_are_bumpless();
        test_bodylock_planner_preserves_motion_feedforward_near_terminal();
        test_controller_ads_to_bodylock_handoff_preserves_delivered_envelope();
        test_controller_recoil_uses_runtime_ads_or_hipfire_profile_selection();
        test_controller_recoil_profile_is_not_suppressed_by_target_direction_state();
        test_controller_recoil_uses_fallback_when_no_recognizer_state_is_configured();
    } catch (const std::exception& exc) {
        std::cerr << "[NativeControllerTests] FAIL " << exc.what() << "\n";
        return 1;
    }

    std::cout << "[NativeControllerTests] PASS\n";
    return 0;
}
