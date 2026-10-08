#include "runtime_loop.h"
#include "runtime_timing.h"
#include "runtime_stop_signal.h"
#include "runtime_control_bridge.h"

#include "controller_native/runtime_config.h"
#include "pipeline_contract/target_acquisition.h"
#include "vision_native/tensorrt_engine.h"

#include <Windows.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <exception>
#include <filesystem>
#include <iostream>
#include <iomanip>
#include "controller_native/bodylock_feedback_geometry.h"
#include "controller_native/bodylock_follow_controller.h"
#include "controller_native/ads_acquisition_controller.h"
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

std::atomic<runtime_app::RuntimeLoop*> active_runtime_loop{nullptr};

struct CliOptions {
    std::filesystem::path config_path = "config.toml";
    std::optional<std::string> auto_fire_output;
    unsigned int max_ticks = 0;
    bool perf_log = false;
    bool run_once = false;
    bool dump_effective_config = false;
    bool probe_input = false;
    bool list_input_devices = false;
    bool describe_ads_geometry = false;
    std::optional<std::string> inspect_engine;
    std::optional<std::string> profile;
    std::string game;
    std::optional<int> capture_fps;
};

CliOptions parse_args(int argc, char** argv) {
    CliOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--config" && index + 1 < argc) {
            options.config_path = argv[++index];
        } else if (arg == "--auto-fire-output" && index + 1 < argc) {
            options.auto_fire_output = argv[++index];
        } else if (arg == "--perf-log") {
            options.perf_log = true;
        } else if (arg == "--once") {
            options.run_once = true;
        } else if (arg == "--dump-effective-config") {
            options.dump_effective_config = true;
        } else if (arg == "--probe-input") {
            options.probe_input = true;
        } else if (arg == "--list-input-devices") {
            options.list_input_devices = true;
        } else if (arg == "--describe-ads-geometry") {
            options.describe_ads_geometry = true;
        } else if (arg == "--inspect-engine" && index + 1 < argc) {
            options.inspect_engine = argv[++index];
        } else if (arg == "--profile" && index + 1 < argc) {
            options.profile = argv[++index];
        } else if (arg == "--game" && index + 1 < argc) {
            options.game = argv[++index];
        } else if (arg == "--capture-fps" && index + 1 < argc) {
            options.capture_fps = std::stoi(argv[++index]);
        } else if (arg == "--max-ticks" && index + 1 < argc) {
            const unsigned long parsed = std::stoul(argv[++index]);
            if (parsed == 0ul) {
                throw std::runtime_error("--max-ticks must be greater than zero");
            }
            options.max_ticks = static_cast<unsigned int>(parsed);
        } else {
            throw std::runtime_error(
                "unknown or retired runtime option: " + arg);
        }
    }
    return options;
}

unsigned int max_ticks_from_options(const CliOptions& options) {
    if (options.run_once) {
        return 1u;
    }
    return options.max_ticks;
}

void apply_cli_overrides(
    const CliOptions& options,
    controller_native::RuntimeConfig& config) {
    if (!options.auto_fire_output.has_value()) {
        // Continue with independent runtime overrides.
    } else {
        if (*options.auto_fire_output != "RB" && *options.auto_fire_output != "RT") {
            throw std::runtime_error("--auto-fire-output must be RB or RT");
        }
        config.gamepad.auto_fire_output = *options.auto_fire_output;
        config.gamepad.auto_fire.fire_output = *options.auto_fire_output;
        config.effective_sources["gamepad.auto_fire.fire_output"] = "cli";
    }
    if (options.capture_fps.has_value()) {
        if (*options.capture_fps < 1 || *options.capture_fps > 1000) {
            throw std::runtime_error("--capture-fps accepted range is 1..1000");
        }
        config.vision.capture_fps = *options.capture_fps;
        config.effective_sources["runtime.vision.capture_fps"] = "cli";
    }
}

void populate_build_metadata(controller_native::RuntimeConfig& config) {
#if defined(COD_BUILD_COMMIT)
    config.build_commit = COD_BUILD_COMMIT;
#endif
#if defined(COD_CONTROL_ARCHITECTURE_VERSION)
    config.control_architecture_version = COD_CONTROL_ARCHITECTURE_VERSION;
#endif
#if defined(COD_CONTROL_EVENT_SCHEMA_VERSION)
    config.control_event_schema_version = COD_CONTROL_EVENT_SCHEMA_VERSION;
#endif
}

void dump_effective_config(const controller_native::RuntimeConfig& config) {
    auto line = [&config](const char* key, const auto& value) {
        std::cout << key << '=' << value << " source=" << config.effective_source(key) << '\n';
    };
    line("runtime.profile", config.profile);
    line("runtime.game", config.game);
    line("gamepad.aim_response_curve.algorithm", controller_native::aim_response_curve_algorithm_name(config.gamepad.aim_response_curve.algorithm));
    line("gamepad.auto_fire.manual_fire_input", config.gamepad.auto_fire.manual_fire_input);
    line("runtime.provenance.build_commit", config.build_commit);
    line("runtime.provenance.control_architecture_version", config.control_architecture_version);
    line("runtime.provenance.control_event_schema_version", config.control_event_schema_version);
    line("runtime.vision.capture_width", config.vision.capture_width);
    line("runtime.vision.capture_height", config.vision.capture_height);
    line("runtime.vision.tensor_width", config.vision.tensor_width);
    line("runtime.vision.tensor_height", config.vision.tensor_height);
    line("runtime.vision.require_isotropic_resize", config.vision.require_isotropic_resize);
    line("runtime.vision.friendly_filter_enabled", config.vision.friendly_filter_enabled);
    line("runtime.vision.target_height_ratio", config.vision.target_height_ratio);
    line("runtime.vision.target_wide_low_height_ratio", config.vision.target_wide_low_height_ratio);
    line("runtime.vision.dynamic_viewport_enabled", config.vision.dynamic_viewport_enabled);
    line("runtime.vision.capture_fps", config.vision.capture_fps);
    line("runtime.vision.idle_capture_fps", config.vision.idle_capture_fps);
    line("runtime.vision.keepwarm_when_idle", config.vision.keepwarm_when_idle);
    line("runtime.vision.aim_release_hold_ms", config.vision.aim_release_hold_ms);
    line("runtime.vision.model_path", config.vision.model_path);
    line("runtime.vision.gpu_service_enabled", config.vision.gpu_service_enabled);
    line("runtime.vision.color_readback_mode", config.vision.color_readback_mode);
    line("runtime.telemetry.enabled", config.telemetry.enabled);
    line("runtime.telemetry.manual_controller_hz", config.telemetry.manual_controller_hz);
    line("runtime.telemetry.queue_capacity", config.telemetry.queue_capacity);
    line("runtime.telemetry.rotate_size_mb", config.telemetry.rotate_size_mb);
    line("runtime.telemetry.max_files", config.telemetry.max_files);
    line("runtime.performance.enabled", config.performance.enabled);
    line("runtime.performance.interval_ms", config.performance.interval_ms);
    line("runtime.performance.directory", config.performance.directory);
    line("runtime.performance.stdout_enabled", config.performance.stdout_enabled);
    line("runtime.scheduler.controller_tick_hz", config.scheduler.controller_tick_hz);
    line("runtime.scheduler.mode", config.scheduler.mode);
    line("runtime.scheduler.spin_tail_us", config.scheduler.spin_tail_us);
    line("runtime.input.auto_detect", config.gamepad.xinput_auto_detect);
    line("runtime.input.controller_index", config.gamepad.xinput_user_index);
    line("runtime.input.device_id", config.gamepad.input_device_id);
    line("runtime.input.device_name", config.gamepad.input_device_name);
    line("runtime.input.rb_counts_as_aiming", config.gamepad.rb_counts_as_aiming);
    line("runtime.output.enabled", config.output.enabled);
    line("runtime.output.validation_mode", config.output.validation_mode);
    line("gamepad.enemy_mark.enabled", config.gamepad.enemy_mark.enabled);
    line(
        "gamepad.enemy_mark.l3_cooldown_ms",
        config.gamepad.enemy_mark.l3_cooldown_ms);
    line(
        "gamepad.enemy_mark.lt_cooldown_ms",
        config.gamepad.enemy_mark.lt_cooldown_ms);
    const auto& aim = config.gamepad.ai_aim;
    line("gamepad.ai_aim.hipfire_multiplier", aim.hipfire_multiplier);
    line("gamepad.ai_aim.aim_response_learning_enabled", aim.aim_response_learning_enabled);
    line("gamepad.tracker.aim_height_ratio", config.gamepad.tracker.aim_height_ratio);
    line(
        "gamepad.tracker.max_observation_age_ms",
        config.gamepad.tracker.max_observation_age_ms);
    line("gamepad.ads.strength_scale", config.ads.strength_scale);
    line("gamepad.ads.vertical_strength_scale", config.ads.vertical_strength_scale);
    line("gamepad.ads.activation_radius_px", aim.ads_activation_radius_px);
    line("gamepad.ads.snap_duration_ms", aim.ads_snap_window_ms);
    line("gamepad.ads.target_wait_ms", aim.ads_target_wait_ms);
    line("gamepad.ads.extension_budget_ms", aim.ads_extension_budget_ms);
    line("gamepad.bodylock.strength", aim.body_lock_max_ai_force);
    line("gamepad.bodylock.vertical_strength", aim.body_lock_max_ai_force_y);
    line("gamepad.ai_aim.aim_response_initial_scale", aim.aim_response_initial_scale);
    line("gamepad.ai_aim.body_free_initial_scale", aim.body_free_initial_scale);
    line("gamepad.ai_aim.body_slow_initial_scale", aim.body_slow_initial_scale);
    line("gamepad.ai_aim.ads_free_initial_scale", aim.ads_free_initial_scale);
    line("gamepad.ai_aim.ads_slow_initial_scale", aim.ads_slow_initial_scale);
    line(
        "gamepad.ai_aim.cue_hold_full_force_min_target_height_ratio",
        aim.cue_hold_full_force_min_target_height_ratio);
    line("gamepad.ai_aim.desired_point_traversal_ms", aim.desired_point_traversal_ms);
    line(
        "gamepad.ai_aim.desired_point_boundary_exit_ms",
        aim.desired_point_boundary_exit_ms);
    const auto& fire = config.gamepad.auto_fire;
    line("gamepad.auto_fire.fire_output", fire.fire_output);
    line("gamepad.auto_fire.aim_only", fire.aim_only);
    line("gamepad.auto_fire.max_source_age_ms", fire.max_source_age_ms);
    line("gamepad.auto_fire.require_aim_ready", fire.require_aim_ready);
#define NATIVE_EDITABLE_FLOAT(SECTION, KEY, MEMBER, NAME, DEFAULT, MIN, MAX, HOT, RETAIN) line(SECTION "." KEY, config.gamepad.MEMBER);
#include "editable_float_parameters.inc"
#undef NATIVE_EDITABLE_FLOAT
    line("gamepad.auto_fire.manual_takeover_release_seconds", fire.manual_takeover_release_seconds);
    line("gamepad.auto_fire.manual_takeover_resume_delay_seconds", fire.manual_takeover_resume_delay_seconds);
    const auto& transfer = config.gamepad.output_transfer;
    line("gamepad.output_transfer.enabled", transfer.enabled);
    line("gamepad.output_transfer.axial", transfer.axial);
    line("gamepad.output_transfer.deadzone", transfer.deadzone);
    line("gamepad.output_transfer.game_exponent", transfer.game_exponent);
    const auto& recoil = config.gamepad.recoil;
    line("gamepad.recoil.enabled", recoil.enabled);
    line("gamepad.recoil.feedback_amount", recoil.feedback_amount);
    line("gamepad.recoil.hipfire_multiplier", recoil.hipfire_multiplier);
    line("gamepad.recoil.feedback_min_amount", recoil.feedback_min_amount);
    line("gamepad.recoil.feedback_max_amount", recoil.feedback_max_amount);
    for (const std::string& diagnostic : config.diagnostics) {
        std::cerr << "[NativeRuntime][Config] " << diagnostic << '\n';
    }
}

void print_startup_summary(
    const CliOptions& options,
    const controller_native::RuntimeConfig& config) {
    std::cout
        << "[NativeRuntime] config=" << options.config_path.string()
        << " game=" << config.game
        << " vision=" << config.vision.capture_width << "x" << config.vision.capture_height
        << "->" << config.vision.tensor_width << "x" << config.vision.tensor_height
        << "@" << config.vision.capture_fps
        << " model=" << config.vision.model_path
        << " perf_log=" << (options.perf_log || config.vision.perf_log ? "true" : "false")
        << " telemetry=" << (config.telemetry.enabled ? "on" : "off")
        << " telemetry_dir=" << config.telemetry.directory
        << " perf_summary=" << (config.performance.enabled ? "on" : "off")
        << " perf_summary_interval_ms=" << config.performance.interval_ms
        << " gpu_service=" << (config.vision.gpu_service_enabled ? "on" : "off")
        << " vision_capture_fps=" << config.vision.capture_fps
        << " vision_idle_fps=" << config.vision.idle_capture_fps
        << " vision_aim_release_hold_ms=" << config.vision.aim_release_hold_ms
        << " tracker_max_observation_age_ms="
        << config.gamepad.tracker.max_observation_age_ms
        << " aim_controller=production"
        << " recoil=" << (config.gamepad.recoil.enabled ? "on" : "off")
        << " recoil_hipfire_multiplier=" << config.gamepad.recoil.hipfire_multiplier
        << " auto_fire=" << config.gamepad.auto_fire.fire_output
        << " enemy_mark=" << (config.gamepad.enemy_mark.enabled ? "on" : "off")
        << " fusion=" << (config.vision.fusion_enabled ? "on" : "off")
        << " fusion_session=" << config.vision.fusion_session
        << '\n';
}

BOOL WINAPI handle_console_signal(DWORD control_type) {
    switch (control_type) {
    case CTRL_CLOSE_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        if (runtime_app::RuntimeLoop* loop = active_runtime_loop.load()) {
            loop->request_stop();
            return TRUE;
        }
        return FALSE;
    default:
        return FALSE;
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        SetConsoleCtrlHandler(handle_console_signal, TRUE);
        const CliOptions options = parse_args(argc, argv);
        if (options.inspect_engine) {
            // Load through the production engine owner, before input/output,
            // capture, shared channels or runtime configuration are created.
            vision_native::TensorRTEngine engine(*options.inspect_engine);
            std::cout << "{\"input_width\":" << engine.input_width()
                      << ",\"input_height\":" << engine.input_height() << "}\n";
            return 0;
        }
        if (options.describe_ads_geometry) {
            // Pure geometry: no configuration, engine, input scan or output.
            using namespace pipeline_contract;
            std::cout << std::setprecision(9)
                << "{\"schema_version\":3,\"size_gain\":" << kPickupSizeGain
                << ",\"wide_low_aspect_threshold\":" << kWideLowAspectThreshold
                << ",\"region_shrink_x\":" << kAimRegionShrinkX
                << ",\"region_half_height\":" << kAimRegionHalfHeightRatio
                << ",\"wide_region_half_height\":" << kWideLowAimRegionHalfHeightRatio
                << ",\"pickup_min_height\":" << kMinPickupHeightRatio
                << ",\"tracking_min_height\":" << kMinTrackingHeightRatio
                << ",\"pickup_min_area\":" << kMinPickupAreaRatio
                << ",\"tracking_min_area\":" << kMinTrackingAreaRatio
                << ",\"min_aspect\":" << kMinAspectRatio
                << ",\"wide_min_aspect\":" << kMinWideLowAspectRatio
                << ",\"max_aspect\":" << kMaxAspectRatio
                << ",\"default_completion_radius\":" << controller_native::GamepadAiAimConfig{}.arrival_radius_px
                << ",\"feedback_minimum_px\":1,\"feedback_multiplier\":1"
                << ",\"feedback_calibration\":" << controller_native::GamepadAiAimConfig{}.adapter_response_px_per_second
                << ",\"horizon_min\":" << controller_native::kArrivalHorizonMinimumSeconds
                << ",\"horizon_max\":" << controller_native::kArrivalHorizonMaximumSeconds
                << ",\"release_hysteresis\":" << controller_native::kPhysicalAdsReleaseHysteresis
                << ",\"ready_hysteresis\":" << controller_native::kPhysicalAdsReadyHysteresis
                << ",\"release_samples\":" << controller_native::kPhysicalAdsIdleDebounceSamples
                << ",\"ads\":{\"nominal_horizon\":" << controller_native::GamepadAiAimConfig{}.ads_snap_window_ms / 1000.f
                << ",\"base_force_x\":" << controller_native::GamepadAiAimConfig{}.ads_snap_max_ai_force
                << ",\"base_force_y\":" << controller_native::GamepadAiAimConfig{}.ads_snap_max_ai_force_y
                << ",\"headroom\":" << controller_native::kAdsVectorForceHeadroom
                << ",\"close_horizon\":" << controller_native::AdsAcquisitionControllerConfig{}.close_arrival_horizon_seconds
                << ",\"close_begin\":" << controller_native::AdsAcquisitionControllerConfig{}.close_target_size_begin
                << ",\"close_full\":" << controller_native::AdsAcquisitionControllerConfig{}.close_target_size_full
                << ",\"above_scale\":" << controller_native::AdsAcquisitionControllerConfig{}.target_above_horizon_scale
                << ",\"speed\":" << controller_native::GamepadAiAimConfig{}.adapter_ads_speed
                << ",\"budget\":" << controller_native::GamepadAiAimConfig{}.adapter_force_budget_scale
                << "},\"references\":[";
            bool first = true;
            for (float normalized : {.03f,.08f,.1f,.24f,.25f,.5f,.9f}) {
                for (float ratio : {.35f,.8f,1.538f,1.539f,2.4f}) {
                    const float height = 416 * normalized, width = height * ratio;
                    const auto geometry = body_aim_geometry(0,0,width,height,.4f,.65f);
                    if (!first) std::cout << ',';
                    first = false;
                    std::cout << "{\"width\":" << width << ",\"height\":" << height
                        << ",\"radius\":" << target_scaled_pickup_radius(150, height / 416)
                        << ",\"wide_low\":" << (geometry.wide_low ? "true" : "false")
                        << ",\"pickup_admitted\":" << (body_geometry_admitted(width,height,480,416,false) ? "true" : "false")
                        << ",\"tracking_admitted\":" << (body_geometry_admitted(width,height,480,416,true) ? "true" : "false")
                        << ",\"aim\":[" << geometry.aim_x << ',' << geometry.aim_y
                        << "],\"region\":[" << geometry.left << ',' << geometry.top << ','
                        << geometry.right << ',' << geometry.bottom << "]}";
                }
            }
            std::cout << "],\"feedback_references\":[";
            first = true;
            for (float value : {1.f,2.f,8.f,18.f,27.f,36.f,100.f,3000.f}) {
                for (float force : {0.f,.3f,.8f,1.f,3.f}) {
                    controller_native::BodylockFollowControllerConfig config;
                    config.feedback_range_x_px = config.feedback_range_y_px = value;
                    config.max_force_x = config.max_force_y = force;
                    config.fallback_response_px_per_stick_second = controller_native::GamepadAiAimConfig{}.adapter_response_px_per_second;
                    pipeline_contract::TargetPlan plan;
                    plan.mode = pipeline_contract::ControlMode::BodyLockFollow;
                    plan.lifecycle = pipeline_contract::TargetLifecycle::Observed;
                    plan.reliability = plan.aim_authority = 1;
                    plan.error_px = {20,0};
                    plan.response_scale = config.fallback_response_px_per_stick_second;
                    const auto output = controller_native::BodylockFollowController(config).compute_detailed(plan,{},.001f);
                    if (!first) std::cout << ',';
                    first = false;
                    std::cout << "{\"value\":" << value << ",\"force\":" << force
                        << ",\"distance\":" << config.feedback_range_x_px
                        << ",\"position_demand\":" << output.position_stick.x
                        << ",\"linear_output\":" << output.stick.x << '}';
                }
            }
            std::cout << "],\"follow_examples\":[";
            first = true;
            for (float distance : {1.f,18.f,27.f,100.f,3000.f}) {
                for (auto forces : {pipeline_contract::Vec2f{0,.42f},{.3f,.42f},{.8f,.2f},{3,3}}) {
                    for (auto error : {pipeline_contract::Vec2f{0,0},{.5f,-.5f},{10,5},{-20,30},{320,-256}}) {
                        controller_native::BodylockFollowControllerConfig config;
                        config.feedback_range_x_px=config.feedback_range_y_px=distance;
                        config.max_force_x=forces.x;config.max_force_y=forces.y;
                        config.fallback_response_px_per_stick_second=controller_native::GamepadAiAimConfig{}.adapter_response_px_per_second;
                        pipeline_contract::TargetPlan plan;
                        plan.mode=pipeline_contract::ControlMode::BodyLockFollow;
                        plan.lifecycle=pipeline_contract::TargetLifecycle::Observed;
                        plan.aim_authority=plan.reliability=1;
                        plan.response_scale=config.fallback_response_px_per_stick_second;
                        plan.error_px=error;
                        const auto output=controller_native::BodylockFollowController(config).compute_detailed(plan,{},.001f);
                        if (!first) std::cout << ',';
                        first=false;
                        std::cout << "{\"distance\":" << distance << ",\"force\":[" << forces.x << ',' << forces.y
                            << "],\"error\":[" << error.x << ',' << error.y << "],\"stick\":[" << output.stick.x
                            << ',' << output.stick.y << "]}";
                    }
                }
            }
            std::cout << "],\"ads_examples\":[";
            first=true;
            for (float horizon : {.01f,.135f,.5f}) {
                for (float size : {.03f,.18f,.25f,.4f,.9f}) {
                    for (auto forces : {Vec2f{0,1},{.05f,.2f},{1,1},{3,3}}) {
                        for (auto error : {Vec2f{0,0},{.5f,-.5f},{10,5},{-20,30},{320,-256}}) {
                            controller_native::AdsAcquisitionControllerConfig config;
                            config.arrival_horizon_seconds=horizon;
                            config.max_force_x=forces.x;config.max_force_y=forces.y;
                            TargetPlan plan;
                            plan.mode=ControlMode::AdsAcquire;
                            plan.lifecycle=TargetLifecycle::Observed;
                            plan.aim_authority=plan.reliability=1;
                            plan.response_scale=config.fallback_response_px_per_stick_second;
                            plan.normalized_size=size;plan.error_px=error;
                            const auto output=controller_native::AdsAcquisitionController(config).compute(plan,{},.001f);
                            if (!first) std::cout << ',';
                            first=false;
                            std::cout << "{\"horizon\":" << horizon << ",\"size\":" << size
                                << ",\"force\":[" << forces.x << ',' << forces.y
                                << "],\"error\":[" << error.x << ',' << error.y
                                << "],\"stick\":[" << output.x << ',' << output.y << "]}";
                        }
                    }
                }
            }
            std::cout << "],\"parameter_semantics\":\"range-response-v4\""
                << ",\"velocity_feedback_minimum_seconds\":" << controller_native::kVelocityFeedbackMinimumSeconds
                << ",\"velocity_feedback_delay_margin\":" << controller_native::kVelocityFeedbackDelayMargin
                << ",\"follow_effect_delay_seconds\":" << controller_native::BodylockFollowControllerConfig{}.response_effect_delay_seconds
                << ",\"independent_examples\":[";
            first=true;
            for (float minimum_stick : {0.f,.1f,.15f,.2f}) for (float radius : {75.f,150.f,300.f}) for (float time_ms : {60.f,135.f,350.f}) {
                for (auto caps : {Vec2f{0,0},{0,.8f},{.8f,0},{.1f,.2f},{.3f,.42f},{1,1}}) {
                    for (auto error : {Vec2f{0,0},{1,-2},{80,40},{-320,256}}) {
                        TargetPlan plan;
                        plan.lifecycle=TargetLifecycle::Observed;
                        plan.aim_authority=plan.reliability=1;
                        plan.response_scale=500;
                        plan.normalized_size=.25f;plan.error_px=error;
                        plan.position_response_radius_px=radius;
                        plan.position_arrival_radius_px=2.f;
                        controller_native::BodylockFollowControllerConfig follow;
                        follow.max_force_x=caps.x;follow.max_force_y=caps.y;
                        follow.range_position_response=true;
                        follow.minimum_position_stick=minimum_stick;
                        follow.response_time_x_seconds=time_ms/1000.f;
                        follow.response_time_y_seconds=time_ms*.8f/1000.f;
                        plan.mode=ControlMode::BodyLockFollow;
                        const auto b=controller_native::BodylockFollowController(follow).compute(plan,{},.001f);
                        controller_native::AdsAcquisitionControllerConfig acquire;
                        acquire.max_force_x=caps.x;acquire.max_force_y=caps.y;
                        acquire.range_position_response=true;
                        acquire.minimum_position_stick=minimum_stick;
                        acquire.force_headroom=1.f;acquire.arrival_horizon_seconds=time_ms/1000.f;
                        plan.mode=ControlMode::AdsAcquire;
                        const auto a=controller_native::AdsAcquisitionController(acquire).compute(plan,{},.001f);
                        if(!first)std::cout << ',';
                        first=false;
                        std::cout << "{\"minimum_stick\":" << minimum_stick << ",\"radius\":" << radius << ",\"time\":" << time_ms << ",\"limits\":[" << caps.x << ',' << caps.y
                            << "],\"error\":[" << error.x << ',' << error.y
                            << "],\"follow\":[" << b.x << ',' << b.y << "],\"ads\":[" << a.x << ',' << a.y << "]}";
                    }
                }
            }
            std::cout << "]}\n";
            return 0;
        }
        if (options.list_input_devices) {
            // Enumeration has no config, TensorRT, virtual output or runtime
            // side effects. Quoting escapes bytes required by JSON while
            // preserving SDL's UTF-8 device names on the pipe.
            const auto quoted = [](const std::string& value) {
                std::ostringstream out;
                out << '"';
                constexpr char hex[] = "0123456789abcdef";
                for (unsigned char c : value) {
                    if (c == '"' || c == '\\') out << '\\' << char(c);
                    else if (c < 32) out << "\\u00" << hex[c >> 4] << hex[c & 15];
                    else out << char(c);
                }
                out << '"';
                return out.str();
            };
            std::cout << '[';
            bool first = true;
            const auto devices = controller_native::scan_sdl_joystick_devices();
            for (const auto& device : devices) {
                if (!device.opened || device.axes < 4 || device.id.empty()) continue;
                // Never expose an identity shared by indistinguishable pads.
                if (std::count_if(devices.begin(), devices.end(), [&](const auto& d) {
                    return d.opened && d.id == device.id;
                }) != 1) continue;
                if (!first) std::cout << ',';
                first = false;
                std::cout << "{\"id\":" << quoted(device.id) << ",\"name\":" << quoted(device.name) << '}';
            }
            std::cout << "]\n";
            return 0;
        }
        controller_native::RuntimeConfig config =
            controller_native::load_runtime_config(
                options.config_path,
                options.profile.value_or(std::string{}), options.game);
        apply_cli_overrides(options, config);
        // Read the physical backend without creating virtual output or vision.
        if (options.probe_input) return runtime_app::probe_physical_input(config.gamepad);
        populate_build_metadata(config);
        if (options.dump_effective_config) {
            dump_effective_config(config);
            std::cout << "# effective-config complete\n" << std::flush;
            if (!std::cout) throw std::runtime_error("effective configuration output failed");
            return 0;
        }
        const bool perf_log = options.perf_log || config.vision.perf_log;
        print_startup_summary(options, config);

        runtime_app::HighResolutionTimerPeriod timer_period(1u);
        std::cout << "[NativeRuntime] timer_resolution_ms=1"
                  << " active=" << (timer_period.active() ? 1 : 0)
                  << '\n';

        if (config.scheduler.efficiency_core_affinity) {
            const bool pinned = runtime_app::pin_process_to_efficiency_cores(
                config.scheduler.efficiency_core_count);
            std::cout << "[NativeRuntime] efficiency_core_affinity="
                      << (pinned ? "pinned" : "skipped")
                      << " count="
                      << config.scheduler.efficiency_core_count
                      << '\n';
        }

        // Create the signal before initialization so Stop also works while
        // loading an engine. Join its listener before destroying the loop.
        runtime_app::RuntimeStopSignal stop_signal;
        runtime_app::RuntimeLoop loop(config, perf_log, max_ticks_from_options(options));
        runtime_app::RuntimeControlBridge control_bridge(config, [options] {
            auto next = controller_native::load_runtime_config(options.config_path,
                options.profile.value_or(std::string{}), options.game);
            apply_cli_overrides(options, next);
            return next;
        });
        loop.attach_control_bridge(&control_bridge);
        auto stop_listener = stop_signal.listen([&loop] { loop.request_stop(); });
        std::cout << "[NativeRuntime] initialized; entering controller loop\n" << std::flush;
        active_runtime_loop.store(&loop);
        const int exit_code = loop.run();
        active_runtime_loop.store(nullptr);
        return exit_code;
    } catch (const std::exception& exc) {
        active_runtime_loop.store(nullptr);
        std::cerr << "[NativeRuntime][Error] " << exc.what() << '\n';
        return 1;
    }
}
