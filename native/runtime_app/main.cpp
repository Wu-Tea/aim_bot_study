#include "runtime_loop.h"
#include "runtime_timing.h"

#include "controller_native/runtime_config.h"

#include <Windows.h>

#include <atomic>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
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
    std::optional<std::string> profile;
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
        } else if (arg == "--profile" && index + 1 < argc) {
            options.profile = argv[++index];
        } else if (arg == "--capture-fps" && index + 1 < argc) {
            options.capture_fps = std::stoi(argv[++index]);
        } else if (arg == "--max-ticks" && index + 1 < argc) {
            const unsigned long parsed = std::stoul(argv[++index]);
            if (parsed == 0ul) {
                throw std::runtime_error("--max-ticks must be greater than zero");
            }
            options.max_ticks = static_cast<unsigned int>(parsed);
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
    }
    if (options.capture_fps.has_value()) {
        if (*options.capture_fps < 1 || *options.capture_fps > 1000) {
            throw std::runtime_error("--capture-fps accepted range is 1..1000");
        }
        config.vision.capture_fps = *options.capture_fps;
        config.vision.gpu_service_active_fps = *options.capture_fps;
        config.effective_sources["runtime.vision.capture_fps"] = "cli";
        config.effective_sources["runtime.vision.gpu_service_active_fps"] = "cli";
    }
}

void dump_effective_config(const controller_native::RuntimeConfig& config) {
    auto line = [&config](const char* key, const auto& value) {
        std::cout << key << '=' << value << " source=" << config.effective_source(key) << '\n';
    };
    line("runtime.profile", config.profile);
    line("runtime.vision.capture_width", config.vision.capture_width);
    line("runtime.vision.capture_height", config.vision.capture_height);
    line("runtime.vision.capture_fps", config.vision.capture_fps);
    line("runtime.vision.idle_capture_fps", config.vision.idle_capture_fps);
    line("runtime.vision.keepwarm_when_idle", config.vision.keepwarm_when_idle);
    line("runtime.vision.model_path", config.vision.model_path);
    line("runtime.vision.gpu_service_enabled", config.vision.gpu_service_enabled);
    line("runtime.vision.gpu_service_active_fps", config.vision.gpu_service_active_fps);
    for (const std::string& diagnostic : config.diagnostics) {
        std::cerr << "[NativeRuntime][Config] " << diagnostic << '\n';
    }
}

void print_startup_summary(
    const CliOptions& options,
    const controller_native::RuntimeConfig& config) {
    std::cout
        << "[NativeRuntime] config=" << options.config_path.string()
        << " vision=" << config.vision.capture_width << "x" << config.vision.capture_height
        << "@" << config.vision.capture_fps
        << " model=" << config.vision.model_path
        << " perf_log=" << (options.perf_log || config.vision.perf_log ? "true" : "false")
        << " aim_perf_file_log=" << (config.vision.aim_perf_file_log ? "true" : "false")
        << " aim_perf_log_dir=" << config.vision.aim_perf_log_dir
        << " aim_perf_log_interval_ticks=" << config.vision.aim_perf_log_interval_ticks
        << " gpu_service=" << (config.vision.gpu_service_enabled ? "on" : "off")
        << " gpu_service_active_fps=" << config.vision.gpu_service_active_fps
        << " gpu_service_idle_fps=" << config.vision.gpu_service_idle_fps
        << " tracker_backend="
        << tracking_native::tracker_backend_kind_name(config.gamepad.tracker_backend)
        << " tracker_motion=component_aware_final"
        << " recoil=" << (config.gamepad.recoil.enabled ? "on" : "off")
        << " recoil_state=" << (
            config.gamepad.recoil.recognizer_state_path.empty()
                ? "none"
                : config.gamepad.recoil.recognizer_state_path)
        << " auto_fire=" << config.gamepad.auto_fire.fire_output
        << " fusion=" << (config.vision.fusion_enabled ? "on" : "off")
        << " fusion_session=" << config.vision.fusion_session
        << '\n';
}

BOOL WINAPI handle_console_signal(DWORD control_type) {
    switch (control_type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
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
        controller_native::RuntimeConfig config =
            controller_native::load_runtime_config(
                options.config_path,
                options.profile.value_or(std::string{}));
        apply_cli_overrides(options, config);
        if (options.dump_effective_config) {
            dump_effective_config(config);
            return 0;
        }
        const bool perf_log = options.perf_log || config.vision.perf_log;
        print_startup_summary(options, config);

        runtime_app::HighResolutionTimerPeriod timer_period(1u);
        std::cout << "[NativeRuntime] timer_resolution_ms=1"
                  << " active=" << (timer_period.active() ? 1 : 0)
                  << '\n';

        runtime_app::RuntimeLoop loop(config, perf_log, max_ticks_from_options(options));
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
