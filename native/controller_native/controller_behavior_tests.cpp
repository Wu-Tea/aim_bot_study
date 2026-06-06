#include "aim_assist_dynamics.h"
#include "ai_aim.h"
#include "native_gamepad_controller.h"
#include "recoil_compensation.h"
#include "recoil_profile.h"
#include "target_tracker.h"
#include "weapon_recognizer.h"

#include <cmath>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
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

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to read test file: " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

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

std::string recoil_calibration_json(
    const std::string& aim_mode,
    float x_rate,
    float y_rate) {
    std::ostringstream out;
    out
        << "{\n"
        << "  \"game\": \"cod22\",\n"
        << "  \"aim_mode\": \"" << aim_mode << "\",\n"
        << "  \"stance\": \"standing\",\n"
        << "  \"pixels_per_full_stick_x_per_second\": " << x_rate << ",\n"
        << "  \"pixels_per_full_stick_y_per_second\": " << y_rate << ",\n"
        << "  \"created_at\": \"2026-05-20T00:00:00Z\"\n"
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

std::string weapon_identity_json(
    const std::string& weapon_id,
    const std::string& display_name,
    const std::string& alias_name) {
    std::ostringstream out;
    out
        << "{\n"
        << "  \"canonical_weapon_id\": \"" << weapon_id << "\",\n"
        << "  \"game\": \"cod22\",\n"
        << "  \"weapon_family\": \"test\",\n"
        << "  \"display_name\": \"" << display_name << "\",\n"
        << "  \"alias_names\": [\"" << alias_name << "\"],\n"
        << "  \"blueprint_names\": [],\n"
        << "  \"signature_refs\": [],\n"
        << "  \"notes\": \"test\",\n"
        << "  \"created_at\": \"2026-06-06T12:00:00Z\",\n"
        << "  \"updated_at\": \"2026-06-06T12:00:00Z\"\n"
        << "}\n";
    return out.str();
}

std::string small_artery_name_utf8() {
    return std::string("\xE5\xB0\x8F\xE5\x8A\xA8\xE8\x84\x89");
}

std::string lesser_artery_name_utf8() {
    return std::string("\xE5\xB0\x91\xE5\x8A\xA8\xE8\x84\x89");
}

std::string artery_suffix_with_space_utf8() {
    return std::string("\xE5\x8A\xA8 \xE8\x84\x89");
}

void test_recoil_weapon_recognizer_writes_current_weapon_state_from_text() {
    const std::filesystem::path root = make_temp_test_dir("weapon_recognizer");
    const std::filesystem::path weapon_dir = root / "weapons";
    const std::filesystem::path profile_dir = root / "profiles";
    const std::filesystem::path state_path = root / "current_weapon.json";
    std::filesystem::create_directories(weapon_dir);
    std::filesystem::create_directories(profile_dir);

    write_text_file(
        weapon_dir / "identity-cod22-m4.json",
        weapon_identity_json("cod22-m4", "M4", "M 4"));
    write_text_file(
        profile_dir / "profile-cod22-m4-ads-standing-current.json",
        recoil_profile_json("profile-cod22-m4-ads-standing-current", "cod22-m4", "ads", 0.95f, 12.0f));

    controller_native::GamepadRecoilConfig config;
    config.recognizer_game = "cod22";
    config.weapon_directory = weapon_dir.string();
    config.profile_directory = profile_dir.string();
    config.recognizer_state_path = state_path.string();

    controller_native::NativeRecoilWeaponRecognizer recognizer(config);
    const std::optional<controller_native::RecoilWeaponRecognitionEvent> event =
        recognizer.process_text_candidates({"M4"}, "2026-06-06T12:00:00Z");

    require_true(event.has_value(), "native recoil recognizer should resolve OCR text candidates");
    require_true(event->canonical_weapon_id == "cod22-m4", "native recoil recognizer should emit canonical id");
    require_true(event->source == "switch_text", "native recoil recognizer should mark switch_text source");
    require_true(event->matched_name == "M4", "native recoil recognizer should keep matched OCR text");
    require_true(event->profile_ids.size() == 1, "native recoil recognizer should attach matching profile ids");
    require_true(
        event->profile_ids[0] == "profile-cod22-m4-ads-standing-current",
        "native recoil recognizer should attach the current weapon profile id");

    recognizer.write_latest_state(*event);
    const std::string state = std::filesystem::exists(state_path) ? read_text_file(state_path) : std::string();
    require_true(
        state.find("\"type\": \"current_weapon\"") != std::string::npos,
        "native recoil recognizer should write current_weapon state");
    require_true(
        state.find("\"canonical_weapon_id\": \"cod22-m4\"") != std::string::npos,
        "native recoil recognizer state should include canonical id");
    require_true(
        state.find("\"profile-cod22-m4-ads-standing-current\"") != std::string::npos,
        "native recoil recognizer state should include profile ids");
}

void test_recoil_weapon_recognizer_does_not_create_unknown_identity_from_live_ocr() {
    const std::filesystem::path root = make_temp_test_dir("weapon_recognizer_unknown");
    const std::filesystem::path weapon_dir = root / "weapons";
    const std::filesystem::path profile_dir = root / "profiles";
    std::filesystem::create_directories(weapon_dir);
    std::filesystem::create_directories(profile_dir);

    controller_native::GamepadRecoilConfig config;
    config.recognizer_game = "cod22";
    config.weapon_directory = weapon_dir.string();
    config.profile_directory = profile_dir.string();
    config.recognizer_state_path = (root / "current_weapon.json").string();

    controller_native::NativeRecoilWeaponRecognizer recognizer(config);
    const std::optional<controller_native::RecoilWeaponRecognitionEvent> event =
        recognizer.process_text_candidates({"random menu text"}, "2026-06-06T12:00:00Z", true);

    require_true(event.has_value(), "native recoil recognizer should publish fallback state for unknown OCR");
    require_true(
        event->canonical_weapon_id == "cod22-unknown",
        "unknown OCR should not pretend to be a known weapon");
    require_true(event->profile_ids.empty(), "unknown OCR should not attach stale profile ids");
    require_true(
        recognizer.identity_records().empty(),
        "native recoil recognizer should leave identity records unchanged for unknown OCR");
}

void test_recoil_weapon_recognizer_clears_previous_profile_on_unknown_switch() {
    const std::filesystem::path root = make_temp_test_dir("weapon_recognizer_unknown_switch");
    const std::filesystem::path weapon_dir = root / "weapons";
    const std::filesystem::path profile_dir = root / "profiles";
    const std::filesystem::path state_path = root / "current_weapon.json";
    std::filesystem::create_directories(weapon_dir);
    std::filesystem::create_directories(profile_dir);

    const std::string weapon_name = small_artery_name_utf8();
    const std::string weapon_id = "cod22-" + weapon_name;
    const std::string profile_id = "profile-cod22-" + weapon_name + "-ads-standing-current";
    write_text_file(
        weapon_dir / std::filesystem::u8path("identity-cod22-" + weapon_id + ".json"),
        weapon_identity_json(weapon_id, weapon_name, weapon_name));
    write_text_file(
        profile_dir / std::filesystem::u8path(profile_id + ".json"),
        recoil_profile_json(profile_id, weapon_id, "ads", 0.95f, 12.0f));

    controller_native::GamepadRecoilConfig config;
    config.recognizer_game = "cod22";
    config.weapon_directory = weapon_dir.string();
    config.profile_directory = profile_dir.string();
    config.recognizer_state_path = state_path.string();

    controller_native::NativeRecoilWeaponRecognizer recognizer(config);
    const std::optional<controller_native::RecoilWeaponRecognitionEvent> known =
        recognizer.process_text_candidates({weapon_name}, "2026-06-06T12:00:00Z", true);
    require_true(known.has_value(), "native recoil recognizer should first resolve the profiled weapon");
    require_true(known->canonical_weapon_id == weapon_id, "known switch should resolve the profiled weapon");
    require_true(known->profile_ids.size() == 1, "known switch should attach the profiled weapon profile");

    const std::optional<controller_native::RecoilWeaponRecognitionEvent> unknown =
        recognizer.process_text_candidates({"unknown weapon"}, "2026-06-06T12:00:01Z", true);
    require_true(unknown.has_value(), "unknown switch should publish a fallback weapon state");
    require_true(
        unknown->canonical_weapon_id == "cod22-unknown",
        "unknown switch should clear the previously confirmed weapon id");
    require_true(unknown->profile_ids.empty(), "unknown switch should clear stale profile ids");
    require_true(unknown->source == "switch_unresolved", "unknown switch should mark unresolved source");

    recognizer.write_latest_state(*unknown);
    const std::string state = read_text_file(state_path);
    require_true(
        state.find("\"profile_status\": \"no_profile\"") != std::string::npos,
        "unknown switch state should make recoil selector fall back to feedback");
    require_true(
        state.find(profile_id) == std::string::npos,
        "unknown switch state should not keep the previous weapon profile id");
}

void test_recoil_weapon_recognizer_clears_previous_profile_on_empty_switch_ocr() {
    const std::filesystem::path root = make_temp_test_dir("weapon_recognizer_empty_switch");
    const std::filesystem::path weapon_dir = root / "weapons";
    const std::filesystem::path profile_dir = root / "profiles";
    std::filesystem::create_directories(weapon_dir);
    std::filesystem::create_directories(profile_dir);

    const std::string weapon_name = small_artery_name_utf8();
    const std::string weapon_id = "cod22-" + weapon_name;
    const std::string profile_id = "profile-cod22-" + weapon_name + "-ads-standing-current";
    write_text_file(
        weapon_dir / std::filesystem::u8path("identity-cod22-" + weapon_id + ".json"),
        weapon_identity_json(weapon_id, weapon_name, weapon_name));
    write_text_file(
        profile_dir / std::filesystem::u8path(profile_id + ".json"),
        recoil_profile_json(profile_id, weapon_id, "ads", 0.95f, 12.0f));

    controller_native::GamepadRecoilConfig config;
    config.recognizer_game = "cod22";
    config.weapon_directory = weapon_dir.string();
    config.profile_directory = profile_dir.string();
    config.recognizer_state_path = (root / "current_weapon.json").string();

    controller_native::NativeRecoilWeaponRecognizer recognizer(config);
    const std::optional<controller_native::RecoilWeaponRecognitionEvent> known =
        recognizer.process_text_candidates({weapon_name}, "2026-06-06T12:00:00Z", true);
    require_true(known.has_value(), "native recoil recognizer should first resolve the profiled weapon");

    const std::optional<controller_native::RecoilWeaponRecognitionEvent> unknown =
        recognizer.process_text_candidates({}, "2026-06-06T12:00:01Z", true);
    require_true(unknown.has_value(), "empty switch OCR should still publish fallback state");
    require_true(
        unknown->canonical_weapon_id == "cod22-unknown",
        "empty switch OCR should clear the previously confirmed weapon id");
    require_true(unknown->profile_ids.empty(), "empty switch OCR should clear stale profile ids");
}

void test_recoil_weapon_recognizer_matches_utf8_weapon_and_profile_filename() {
    const std::filesystem::path root = make_temp_test_dir("weapon_recognizer_utf8");
    const std::filesystem::path weapon_dir = root / "weapons";
    const std::filesystem::path profile_dir = root / "profiles";
    std::filesystem::create_directories(weapon_dir);
    std::filesystem::create_directories(profile_dir);

    const std::string weapon_name = small_artery_name_utf8();
    const std::string weapon_id = "cod22-" + weapon_name;
    const std::string profile_id = "profile-cod22-" + weapon_name + "-ads-standing-current";
    write_text_file(
        weapon_dir / std::filesystem::u8path("identity-cod22-" + weapon_id + ".json"),
        weapon_identity_json(weapon_id, weapon_name, weapon_name));
    write_text_file(
        profile_dir / std::filesystem::u8path(profile_id + ".json"),
        recoil_profile_json(profile_id, weapon_id, "ads", 0.95f, 12.0f));

    controller_native::GamepadRecoilConfig config;
    config.recognizer_game = "cod22";
    config.weapon_directory = weapon_dir.string();
    config.profile_directory = profile_dir.string();
    config.recognizer_state_path = (root / "current_weapon.json").string();

    controller_native::NativeRecoilWeaponRecognizer recognizer(config);
    const std::optional<controller_native::RecoilWeaponRecognitionEvent> event =
        recognizer.process_text_candidates({weapon_name}, "2026-06-06T12:00:00Z");

    require_true(event.has_value(), "native recoil recognizer should resolve UTF-8 weapon names");
    require_true(event->canonical_weapon_id == weapon_id, "native recoil recognizer should keep UTF-8 canonical id");
    require_true(event->profile_ids.size() == 1, "native recoil recognizer should match UTF-8 profile filename");
    require_true(event->profile_ids[0] == profile_id, "native recoil recognizer should keep UTF-8 profile id");
}

void test_recoil_weapon_recognizer_prefers_profiled_weapon_for_ambiguous_ocr_suffix() {
    const std::filesystem::path root = make_temp_test_dir("weapon_recognizer_profile_bias");
    const std::filesystem::path weapon_dir = root / "weapons";
    const std::filesystem::path profile_dir = root / "profiles";
    std::filesystem::create_directories(weapon_dir);
    std::filesystem::create_directories(profile_dir);

    const std::string small_name = small_artery_name_utf8();
    const std::string lesser_name = lesser_artery_name_utf8();
    const std::string small_id = "cod22-" + small_name;
    const std::string lesser_id = "cod22-" + lesser_name;
    const std::string profile_id = "profile-cod22-" + small_name + "-ads-standing-current";
    write_text_file(
        weapon_dir / std::filesystem::u8path("identity-cod22-" + small_id + ".json"),
        weapon_identity_json(small_id, small_name, small_name));
    write_text_file(
        weapon_dir / std::filesystem::u8path("identity-cod22-" + lesser_id + ".json"),
        weapon_identity_json(lesser_id, lesser_name, lesser_name));
    write_text_file(
        profile_dir / std::filesystem::u8path(profile_id + ".json"),
        recoil_profile_json(profile_id, small_id, "ads", 0.95f, 12.0f));

    controller_native::GamepadRecoilConfig config;
    config.recognizer_game = "cod22";
    config.weapon_directory = weapon_dir.string();
    config.profile_directory = profile_dir.string();
    config.recognizer_state_path = (root / "current_weapon.json").string();

    controller_native::NativeRecoilWeaponRecognizer recognizer(config);
    const std::optional<controller_native::RecoilWeaponRecognitionEvent> event =
        recognizer.process_text_candidates({artery_suffix_with_space_utf8()}, "2026-06-06T12:00:00Z");

    require_true(event.has_value(), "native recoil recognizer should resolve profiled ambiguous OCR suffix");
    require_true(event->canonical_weapon_id == small_id, "native recoil recognizer should prefer profiled weapon");
    require_true(event->profile_ids.size() == 1, "native recoil recognizer should attach profile for profiled weapon");
    require_true(event->profile_ids[0] == profile_id, "native recoil recognizer should attach small artery profile");
}

void test_recoil_weapon_recognizer_ignores_numeric_ocr_noise() {
    const std::filesystem::path root = make_temp_test_dir("weapon_recognizer_noise");
    const std::filesystem::path weapon_dir = root / "weapons";
    const std::filesystem::path profile_dir = root / "profiles";
    std::filesystem::create_directories(weapon_dir);
    std::filesystem::create_directories(profile_dir);

    controller_native::GamepadRecoilConfig config;
    config.recognizer_game = "cod22";
    config.weapon_directory = weapon_dir.string();
    config.profile_directory = profile_dir.string();
    config.recognizer_state_path = (root / "current_weapon.json").string();

    controller_native::NativeRecoilWeaponRecognizer recognizer(config);
    const std::optional<controller_native::RecoilWeaponRecognitionEvent> event =
        recognizer.process_text_candidates({"2", "113"}, "2026-06-06T12:00:00Z");

    require_true(!event.has_value(), "native recoil recognizer should ignore numeric UI OCR noise");
    require_true(
        recognizer.identity_records().empty(),
        "native recoil recognizer should not create weapon identity records from numeric OCR noise");
}

void test_recoil_weapon_switch_scheduler_triggers_only_on_y_rising_edge() {
    controller_native::RecoilWeaponSwitchCaptureScheduler scheduler;
    const auto start = std::chrono::steady_clock::time_point{};

    scheduler.update_y_button(false, start);
    require_true(scheduler.pending_count() == 0, "recoil OCR scheduler should start empty");

    scheduler.update_y_button(true, start);
    require_true(scheduler.pending_count() == 2, "Y press should schedule two delayed recoil OCR captures");
    require_true(
        !scheduler.consume_due_capture(start + std::chrono::milliseconds(599)),
        "recoil OCR scheduler should wait for the first switch delay");
    require_true(
        scheduler.consume_due_capture(start + std::chrono::milliseconds(600)),
        "recoil OCR scheduler should trigger at the first Python switch delay");
    require_true(scheduler.pending_count() == 1, "first recoil OCR capture should leave the backup capture pending");
    scheduler.clear_pending();
    require_true(
        scheduler.pending_count() == 0,
        "resolved recoil OCR capture should cancel the backup capture for the same Y press");

    scheduler.update_y_button(true, start + std::chrono::milliseconds(620));
    require_true(scheduler.pending_count() == 0, "holding Y should not schedule more recoil OCR captures");

    scheduler.update_y_button(false, start + std::chrono::milliseconds(800));
    scheduler.update_y_button(true, start + std::chrono::milliseconds(900));
    require_true(scheduler.pending_count() == 2, "pressing Y again should schedule a fresh recoil OCR capture pair");
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
    state.dx = 36.0f;
    state.dy = 24.0f;
    state.observed_at_seconds = now_seconds();
    controller.submit_vision_state(state);

    controller_native::GamepadOutputState output =
        controller.build_output(aiming_physical_state());
    const std::vector<controller_native::NativeControllerStageTrace>& traces =
        controller.last_pipeline_traces();

    require_true(traces.size() == 4, "controller should trace four runtime stages");
    require_true(traces[0].stage_name == "ai_aim", "first trace should be ai_aim");
    require_true(
        traces[1].stage_name == "aim_assist_dynamics",
        "second trace should be aim_assist_dynamics");
    require_true(traces[2].stage_name == "auto_fire", "third trace should be auto_fire");
    require_true(traces[3].stage_name == "recoil", "fourth trace should be recoil");
    require_true(
        std::fabs(traces[0].delta_right_y) > 0.0001f,
        "ai_aim trace should record its right-stick Y delta");
    require_true(
        !traces[2].before_auto_fire_active && traces[2].after_auto_fire_active,
        "auto_fire trace should expose activation state transition");
    require_near(
        traces.back().after_right_y,
        output.right_y,
        0.0001f,
        "last trace should end at final output right_y");
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
    controller.submit_vision_result(target);

    controller_native::GamepadOutputState first = controller.build_output(aiming_physical_state());
    require_true(!first.rb, "auto-fire should wait for first settled frame");

    target.result_at_ns = now_ns();
    controller.submit_vision_result(target);
    controller_native::GamepadOutputState second = controller.build_output(aiming_physical_state());
    require_true(second.rb, "auto-fire should start after required settled frames");
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
    controller.submit_vision_result(target);

    controller_native::GamepadOutputState output = controller.build_output(aiming_physical_state());
    require_true(output.rb, "disabled aim-ready gate should keep legacy first-frame fire");
}

void test_no_update_vision_result_preserves_latest_target() {
    controller_native::GamepadRuntimeConfig config;
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
    target.result_at_ns = 1;
    controller.submit_vision_result(target);

    controller_native::GamepadOutputState output = controller.build_output(aiming_physical_state());
    require_true(output.right_x > 0.40f, "fresh target should produce right-stick assist");

    vision_native::VisionResult no_update;
    no_update.frame_updated = false;
    no_update.has_target = false;
    no_update.result_at_ns = 2;
    controller.submit_vision_result(no_update);

    output = controller.build_output(aiming_physical_state());
    require_true(output.right_x > 0.40f, "no-update poll must not clear latest target");

    vision_native::VisionResult valid_no_target;
    valid_no_target.frame_updated = true;
    valid_no_target.has_target = false;
    valid_no_target.result_at_ns = 3;
    controller.submit_vision_result(valid_no_target);

    output = controller.build_output(aiming_physical_state());
    require_near(output.right_x, 0.0f, 0.001f, "processed no-target frame should clear assist");
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

void test_controller_projects_target_during_no_update_ticks() {
    controller_native::GamepadRuntimeConfig config;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.max_ai_force = 1.0f;
    config.ai_aim.max_ai_force_y = 1.0f;
    config.ai_aim.piecewise_mid_pixels = 0.0f;
    config.ai_aim.piecewise_mid_pixels_y = 0.0f;
    config.ai_aim.ads_snap_time_to_go_gain = 0.0f;
    config.ai_aim.target_max_age_ms = 500.0f;
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
    controller.submit_vision_result(target);

    controller_native::GamepadOutputState first = controller.build_output(aiming_physical_state());
    require_true(first.right_x > 0.40f, "fresh target should produce assist before projection");

    vision_native::VisionResult no_update;
    no_update.frame_updated = false;
    no_update.has_target = false;
    controller.submit_vision_result(no_update);

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    controller_native::GamepadOutputState second = controller.build_output(aiming_physical_state());
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    controller_native::GamepadOutputState third = controller.build_output(aiming_physical_state());
    require_true(
        third.right_x < second.right_x - 0.03f,
        "controller should use projected target error after output moves the reticle");
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

    input.target_tier = "cue_hold";
    require_near(ai_aim.compute(input).assist_x, 0.125f, 0.001f, "cue assist scale");
}

void test_ai_aim_body_lock_uses_upper_body_point_from_body_box() {
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
    input.dx = 90.0f;
    input.dy = 0.0f;
    input.target_tier = "strong";
    input.observed_at_seconds = 10.0;
    input.now_seconds = 10.01;
    input.screen_center_x = 320.0f;
    input.screen_center_y = 256.0f;
    input.has_body_box = true;
    input.body_x1 = 300.0f;
    input.body_y1 = 120.0f;
    input.body_x2 = 340.0f;
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
        0.56f,
        0.001f,
        "body-lock should use upper-body y from the body box");
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
        0.093333f,
        0.002f,
        "body-lock should credit aligned manual input with the Python near-lock formula");
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
        second.assist_y > 0.01f,
        "body-lock should restore some vertical tail help after target history exists");
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
        0.001f,
        "body-lock should clear y carry when near-zero vertical error flips sign");
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
    input.manual_right_x = -0.50f;
    const controller_native::NativeAiAimOutput output = ai_aim.compute(input);
    const float final_x = input.manual_right_x + output.assist_x;

    require_near(
        final_x,
        0.0f,
        0.03f,
        "body-lock should reset confidence instead of suppressing manual input for a new body box");
}

void test_controller_ads_snap_only_runs_inside_ads_window_without_body_lock() {
    controller_native::GamepadRuntimeConfig config;
    config.ai_aim.max_pixels = 100.0f;
    config.ai_aim.max_ai_force = 1.0f;
    config.ai_aim.ads_snap_window_ms = 20;
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
    controller.submit_vision_result(target);

    controller_native::GamepadOutputState first = controller.build_output(aiming_physical_state());
    require_true(first.right_x > 0.40f, "fresh ADS snap window should assist strong targets");

    std::this_thread::sleep_for(std::chrono::milliseconds(35));
    target.result_at_ns = now_ns();
    controller.submit_vision_result(target);

    controller_native::GamepadOutputState second = controller.build_output(aiming_physical_state());
    require_near(
        second.right_x,
        0.0f,
        0.001f,
        "ADS snap should stop after the snap window when body-lock is unavailable");
}

void test_auto_fire_ready_uses_body_lock_error_when_body_box_is_active() {
    controller_native::GamepadRuntimeConfig config;
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
    controller.submit_vision_result(target);

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

void test_aim_assist_dynamics_guards_small_recoil_sign_flip() {
    controller_native::GamepadAimAssistDynamicsConfig config;
    config.enabled = true;
    config.recoil_jitter_guard_enabled = true;
    config.recoil_jitter_assist_threshold = 32767.0f;
    config.recoil_jitter_flip_scale = 0.20f;
    config.recoil_jitter_memory_seconds = 0.050f;
    controller_native::NativeAimAssistDynamics dynamics(config);

    controller_native::NativeAimAssistDynamicsInput input;
    input.recoil_active = true;
    input.now_seconds = 20.0;
    input.assisted_right_x = 0.20f;
    controller_native::NativeAimAssistDynamicsOutput output = dynamics.apply(input);
    require_near(output.right_x, 0.20f, 0.001f, "first assist sample should pass through");

    input.now_seconds = 20.02;
    input.assisted_right_x = -0.20f;
    output = dynamics.apply(input);
    require_near(output.right_x, -0.04f, 0.001f, "small recoil sign flip should be scaled");
}

void test_recoil_profile_despike_repairs_playback_cache_only() {
    controller_native::RecoilProfile profile;
    profile.profile_id = "test";
    profile.sample_interval_ms = 10;
    profile.samples_x = {0.0f, 10.0f, 0.0f};
    profile.samples_y = {0.0f, -12.0f, 0.0f};

    controller_native::GamepadRecoilConfig config;
    config.profile_despike_enabled = true;
    config.profile_despike_threshold_px = 2.0f;
    config.profile_despike_ratio = 3.0f;

    const controller_native::RecoilProfile playback =
        controller_native::build_recoil_playback_profile(profile, config);
    require_near(playback.samples_x[1], 0.0f, 0.001f, "despiked x midpoint");
    require_near(playback.samples_y[1], 0.0f, 0.001f, "despiked y midpoint");
    require_near(profile.samples_x[1], 10.0f, 0.001f, "raw x profile must remain unchanged");
    require_near(profile.samples_y[1], -12.0f, 0.001f, "raw y profile must remain unchanged");
}

void test_recoil_profile_selection_matches_recognizer_state_and_context() {
    const std::filesystem::path root = make_temp_test_dir("profile_select");
    const std::filesystem::path profile_dir = root / "profiles";
    std::filesystem::create_directories(profile_dir);
    const std::filesystem::path state_path = root / "latest-state.json";

    write_text_file(
        profile_dir / "profile-cod22-m4-ads-standing-low.json",
        recoil_profile_json("profile-cod22-m4-ads-standing-low", "cod22-m4", "ads", 0.25f, 1.0f));
    write_text_file(
        profile_dir / "profile-cod22-m4-ads-standing-high.json",
        recoil_profile_json("profile-cod22-m4-ads-standing-high", "cod22-m4", "ads", 0.95f, 8.0f));
    write_text_file(
        profile_dir / "profile-cod22-m4-hipfire-standing-high.json",
        recoil_profile_json("profile-cod22-m4-hipfire-standing-high", "cod22-m4", "hipfire", 0.99f, 2.0f));
    write_text_file(
        profile_dir / "profile-cod22-kastov-ads-standing-high.json",
        recoil_profile_json("profile-cod22-kastov-ads-standing-high", "cod22-kastov", "ads", 0.99f, 12.0f));
    write_text_file(
        state_path,
        recognizer_state_json("cod22-m4", "profile-cod22-m4-ads-standing-high"));

    const std::optional<controller_native::RecoilProfile> selected =
        controller_native::load_matching_recoil_profile(profile_dir, state_path, "ads");
    require_true(selected.has_value(), "profile selector should find an ADS profile");
    require_true(
        selected->profile_id == "profile-cod22-m4-ads-standing-high",
        "profile selector should choose highest-confidence matching ADS profile");
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
    config.recoil.target_direction_yield_enabled = false;
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

void test_recoil_timeline_outputs_delta_while_fire_active() {
    controller_native::RecoilProfile profile;
    profile.profile_id = "timeline";
    profile.sample_interval_ms = 10;
    profile.samples_x = {0.0f, 0.0f, 0.0f};
    profile.samples_y = {0.0f, 10.0f, 20.0f};

    controller_native::GamepadRecoilConfig config;
    config.profile_amount = 1.0f;
    config.profile_x_amount = 1.0f;
    config.profile_velocity_reference_ms = 10.0f;
    config.profile_despike_enabled = false;
    config.piecewise_mid_pixels_y = 10.0f;
    config.piecewise_max_pixels_y = 20.0f;
    config.piecewise_mid_ratio_y = 0.50f;

    controller_native::NativeRecoilCompensation recoil(config);
    recoil.set_profile(profile);

    controller_native::NativeRecoilInput input;
    input.fire_active = true;
    input.now_seconds = 30.0;
    recoil.compute(input);

    input.now_seconds = 30.01;
    const controller_native::NativeRecoilOutput output = recoil.compute(input);
    require_true(output.recoil_active, "recoil should be active while firing");
    require_true(output.right_y_delta < -0.49f, "timeline recoil should pull against positive y samples");
}

void test_recoil_profile_playback_uses_matching_calibration_when_available() {
    const std::filesystem::path root = make_temp_test_dir("calibrated_recoil");
    const std::filesystem::path calibration_dir = root / "calibration";
    std::filesystem::create_directories(calibration_dir);
    write_text_file(
        calibration_dir / "cod22-ads-standing.json",
        recoil_calibration_json("ads", 500.0f, 1000.0f));

    controller_native::RecoilProfile profile;
    profile.profile_id = "profile-cod22-m4-ads-standing-v1";
    profile.canonical_weapon_id = "cod22-m4";
    profile.game = "cod22";
    profile.stance = "standing";
    profile.aim_mode = "ads";
    profile.sample_interval_ms = 10;
    profile.samples_x = {0.0f, 0.0f};
    profile.samples_y = {0.0f, 10.0f};

    controller_native::GamepadRecoilConfig config;
    config.profile_amount = 1.0f;
    config.profile_x_amount = 1.0f;
    config.profile_despike_enabled = false;
    config.calibration_directory = calibration_dir.string();

    controller_native::NativeRecoilCompensation recoil(config);
    recoil.set_profile(profile);

    controller_native::NativeRecoilInput input;
    input.fire_active = true;
    input.aiming = true;
    input.now_seconds = 50.0;
    recoil.compute(input);

    input.now_seconds = 50.01;
    const controller_native::NativeRecoilOutput output = recoil.compute(input);
    require_true(output.recoil_active, "calibrated recoil should be active while firing");
    require_near(
        output.right_y_delta,
        -1.0f,
        0.001f,
        "calibrated 10 px over 10 ms at 1000 px/s should map to full stick");
}

void test_recoil_selection_logging_reports_fallback_and_profile_once() {
    const std::filesystem::path root = make_temp_test_dir("recoil_selection_logs");
    const std::filesystem::path profile_dir = root / "profiles";
    std::filesystem::create_directories(profile_dir);
    const std::filesystem::path state_path = root / "state.json";

    controller_native::GamepadRecoilConfig config;
    config.selection_log_enabled = true;
    config.profile_directory = profile_dir.string();
    config.recognizer_state_path = state_path.string();
    config.feedback_amount = 0.15f;
    config.target_direction_yield_enabled = false;

    write_text_file(state_path, recognizer_state_json("cod22-missing", "profile-missing"));

    controller_native::NativeRecoilInput input;
    input.fire_active = true;
    input.aiming = true;
    input.now_seconds = 90.0;

    std::ostringstream captured;
    std::streambuf* previous = std::cout.rdbuf(captured.rdbuf());
    {
        controller_native::NativeRecoilCompensation recoil(config);
        recoil.load_profile_directory(profile_dir);
        recoil.compute(input);
        input.now_seconds = 90.01;
        recoil.compute(input);
    }
    std::cout.rdbuf(previous);

    const std::string fallback_log = "[Recoil] active_profile aim=ads profile=none fallback=15%";
    const std::string fallback_output = captured.str();
    require_true(
        fallback_output.find(fallback_log) != std::string::npos,
        "recoil should log fallback profile selection when recognizer has no matching profile");
    require_true(
        fallback_output.find(fallback_log) == fallback_output.rfind(fallback_log),
        "recoil should not repeat identical fallback selection logs");

    write_text_file(
        profile_dir / "profile-cod22-m4-ads-standing-high.json",
        recoil_profile_json("profile-cod22-m4-ads-standing-high", "cod22-m4", "ads", 0.95f, 12.0f));
    write_text_file(
        state_path,
        recognizer_state_json("cod22-m4", "profile-cod22-m4-ads-standing-high"));

    captured.str("");
    captured.clear();
    previous = std::cout.rdbuf(captured.rdbuf());
    {
        controller_native::NativeRecoilCompensation recoil(config);
        recoil.load_profile_directory(profile_dir);
        input.now_seconds = 91.0;
        recoil.compute(input);
        input.now_seconds = 91.01;
        recoil.compute(input);
    }
    std::cout.rdbuf(previous);

    const std::string profile_log =
        "[Recoil] active_profile aim=ads profile=profile-cod22-m4-ads-standing-high";
    const std::string profile_output = captured.str();
    require_true(
        profile_output.find(profile_log) != std::string::npos,
        "recoil should log selected profile id");
    require_true(
        profile_output.find(profile_log) == profile_output.rfind(profile_log),
        "recoil should not repeat identical selected profile logs");
}

}  // namespace

int main() {
    try {
        test_auto_fire_requires_fire_authority();
        test_auto_fire_blocks_stale_source();
        test_controller_passes_extended_buttons_and_dpad_through();
        test_controller_records_pipeline_stage_traces();
        test_auto_fire_manual_takeover_releases_output_briefly();
        test_auto_fire_requires_aim_ready_settle_frames();
        test_auto_fire_aim_ready_gate_can_be_disabled();
        test_no_update_vision_result_preserves_latest_target();
        test_target_tracker_projects_camera_motion_between_vision_frames();
        test_controller_projects_target_during_no_update_ticks();
        test_ai_aim_scales_weak_and_cue_targets();
        test_ai_aim_body_lock_uses_upper_body_point_from_body_box();
        test_body_lock_suppresses_harmful_manual_input_after_confidence_builds();
        test_body_lock_counts_aligned_manual_input_as_planned_correction_near_lock();
        test_body_lock_damps_orthogonal_manual_input_near_lock();
        test_body_lock_restores_vertical_tail_help_after_continuous_target_history();
        test_body_lock_uses_lateral_motion_when_current_error_is_inside_deadzone();
        test_body_lock_can_disable_release_tail_to_zero_x_axis_inside_window();
        test_body_lock_preserves_more_horizontal_tail_for_moving_target_inside_release_window();
        test_body_lock_clears_release_tail_carry_on_near_zero_x_sign_flip();
        test_body_lock_clears_vertical_axis_on_near_zero_sign_flip();
        test_body_lock_applies_motion_lead_after_configured_history_frames();
        test_body_lock_confidence_resets_when_body_box_no_longer_matches_target();
        test_controller_ads_snap_only_runs_inside_ads_window_without_body_lock();
        test_auto_fire_ready_uses_body_lock_error_when_body_box_is_active();
        test_ads_snap_counts_same_direction_manual_as_planned_correction();
        test_ads_snap_softens_opposing_manual_instead_of_canceling_snap();
        test_ai_aim_piecewise_mapping_matches_python_midpoint();
        test_ads_snap_time_to_go_can_override_piecewise_mapping();
        test_ads_snap_clamps_vertical_target_delta_before_mapping();
        test_ads_snap_smoothing_interpolates_first_assist_frame();
        test_ai_aim_deadzone_suppresses_tiny_target_error();
        test_aim_assist_dynamics_guards_small_recoil_sign_flip();
        test_recoil_profile_despike_repairs_playback_cache_only();
        test_recoil_weapon_recognizer_writes_current_weapon_state_from_text();
        test_recoil_weapon_recognizer_does_not_create_unknown_identity_from_live_ocr();
        test_recoil_weapon_recognizer_clears_previous_profile_on_unknown_switch();
        test_recoil_weapon_recognizer_clears_previous_profile_on_empty_switch_ocr();
        test_recoil_weapon_recognizer_matches_utf8_weapon_and_profile_filename();
        test_recoil_weapon_recognizer_prefers_profiled_weapon_for_ambiguous_ocr_suffix();
        test_recoil_weapon_recognizer_ignores_numeric_ocr_noise();
        test_recoil_weapon_switch_scheduler_triggers_only_on_y_rising_edge();
        test_recoil_profile_selection_matches_recognizer_state_and_context();
        test_controller_recoil_uses_runtime_ads_or_hipfire_profile_selection();
        test_controller_recoil_uses_fallback_when_no_recognizer_state_is_configured();
        test_recoil_timeline_outputs_delta_while_fire_active();
        test_recoil_profile_playback_uses_matching_calibration_when_available();
        test_recoil_selection_logging_reports_fallback_and_profile_once();
    } catch (const std::exception& exc) {
        std::cerr << "[NativeControllerTests] FAIL " << exc.what() << "\n";
        return 1;
    }

    std::cout << "[NativeControllerTests] PASS\n";
    return 0;
}
