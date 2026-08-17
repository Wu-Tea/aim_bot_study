#pragma once

#include "native_gamepad_controller.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace controller_native::incident_fixture {

struct TargetSpec {
    float center_x = 320.0f;
    float center_y = 256.0f;
    float body_width = 56.0f;
    float body_height = 140.0f;
    float aim_height_ratio = 0.365f;
    std::uint64_t observation_id = 0;
    std::uint64_t selector_generation = 0;
    bool candidate_has_aim_region = true;
    bool color_classified = false;
    bool fire_authority = false;
    bool has_enemy_cue = false;
    bool enemy_identity_confirmed = false;
    float confidence = 0.95f;
};

inline GamepadRuntimeConfig base_config(
    float max_observation_age_ms,
    float ads_activation_radius_px) {
    GamepadRuntimeConfig config;
    config.recoil.enabled = false;
    config.recoil.profile_playback_enabled = false;
    config.recoil.selection_log_enabled = false;
    config.tracker.max_observation_age_ms = max_observation_age_ms;
    config.ai_aim.ads_activation_radius_px = ads_activation_radius_px;
    config.ai_aim.ads_snap_max_ai_force = 1.0f;
    config.ai_aim.ads_snap_max_ai_force_y = 1.0f;
    return config;
}

inline PhysicalGamepadState ads_input(
    float right_x = 0.0f,
    float right_y = 0.0f,
    bool firing = false) {
    PhysicalGamepadState physical;
    physical.connected = true;
    physical.left_trigger = 1.0f;
    physical.right_trigger = firing ? 1.0f : 0.0f;
    physical.right_x = right_x;
    physical.right_y = right_y;
    return physical;
}

inline pipeline_contract::VisionCandidateSnapshot observed_candidate(
    const TargetSpec& spec,
    float dx,
    float dy) {
    pipeline_contract::VisionCandidateSnapshot value;
    value.id = spec.observation_id;
    value.valid = true;
    value.has_aim_point = true;
    value.aim_point_px = {spec.center_x + dx, spec.center_y + dy};
    value.body_box_px = {
        value.aim_point_px.x - spec.body_width * 0.5f,
        value.aim_point_px.y - spec.body_height * spec.aim_height_ratio,
        spec.body_width,
        spec.body_height,
    };
    if (spec.candidate_has_aim_region) {
        value.aim_region_px = value.body_box_px;
        value.aim_region_source =
            pipeline_contract::AimRegionSource::BodyBoxFallback;
        value.has_aim_region = true;
    }
    value.confidence = spec.confidence;
    value.color_classified = spec.color_classified;
    value.has_cue_point = spec.has_enemy_cue;
    value.cue_score = spec.has_enemy_cue ? 1.0f : 0.0f;
    value.suggested_authority_state =
        common_native::TargetAuthorityState::StrongAssist;
    return value;
}

inline ControllerVisionSnapshot observed_snapshot(
    const TargetSpec& spec,
    std::uint64_t frame_id,
    double now,
    float dx,
    float dy,
    bool selector_changed = false) {
    const auto selected = observed_candidate(spec, dx, dy);
    ControllerVisionSnapshot snapshot;
    snapshot.frame_updated = true;
    snapshot.selector_identity_protocol = true;
    snapshot.frame_id = frame_id;
    snapshot.capture_time_seconds = now;
    snapshot.ready_time_seconds = now;
    snapshot.selected_observation_id = spec.observation_id;
    snapshot.selector_target_generation = spec.selector_generation;
    snapshot.selector_target_changed = selector_changed;
    snapshot.enemy_cue_current = spec.has_enemy_cue;
    snapshot.enemy_identity_confirmed =
        spec.enemy_identity_confirmed || spec.has_enemy_cue;
    snapshot.state.has_target = true;
    snapshot.state.aim_authority = true;
    snapshot.state.fire_authority = spec.fire_authority;
    snapshot.state.enemy_cue_current = snapshot.enemy_cue_current;
    snapshot.state.enemy_identity_confirmed =
        snapshot.enemy_identity_confirmed;
    snapshot.state.dx = dx;
    snapshot.state.dy = dy;
    snapshot.state.target_x = selected.aim_point_px.x;
    snapshot.state.target_y = selected.aim_point_px.y;
    snapshot.state.screen_center_x = spec.center_x;
    snapshot.state.screen_center_y = spec.center_y;
    snapshot.state.has_body_box = true;
    snapshot.state.body_x1 = selected.body_box_px.x;
    snapshot.state.body_y1 = selected.body_box_px.y;
    snapshot.state.body_x2 = selected.body_box_px.x + selected.body_box_px.w;
    snapshot.state.body_y2 = selected.body_box_px.y + selected.body_box_px.h;
    snapshot.state.target_tier = "observed_strong";
    snapshot.state.observed_at_seconds = now;
    snapshot.candidates.push_back(selected);
    return snapshot;
}

inline ControllerVisionSnapshot cue_snapshot(
    const TargetSpec& spec,
    std::uint64_t frame_id,
    double now,
    float dx,
    float dy) {
    const auto body = observed_candidate(spec, dx, dy).body_box_px;
    ControllerVisionSnapshot snapshot;
    snapshot.frame_updated = true;
    snapshot.selector_identity_protocol = true;
    snapshot.frame_id = frame_id;
    snapshot.capture_time_seconds = now;
    snapshot.ready_time_seconds = now;
    snapshot.selector_target_generation = spec.selector_generation;
    snapshot.enemy_cue_current = true;
    snapshot.enemy_identity_confirmed = true;
    snapshot.state.has_target = true;
    snapshot.state.aim_authority = true;
    snapshot.state.fire_authority = false;
    snapshot.state.enemy_cue_current = true;
    snapshot.state.enemy_identity_confirmed = true;
    snapshot.state.dx = dx;
    snapshot.state.dy = dy;
    snapshot.state.target_x = spec.center_x + dx;
    snapshot.state.target_y = spec.center_y + dy;
    snapshot.state.screen_center_x = spec.center_x;
    snapshot.state.screen_center_y = spec.center_y;
    snapshot.state.has_body_box = true;
    snapshot.state.body_x1 = body.x;
    snapshot.state.body_y1 = body.y;
    snapshot.state.body_x2 = body.x + body.w;
    snapshot.state.body_y2 = body.y + body.h;
    snapshot.state.target_tier = "cue_hold";
    snapshot.state.observed_at_seconds = now;
    return snapshot;
}

inline ControllerVisionSnapshot empty_snapshot(
    const TargetSpec& spec,
    std::uint64_t frame_id,
    double now) {
    ControllerVisionSnapshot snapshot;
    snapshot.frame_updated = true;
    snapshot.selector_identity_protocol = true;
    snapshot.frame_id = frame_id;
    snapshot.capture_time_seconds = now;
    snapshot.ready_time_seconds = now;
    snapshot.state.screen_center_x = spec.center_x;
    snapshot.state.screen_center_y = spec.center_y;
    return snapshot;
}

inline bool finite_unit(float value) noexcept {
    return std::isfinite(value) && std::fabs(value) <= 1.0f + 1.0e-6f;
}

inline std::filesystem::path output_path_from_args(
    int argc,
    char** argv,
    const std::filesystem::path& default_path = {}) {
    if (argc == 3 && std::string(argv[1]) == "--output") {
        return std::filesystem::path(argv[2]);
    }
    if (argc == 1 && !default_path.empty()) return default_path;
    throw std::invalid_argument("usage: fixture --output <report.json>");
}

inline std::ofstream open_report(
    const std::filesystem::path& output,
    bool binary = false) {
    if (!output.parent_path().empty()) {
        std::filesystem::create_directories(output.parent_path());
    }
    auto mode = std::ios::out | std::ios::trunc;
    if (binary) mode |= std::ios::binary;
    std::ofstream stream(output, mode);
    if (!stream) throw std::runtime_error("failed to open report output");
    return stream;
}

}  // namespace controller_native::incident_fixture
