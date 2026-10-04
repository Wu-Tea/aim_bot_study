#include "../runtime_app/runtime_reload_policy.h"
#include "runtime_config.h"
#include "test_support/native_test_registry.h"
#include "../runtime_app/runtime_stop_signal.h"
#include "../runtime_app/runtime_control_bridge.h"
#include <atomic>
#include <future>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <random>

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

void test_config_rejects_malformed_lines() {
    for (const std::string text : {
        "[runtime.output\nenabled=false\n", "[runtime.output]\nenabled false\n",
        "[runtime.output]\n=false\n", "[runtime.output]\nenabled=\n",
        "[runtime.vision]\nmodel_path=\"unfinished\n"}) {
        TempConfig config("native-config-malformed-lines.toml", text);
        bool rejected = false;
        try { (void)controller_native::load_runtime_config(config.path()); }
        catch (const std::runtime_error&) { rejected = true; }
        require(rejected, "malformed configuration line must fail before desktop save or runtime startup");
    }
}

void test_vision_selection_config() {
    TempConfig file("cod_native_vision_selection.toml",
        "[runtime.vision]\nfriendly_filter_enabled = false\ntarget_height_ratio = 0.35\ntarget_wide_low_height_ratio = 0.6\n");
    const auto config = controller_native::load_runtime_config(file.path());
    require(config.diagnostics.empty(), "selection keys must be recognized");
    require(!config.vision.friendly_filter_enabled, "friend switch must parse");
    require(std::fabs(config.vision.target_height_ratio - 0.35f) < 1e-6f, "person ratio must parse");
    require(std::fabs(config.vision.target_wide_low_height_ratio - 0.6f) < 1e-6f, "wide ratio must parse");
    for (const auto* key : {"target_height_ratio", "target_wide_low_height_ratio"}) {
        for (const auto* value : {"0", "1", "-0.1", "nan"}) {
            TempConfig invalid("cod_native_invalid_selection.toml",
                std::string("[runtime.vision]\n") + key + " = " + value + "\n");
            bool threw = false;
            try { (void)controller_native::load_runtime_config(invalid.path()); }
            catch (const std::runtime_error&) { threw = true; }
            require(threw, "invalid person geometry ratio must fail at config boundary");
        }
    }
}

void test_withdrawn_light_search_key_is_inert() {
    TempConfig file("cod_native_withdrawn_light_search.toml",
        "[runtime.vision]\nlight_search_enabled = true\n");
    const auto config = controller_native::load_runtime_config(file.path());
    require(has_diagnostic(config, "light_search_enabled"),
        "withdrawn light search must be an unknown/inert configuration key");
}

void test_vision_release_hold_config() {
    TempConfig defaults("cod_native_release_hold_default.toml", "[runtime.vision]\n");
    require(controller_native::load_runtime_config(defaults.path()).vision.aim_release_hold_ms == 0,
        "missing release hold must retain previous scheduling");
    for (const int value : {0, 1000, 5000}) {
        TempConfig file("cod_native_release_hold.toml",
            "[runtime.vision]\naim_release_hold_ms = " + std::to_string(value) + "\n");
        const auto config = controller_native::load_runtime_config(file.path());
        require(config.diagnostics.empty(), "release hold key must be recognized");
        require(config.vision.aim_release_hold_ms == value, "release hold must parse");
    }
    for (const auto* value : {"-1", "5001", "nan", "99999999999999999999999"}) {
        TempConfig file("cod_native_release_hold_invalid.toml",
            std::string("[runtime.vision]\naim_release_hold_ms = ") + value + "\n");
        bool threw = false;
        try { (void)controller_native::load_runtime_config(file.path()); }
        catch (const std::runtime_error&) { threw = true; }
        require(threw, "invalid release hold must fail at config boundary");
    }
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

void test_response_prior_config_boundaries() {
    for (const float value : {0.0f, 80.0f, 650.0f, 4000.0f}) {
        TempConfig file("cod_response_prior_valid.toml",
            "[gamepad.ai_aim]\naim_response_initial_scale = " + std::to_string(value) + "\n");
        const auto config = controller_native::load_runtime_config(file.path());
        require(config.diagnostics.empty(), "response prior must be a recognized key");
        require(config.gamepad.ai_aim.aim_response_initial_scale == value,
                "response prior must preserve its configured value");
    }
    for (const char* value : {"-1", "79", "4001", "nan", "inf"}) {
        TempConfig file("cod_response_prior_invalid.toml",
            std::string("[gamepad.ai_aim]\naim_response_initial_scale = ") + value + "\n");
        bool rejected = false;
        try { (void)controller_native::load_runtime_config(file.path()); }
        catch (const std::runtime_error&) { rejected = true; }
        require(rejected, "invalid response prior must be rejected");
    }
}

void test_region_prior_config_boundaries() {
    for (const char* key : {"body_free_initial_scale", "body_slow_initial_scale",
                            "ads_free_initial_scale", "ads_slow_initial_scale"}) {
        for (const char* value : {"0", "80", "1363.69", "4000"}) {
            TempConfig file("cod_region_prior_valid.toml", std::string("[gamepad.ai_aim]\n")+key+"="+value+"\n");
            const auto config = controller_native::load_runtime_config(file.path());
            require(config.diagnostics.empty(), "region priors must parse without unknown-key diagnostics");
            TempConfig empty("cod_region_prior_empty.toml", "[gamepad.ai_aim]\n");
            require(runtime_app::hot_reload_restrictions(controller_native::load_runtime_config(empty.path()), config).empty(),
                "region priors must be eligible for hot reload");
        }
        for (const char* value : {"-1", "79", "4001", "nan", "inf", "wrong", "123junk", "\"900\""}) {
            TempConfig file("cod_region_prior_invalid.toml", std::string("[gamepad.ai_aim]\n")+key+"="+value+"\n");
            bool rejected = false;
            try { (void)controller_native::load_runtime_config(file.path()); } catch (const std::exception&) { rejected = true; }
            require(rejected, "invalid regional prior must fail at the config boundary");
        }
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

namespace {

void test_retired_recoil_profile_cannot_be_enabled_by_config() {
    TempConfig file("cod_native_retired_recoil_profile.toml",
                    "[gamepad.recoil]\nprofile_playback_enabled = true\n"
                    "native_recognizer_enabled = true\nfeedback_amount = 0.23\n");
    const auto config = controller_native::load_runtime_config(file.path());
    require(!config.gamepad.recoil.profile_playback_enabled,
            "retired profile playback was enabled by TOML");
    require(!config.gamepad.recoil.native_recognizer_enabled,
            "retired profile recognizer was enabled by TOML");
    require(std::fabs(config.gamepad.recoil.feedback_amount - 0.23f) < 1e-6f,
            "retiring profiles changed the live recoil amount");
    require(has_diagnostic(config, "profile_playback_enabled") &&
            has_diagnostic(config, "native_recognizer_enabled"),
            "retired recoil keys must be unknown rather than advertised settings");
    TempConfig stale("cod_native_retired_recoil_paths.toml",
        "[gamepad.recoil]\nprofile_directory = \"unused-profile\"\n"
        "recognizer_state_path = \"unused-state.json\"\nprofile_amount = 9\n");
    const auto retired = controller_native::load_runtime_config(stale.path());
    require(has_diagnostic(retired, "profile_directory") &&
            has_diagnostic(retired, "recognizer_state_path") &&
            has_diagnostic(retired, "profile_amount"),
            "retired file-backed recoil settings must be inert and diagnosed");
    require(retired.gamepad.recoil.recognizer_state_path.empty() &&
            retired.gamepad.recoil.profile_amount == 1.0f,
            "retired recoil configuration still changes runtime state");
}

void test_external_stop_signal_obeys_listener_lifetime() {
    for (bool early : {false, true}) {
        runtime_app::RuntimeStopSignal signal;
        const auto name = L"Local\\cod_native_runtime_stop_" + std::to_wstring(GetCurrentProcessId());
        const auto event = OpenEventW(EVENT_MODIFY_STATE, FALSE, name.c_str());
        require(event != nullptr, "stop signal must be available during initialization");
        std::promise<void> called;
        auto future = called.get_future();
        if (early) SetEvent(event);
        {
            auto listener = signal.listen([&called] { called.set_value(); });
            if (!early) SetEvent(event);
            require(future.wait_for(std::chrono::seconds(2)) == std::future_status::ready,
                "normal or early stop request must reach the runtime owner");
        }
        CloseHandle(event);
    }
    std::atomic<bool> called{false};
    {
        runtime_app::RuntimeStopSignal signal;
        auto listener = signal.listen([&called] { called.store(true); });
    }
    require(!called.load(), "listener cancellation must not request a spurious stop");
}

void test_game_blocks_resolve_before_deriving_gains() {
    TempConfig file("cod_game_blocks.toml",
        "[runtime]\ngame = \"bo3\"\n"
        "[gamepad.ads]\nstrength_scale = 0.8\n"
        "[gamepad.output_transfer]\nenabled = false\n"
        "[games.apex.gamepad.ads]\nstrength_scale = 0.5\n"
        "[games.apex.runtime.vision]\nmodel_path = \"apex.engine\"\n"
        "[games.bo3.gamepad.output_transfer]\nenabled = true\ndeadzone = 0.20\n");
    const auto base = controller_native::load_runtime_config(file.path(), "", "default");
    const auto apex = controller_native::load_runtime_config(file.path(), "", "apex");
    const auto bo3 = controller_native::load_runtime_config(file.path());
    require(base.game == "default" && !base.gamepad.output_transfer.enabled,
        "explicit default must override configured game without inheriting game settings");
    require(apex.game == "apex" && apex.vision.model_path == "apex.engine" &&
        !apex.gamepad.output_transfer.enabled && apex.ads.strength_scale == .5f,
        "selected game must replace base values and ignore other games");
    require(std::fabs(apex.gamepad.ai_aim.ads_snap_max_ai_force /
        base.gamepad.ai_aim.ads_snap_max_ai_force - .5f / .8f) < 1e-5f,
        "game override must not multiply a gain twice");
    require(bo3.game == "bo3" && bo3.gamepad.output_transfer.enabled &&
        bo3.gamepad.output_transfer.deadzone == .20f,
        "configured game must select its native block");
    require(apex.diagnostics.empty() && bo3.diagnostics.empty() &&
        apex.effective_source("runtime.vision.model_path") == "game:apex",
        "inactive blocks must not produce unknown-key diagnostics");
    bool rejected = false;
    try { (void)controller_native::load_runtime_config(file.path(), "", "missing"); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "unknown game must fail instead of silently using default");
}

void test_game_output_transfer_config() {
    TempConfig defaults("cod_output_transfer_default.toml", "");
    require(!controller_native::load_runtime_config(defaults.path()).gamepad.output_transfer.enabled,
        "existing profiles must not enable output transfer");
    TempConfig file("cod_output_transfer.toml",
        "[gamepad.output_transfer]\nenabled = true\naxial = true\ndeadzone = 0.18\ngame_exponent = 2.0\n");
    auto config = controller_native::load_runtime_config(file.path());
    require(config.diagnostics.empty() && config.gamepad.output_transfer.enabled &&
        config.gamepad.output_transfer.axial && config.gamepad.output_transfer.deadzone == .18f &&
        config.gamepad.output_transfer.game_exponent == 2.0f, "output adapter keys must parse");
    for (const auto* key : {"deadzone", "game_exponent"}) {
        for (const auto* value : {"-0.01", "3.01", "nan", "inf"}) {
            TempConfig invalid("cod_output_transfer_invalid.toml",
                std::string("[gamepad.output_transfer]\n") + key + " = " + value + "\n");
            bool threw = false;
            try { (void)controller_native::load_runtime_config(invalid.path()); }
            catch (const std::runtime_error&) { threw = true; }
            require(threw, "invalid output transfer parameter must fail at config boundary");
        }
    }
}

void test_hipfire_recoil_config() {
    TempConfig defaults("cod_native_hipfire_recoil_default.toml", "[gamepad.recoil]\n");
    require(controller_native::load_runtime_config(defaults.path()).gamepad.recoil.hipfire_multiplier == 1.0f,
        "default hipfire recoil must preserve existing game profiles");
    TempConfig half("cod_native_hipfire_recoil_half.toml",
        "[gamepad.recoil]\nfeedback_amount = 0.2\nhipfire_multiplier = 0.5\n");
    const auto config = controller_native::load_runtime_config(half.path());
    require(config.diagnostics.empty() && config.gamepad.recoil.hipfire_multiplier == .5f,
        "hipfire multiplier must parse independently of ADS amount");
    for (const auto* value : {"-0.1", "1.01", "nan", "inf"}) {
        TempConfig invalid("cod_native_hipfire_recoil_invalid.toml",
            std::string("[gamepad.recoil]\nhipfire_multiplier = ") + value + "\n");
        bool threw = false;
        try { (void)controller_native::load_runtime_config(invalid.path()); }
        catch (const std::runtime_error&) { threw = true; }
        require(threw, "hipfire multiplier must be finite and within 0..1");
    }
}

}  // namespace

void test_hot_reload_diff_and_control_channel() {
    TempConfig first("cod_hot_reload_first.toml", "[gamepad.ads]\nstrength_scale=0.8\n");
    TempConfig second("cod_hot_reload_second.toml", "[gamepad.ads]\nstrength_scale=0.5\n[gamepad.auto_fire]\nfire_output=\"RT\"\nmanual_fire_input=\"RT\"\n");
    auto a = controller_native::load_runtime_config(first.path());
    auto b = controller_native::load_runtime_config(second.path());
    require(runtime_app::hot_reload_restrictions(a, b).empty(), "numeric and fire bindings must be hot eligible");
    auto structural = b;
    structural.effective_values["runtime.vision.model_path"] = "\"other.engine\"";
    require(!runtime_app::hot_reload_restrictions(a, structural).empty(), "model change must reject entire hot transaction");
    auto invalid = b;
    invalid.effective_values["gamepad.ads.strength_scale"] = "\"wrong\"";
    bool rejected = false;
    try { (void)runtime_app::hot_reload_restrictions(a, invalid); } catch (...) { rejected = true; }
    require(rejected, "legacy numeric parser fallback cannot silently validate hot reload");
    runtime_app::RuntimeControlBridge bridge(a, [b] { return b; });
    const auto suffix = std::to_wstring(GetCurrentProcessId());
    HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, (L"Local\\cod_native_control_" + suffix).c_str());
    require(mapping != nullptr, "control mapping missing");
    auto* memory = static_cast<runtime_app::RuntimeControlMemory*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0));
    HANDLE request = OpenEventW(EVENT_MODIFY_STATE, FALSE, (L"Local\\cod_native_reload_" + suffix).c_str());
    InterlockedExchange64(&memory->requested_id, 719);
    SetEvent(request);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    std::shared_ptr<const controller_native::RuntimeConfig> prepared;
    while (!prepared && std::chrono::steady_clock::now() < deadline) { prepared = bridge.take_prepared(); std::this_thread::yield(); }
    require(prepared && prepared->gamepad.auto_fire.fire_output == "RT", "worker must load immutable candidate off tick");
    bridge.complete();
    while (memory->snapshot.completed_id != 719 && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    require(memory->snapshot.status == 2 && memory->snapshot.revision == 1, "GUI acknowledgement must follow actual commit");
    CloseHandle(request); UnmapViewOfFile(memory); CloseHandle(mapping);
}

void test_hipfire_ai_and_response_learning_config() {
    TempConfig base("cod_ai_controls_base.toml", "[gamepad.ai_aim]\n");
    auto before = controller_native::load_runtime_config(base.path());
    require(before.gamepad.ai_aim.hipfire_multiplier == 1.0f && before.gamepad.ai_aim.aim_response_learning_enabled,
        "new controls must preserve existing defaults");
    TempConfig changed("cod_ai_controls_changed.toml", "[gamepad.ai_aim]\nhipfire_multiplier=0.75\naim_response_learning_enabled=false\n");
    auto after = controller_native::load_runtime_config(changed.path());
    require(after.diagnostics.empty() && after.gamepad.ai_aim.hipfire_multiplier == .75f &&
        !after.gamepad.ai_aim.aim_response_learning_enabled, "decimal multiplier and disabled learning must reach native config");
    require(runtime_app::hot_reload_restrictions(before, after).empty() &&
        runtime_app::preserve_response_learning_on_reload(before, after), "new controls must be hot eligible without discarding learned response");
    auto prior_changed = after;
    prior_changed.effective_values["gamepad.ai_aim.body_free_initial_scale"] = "700";
    require(!runtime_app::preserve_response_learning_on_reload(after, prior_changed), "changing response priors still requires learning reset");
    for (const auto* value : {"-0.1", "3.01", "nan", "inf", "0.75junk"}) {
        TempConfig invalid("cod_ai_controls_invalid.toml", std::string("[gamepad.ai_aim]\nhipfire_multiplier=") + value + "\n");
        bool rejected = false;
        try { (void)controller_native::load_runtime_config(invalid.path()); } catch (...) { rejected = true; }
        require(rejected, "AI multiplier must be finite, complete decimal input within 0..3");
    }
    TempConfig invalid_bool("cod_ai_controls_bool.toml", "[gamepad.ai_aim]\naim_response_learning_enabled=\"false\"\n");
    bool rejected = false;
    try { (void)controller_native::load_runtime_config(invalid_bool.path()); } catch (...) { rejected = true; }
    require(rejected, "learning switch must be a boolean, not a quoted string");
}

void test_custom_curve_config_and_roundtrip() {
    TempConfig file("cod_custom_curve.toml", "[gamepad.aim_response_curve]\nalgorithm=\"custom_lut\"\ncustom_points=\"0:0;0.25:0.1;0.5:0.3;1:1\"\n");
    auto config = controller_native::load_runtime_config(file.path());
    require(config.diagnostics.empty() && config.gamepad.aim_response_curve.custom_count == 4, "custom curve reaches native config");
    for (const auto* points : {"0:0;1:0.9", "0:0;0.5:0.8;1:0.7", "0:0;0.5:0.2;0.5:0.8;1:1", "0:0;nan:0.5;1:1", "0:0;1:1;", "0:0;0.5oops:0.5;1:1"}) {
        TempConfig bad("cod_invalid_custom_curve.toml", std::string("[gamepad.aim_response_curve]\nalgorithm=\"custom_lut\"\ncustom_points=\"") + points + "\"\n");
        bool rejected = false;
        try { (void)controller_native::load_runtime_config(bad.path()); } catch (const std::runtime_error&) { rejected = true; }
        require(rejected, "invalid custom curves must fail at config ownership boundary");
    }
    std::mt19937 rng(20261004);
    std::uniform_real_distribution<float> random(.01f, 1.f);
    for (int curve_index = 0; curve_index < 100; ++curve_index) {
        auto& curve = config.gamepad.aim_response_curve;
        curve.custom_count = 11;
        curve.custom_response[0] = 0;
        for (int i = 0; i < 11; ++i) {
            curve.custom_stick[i] = i / 10.f;
            if (i) curve.custom_response[i] = curve.custom_response[i - 1] + random(rng);
        }
        const float total = curve.custom_response[10];
        for (int i = 1; i < 11; ++i) curve.custom_response[i] /= total;
        float previous = -1;
        for (int i = 0; i <= 500; ++i) {
            const float value = i / 500.f;
            const auto response = controller_native::forward_aim_response_curve({value, 0}, curve);
            const auto stick = controller_native::inverse_aim_response_curve(response, curve);
            require(std::isfinite(response.x) && response.x >= previous && std::fabs(stick.x - value) < 3e-5f && stick.y == 0,
                "custom mapping is monotonic, finite, direction-preserving and reversible");
            previous = response.x;
            const pipeline_contract::Vec2f diagonal{value * .6f, -value * .8f};
            const auto diagonal_response = controller_native::forward_aim_response_curve(diagonal, curve);
            const auto roundtrip = controller_native::inverse_aim_response_curve(diagonal_response, curve);
            require(std::fabs(roundtrip.x - diagonal.x) < 3e-5f && std::fabs(roundtrip.y - diagonal.y) < 3e-5f,
                "custom curve preserves diagonal direction and negative-axis sign");
        }
    }
}

void test_scalar_config_rejects_invalid_tokens() {
    const std::pair<const char*, const char*> fields[] = {
        {"gamepad.ads", "strength_scale"},
        {"runtime.scheduler", "controller_tick_hz"},
        {"runtime.input", "controller_index"},
        {"runtime.output", "enabled"},
    };
    const char* invalid[][6] = {
        {"\"wrong\"", "0.5junk", "nan", "inf", "1e100", ""},
        {"\"1000\"", "1000junk", "nan", "inf", "9999999999999999", ""},
        {"\"0\"", "0junk", "-1", "inf", "9999999999999999", ""},
        {"\"false\"", "falsejunk", "unknown", "nan", "2", ""},
    };
    for (int field = 0; field < 4; ++field) {
        for (const auto* value : invalid[field]) {
            const std::string text = std::string("[") + fields[field].first + "]\n" +
                fields[field].second + "=" + value + "\n";
            TempConfig file("cod_scalar_invalid.toml", text);
            bool rejected = false;
            try { (void)controller_native::load_runtime_config(file.path()); }
            catch (const std::exception&) { rejected = true; }
            require(rejected, "explicit invalid scalar must fail, not silently keep a default or parse a prefix");
        }
    }
    TempConfig valid("cod_scalar_valid.toml",
        "[gamepad.ads]\nstrength_scale=0.5\n"
        "[runtime.scheduler]\ncontroller_tick_hz=1000\n"
        "[runtime.input]\ncontroller_index=0\n"
        "[runtime.output]\nenabled=false\n");
    const auto config = controller_native::load_runtime_config(valid.path());
    require(config.ads.strength_scale == .5f && config.scheduler.controller_tick_hz == 1000 &&
            config.gamepad.xinput_user_index == 0 && !config.output.enabled,
            "valid scalar configuration keeps its existing meaning");
}

void register_runtime_config_tests(native_test::Registry& registry) {
    registry.add_case("BaseContracts", "config_rejects_malformed_lines", test_config_rejects_malformed_lines);
    registry.add_case("BaseContracts", "scalar_config_rejects_invalid_tokens", test_scalar_config_rejects_invalid_tokens);
    registry.add_case("BaseContracts", "custom_curve_config_and_randomized_roundtrip", test_custom_curve_config_and_roundtrip);
    registry.add_case("BaseContracts", "hipfire_ai_and_response_learning_config", test_hipfire_ai_and_response_learning_config);
    registry.add_case("BaseContracts", "region_prior_config_boundaries", test_region_prior_config_boundaries);
    registry.add_case("BaseContracts", "hot_reload_diff_control_channel", test_hot_reload_diff_and_control_channel);
    registry.add_case("BaseContracts", "external_stop_signal_listener_lifetime", test_external_stop_signal_obeys_listener_lifetime);
    registry.add_case("BaseContracts", "game_blocks_resolve_before_deriving_gains", test_game_blocks_resolve_before_deriving_gains);
    registry.add_case("BaseContracts", "game_output_transfer_config", test_game_output_transfer_config);
    registry.add_case("BaseContracts", "hipfire_recoil_config", test_hipfire_recoil_config);
    registry.add_case("BaseContracts", "vision_release_hold_config", test_vision_release_hold_config);
    registry.add_case("BaseContracts", "withdrawn_light_search_key_is_inert", test_withdrawn_light_search_key_is_inert);
    registry.add_case("BaseContracts", "vision_selection_config", test_vision_selection_config);
    registry.add_case("BaseContracts", "response_prior_config_boundaries", test_response_prior_config_boundaries);
    registry.add_case("BaseContracts", "retired_recoil_profile_is_inert", test_retired_recoil_profile_cannot_be_enabled_by_config);
    registry.add_case("BaseContracts", "current_control_keys_parse", test_current_control_keys_parse);
    registry.add_case("BaseContracts", "recoil_defaults_use_product_dynamic_range", test_recoil_defaults_use_product_dynamic_range);
    registry.add_case("BaseContracts", "retired_low_rate_keys_are_unknown_and_inert", test_retired_low_rate_keys_are_unknown_and_inert);
    registry.add_case("BaseContracts", "profile_override_has_one_capture_cadence", test_profile_override_has_one_capture_cadence);
    registry.add_case("BaseContracts", "profiles_default_idle_vision_to_60_hz", test_profiles_default_idle_vision_to_60_hz);
    registry.add_case("BaseContracts", "invalid_safety_boundary_fails_closed", test_invalid_safety_boundary_fails_closed);
    registry.add_case("BaseContracts", "example_config_contains_no_retired_keys", test_example_config_contains_no_retired_keys);
}
