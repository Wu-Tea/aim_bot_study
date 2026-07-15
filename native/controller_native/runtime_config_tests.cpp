#include "runtime_config.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition) {
    if (!condition) {
        std::abort();
    }
}

void test_vision_gpu_service_defaults_are_enabled() {
    const auto missing = std::filesystem::temp_directory_path() /
        "cod_native_runtime_config_defaults_missing.toml";
    std::filesystem::remove(missing);
    const controller_native::RuntimeConfig config =
        controller_native::load_runtime_config(missing);
    require(config.vision.gpu_service_enabled);
    require(config.vision.gpu_service_active_fps == 120);
    require(config.vision.gpu_service_idle_fps == 20);
    require(config.vision.gpu_service_keepwarm_when_idle);
    require(config.vision.gpu_service_repeat_last_on_no_update);
}

void test_vision_gpu_service_config_values_parse() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "cod_native_runtime_config_test.toml";
    {
        std::ofstream output(path);
        output << "[runtime.vision]\n"
               << "gpu_service_enabled = true\n"
               << "gpu_service_active_fps = 120\n"
               << "gpu_service_idle_fps = 15\n"
               << "gpu_service_keepwarm_when_idle = false\n"
               << "gpu_service_repeat_last_on_no_update = false\n";
    }

    const controller_native::RuntimeConfig config =
        controller_native::load_runtime_config(path);
    std::filesystem::remove(path);

    require(config.vision.gpu_service_enabled);
    require(config.vision.gpu_service_active_fps == 120);
    require(config.vision.gpu_service_idle_fps == 15);
    require(!config.vision.gpu_service_keepwarm_when_idle);
    require(!config.vision.gpu_service_repeat_last_on_no_update);
}

void test_vision_gpu_service_can_be_disabled() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "cod_native_runtime_config_disable_test.toml";
    {
        std::ofstream output(path);
        output << "[runtime.vision]\n"
               << "gpu_service_enabled = false\n";
    }

    const controller_native::RuntimeConfig config =
        controller_native::load_runtime_config(path);
    std::filesystem::remove(path);

    require(!config.vision.gpu_service_enabled);
}

void test_balanced_profile_uses_canonical_vision_defaults() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "cod_native_runtime_profile_test.toml";
    {
        std::ofstream output(path);
        output << "[runtime]\nprofile = \"balanced\"\n";
    }
    const controller_native::RuntimeConfig config =
        controller_native::load_runtime_config(path);
    std::filesystem::remove(path);
    require(config.profile == "balanced");
    require(config.vision.capture_fps == 160);
    require(config.vision.idle_capture_fps == 20);
    require(config.vision.keepwarm_when_idle);
    require(config.vision.gpu_service_active_fps == 160);
    require(!config.telemetry.enabled);
    require(!config.vision.aim_perf_file_log);
    require(config.effective_source("runtime.vision.capture_fps") == "profile");
}

void test_user_values_override_profile_and_legacy_rate_is_explicit() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "cod_native_runtime_precedence_test.toml";
    {
        std::ofstream output(path);
        output << "[runtime]\nprofile = \"balanced\"\n"
               << "[runtime.vision]\ncapture_fps = 144\n"
               << "gpu_service_active_fps = 120\n";
    }
    const controller_native::RuntimeConfig config =
        controller_native::load_runtime_config(path);
    std::filesystem::remove(path);
    require(config.vision.capture_fps == 144);
    require(config.vision.gpu_service_active_fps == 120);
    require(config.effective_source("runtime.vision.capture_fps") == "user");
    require(config.effective_source("runtime.vision.gpu_service_active_fps") == "legacy_user");
    require(!config.diagnostics.empty());
}

void test_unknown_keys_are_reported() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "cod_native_runtime_unknown_test.toml";
    {
        std::ofstream output(path);
        output << "[runtime.vision]\ncapture_fsp = 160\nunknown_two = true\n";
    }
    const controller_native::RuntimeConfig config =
        controller_native::load_runtime_config(path);
    std::filesystem::remove(path);
    require(config.diagnostics.size() == 2);
    require(config.diagnostics[0].find("capture_fsp") != std::string::npos);
    require(config.diagnostics[1].find("unknown_two") != std::string::npos);
}

void test_unknown_keys_are_reported_in_every_native_legacy_section() {
    const auto path = std::filesystem::temp_directory_path() / "cod_native_unknown_sections.toml";
    {
        std::ofstream output(path);
        output << "[runtime.gamepad]\ntracker_backed = \"x\"\n"
               << "[gamepad.auto_fire]\naim_ony = true\n"
               << "[gamepad.ai_aim]\nsmoothng = 0.2\n"
               << "[gamepad.aim_assist_dynamics]\nenabeld = true\n"
               << "[gamepad.recoil]\nprofile_amunt = 1\n";
    }
    const auto config = controller_native::load_runtime_config(path);
    std::filesystem::remove(path);
    require(config.diagnostics.size() == 5);
}

void test_invalid_profile_fails_with_available_names() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "cod_native_runtime_invalid_profile_test.toml";
    {
        std::ofstream output(path);
        output << "[runtime]\nprofile = \"fastest\"\n";
    }
    bool failed = false;
    try {
        (void)controller_native::load_runtime_config(path);
    } catch (const std::runtime_error& error) {
        const std::string message = error.what();
        failed = message.find("performance") != std::string::npos &&
            message.find("low_latency") != std::string::npos &&
            message.find("pascal_balanced") == std::string::npos;
    }
    std::filesystem::remove(path);
    require(failed);
}

void test_compact_ads_and_bodylock_modules_resolve_detailed_controls() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "cod_native_runtime_modules_test.toml";
    {
        std::ofstream output(path);
        output << "[runtime]\nprofile = \"balanced\"\n"
               << "[gamepad.ads]\nstrength_scale = 0.5\nvertical_strength_scale = 0.5\n"
               << "sustain_smoothing = 0.25\nacquisition_smoothing = 0.10\nrange_px = 150\nsnap_duration_ms = 90\n"
               << "fov_scale = 0.85\nmanual_opposition_suppression = 0.40\n"
               << "completion_radius_px = 7\ncompletion_fresh_frames = 4\nmax_acquisition_ms = 240\n"
               << "[gamepad.bodylock]\nstrength = 0.33\nvertical_strength = 0.44\n"
               << "smoothing = 0.18\nactivation_range_px = 140\ntolerance_px = 20\n"
               << "lead_strength = 1.20\nmanual_escape_threshold = 0.50\n"
               << "manual_escape_preservation = 0.60\n"
               << "manual_takeover_enabled = false\nmanual_takeover_threshold = 0.42\n"
               << "manual_takeover_commit_ms = 24\nmanual_takeover_release_ms = 96\n";
    }
    const controller_native::RuntimeConfig config = controller_native::load_runtime_config(path);
    std::filesystem::remove(path);
    require(config.gamepad.ai_aim.max_ai_force == 0.32f);
    require(config.gamepad.ai_aim.ads_snap_max_ai_force == 0.50f);
    require(config.gamepad.ai_aim.max_ai_force_y == 0.40f);
    require(config.gamepad.ai_aim.ads_snap_max_ai_force_y == 0.50f);
    require(config.gamepad.ai_aim.smoothing == 0.25f);
    require(config.gamepad.ai_aim.ads_snap_smoothing == 0.10f);
    require(config.gamepad.ai_aim.max_pixels == 150.0f);
    require(config.gamepad.ai_aim.ads_snap_window_ms == 90);
    require(config.gamepad.ai_aim.ads_snap_fov_scale == 0.85f);
    require(config.gamepad.ai_aim.ads_snap_opposing_manual_suppression_max == 0.40f);
    require(config.gamepad.ai_aim.ads_completion_radius_px == 7.0f);
    require(config.gamepad.ai_aim.ads_completion_fresh_frames == 4);
    require(config.gamepad.ai_aim.ads_max_acquisition_ms == 240.0f);
    require(config.gamepad.ai_aim.body_lock_max_ai_force == 0.33f);
    require(config.gamepad.ai_aim.body_lock_max_ai_force_y == 0.44f);
    require(config.gamepad.ai_aim.body_lock_smoothing == 0.18f);
    require(config.gamepad.ai_aim.body_lock_activation_box_px == 140.0f);
    require(config.gamepad.ai_aim.body_lock_box_tolerance_px == 20.0f);
    require(config.gamepad.ai_aim.body_lock_vertical_lead_scale == 1.20f);
    require(config.gamepad.ai_aim.body_lock_manual_escape_input_threshold == 0.50f);
    require(config.gamepad.ai_aim.body_lock_manual_escape_preservation == 0.60f);
    require(!config.gamepad.ai_aim.body_lock_manual_takeover_enabled);
    require(config.gamepad.ai_aim.body_lock_manual_takeover_input_threshold == 0.42f);
    require(config.gamepad.ai_aim.body_lock_manual_takeover_commit_ms == 24.0f);
    require(config.gamepad.ai_aim.body_lock_manual_takeover_release_ms == 96.0f);
    require(config.effective_source("gamepad.ads.strength_scale") == "user");
}

void test_invalid_user_override_reports_key_and_range() {
    const auto path = std::filesystem::temp_directory_path() / "cod_native_invalid_range.toml";
    { std::ofstream output(path); output << "[runtime.vision]\ncapture_fps = 0\n"; }
    bool failed = false;
    try { (void)controller_native::load_runtime_config(path); }
    catch (const std::runtime_error& error) {
        const std::string message = error.what();
        failed = message.find("runtime.vision.capture_fps") != std::string::npos &&
            message.find("1..1000") != std::string::npos;
    }
    std::filesystem::remove(path);
    require(failed);
}

void test_normal_template_preserves_controller_baseline() {
    const auto config = controller_native::load_runtime_config("config.native.example.toml");
    const auto& aim = config.gamepad.ai_aim;
    require(aim.max_ai_force == 0.64f);
    require(aim.max_ai_force_y == 0.80f);
    require(aim.ads_snap_max_ai_force == 1.0f);
    require(aim.ads_snap_max_ai_force_y == 1.0f);
    require(aim.smoothing == 0.62f);
    require(aim.ads_snap_smoothing == 0.0f);
    require(aim.body_lock_max_ai_force == 0.30f);
    require(aim.body_lock_max_ai_force_y == 0.42f);
    require(aim.body_lock_smoothing == 0.14f);
    require(aim.body_lock_manual_escape_input_threshold == 0.45f);
    require(aim.body_lock_manual_escape_preservation == 0.55f);
}

void test_normal_template_does_not_advertise_inactive_fps_legacy_knobs() {
    std::ifstream input("config.native.example.toml");
    const std::string text{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    require(text.find("responsiveness") == std::string::npos);
    require(text.find("weak_memory_decay") == std::string::npos);
    require(text.find("recoil_jitter_") == std::string::npos);
    require(text.find("manual_curve_straighten_") == std::string::npos);
}

void test_committed_legacy_full_fixture_resolves_every_assignment() {
    const std::filesystem::path path =
        "native/controller_native/testdata/legacy_full_config.toml";
    const auto config = controller_native::load_runtime_config(path);
    std::ifstream input(path);
    std::size_t assignments = 0;
    std::string line;
    while (std::getline(input, line)) {
        const auto first = line.find_first_not_of(" \t");
        if (first != std::string::npos && line[first] != '#' && line.find('=') != std::string::npos)
            ++assignments;
    }
    require(config.effective_sources.size() == assignments);
    for (const auto& diagnostic : config.diagnostics)
        require(diagnostic.find("unknown config key") == std::string::npos);
    require(config.vision.capture_fps == 140);
    require(config.vision.gpu_service_active_fps == 120);
    require(config.gamepad.ai_aim.ads_snap_max_ai_force == 1.0f);
    require(config.gamepad.ai_aim.body_lock_max_ai_force == 0.30f);
    require(config.gamepad.ai_aim.body_lock_smoothing == 0.14f);
    require(
        config.gamepad.ai_aim.body_lock_manual_escape_input_threshold == 0.45f);
    require(config.gamepad.ai_aim.body_lock_manual_escape_preservation == 0.55f);
    require(config.gamepad.recoil.feedback_amount == 0.20f);
}

void test_environment_overrides_user_and_reports_source() {
    const auto path = std::filesystem::temp_directory_path() / "cod_native_env_precedence.toml";
    { std::ofstream output(path); output << "[runtime.vision]\ncapture_fps = 144\n"; }
#if defined(_WIN32)
    _putenv_s("VISION_CAPTURE_FPS", "160");
#else
    setenv("VISION_CAPTURE_FPS", "160", 1);
#endif
    const auto config = controller_native::load_runtime_config(path);
#if defined(_WIN32)
    _putenv_s("VISION_CAPTURE_FPS", "");
#else
    unsetenv("VISION_CAPTURE_FPS");
#endif
    std::filesystem::remove(path);
    require(config.vision.capture_fps == 160);
    require(config.vision.gpu_service_active_fps == 160);
    require(config.effective_source("runtime.vision.capture_fps") == "environment");
}

} // namespace

int main() {
    test_vision_gpu_service_defaults_are_enabled();
    test_vision_gpu_service_config_values_parse();
    test_vision_gpu_service_can_be_disabled();
    test_balanced_profile_uses_canonical_vision_defaults();
    test_user_values_override_profile_and_legacy_rate_is_explicit();
    test_unknown_keys_are_reported();
    test_unknown_keys_are_reported_in_every_native_legacy_section();
    test_invalid_profile_fails_with_available_names();
    test_compact_ads_and_bodylock_modules_resolve_detailed_controls();
    test_invalid_user_override_reports_key_and_range();
    test_normal_template_preserves_controller_baseline();
    test_normal_template_does_not_advertise_inactive_fps_legacy_knobs();
    test_committed_legacy_full_fixture_resolves_every_assignment();
    test_environment_overrides_user_and_reports_source();
    return 0;
}
