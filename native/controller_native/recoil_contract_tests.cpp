#include "recoil_compensation.h"
#include "recoil_profile.h"
#include "test_support/native_test_registry.h"

#include "../recoil_native/recoil_visual_model.h"

#include <cmath>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

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

std::filesystem::path make_temp_test_dir(const std::string& label) {
    const auto stamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("cod_native_recoil_contract_" + label + "_" + std::to_string(stamp));
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

template <typename T, typename = void>
struct has_target_dx_member : std::false_type {};

template <typename T>
struct has_target_dx_member<T, std::void_t<decltype(std::declval<T>().target_dx)>>
    : std::true_type {};

template <typename T, typename = void>
struct has_target_dy_member : std::false_type {};

template <typename T>
struct has_target_dy_member<T, std::void_t<decltype(std::declval<T>().target_dy)>>
    : std::true_type {};

template <typename T, typename = void>
struct has_target_observed_at_seconds_member : std::false_type {};

template <typename T>
struct has_target_observed_at_seconds_member<
    T,
    std::void_t<decltype(std::declval<T>().target_observed_at_seconds)>> : std::true_type {};

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

void test_recoil_visual_model_disabled_returns_zero_displacement() {
    recoil_native::DisabledRecoilVisualModel model;
    const recoil_native::RecoilVisualDisplacement displacement =
        model.compute(recoil_native::RecoilVisualInput{});
    require_near(displacement.stick.x, 0.0f, 0.001f, "disabled recoil visual model x");
    require_near(displacement.stick.y, 0.0f, 0.001f, "disabled recoil visual model y");
}

void test_recoil_profile_despike_repairs_playback_cache_only() {
    controller_native::RecoilProfile profile;
    profile.profile_id = "test";
    profile.sample_interval_ms = 10;
    profile.samples_x = {0.0f, 10.0f, 0.0f};
    profile.samples_y = {0.0f, -12.0f, 0.0f};

    controller_native::GamepadRecoilConfig config;
    config.profile_playback_enabled = true;  // Offline playback fixture only.
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

void test_recoil_input_contract_excludes_target_feedback_fields() {
    require_true(
        !has_target_dx_member<controller_native::NativeRecoilInput>::value,
        "recoil input must not accept target dx from controller or tracker");
    require_true(
        !has_target_dy_member<controller_native::NativeRecoilInput>::value,
        "recoil input must not accept target dy from controller or tracker");
    require_true(
        !has_target_observed_at_seconds_member<controller_native::NativeRecoilInput>::value,
        "recoil input must not accept target freshness from controller or tracker");
}

void test_recoil_profile_playback_is_deterministic_without_controller_state() {
    controller_native::RecoilProfile profile;
    profile.profile_id = "deterministic";
    profile.sample_interval_ms = 10;
    profile.samples_x = {0.0f, 5.0f, 10.0f};
    profile.samples_y = {0.0f, 10.0f, 20.0f};

    controller_native::GamepadRecoilConfig config;
    config.profile_playback_enabled = true;  // Offline playback fixture only.
    config.profile_amount = 1.0f;
    config.profile_x_amount = 1.0f;
    config.profile_velocity_reference_ms = 10.0f;
    config.profile_despike_enabled = false;
    config.piecewise_mid_pixels_y = 10.0f;
    config.piecewise_max_pixels_y = 20.0f;
    config.piecewise_mid_ratio_y = 0.50f;

    auto sample_output = [&]() {
        controller_native::NativeRecoilCompensation recoil(config);
        recoil.set_profile(profile);
        controller_native::NativeRecoilInput input;
        input.fire_active = true;
        input.aiming = true;
        input.now_seconds = 80.0;
        recoil.compute(input);
        input.now_seconds = 80.01;
        return recoil.compute(input);
    };

    const controller_native::NativeRecoilOutput first = sample_output();
    const controller_native::NativeRecoilOutput second = sample_output();
    require_true(first.recoil_active && second.recoil_active, "profile recoil should be active");
    require_near(
        first.right_x_delta,
        second.right_x_delta,
        0.0001f,
        "profile recoil x should be deterministic without controller target state");
    require_near(
        first.right_y_delta,
        second.right_y_delta,
        0.0001f,
        "profile recoil y should be deterministic without controller target state");

    controller_native::GamepadRecoilConfig fallback_config;
    fallback_config.feedback_amount = 0.17f;
    controller_native::NativeRecoilCompensation fallback_recoil(fallback_config);
    controller_native::NativeRecoilInput fallback_input;
    fallback_input.fire_active = true;
    fallback_input.now_seconds = 90.0;
    const controller_native::NativeRecoilOutput fallback = fallback_recoil.compute(fallback_input);
    require_near(
        fallback.right_y_delta,
        -0.17f,
        0.0001f,
        "fallback recoil should be fixed feed-forward down-pull");
}

void test_recoil_fallback_feedback_is_constant_linear_down_pull() {
    controller_native::GamepadRecoilConfig config;
    config.profile_playback_enabled = true;  // Offline playback fixture only.
    config.profile_directory.clear();
    config.recognizer_state_path.clear();
    config.feedback_amount = 0.30f;

    controller_native::NativeRecoilCompensation recoil(config);
    controller_native::NativeRecoilInput input;
    input.fire_active = true;

    for (const double now_seconds : {90.000, 90.120, 90.500, 90.620}) {
        input.now_seconds = now_seconds;
        const controller_native::NativeRecoilOutput output = recoil.compute(input);
        require_true(output.recoil_active, "fallback recoil should stay active while firing");
        require_near(
            output.right_x_delta,
            0.0f,
            0.0001f,
            "fallback recoil should not add horizontal movement");
        require_near(
            output.right_y_delta,
            -0.30f,
            0.0001f,
            "fallback recoil should remain a constant 30 percent down-pull without timed pulses");
    }
}

void test_recoil_fallback_is_clamped_to_product_range() {
    controller_native::GamepadRecoilConfig config;
    config.profile_playback_enabled = false;
    controller_native::NativeRecoilInput input;
    input.fire_active = true;

    config.feedback_amount = 0.0f;
    controller_native::NativeRecoilCompensation lower(config);
    require_near(
        lower.compute(input).right_y_delta,
        -0.14f,
        0.0001f,
        "fallback recoil fell below the 0.14 product floor");

    config.feedback_amount = 1.0f;
    controller_native::NativeRecoilCompensation upper(config);
    require_near(
        upper.compute(input).right_y_delta,
        -0.34f,
        0.0001f,
        "fallback recoil exceeded the 0.34 product ceiling");
}

void test_disabled_profile_playback_forces_default_down_pull() {
    controller_native::GamepadRecoilConfig config;
    config.profile_playback_enabled = false;
    config.feedback_amount = 0.24f;

    controller_native::RecoilProfile profile;
    profile.profile_id = "must-not-play";
    profile.sample_interval_ms = 10;
    profile.samples_x = {0.0f, 20.0f};
    profile.samples_y = {0.0f, 20.0f};

    controller_native::NativeRecoilCompensation recoil(config);
    recoil.set_profile(profile);
    controller_native::NativeRecoilInput input;
    input.fire_active = true;
    input.aiming = true;
    input.now_seconds = 10.0;
    const auto output = recoil.compute(input);

    require_true(output.recoil_active, "fallback recoil must remain active while firing");
    require_near(
        output.right_x_delta,
        0.0f,
        0.0001f,
        "disabled profile playback must never add horizontal recoil");
    require_near(
        output.right_y_delta,
        -0.24f,
        0.0001f,
        "disabled profile playback must use only the configured default down-pull");
}

void test_recoil_timeline_outputs_delta_while_fire_active() {
    controller_native::RecoilProfile profile;
    profile.profile_id = "timeline";
    profile.sample_interval_ms = 10;
    profile.samples_x = {0.0f, 0.0f, 0.0f};
    profile.samples_y = {0.0f, 10.0f, 20.0f};

    controller_native::GamepadRecoilConfig config;
    config.profile_playback_enabled = true;  // Offline playback fixture only.
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

void test_recoil_uncalibrated_y_uses_velocity_scaled_sample_delta() {
    controller_native::RecoilProfile profile;
    profile.profile_id = "sample-delta-y";
    profile.sample_interval_ms = 10;
    profile.samples_x = {0.0f, 0.0f, 0.0f};
    profile.samples_y = {0.0f, 10.0f, 20.0f};

    controller_native::GamepadRecoilConfig config;
    config.profile_playback_enabled = true;  // Offline playback fixture only.
    config.profile_amount = 1.0f;
    config.profile_velocity_reference_ms = 10.0f;
    config.profile_despike_enabled = false;
    config.piecewise_mid_pixels_y = 10.0f;
    config.piecewise_max_pixels_y = 40.0f;
    config.piecewise_mid_ratio_y = 0.50f;

    controller_native::NativeRecoilCompensation recoil(config);
    recoil.set_profile(profile);

    controller_native::NativeRecoilInput input;
    input.fire_active = true;
    input.now_seconds = 40.0;
    recoil.compute(input);

    input.now_seconds = 40.01;
    const controller_native::NativeRecoilOutput first = recoil.compute(input);
    input.now_seconds = 40.02;
    const controller_native::NativeRecoilOutput second = recoil.compute(input);
    require_true(first.recoil_active && second.recoil_active, "profile recoil should stay active");
    require_near(
        first.right_y_delta,
        -0.50f,
        0.0001f,
        "uncalibrated recoil y should map the first sample delta");
    require_near(
        second.right_y_delta,
        first.right_y_delta,
        0.0001f,
        "uncalibrated recoil y should use per-sample delta playback instead of cumulative pull");
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
    config.profile_playback_enabled = true;  // Offline playback fixture only.
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
    config.profile_playback_enabled = true;  // Offline playback fixture only.
    config.selection_log_enabled = true;
    config.profile_directory = profile_dir.string();
    config.recognizer_state_path = state_path.string();
    config.feedback_amount = 0.15f;
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

void register_recoil_contract_tests(native_test::Registry& registry) {
    registry.add_case("FeatureRecoilAndWeapon", "visual_model_disabled_returns_zero", test_recoil_visual_model_disabled_returns_zero_displacement);
    registry.add_case("FeatureRecoilAndWeapon", "profile_despike_repairs_playback_cache_only", test_recoil_profile_despike_repairs_playback_cache_only);
    registry.add_case("FeatureRecoilAndWeapon", "profile_selection_matches_weapon_context", test_recoil_profile_selection_matches_recognizer_state_and_context);
    registry.add_case("FeatureRecoilAndWeapon", "input_contract_excludes_target_feedback", test_recoil_input_contract_excludes_target_feedback_fields);
    registry.add_case("FeatureRecoilAndWeapon", "profile_playback_is_deterministic", test_recoil_profile_playback_is_deterministic_without_controller_state);
    registry.add_case("FeatureRecoilAndWeapon", "fallback_is_constant_linear_down_pull", test_recoil_fallback_feedback_is_constant_linear_down_pull);
    registry.add_case("FeatureRecoilAndWeapon", "fallback_is_clamped_to_product_range", test_recoil_fallback_is_clamped_to_product_range);
    registry.add_case("FeatureRecoilAndWeapon", "disabled_profile_forces_default_pull", test_disabled_profile_playback_forces_default_down_pull);
    registry.add_case("FeatureRecoilAndWeapon", "timeline_outputs_delta_while_firing", test_recoil_timeline_outputs_delta_while_fire_active);
    registry.add_case("FeatureRecoilAndWeapon", "uncalibrated_y_uses_velocity_scaled_delta", test_recoil_uncalibrated_y_uses_velocity_scaled_sample_delta);
    registry.add_case("FeatureRecoilAndWeapon", "playback_uses_matching_calibration", test_recoil_profile_playback_uses_matching_calibration_when_available);
    registry.add_case("FeatureRecoilAndWeapon", "selection_logging_reports_once", test_recoil_selection_logging_reports_fallback_and_profile_once);
}
