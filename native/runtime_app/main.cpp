#include "runtime_loop.h"

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
        return;
    }
    if (*options.auto_fire_output != "RB" && *options.auto_fire_output != "RT") {
        throw std::runtime_error("--auto-fire-output must be RB or RT");
    }
    config.gamepad.auto_fire_output = *options.auto_fire_output;
    config.gamepad.auto_fire.fire_output = *options.auto_fire_output;
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
            controller_native::load_runtime_config(options.config_path);
        apply_cli_overrides(options, config);
        const bool perf_log = options.perf_log || config.vision.perf_log;
        print_startup_summary(options, config);

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
