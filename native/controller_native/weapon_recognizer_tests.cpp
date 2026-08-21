#include "weapon_recognizer.h"
#include "test_support/native_test_registry.h"

#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require_true(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::filesystem::path make_temp_test_dir(const std::string& label) {
    const auto stamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("cod_native_weapon_recognizer_" + label + "_" + std::to_string(stamp));
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

}  // namespace

void register_weapon_recognizer_tests(native_test::Registry& registry) {
    registry.add_case("FeatureRecoilAndWeapon", "recognizer_writes_weapon_state_from_text", test_recoil_weapon_recognizer_writes_current_weapon_state_from_text);
    registry.add_case("FeatureRecoilAndWeapon", "recognizer_rejects_unknown_live_ocr_identity", test_recoil_weapon_recognizer_does_not_create_unknown_identity_from_live_ocr);
    registry.add_case("FeatureRecoilAndWeapon", "unknown_switch_clears_previous_profile", test_recoil_weapon_recognizer_clears_previous_profile_on_unknown_switch);
    registry.add_case("FeatureRecoilAndWeapon", "empty_switch_ocr_clears_previous_profile", test_recoil_weapon_recognizer_clears_previous_profile_on_empty_switch_ocr);
    registry.add_case("FeatureRecoilAndWeapon", "recognizer_matches_utf8_profile_filename", test_recoil_weapon_recognizer_matches_utf8_weapon_and_profile_filename);
    registry.add_case("FeatureRecoilAndWeapon", "recognizer_prefers_profiled_ambiguous_suffix", test_recoil_weapon_recognizer_prefers_profiled_weapon_for_ambiguous_ocr_suffix);
    registry.add_case("FeatureRecoilAndWeapon", "recognizer_ignores_numeric_noise", test_recoil_weapon_recognizer_ignores_numeric_ocr_noise);
    registry.add_case("FeatureRecoilAndWeapon", "weapon_switch_uses_y_rising_edge", test_recoil_weapon_switch_scheduler_triggers_only_on_y_rising_edge);
}
