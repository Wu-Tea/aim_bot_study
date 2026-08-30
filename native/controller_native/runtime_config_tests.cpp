#include "runtime_config.h"
#include "test_support/native_test_registry.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

class TempConfig {
public:
    TempConfig(const char* name, const std::string& contents)
        : path_(std::filesystem::temp_directory_path() / name) {
        std::ofstream output(path_, std::ios::trunc);
        output << contents;
    }

    ~TempConfig() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

bool has_diagnostic(
    const controller_native::RuntimeConfig& config,
    const std::string& needle) {
    for (const auto& diagnostic : config.diagnostics) {
        if (diagnostic.find(needle) != std::string::npos) return true;
    }
    return false;
}

void test_current_control_keys_parse() {
    TempConfig file(
        "cod_native_current_control_config.toml",
        "[runtime.vision]\n"
        "capture_fps = 180\n"
        "[runtime.telemetry]\n"
        "enabled = true\n"
        "directory = \"runs/current-telemetry\"\n"
        "[runtime.scheduler]\n"
        "efficiency_core_affinity = false\n"
        "efficiency_core_count = 3\n"
        "[gamepad.enemy_mark]\n"
        "enabled = false\n"
        "l3_cooldown_ms = 750\n"
        "lt_cooldown_ms = 500\n"
        "[gamepad.tracker]\n"
        "aim_height_ratio = 0.31\n"
        "max_observation_age_ms = 42\n"
        "[gamepad.auto_fire]\n"
        "fire_output = \"RT\"\n"
        "manual_fire_activates_ai_aim = false\n"
        "[gamepad.ads]\n"
        "strength_scale = 1.25\n"
        "vertical_strength_scale = 1.10\n"
        "activation_radius_px = 140\n"
        "pickup_base_radius_px = 155\n"
        "scope_ready_trigger = 0.82\n"
        "snap_duration_ms = 120\n"
        "completion_radius_px = 7\n"
        "completion_fresh_frames = 4\n"
        "target_wait_ms = 240\n"
        "extension_budget_ms = 180\n"
        "[gamepad.bodylock]\n"
        "strength = 0.44\n"
        "vertical_strength = 0.49\n"
        "activation_range_px = 92\n"
        "tolerance_px = 9\n"
        "[gamepad.ai_aim]\n"
        "aim_response_effect_delay_ms = 11\n"
        "cue_hold_full_force_min_target_height_ratio = 0.28\n"
        "visual_authority_enabled = false\n");

    const auto config = controller_native::load_runtime_config(file.path());
    require(config.vision.capture_fps == 180, "capture cadence not parsed");
    require(config.telemetry.enabled, "telemetry enable not parsed");
    require(config.telemetry.directory == "runs/current-telemetry",
            "telemetry directory not parsed");
    require(!config.scheduler.efficiency_core_affinity,
            "scheduler efficiency-core affinity not parsed");
    require(config.scheduler.efficiency_core_count == 3,
            "scheduler efficiency-core count not parsed");
    require(config.gamepad.auto_fire.fire_output == "RT",
            "auto-fire output not parsed");
    require(!config.gamepad.auto_fire.manual_fire_activates_ai_aim,
            "manual-fire AI-aim switch not parsed");
    require(!config.gamepad.enemy_mark.enabled,
            "enemy-mark switch not parsed");
    require(config.gamepad.enemy_mark.l3_cooldown_ms == 750,
            "enemy-mark L3 cooldown not parsed");
    require(config.gamepad.enemy_mark.lt_cooldown_ms == 500,
            "enemy-mark LT cooldown not parsed");
    require(std::fabs(config.gamepad.tracker.aim_height_ratio - 0.31f) < 1e-5f,
            "aim height not parsed");
    require(std::fabs(config.gamepad.tracker.max_observation_age_ms - 42.0f) < 1e-5f,
            "observation age not parsed");
    require(std::fabs(config.gamepad.ai_aim.ads_snap_max_ai_force - 1.25f) < 1e-5f,
            "ADS strength scale not applied");
    require(std::fabs(config.gamepad.ai_aim.ads_snap_max_ai_force_y - 1.10f) < 1e-5f,
            "ADS vertical strength scale not applied");
    require(config.gamepad.ai_aim.ads_snap_window_ms == 120,
            "ADS acquisition duration not parsed");
    require(std::fabs(
                config.gamepad.ai_aim.ads_activation_radius_px - 140.0f) <
            1e-5f,
            "ADS response radius not parsed");
    require(std::fabs(
                config.gamepad.ai_aim.ads_pickup_base_radius_px - 155.0f) <
            1e-5f,
            "ADS pickup base radius not parsed");
    require(std::fabs(
                config.gamepad.ai_aim.ads_scope_ready_trigger - 0.82f) <
            1e-5f,
            "ADS scope-ready trigger not parsed");
    require(config.gamepad.ai_aim.ads_completion_fresh_frames == 4,
            "settle frame count not parsed");
    require(std::fabs(
                config.gamepad.ai_aim.ads_target_wait_ms - 240.0f) < 1e-5f,
            "ADS target-wait deadline not parsed");
    require(std::fabs(
                config.gamepad.ai_aim.ads_extension_budget_ms - 180.0f) <
            1e-5f,
            "ADS extension budget not parsed");
    require(std::fabs(config.gamepad.ai_aim.body_lock_max_ai_force - 0.44f) < 1e-5f,
            "BodyLock force not parsed");
    require(std::fabs(
                config.gamepad.ai_aim.aim_response_effect_delay_ms - 11.0f) <
            1e-5f,
            "aim response effect delay not parsed");
    require(std::fabs(
                config.gamepad.ai_aim.
                    cue_hold_full_force_min_target_height_ratio -
                0.28f) < 1e-5f,
            "close cue-hold full-force threshold not parsed");
    require(!config.gamepad.ai_aim.visual_authority_enabled,
            "visual-authority A/B switch not parsed");
    require(config.diagnostics.empty(), "current config produced diagnostics");

    TempConfig legacy_file(
        "cod_native_legacy_ads_budget_config.toml",
        "[gamepad.ads]\n"
        "max_acquisition_ms = 240\n");
    const auto legacy =
        controller_native::load_runtime_config(legacy_file.path());
    require(std::fabs(
                legacy.gamepad.ai_aim.ads_extension_budget_ms - 240.0f) <
            1e-5f,
            "legacy ADS key no longer maps to the extension budget");
    require(std::fabs(
                legacy.gamepad.ai_aim.ads_target_wait_ms - 220.0f) < 1e-5f,
            "legacy extension key must not overwrite target-wait deadline");
}

void test_recoil_defaults_use_product_dynamic_range() {
    const controller_native::GamepadRecoilConfig defaults;
    require(std::fabs(defaults.feedback_min_amount - 0.14f) < 1e-5f,
            "default fallback recoil floor is not 0.14");
    require(std::fabs(defaults.feedback_max_amount - 0.34f) < 1e-5f,
            "default fallback recoil ceiling is not 0.34");

    const auto example = controller_native::load_runtime_config(
        std::filesystem::path("config.native.example.toml"));
    require(std::fabs(example.gamepad.recoil.feedback_min_amount - 0.14f) <
                1e-5f,
            "example config fallback recoil floor is not 0.14");
    require(std::fabs(example.gamepad.recoil.feedback_max_amount - 0.34f) <
                1e-5f,
            "example config fallback recoil ceiling is not 0.34");
}

void test_retired_low_rate_keys_are_unknown_and_inert() {
    TempConfig file(
        "cod_native_retired_low_rate_keys.toml",
        "[runtime.vision]\n"
        "aim_perf_file_log = true\n"
        "aim_perf_log_dir = \"runs/old-aim-perf\"\n"
        "aim_perf_log_interval_ticks = 1\n"
        "[runtime.telemetry]\n"
        "mode = \"profile\"\n"
        "vision_on_new_frame = true\n"
        "candidate_details = \"on_event\"\n"
        "event_pre_ms = 500\n"
        "event_post_ms = 1000\n"
        "[runtime.scheduler]\n"
        "ai_proposal_mode = \"fixed\"\n"
        "ai_proposal_hz = 250\n"
        "ai_proposal_update_stage = \"solver-only\"\n"
        "[gamepad.tracker]\n"
        "backend = \"fps_reference\"\n"
        "projection_age_ms = 96\n"
        "lead_seconds = 0.026\n"
        "causal_memory_enabled = true\n"
        "[gamepad.intent]\n"
        "wrong_way_manual_preservation_floor = 0.5\n"
        "fresh_vision_wrong_way_manual_floor = 0.2\n"
        "helpful_manual_overdrive_enabled = true\n"
        "helpful_manual_overdrive_max_scale = 1.20\n"
        "helpful_manual_direction_weight = 0.45\n"
        "[gamepad.aim_assist_dynamics]\n"
        "enabled = false\n"
        "[gamepad.ads]\n"
        "start_delay_ms = 2\n"
        "start_ramp_ms = 8\n"
        "[gamepad.ai_aim]\n"
        "ads_start_delay_ms = 2\n"
        "ads_start_ramp_ms = 8\n"
        "[gamepad.recoil]\n"
        "operation_aware_recoil = true\n");

    const auto config = controller_native::load_runtime_config(file.path());
    require(has_diagnostic(config, "gamepad.tracker.backend"),
            "retired tracker backend must be rejected");
    require(has_diagnostic(config, "runtime.vision.aim_perf_file_log"),
            "retired aim perf switch must be rejected");
    require(has_diagnostic(config, "runtime.vision.aim_perf_log_dir"),
            "retired aim perf directory must be rejected");
    require(has_diagnostic(config, "runtime.vision.aim_perf_log_interval_ticks"),
            "retired aim perf interval must be rejected");
    require(has_diagnostic(config, "runtime.telemetry.mode"),
            "retired telemetry mode must be rejected");
    require(has_diagnostic(config, "runtime.telemetry.vision_on_new_frame"),
            "retired telemetry vision switch must be rejected");
    require(has_diagnostic(config, "runtime.telemetry.candidate_details"),
            "retired candidate detail policy must be rejected");
    require(has_diagnostic(config, "runtime.telemetry.event_pre_ms") &&
                has_diagnostic(config, "runtime.telemetry.event_post_ms"),
            "retired telemetry event windows must be rejected");
    require(has_diagnostic(config, "runtime.scheduler.ai_proposal_mode") &&
                has_diagnostic(config, "runtime.scheduler.ai_proposal_hz") &&
                has_diagnostic(
                    config, "runtime.scheduler.ai_proposal_update_stage"),
            "retired independent AI proposal cadence must be rejected");
    require(has_diagnostic(config, "gamepad.tracker.projection_age_ms"),
            "retired projection must be rejected");
    require(has_diagnostic(config, "gamepad.tracker.lead_seconds"),
            "retired lead must be rejected");
    require(has_diagnostic(config, "gamepad.tracker.causal_memory_enabled"),
            "retired causal memory must be rejected");
    require(has_diagnostic(config, "gamepad.intent.wrong_way_manual_preservation_floor"),
            "retired axis floor must be rejected");
    require(has_diagnostic(config, "gamepad.intent.helpful_manual_overdrive_enabled"),
            "retired manual overdrive switch must be rejected");
    require(has_diagnostic(config, "gamepad.intent.helpful_manual_overdrive_max_scale"),
            "retired manual overdrive scale must be rejected");
    require(has_diagnostic(config, "gamepad.intent.helpful_manual_direction_weight"),
            "retired manual direction weight must be rejected");
    require(has_diagnostic(config, "gamepad.aim_assist_dynamics.enabled"),
            "retired dynamics switch must be rejected");
    require(has_diagnostic(config, "gamepad.ads.start_delay_ms") &&
                has_diagnostic(config, "gamepad.ads.start_ramp_ms"),
            "retired ADS onset shaping must be rejected");
    require(has_diagnostic(config, "gamepad.ai_aim.ads_start_delay_ms") &&
                has_diagnostic(config, "gamepad.ai_aim.ads_start_ramp_ms"),
            "retired legacy ADS onset shaping must be rejected");
    require(has_diagnostic(config, "gamepad.recoil.operation_aware_recoil"),
            "retired operation-aware recoil switch must be rejected");
    require(std::fabs(config.gamepad.tracker.max_observation_age_ms - 50.0f) < 1e-5f,
            "retired keys changed current defaults");
}

void test_profile_override_has_one_capture_cadence() {
    TempConfig file(
        "cod_native_profile_override.toml",
        "[runtime]\nprofile = \"balanced\"\n"
        "[runtime.vision]\ncapture_fps = 175\n");
    const auto from_file = controller_native::load_runtime_config(file.path());
    require(from_file.vision.capture_fps == 175,
            "explicit capture cadence must override profile");

    const auto from_cli = controller_native::load_runtime_config(
        file.path(), "performance");
    require(from_cli.vision.capture_fps == 175,
            "file cadence must remain the sole explicit source after profile defaults");
    require(from_cli.profile == "performance", "profile override not applied");
}

void test_profiles_default_idle_vision_to_60_hz() {
    for (const char* profile : {"performance", "balanced", "low_latency"}) {
        TempConfig file(
            "cod_native_idle_vision_default.toml",
            std::string("[runtime]\nprofile = \"") + profile + "\"\n");
        const auto config = controller_native::load_runtime_config(file.path());
        require(config.vision.idle_capture_fps == 60,
                "every runtime profile must default idle Vision to 60 Hz");
    }
}

void test_invalid_safety_boundary_fails_closed() {
    TempConfig file(
        "cod_native_invalid_fire_pulse.toml",
        "[gamepad.auto_fire]\n"
        "pulse_width_ms = 120\n"
        "pulse_period_ms = 100\n");
    bool threw = false;
    try {
        (void)controller_native::load_runtime_config(file.path());
    } catch (const std::runtime_error&) {
        threw = true;
    }
    require(threw, "invalid auto-fire pulse boundary must fail closed");
}

void test_example_config_contains_no_retired_keys() {
    const auto config = controller_native::load_runtime_config(
        std::filesystem::path("config.native.example.toml"));
    require(config.diagnostics.empty(),
            "example config must contain only current keys");
}

}  // namespace

void register_runtime_config_tests(native_test::Registry& registry) {
    registry.add_case("BaseContracts", "current_control_keys_parse", test_current_control_keys_parse);
    registry.add_case("BaseContracts", "recoil_defaults_use_product_dynamic_range", test_recoil_defaults_use_product_dynamic_range);
    registry.add_case("BaseContracts", "retired_low_rate_keys_are_unknown_and_inert", test_retired_low_rate_keys_are_unknown_and_inert);
    registry.add_case("BaseContracts", "profile_override_has_one_capture_cadence", test_profile_override_has_one_capture_cadence);
    registry.add_case("BaseContracts", "profiles_default_idle_vision_to_60_hz", test_profiles_default_idle_vision_to_60_hz);
    registry.add_case("BaseContracts", "invalid_safety_boundary_fails_closed", test_invalid_safety_boundary_fails_closed);
    registry.add_case("BaseContracts", "example_config_contains_no_retired_keys", test_example_config_contains_no_retired_keys);
}
