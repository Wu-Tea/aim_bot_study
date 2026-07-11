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
    const controller_native::RuntimeConfig config =
        controller_native::load_runtime_config(std::filesystem::path{});
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
            message.find("pascal_balanced") != std::string::npos;
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
               << "[gamepad.ads]\nstrength = 0.72\nvertical_strength = 0.81\n"
               << "smoothing = 0.25\nrange_px = 150\nsnap_duration_ms = 90\n"
               << "fov_scale = 0.85\nmanual_opposition_suppression = 0.40\n"
               << "[gamepad.bodylock]\nstrength = 0.33\nvertical_strength = 0.44\n"
               << "smoothing = 0.18\nactivation_range_px = 140\ntolerance_px = 20\n"
               << "lead_strength = 1.20\nmanual_escape_threshold = 0.50\n"
               << "manual_escape_preservation = 0.60\n";
    }
    const controller_native::RuntimeConfig config = controller_native::load_runtime_config(path);
    std::filesystem::remove(path);
    require(config.gamepad.ai_aim.max_ai_force == 0.72f);
    require(config.gamepad.ai_aim.ads_snap_max_ai_force == 0.72f);
    require(config.gamepad.ai_aim.ads_snap_max_ai_force_y == 0.81f);
    require(config.gamepad.ai_aim.ads_snap_smoothing == 0.25f);
    require(config.gamepad.ai_aim.max_pixels == 150.0f);
    require(config.gamepad.ai_aim.ads_snap_window_ms == 90);
    require(config.gamepad.ai_aim.ads_snap_fov_scale == 0.85f);
    require(config.gamepad.ai_aim.ads_snap_opposing_manual_suppression_max == 0.40f);
    require(config.gamepad.ai_aim.body_lock_max_ai_force == 0.33f);
    require(config.gamepad.ai_aim.body_lock_max_ai_force_y == 0.44f);
    require(config.gamepad.ai_aim.body_lock_smoothing == 0.18f);
    require(config.gamepad.ai_aim.body_lock_activation_box_px == 140.0f);
    require(config.gamepad.ai_aim.body_lock_box_tolerance_px == 20.0f);
    require(config.gamepad.ai_aim.body_lock_vertical_lead_scale == 1.20f);
    require(config.gamepad.ai_aim.body_lock_manual_escape_input_threshold == 0.50f);
    require(config.gamepad.ai_aim.body_lock_manual_escape_preservation == 0.60f);
    require(config.effective_source("gamepad.ads.strength") == "user");
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

} // namespace

int main() {
    test_vision_gpu_service_defaults_are_enabled();
    test_vision_gpu_service_config_values_parse();
    test_vision_gpu_service_can_be_disabled();
    test_balanced_profile_uses_canonical_vision_defaults();
    test_user_values_override_profile_and_legacy_rate_is_explicit();
    test_unknown_keys_are_reported();
    test_invalid_profile_fails_with_available_names();
    test_compact_ads_and_bodylock_modules_resolve_detailed_controls();
    test_invalid_user_override_reports_key_and_range();
    return 0;
}
