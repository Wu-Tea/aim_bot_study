#include "runtime_timing.h"
#include "vision_controller_adapter.h"
#include "vision_service.h"

#include "controller_native/runtime_config.h"
#include "mouse_native/mouse_controller_session.h"
#include "mouse_native/mouse_diagnostics.h"
#include "runtime_provenance.h"
#include "mouse_native/mouse_runtime_supervisor.h"
#include "vision_native/vision_engine.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <memory>
#include <stdexcept>
#include <string>
#include <sstream>
#include <vector>

namespace {

std::atomic_bool* g_stop_requested = nullptr;

struct MouseRuntimeOptions {
    std::filesystem::path config_path = "config.toml";
    std::string profile{};
    unsigned int max_ticks = 0;
    bool check_transport = false;
    bool check_config = false;
    bool speed_override = false, breakaway_override = false, deadzone_override = false;
    bool dpi_override = false, sensitivity_override = false, fov_override = false, ads_multiplier_override = false;
    mouse_native::MouseRelayTestMode relay_test = mouse_native::MouseRelayTestMode::None;
    unsigned int duration_seconds = 10;
    int mouse_device = 0;
    std::wstring mouse_hardware;
    std::wstring supervisor_mapping;
    mouse_native::MouseCodDefaultConfig mouse_defaults{};
    mouse_native::MouseControllerTuning mouse_tuning{2.0f, 4.0f, 0.5f};
    mouse_native::MouseControllerTransport transport =
        mouse_native::MouseControllerTransport::VirtualHid;
};

mouse_native::MouseControllerTransport parse_transport(const std::string& value) {
    if (value == "virtual-hid" || value == "fakerinput")
        return mouse_native::MouseControllerTransport::VirtualHid;
    if (value == "interception") return mouse_native::MouseControllerTransport::Interception;
    if (value == "kmdf-vhf" || value == "driver") {
        return mouse_native::MouseControllerTransport::KmdfVhf;
    }
    if (value == "win32" || value == "win32-debug" || value == "debug") {
        return mouse_native::MouseControllerTransport::Win32Debug;
    }
    throw std::runtime_error(
        "--transport must be virtual-hid, interception, win32-debug or kmdf-vhf");
}

MouseRuntimeOptions parse_args(int argc, char** argv) {
    MouseRuntimeOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--check-transport") {
            options.check_transport = true;
        } else if (argument == "--check-config") {
            options.check_config = true;
        } else if ((argument == "--mouse-hardware" || argument == "--mouse-supervisor") && index + 1 < argc) {
            const std::string value = argv[++index];
            if (value.empty() || std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 32 || c >= 127; }))
                throw std::runtime_error("expected nonempty ASCII identity for " + argument);
            auto& destination = argument == "--mouse-hardware" ? options.mouse_hardware : options.supervisor_mapping;
            destination.assign(value.begin(), value.end());
        } else if (argument == "--mouse-device" && index + 1 < argc) {
            const std::string value = argv[++index];
            std::size_t used = 0;
            options.mouse_device = std::stoi(value, &used);
            if (used != value.size() || options.mouse_device < 11 || options.mouse_device > 20)
                throw std::runtime_error("--mouse-device must be 11..20; use --check-transport to list devices");
        } else if ((argument == "--mouse-speed" || argument == "--mouse-breakaway" ||
                    argument == "--mouse-bodylock-deadzone") && index + 1 < argc) {
            const std::string value = argv[++index];
            std::size_t used = 0;
            const float number = std::stof(value, &used);
            if (used != value.size() || !std::isfinite(number))
                throw std::runtime_error("invalid value for " + argument);
            if (argument == "--mouse-speed") {
                options.mouse_tuning.speed = number; options.speed_override = true;
            } else if (argument == "--mouse-breakaway") {
                options.mouse_tuning.breakaway = number; options.breakaway_override = true;
            } else {
                options.mouse_tuning.bodylock_deadzone = number; options.deadzone_override = true;
            }
        } else if ((argument == "--mouse-dpi" || argument == "--mouse-sensitivity" ||
                    argument == "--mouse-fov" || argument == "--mouse-ads-multiplier") &&
                   index + 1 < argc) {
            const std::string value = argv[++index];
            std::size_t used = 0;
            const float number = std::stof(value, &used);
            if (used != value.size() || !std::isfinite(number) || number <= 0 ||
                (argument == "--mouse-fov" && number >= 180)) {
                throw std::runtime_error("invalid value for " + argument);
            }
            if (argument == "--mouse-dpi") {
                options.mouse_defaults.dpi = number; options.dpi_override = true;
            } else if (argument == "--mouse-sensitivity") {
                options.mouse_defaults.sensitivity = number; options.sensitivity_override = true;
            } else if (argument == "--mouse-fov") {
                options.mouse_defaults.horizontal_fov_16_9 = number; options.fov_override = true;
            } else {
                options.mouse_defaults.ads_multiplier = number; options.ads_multiplier_override = true;
            }
        } else if (argument == "--relay-test" && index + 1 < argc) {
            const std::string mode = argv[++index];
            using Mode = mouse_native::MouseRelayTestMode;
            if (mode == "passthrough") options.relay_test = Mode::Passthrough;
            else if (mode == "block") options.relay_test = Mode::Block;
            else if (mode == "invert") options.relay_test = Mode::Invert;
            else throw std::runtime_error("--relay-test must be passthrough, block or invert");
        } else if (argument == "--duration-seconds" && index + 1 < argc) {
            const std::string value = argv[++index];
            std::size_t used = 0;
            const unsigned long seconds = std::stoul(value, &used);
            if (used != value.size() || seconds < 1 || seconds > 60) {
                throw std::runtime_error("--duration-seconds must be 1..60");
            }
            options.duration_seconds = static_cast<unsigned int>(seconds);
        } else if (argument == "--config" && index + 1 < argc) {
            options.config_path = argv[++index];
        } else if (argument == "--profile" && index + 1 < argc) {
            options.profile = argv[++index];
        } else if (argument == "--max-ticks" && index + 1 < argc) {
            options.max_ticks = static_cast<unsigned int>(std::stoul(argv[++index]));
            if (options.max_ticks == 0) {
                throw std::runtime_error("--max-ticks must be greater than zero");
            }
        } else if (argument == "--transport" && index + 1 < argc) {
            options.transport = parse_transport(argv[++index]);
        } else {
            throw std::runtime_error("unknown or incomplete argument: " + argument);
        }
    }
    if (options.check_transport && options.relay_test != mouse_native::MouseRelayTestMode::None) {
        throw std::runtime_error("choose either --check-transport or --relay-test");
    }
    if (options.transport == mouse_native::MouseControllerTransport::VirtualHid &&
        options.mouse_device == 0 && options.mouse_hardware.empty()) {
        // Same verified physical interface as the standalone relay. Runtime
        // device numbers are resolved anew and never persisted as identities.
        options.mouse_hardware = L"HID\\VID_1532&PID_00B8&REV_0100&MI_00";
    }
    if (!options.supervisor_mapping.empty() &&
        (options.transport != mouse_native::MouseControllerTransport::VirtualHid || options.check_transport))
        throw std::runtime_error("supervisor worker requires an active virtual-hid session");
    return options;
}

BOOL WINAPI handle_console_signal(DWORD control_type) {
    switch (control_type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        if (g_stop_requested != nullptr) {
            g_stop_requested->store(true);
            return TRUE;
        }
        return FALSE;
    default:
        return FALSE;
    }
}

std::uint64_t steady_ns(std::chrono::steady_clock::time_point value) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            value.time_since_epoch()).count());
}

double steady_seconds(std::chrono::steady_clock::time_point value) {
    return static_cast<double>(steady_ns(value)) / 1.0e9;
}

std::uint64_t new_lease_id() {
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    std::uint64_t value = static_cast<std::uint64_t>(counter.QuadPart) ^
        (static_cast<std::uint64_t>(GetCurrentProcessId()) << 32u);
    return value != 0 ? value : 1;
}

const char* calibration_failure_name(
    mouse_native::MouseCalibrationFailure failure) {
    using Failure = mouse_native::MouseCalibrationFailure;
    switch (failure) {
    case Failure::None: return "none";
    case Failure::InvalidObservation: return "no_fresh_person";
    case Failure::TargetChanged: return "target_changed";
    case Failure::PhysicalMouseMoved: return "physical_mouse_moved";
    case Failure::OutputFailed: return "virtual_output_failed";
    case Failure::WrongResponseDirection: return "wrong_response_direction";
    case Failure::ResponseOutOfRange: return "response_out_of_range";
    case Failure::ReturnMissed: return "return_missed";
    case Failure::TimedOut: return "timed_out";
    case Failure::InvalidState: return "calibration_busy";
    case Failure::AimModeChanged: return "RMB_changed_during_calibration";
    }
    return "unknown";
}

bool both_profiles_calibrated(const mouse_native::MouseControllerSession& session) {
    return mouse_native::valid(
               session.runtime().profile(mouse_native::MouseAimMode::Hipfire)) &&
        mouse_native::valid(
               session.runtime().profile(mouse_native::MouseAimMode::Ads));
}

class MouseVisionEnginePoller final : public runtime_app::IVisionServicePoller {
public:
    explicit MouseVisionEnginePoller(
        std::unique_ptr<vision_native::VisionEngine> engine)
        : engine_(std::move(engine)) {}

    void set_aiming(bool aiming) override {
        engine_->set_aiming(aiming);
    }

    void set_user_aim_intent(
        const pipeline_contract::UserAimIntent& intent) override {
        engine_->set_user_aim_intent(intent);
    }

    void set_viewport(const runtime_app::ViewportRequest& request) override {
        engine_->set_viewport(
            static_cast<int>(request.level),
            request.width,
            request.height,
            request.sequence,
            request.source_frame_id);
    }

    vision_native::VisionResult poll_once() override {
        return engine_->poll_once();
    }

private:
    std::unique_ptr<vision_native::VisionEngine> engine_;
};

runtime_app::ViewportRequest full_capture_viewport(
    const controller_native::RuntimeConfig& config) {
    runtime_app::ViewportRequest request{};
    request.level = runtime_app::ViewportLevel::Normal;
    request.width = config.vision.capture_width;
    request.height = config.vision.capture_height;
    return request;
}

struct MouseRuntimeCounters {
    std::int64_t input_x = 0, input_y = 0, output_x = 0, output_y = 0;
    std::uint64_t controlled_ticks = 0, transparent_ticks = 0;
    std::uint64_t skipped_deadlines = 0, ticks = 0;
    std::uint64_t previous_sources = 0, previous_reports = 0;
    std::chrono::steady_clock::time_point first_tick{}, previous_tick{};
    std::chrono::steady_clock::time_point last_print = std::chrono::steady_clock::now();
    std::vector<double> intervals_ms;

    void add(const mouse_native::MouseControllerSessionTickResult& tick,
             std::chrono::steady_clock::time_point now) {
        if (ticks++ == 0) first_tick = now;
        else intervals_ms.push_back(std::chrono::duration<double, std::milli>(now - previous_tick).count());
        previous_tick = now;
        input_x += tick.source_counts.dx;
        input_y += tick.source_counts.dy;
        if (tick.output_delivered) {
            output_x += tick.runtime.output.counts.dx;
            output_y += tick.runtime.output.counts.dy;
        }
        controlled_ticks += tick.runtime.controller.controller_used ? 1 : 0;
        transparent_ticks += tick.runtime.controller.transparent ? 1 : 0;
    }
    void print(const mouse_native::MouseControllerSession& session) {
        const auto stats = session.transport_stats();
        const auto printed = std::chrono::steady_clock::now();
        const double window_seconds = std::chrono::duration<double>(printed - last_print).count();
        const double elapsed = std::chrono::duration<double>(previous_tick - first_tick).count();
        double interval_sum = 0;
        for (double dt : intervals_ms) interval_sum += dt;
        std::sort(intervals_ms.begin(), intervals_ms.end());
        const double p99 = intervals_ms.empty() ? 0 : intervals_ms[
            static_cast<std::size_t>(std::ceil(intervals_ms.size() * 0.99)) - 1];
        std::cout << "[MouseRuntime] tick_hz="
            << (interval_sum > 0 ? 1000.0 * intervals_ms.size() / interval_sum : 0)
            << " avg_tick_hz=" << (elapsed > 0 ? (ticks - 1) / elapsed : 0)
            << " tick_p99_ms=" << p99
            << " tick_max_ms=" << (intervals_ms.empty() ? 0 : intervals_ms.back())
            << " skipped_deadlines=" << skipped_deadlines
            << " source_event_hz=" << (stats.source_packets - previous_sources) / window_seconds
            << " nonzero_output_hz=" << (stats.submitted_reports - previous_reports) / window_seconds
            << '\n';
        intervals_ms.clear();
        previous_sources = stats.source_packets;
        previous_reports = stats.submitted_reports;
        last_print = printed;
        const auto& response = session.runtime().effective_profile(session.right_button_down()
            ? mouse_native::MouseAimMode::Ads : mouse_native::MouseAimMode::Hipfire);
        const auto& judgment = session.runtime().facade().controller().last_output_components();
        std::cout << "[MouseRuntime] source_packets=" << stats.source_packets
            << " blocked_moves=" << stats.blocked_moves
            << " replacement_reports=" << stats.submitted_reports
            << " passed_replacements=" << stats.passed_replacements
            << " auto_fire_edges=" << stats.auto_fire_downs << '/' << stats.auto_fire_ups
            << " input_sum=(" << input_x << ',' << input_y << ')'
            << " output_sum=(" << output_x << ',' << output_y << ')'
            << " controller_ticks=" << controlled_ticks
            << " transparent_ticks=" << transparent_ticks
            << " RMB=" << session.right_button_down()
            << " physical_LMB=" << session.left_button_down()
            << " response=" << (!mouse_native::valid(response) ? "disabled" :
                response.calibrated ? "calibrated" : "cod_default_estimate")
            << " mode=" << session.runtime().facade().controller().last_ai_aim_mode()
            << " manual_retention=(" << judgment.mouse_manual_retention.x << ',' << judgment.mouse_manual_retention.y << ')'
            << " manual_conflict=(" << judgment.mouse_manual_conflict_x << ',' << judgment.mouse_manual_conflict_y << ')'
            << std::endl;
    }
};

int run_relay_test(const MouseRuntimeOptions& options, std::atomic_bool& stop_requested) {
    // This uses the same session/capture/output/escape path as the controller.
    // Link test sessions explicitly disable the default response and F11.
    const bool virtual_hid = options.transport == mouse_native::MouseControllerTransport::VirtualHid;
    mouse_native::MouseControllerSession session({}, {}, options.transport, options.relay_test,
        {}, options.mouse_device, options.mouse_hardware, virtual_hid);
    mouse_native::MouseRuntimeSupervisorWorker supervisor;
    if (virtual_hid && !supervisor.attach(options.supervisor_mapping, [&]() {
            stop_requested.store(true);
            session.emergency_release();
        })) throw std::runtime_error("independent mouse supervisor unavailable; no capture");
    if (!session.prepare(new_lease_id(), [&stop_requested, virtual_hid]() {
            stop_requested.store(true);
            if (!virtual_hid) ExitProcess(0);
        })) {
        throw std::runtime_error("mouse link could not start; check transport and hotkey availability");
    }
    if (supervisor.stop_requested()) { session.shutdown(); supervisor.shutdown_completed(); return 0; }
    supervisor.mark_armed(true);
    if (!session.arm()) throw std::runtime_error("mouse link could not arm; release all buttons and check devices");
    using Mode = mouse_native::MouseRelayTestMode;
    std::cout << "[MouseRuntime] RELAY TEST "
        << (options.relay_test == Mode::Block ? "block" :
            options.relay_test == Mode::Invert ? "invert" : "passthrough")
        << "; move the physical mouse; automatic release in " << options.duration_seconds
        << " seconds; Ctrl+Alt+F12 releases and exits" << std::endl;
    const auto started = std::chrono::steady_clock::now();
    auto next_status = started;
    runtime_app::AbsoluteDeadlineState deadlines(started, std::chrono::milliseconds(1));
    runtime_app::HighResolutionTimerPeriod timer_period(1);
    (void)runtime_app::set_current_thread_priority(
        runtime_app::RuntimeThreadPriority::AboveNormal);
    std::cout << "[MouseRuntime] target_tick_hz=1000 scheduler=absolute_deadline_yield AI_dt=elapsed\n";
    MouseRuntimeCounters counters;
    std::uint64_t tick_id = 0;
    while (!stop_requested.load()) {
        const auto now = std::chrono::steady_clock::now();
        if (now - started >= std::chrono::seconds(options.duration_seconds)) break;
        const auto result = session.tick(steady_seconds(now), ++tick_id);
        counters.add(result, now);
        if (result.transport_failed) {
            if (stop_requested.load() || supervisor.stop_requested()) break;
            throw std::runtime_error("mouse transport stopped; input release requested");
        }
        supervisor.heartbeat();
        if (now >= next_status) {
            counters.print(session);
            next_status = now + std::chrono::seconds(1);
        }
        if (options.max_ticks && tick_id >= options.max_ticks) break;
        runtime_app::sleep_until_precise(deadlines.next_deadline());
        counters.skipped_deadlines += deadlines.advance_after_tick(std::chrono::steady_clock::now());
    }
    counters.print(session);
    session.shutdown();
    supervisor.shutdown_completed();
    if (session.transport_error() && !(stop_requested.load() && session.transport_error() == ERROR_OPERATION_ABORTED))
        throw std::runtime_error("mouse cleanup reported an error; inspect supervisor recovery result");
    std::cout << "[MouseRuntime] relay test stopped; physical path released\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::atomic_bool stop_requested{false};
    try {
        if (argc == 4 && std::string(argv[1]) == "--mouse-supervisor-cleanup") {
            if (std::string(argv[2]) != "0" && std::string(argv[2]) != "1") return 2;
            std::size_t used = 0;
            const auto handle = std::stoull(argv[3], &used);
            if (used != std::string(argv[3]).size() || !handle) return 2;
            return mouse_native::run_mouse_supervisor_cleanup(
                std::string(argv[2]) == "1", static_cast<std::uintptr_t>(handle));
        }
        MouseRuntimeOptions options = parse_args(argc, argv);
        controller_native::RuntimeConfig config =
            controller_native::load_runtime_config(options.config_path, options.profile);
        if (!options.speed_override) options.mouse_tuning.speed = config.mouse.speed;
        if (!options.breakaway_override) options.mouse_tuning.breakaway = config.mouse.breakaway;
        if (!options.deadzone_override) options.mouse_tuning.bodylock_deadzone = config.mouse.bodylock_deadzone;
        options.mouse_tuning.bodylock_range_px = config.mouse.bodylock_range_px;
        options.mouse_tuning.bodylock_accel_ms = config.mouse.bodylock_accel_ms;
        options.mouse_tuning.bodylock_decel_ms = config.mouse.bodylock_decel_ms;
        options.mouse_tuning.bodylock_point_tolerance_px = config.mouse.bodylock_point_tolerance_px;
        if (!options.dpi_override) options.mouse_defaults.dpi = config.mouse.dpi;
        if (!options.sensitivity_override) options.mouse_defaults.sensitivity = config.mouse.sensitivity;
        if (!options.fov_override) options.mouse_defaults.horizontal_fov_16_9 = config.mouse.fov;
        if (!options.ads_multiplier_override) options.mouse_defaults.ads_multiplier = config.mouse.ads_multiplier;
        if (!mouse_native::valid(options.mouse_tuning))
            throw std::runtime_error("mouse speed must be 0.5..3; breakaway 1..8 and >= speed; bodylock deadzone 0..0.9");
        const auto hipfire_estimate = mouse_native::make_cod_default_profile(
            options.mouse_defaults, mouse_native::MouseAimMode::Hipfire);
        const auto ads_estimate = mouse_native::make_cod_default_profile(
            options.mouse_defaults, mouse_native::MouseAimMode::Ads);
        if (!mouse_native::valid(hipfire_estimate) || !mouse_native::valid(ads_estimate))
            throw std::runtime_error("mouse settings cannot form a finite response profile");
        if (options.check_config) {
            std::cout << "[MouseRuntime] bodylock range_px=" << options.mouse_tuning.bodylock_range_px
                << " accel_ms=" << options.mouse_tuning.bodylock_accel_ms
                << " decel_ms=" << options.mouse_tuning.bodylock_decel_ms
                << " point_tolerance_px=" << options.mouse_tuning.bodylock_point_tolerance_px << '\n';
            std::cout << "[MouseRuntime] recoil enabled=" << config.mouse.recoil_enabled
                << " counts_per_second=" << config.mouse.recoil_counts_per_second
                << " require_ads=" << config.mouse.recoil_require_ads
                << "; logging enabled=" << config.mouse.log_enabled
                << " directory=" << config.mouse.log_directory << " max_mb=" << config.mouse.log_max_mb << '\n';
            std::cout << "[MouseRuntime] config speed=" << options.mouse_tuning.speed
                << " breakaway=" << options.mouse_tuning.breakaway
                << " bodylock_deadzone=" << options.mouse_tuning.bodylock_deadzone
                << " speed_source=" << (options.speed_override ? "cli" : config.effective_source("mouse.speed"))
                << " deadzone_source=" << (options.deadzone_override ? "cli" : config.effective_source("mouse.bodylock_deadzone"))
                << "; no device or Vision startup\n";
            std::cout << "[MouseRuntime] response dpi=" << options.mouse_defaults.dpi
                << " sensitivity=" << options.mouse_defaults.sensitivity
                << " fov=" << options.mouse_defaults.horizontal_fov_16_9
                << " ads_multiplier=" << options.mouse_defaults.ads_multiplier
                << " dpi_source=" << (options.dpi_override ? "cli" : config.effective_source("mouse.dpi"))
                << " sensitivity_source=" << (options.sensitivity_override ? "cli" : config.effective_source("mouse.sensitivity"))
                << " fov_source=" << (options.fov_override ? "cli" : config.effective_source("mouse.fov"))
                << " ads_multiplier_source=" << (options.ads_multiplier_override ? "cli" : config.effective_source("mouse.ads_multiplier")) << '\n';
            std::cout << std::setprecision(9) << "[MouseRuntime] response_estimate view_height="
                << options.mouse_defaults.view_height_px
                << " cm_per_360=" << mouse_native::cod_cm_per_360(options.mouse_defaults)
                << " hipfire_px_per_count=" << hipfire_estimate.px_per_count_x
                << " ads_px_per_count=" << ads_estimate.px_per_count_x << '\n';
            return 0;
        }
        if (options.transport == mouse_native::MouseControllerTransport::Win32Debug) {
            std::cerr << "[MouseRuntime] DIAGNOSTIC ONLY: Win32 hook/SendInput cannot establish exclusive physical replacement in Raw Input games.\n";
        }
        if (options.check_transport) {
            if (options.transport == mouse_native::MouseControllerTransport::VirtualHid) {
                auto driver = mouse_native::make_interception_driver();
                if (!driver->open()) {
                    std::cerr << "[MouseRuntime] Interception unavailable; no capture or fallback\n";
                    return 2;
                }
                for (const auto& device : driver->devices())
                    std::wcout << L"[MouseRuntime] --mouse-device " << device.id << L"  " << device.hardware_id << L'\n';
                driver->close();
                mouse_native::MouseVirtualHidTransport probe;
                if (!probe.start(options.mouse_device, options.mouse_hardware)) {
                    std::cerr << "[MouseRuntime] physical identity or FakerInput unavailable; Win32 error="
                        << probe.last_error() << "; no capture or movement\n";
                    return 2;
                }
                std::cout << "[MouseRuntime] virtual-hid ready: source=" << probe.selected_device()
                    << "; Interception capture -> FakerInput independent relative HID; no capture or movement during preflight\n";
                probe.stop();
                return 0;
            }
            if (options.transport == mouse_native::MouseControllerTransport::Interception) {
                auto driver = mouse_native::make_interception_driver();
                if (!driver->open()) {
                    std::cerr << "[MouseRuntime] Interception DLL/driver unavailable; no capture, no Win32 fallback. No driver installed by this program.\n";
                    return 2;
                }
                const auto devices = driver->devices();
                for (const auto& device : devices)
                    std::wcout << L"[MouseRuntime] --mouse-device " << device.id << L"  " << device.hardware_id << L'\n';
                driver->close();
                if (devices.empty()) {
                    std::cerr << "[MouseRuntime] no driver mouse devices available; no capture\n";
                    return 2;
                }
                std::cout << "[MouseRuntime] Device enumeration only; no capture or movement. With multiple devices select one explicitly. Game compatibility remains unverified.\n";
                return 0;
            }
            if (options.transport == mouse_native::MouseControllerTransport::Win32Debug) {
                mouse_native::MouseControllerSession probe({}, {}, options.transport);
                if (!probe.prepare(new_lease_id(), []() {})) {
                    std::cerr << "[MouseRuntime] hook/Raw Input/hotkey registration failed; no interception enabled\n";
                    return 2;
                }
                probe.shutdown();
                std::cout << "[MouseRuntime] Win32 hook, Raw Input and F11/F12 registered and released; no interception or movement\n";
                return 0;
            }
            mouse_native::MouseRelayClient probe;
            COD_MOUSE_RELAY_STATUS status{};
            if (!probe.open() || !probe.status(&status)) {
                std::cerr << "[MouseRuntime] mouse filter unavailable, Win32 error="
                          << probe.last_error() << "; no interception enabled\n";
                return 2;
            }
            std::cout << "[MouseRuntime] filter interface available, protocol="
                      << status.Version << "; no interception enabled\n";
            return 0;
        }
        if (options.transport == mouse_native::MouseControllerTransport::VirtualHid && options.supervisor_mapping.empty()) {
            return mouse_native::run_mouse_runtime_supervisor(GetCommandLineW());
        }
        SetConsoleCtrlHandler(handle_console_signal, TRUE);
        g_stop_requested = &stop_requested;
        if (options.relay_test != mouse_native::MouseRelayTestMode::None) {
            const int result = run_relay_test(options, stop_requested);
            g_stop_requested = nullptr;
            return result;
        }
        mouse_native::MouseControllerFacadeConfig mouse_config{};
        mouse_config.controller = config.gamepad;
        mouse_config.tuning = options.mouse_tuning;
        mouse_config.recoil = {config.mouse.recoil_enabled, config.mouse.recoil_counts_per_second,
            config.mouse.recoil_require_ads};
        mouse_native::MouseDiagnosticOptions log_options;
        log_options.enabled = config.mouse.log_enabled;
        log_options.directory = config.mouse.log_directory;
        log_options.max_bytes = static_cast<std::uint64_t>(config.mouse.log_max_mb) * 1024 * 1024;
        std::ostringstream metadata;
        if (log_options.enabled) {
            wchar_t module[32768]{};
            GetModuleFileNameW(nullptr,module,32768);
            metadata << "{\"schema\":1,\"transport\":" << static_cast<int>(options.transport)
                << ",\"executable_sha256\":" << std::quoted(runtime_app::sha256_file_with_context(module))
                << ",\"config_sha256\":" << std::quoted(runtime_app::sha256_file_with_context(options.config_path))
                << ",\"model_sha256\":" << std::quoted(runtime_app::sha256_file_with_context(config.vision.model_path))
                << ",\"profile\":" << std::quoted(config.profile)
                << ",\"dpi\":" << options.mouse_defaults.dpi
                << ",\"sensitivity\":" << options.mouse_defaults.sensitivity
                << ",\"fov\":" << options.mouse_defaults.horizontal_fov_16_9
                << ",\"ads_multiplier\":" << options.mouse_defaults.ads_multiplier
                << ",\"speed\":" << options.mouse_tuning.speed << ",\"breakaway\":" << options.mouse_tuning.breakaway
                << ",\"bodylock_deadzone\":" << options.mouse_tuning.bodylock_deadzone
                << ",\"bodylock_range_px\":" << options.mouse_tuning.bodylock_range_px
                << ",\"bodylock_accel_ms\":" << options.mouse_tuning.bodylock_accel_ms
                << ",\"bodylock_decel_ms\":" << options.mouse_tuning.bodylock_decel_ms
                << ",\"bodylock_point_tolerance_px\":" << options.mouse_tuning.bodylock_point_tolerance_px
                << ",\"recoil_enabled\":" << config.mouse.recoil_enabled
                << ",\"recoil_counts_per_second\":" << config.mouse.recoil_counts_per_second
                << ",\"recoil_require_ads\":" << config.mouse.recoil_require_ads
                << ",\"capture_fps\":" << config.vision.capture_fps
                << ",\"idle_fps\":" << config.vision.idle_capture_fps
                << ",\"capture_wh\":[" << config.vision.capture_width << ',' << config.vision.capture_height << ']'
                << ",\"tensor_wh\":[" << config.vision.tensor_width << ',' << config.vision.tensor_height << ']'
                << ",\"max_observation_age_ms\":" << config.gamepad.tracker.max_observation_age_ms
                << ",\"queue_capacity\":" << log_options.capacity << ",\"max_bytes\":" << log_options.max_bytes
                << ",\"delivery_means\":\"transport API accepted, not independent game observation\"}";
        }
        // Open logs before capture. Destruction follows session release on all
        // exits; slow storage can never delay release of the physical device.
        mouse_native::MouseDiagnostics diagnostics(log_options, metadata.str());
        if (log_options.enabled)
            std::cout << "[MouseRuntime] detailed session log: " << std::filesystem::absolute(diagnostics.directory()) << '\n';
        mouse_native::MouseControllerSession session(
            mouse_config,
            {},
            options.transport,
            mouse_native::MouseRelayTestMode::None,
            options.mouse_defaults,
            options.mouse_device,
            options.mouse_hardware,
            options.transport == mouse_native::MouseControllerTransport::VirtualHid);
        mouse_native::MouseRuntimeSupervisorWorker supervisor;
        if (options.transport == mouse_native::MouseControllerTransport::VirtualHid &&
            !supervisor.attach(options.supervisor_mapping, [&]() {
                stop_requested.store(true);
                session.emergency_release();
            })) throw std::runtime_error("independent mouse supervisor unavailable; no capture");
        if (!session.prepare(
                new_lease_id(),
                [&stop_requested, transport = options.transport]() {
                    stop_requested.store(true);
                    if (transport != mouse_native::MouseControllerTransport::VirtualHid) ExitProcess(0);
                })) {
            const bool debug_transport = options.transport ==
                mouse_native::MouseControllerTransport::Win32Debug;
            throw std::runtime_error(debug_transport
                ? "Win32 mouse capture or Ctrl+Alt+F11/F12 registration failed; physical mouse was not intercepted"
                : "mouse device unavailable/ambiguous or Ctrl+Alt+F11/F12 registration failed; run --check-transport and select --mouse-device; no capture or Win32 fallback");
        }

        auto vision_engine = std::make_unique<vision_native::VisionEngine>(
            config.vision.capture_width,
            config.vision.capture_height,
            0,
            -1,
            config.vision.gpu_service_enabled ? 1 : 0,
            config.vision.model_path,
            config.vision.color_readback_mode,
            config.vision.tensor_width,
            config.vision.tensor_height,
            config.vision.require_isotropic_resize);
        runtime_app::VisionServiceOptions vision_options{};
        vision_options.capture_fps =
            static_cast<double>(config.vision.capture_fps);
        vision_options.idle_fps =
            static_cast<double>(config.vision.idle_capture_fps);
        vision_options.keepwarm_when_idle = config.vision.keepwarm_when_idle;
        runtime_app::VisionService vision_service(
            std::make_unique<MouseVisionEnginePoller>(std::move(vision_engine)),
            vision_options);
        vision_service.set_viewport(full_capture_viewport(config));
        vision_service.start();
        runtime_app::VisionDeliveryGate vision_delivery_gate(
            config.gamepad.tracker.max_observation_age_ms);

        const bool win32_debug_transport = options.transport ==
            mouse_native::MouseControllerTransport::Win32Debug;
        if (supervisor.stop_requested()) {
            session.shutdown(); supervisor.shutdown_completed(); vision_service.stop();
            return 0;
        }
        supervisor.mark_armed(true);
        if (!session.arm()) {
            throw std::runtime_error(
                "Mouse transport could not arm");
        }

        std::cout
            << "[MouseRuntime] transport="
            << (options.transport == mouse_native::MouseControllerTransport::Win32Debug
                    ? "win32"
                    : options.transport == mouse_native::MouseControllerTransport::VirtualHid ? "virtual-hid/FakerInput"
                    : options.transport == mouse_native::MouseControllerTransport::Interception ? "interception" : "kmdf-vhf")
            << (win32_debug_transport
                    ? "; demo-based movement hook armed; relative source -> shared controller -> tagged replacement\n"
                    : "; device filter armed; captured X/Y -> controller -> final report; verify exclusion at receiver\n")
            << "  Calibrate hipfire: aim at one stationary dummy, release RMB, press Ctrl+Alt+F11\n"
            << "  Calibrate ADS: hold RMB on the same kind of dummy, press Ctrl+Alt+F11\n"
            << "  Ready without calibration: COD " << options.mouse_defaults.dpi
            << " DPI x " << options.mouse_defaults.sensitivity
            << ", FOV " << options.mouse_defaults.horizontal_fov_16_9
            << ", ADS multiplier " << options.mouse_defaults.ads_multiplier
            << ", cm/360=" << mouse_native::cod_cm_per_360(options.mouse_defaults) << '\n'
            << "  Response source: cod_default_estimate; F11 replaces only the calibrated mode\n"
            << (options.transport == mouse_native::MouseControllerTransport::VirtualHid
                ? "  All selected physical packets are consumed; final X/Y, buttons and wheel use only the virtual HID. Release buttons before activation.\n"
                : "  Legacy transport: physical buttons/wheel pass unchanged; shared AutoFire adds owned left-button pulses\n")
            << "  Mouse tuning: speed=" << options.mouse_tuning.speed
            << " breakaway=" << options.mouse_tuning.breakaway
            << " bodylock_deadzone=" << options.mouse_tuning.bodylock_deadzone
            << " (original adapter: speed 1, breakaway 1, bodylock deadzone 0)\n"
            << "  Emergency exit: Ctrl+Alt+F12\n";
        std::cout << "  BodyLock: base range=" << (options.mouse_tuning.bodylock_range_px>0
                ? options.mouse_tuning.bodylock_range_px : mouse_config.controller.ai_aim.body_lock_activation_box_px)
            << " px, accel=" << options.mouse_tuning.bodylock_accel_ms
            << " ms, decel=" << options.mouse_tuning.bodylock_decel_ms
            << " ms, point tolerance=" << options.mouse_tuning.bodylock_point_tolerance_px << " px/axis\n";
        if (options.transport == mouse_native::MouseControllerTransport::Win32Debug) {
            std::cout
                << "  Link diagnostics: --relay-test passthrough|block|invert (without Vision)\n";
        }

        // Mouse report cadence is independent of gamepad/Vision configuration.
        // Keep the proven 1 ms deadline/yield wait; no detector-frame pacing.
        const auto tick_interval = std::chrono::milliseconds(1);
        runtime_app::AbsoluteDeadlineState deadlines(
            std::chrono::steady_clock::now(), tick_interval);
        runtime_app::HighResolutionTimerPeriod timer_period(1);
        (void)runtime_app::set_current_thread_priority(
            runtime_app::RuntimeThreadPriority::AboveNormal);
        std::cout << "[MouseRuntime] target_tick_hz=1000 scheduler=absolute_deadline_yield AI_dt=elapsed\n";

        bool armed_was_reported = true;
        bool controller_ready_was_reported = false;
        bool physical_relay_was_reported = false;
        bool basic_ai_output_was_reported = false;
        bool vision_frame_was_reported = false;
        bool vision_person_was_reported = false;
        bool right_button_was_down = false;
        std::uint64_t latest_vision_service_sequence = 0;
        std::uint64_t accepted_vision_frame = 0;
        std::uint64_t tick_id = 0;
        int response_view_height = 0;
        MouseRuntimeCounters counters;
        auto next_status = std::chrono::steady_clock::now() + std::chrono::seconds(1);

        while (!stop_requested.load()) {
            const auto now = std::chrono::steady_clock::now();
            bool accepted_vision_this_tick = false;
            const bool calibrating = session.runtime().calibration_state() !=
                mouse_native::MouseCalibrationState::Idle;
            const bool vision_requested = !both_profiles_calibrated(session) ||
                calibrating || session.right_button_down() ||
                session.left_button_down();
            const std::uint64_t expected_aim_transition_sequence =
                vision_service.set_aiming(vision_requested);
            const auto vision_snapshot = vision_service.latest_snapshot();
            if (vision_snapshot.sequence != 0 &&
                vision_snapshot.sequence != latest_vision_service_sequence) {
                latest_vision_service_sequence = vision_snapshot.sequence;
                const bool current_aiming_epoch =
                    vision_snapshot.controller_aiming == vision_requested &&
                    vision_snapshot.aim_transition_sequence ==
                        expected_aim_transition_sequence;
                const auto consume_now = std::chrono::steady_clock::now();
                if (vision_snapshot.freshness ==
                        runtime_app::VisionSnapshotFreshness::Fresh &&
                    current_aiming_epoch &&
                    vision_delivery_gate.accept(
                        vision_snapshot.result, steady_ns(consume_now))) {
                    const auto& result = vision_snapshot.result;
                    accepted_vision_frame = result.frame_id;
                    accepted_vision_this_tick = true;
                    if (result.capture_output_height > 0 &&
                        result.capture_output_height != response_view_height) {
                        response_view_height = result.capture_output_height;
                        session.set_default_view_height(response_view_height);
                        std::cout << "[MouseRuntime] COD default geometry: full_view="
                            << result.capture_output_width << 'x' << response_view_height
                            << " hipfire_px_per_count=" << session.runtime().effective_profile(
                                mouse_native::MouseAimMode::Hipfire).px_per_count_x
                            << " (FOV projection estimate; not ROI/tensor dimensions)\n";
                    }
                    session.submit_vision_snapshot(
                        runtime_app::adapt_vision_result(result));
                    if (!vision_frame_was_reported) {
                        vision_frame_was_reported = true;
                        std::cout
                            << "[MouseRuntime] VISION AIMING ACTIVE: fresh frame="
                            << result.frame_id
                            << " detections=" << result.detections.size()
                            << " selected="
                            << (result.has_selected_detection ? 1 : 0)
                            << std::endl;
                    }
                    if (!vision_person_was_reported && result.has_target &&
                        result.has_selected_detection &&
                        result.selector_target_generation != 0) {
                        vision_person_was_reported = true;
                        std::cout
                            << "[MouseRuntime] VISION PERSON VERIFIED: frame="
                            << result.frame_id
                            << " generation="
                            << result.selector_target_generation
                            << " target=(" << result.target_x << ','
                            << result.target_y << ')' << std::endl;
                    }
                }
            }

            bool ads = false;
            if (session.take_calibration_hotkey(&ads)) {
                std::cout << "[MouseRuntime] calibration hotkey received mode="
                          << (ads ? "ads" : "hipfire") << std::endl;
                const auto request = session.begin_calibration(ads, steady_ns(now));
                if (request.started) {
                    std::cout << "[MouseRuntime] calibration started mode="
                              << (ads ? "ads" : "hipfire") << std::endl;
                } else {
                    std::cerr << "[MouseRuntime] calibration rejected mode="
                              << (ads ? "ads" : "hipfire")
                              << " reason="
                              << calibration_failure_name(request.failure)
                              << std::endl;
                }
            }

            const auto tick_result = session.tick(steady_seconds(now), ++tick_id);
            if (log_options.enabled) {
                mouse_native::MouseDiagnosticContext context;
                context.tick = tick_id; context.begin_ns = steady_ns(now);
                context.end_ns = steady_ns(std::chrono::steady_clock::now());
                context.vision_sequence = vision_snapshot.sequence;
                context.vision_frame = vision_snapshot.result.frame_id;
                context.capture_ns = vision_snapshot.result.captured_at_ns;
                context.result_ns = vision_snapshot.result.result_at_ns;
                context.accepted_frame = accepted_vision_frame;
                context.vision_accepted = accepted_vision_this_tick;
                diagnostics.enqueue(mouse_native::mouse_diagnostic_record(session,tick_result,context));
            }
            counters.add(tick_result, now);
            if (now >= next_status) {
                counters.print(session);
                if (log_options.enabled) {
                    const auto log = diagnostics.counters();
                    std::cout << "[MouseRuntime] log written=" << log.written << " dropped=" << log.dropped
                        << " discarded=" << log.discarded << " failed=" << log.failed
                        << " budget_exhausted=" << log.budget_exhausted << '\n';
                }
                next_status = now + std::chrono::seconds(1);
            }
            auto vision_intent = tick_result.runtime.controller.vision_intent;
            vision_intent.intent_id = tick_id;
            vision_intent.timestamp = common_native::TimeSeconds{steady_seconds(now)};
            vision_intent.aiming = vision_requested;
            vision_service.set_user_aim_intent(vision_intent);
            const bool right_button_is_down = session.right_button_down();
            if (right_button_is_down != right_button_was_down) {
                right_button_was_down = right_button_is_down;
                std::cout
                    << "[MouseRuntime] CONTROLLER ADS INPUT "
                    << (right_button_is_down ? "ACTIVE" : "INACTIVE")
                    << ": physical RMB -> shared controller LT"
                    << std::endl;
            }
            if (win32_debug_transport && !physical_relay_was_reported &&
                tick_result.through_source_sequence != 0 &&
                tick_result.output_delivered &&
                (tick_result.runtime.controller.actuation.dx != 0 ||
                 tick_result.runtime.controller.actuation.dy != 0)) {
                physical_relay_was_reported = true;
                std::cout
                    << "[MouseRuntime] DEBUG SOURCE OBSERVED: X/Y received and SendInput accepted"
                    << std::endl;
            }
            if (win32_debug_transport && !basic_ai_output_was_reported &&
                tick_result.output_delivered &&
                tick_result.runtime.controller.controller_used) {
                const auto& controller =
                    session.runtime().facade().controller();
                const auto& components = controller.last_output_components();
                const bool material_assist =
                    std::abs(components.shaped_assist_stick.x) > 1.0e-5f ||
                    std::abs(components.shaped_assist_stick.y) > 1.0e-5f;
                if (material_assist) {
                    basic_ai_output_was_reported = true;
                    const bool calibrated = mouse_native::valid(
                        session.runtime().profile(tick_result.runtime.mode));
                    std::cout
                        << "[MouseRuntime] AI OUTPUT SUBMITTED: mode="
                        << controller.last_ai_aim_mode()
                        << " scale="
                        << (calibrated ? "calibrated" : "cod_default_estimate")
                        << " dx="
                        << tick_result.runtime.controller.actuation.dx
                        << " dy="
                        << tick_result.runtime.controller.actuation.dy
                        << std::endl;
                }
            }
            if (tick_result.runtime.calibration_completed) {
                const auto mode = tick_result.runtime.mode;
                const auto& profile = session.runtime().profile(mode);
                std::cout << "[MouseRuntime] calibration complete mode="
                          << (mode == mouse_native::MouseAimMode::Ads ? "ads" : "hipfire")
                          << " px_per_count=" << profile.px_per_count_x
                          << " counts_per_u_second=" << profile.counts_per_u_second_x
                          << " (memory only)\n";
            } else if (tick_result.runtime.calibration_failure !=
                       mouse_native::MouseCalibrationFailure::None) {
                std::cerr << "[MouseRuntime] calibration failed reason="
                          << calibration_failure_name(
                                 tick_result.runtime.calibration_failure);
                if (tick_result.runtime.calibration_failure ==
                    mouse_native::MouseCalibrationFailure::PhysicalMouseMoved) {
                    std::cerr << " physical_counts="
                              << tick_result.runtime.calibration_physical_abs_counts;
                }
                std::cerr
                          << '\n';
            }
            if (tick_result.calibration_started) {
                std::cout << "[MouseRuntime] calibration started from captured physical state, mode="
                    << (tick_result.runtime.mode == mouse_native::MouseAimMode::Ads ? "ads" : "hipfire") << '\n';
            }
            if (tick_result.transport_failed) {
                if (stop_requested.load() || supervisor.stop_requested()) break;
                throw std::runtime_error(
                    "mouse relay transport failed; input release requested");
            }
            supervisor.heartbeat();

            if (win32_debug_transport && !controller_ready_was_reported &&
                both_profiles_calibrated(session)) {
                controller_ready_was_reported = true;
                std::cout << "[MouseRuntime] CALIBRATED PROFILES ACTIVE: hipfire+ads\n";
            }

            if (options.max_ticks != 0 && tick_id >= options.max_ticks) break;
            runtime_app::sleep_until_precise(deadlines.next_deadline());
            counters.skipped_deadlines += deadlines.advance_after_tick(std::chrono::steady_clock::now());
        }

        counters.print(session);
        session.shutdown();
        supervisor.shutdown_completed();
        vision_service.stop();
        if (session.transport_error() && !(stop_requested.load() && session.transport_error() == ERROR_OPERATION_ABORTED))
            throw std::runtime_error("mouse cleanup reported an error; inspect supervisor recovery result");
        g_stop_requested = nullptr;
        diagnostics.stop(true);
        std::cout << "[MouseRuntime] stopped; physical mouse path released"
                  << (armed_was_reported ? " after active relay" : " before relay activation")
                  << '\n';
        return 0;
    } catch (const std::exception& error) {
        stop_requested.store(true);
        g_stop_requested = nullptr;
        std::cerr << "[MouseRuntime][Error] " << error.what() << '\n';
        return 1;
    }
}
