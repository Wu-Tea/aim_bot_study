#include "runtime_loop.h"

#include <Windows.h>

#include <chrono>
#include <cstdlib>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace runtime_app {

namespace {

int virtual_key_from_quit_key(const std::string& quit_key) {
    if (quit_key.empty()) {
        return '0';
    }
    const unsigned char first = static_cast<unsigned char>(quit_key.front());
    return std::toupper(first);
}

void set_environment_variable(const char* name, const std::string& value) {
#if defined(_WIN32)
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

std::chrono::steady_clock::duration capture_interval_for_fps(int capture_fps) {
    if (capture_fps <= 0) {
        return std::chrono::steady_clock::duration::zero();
    }
    const auto microseconds = static_cast<int64_t>(
        std::lround(1'000'000.0 / static_cast<double>(capture_fps)));
    return std::chrono::microseconds(std::max<int64_t>(1, microseconds));
}

std::uint64_t steady_time_point_ns(const std::chrono::steady_clock::time_point& value) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(value.time_since_epoch())
            .count());
}

double elapsed_ms_between_ns(std::uint64_t start_ns, std::uint64_t end_ns) {
    if (start_ns == 0 || end_ns <= start_ns) {
        return 0.0;
    }
    return static_cast<double>(end_ns - start_ns) / 1'000'000.0;
}

const char* safe_c_string(const char* value, const char* fallback) {
    if (value == nullptr || value[0] == '\0') {
        return fallback;
    }
    return value;
}

float capture_transfer_ms(const vision_native::VisionResult& result) {
    return std::max(0.0f, result.wait_ms - result.capture_acquire_ms);
}

bool environment_flag_enabled(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return false;
    }
    const std::string text(value);
    return text != "0" && text != "false" && text != "False" && text != "off" && text != "OFF";
}

unsigned int environment_uint_or(const char* name, unsigned int fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return fallback;
    }
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value, &end, 10);
    if (end == value || parsed == 0ul) {
        return fallback;
    }
    return static_cast<unsigned int>(
        std::min<unsigned long>(parsed, std::numeric_limits<unsigned int>::max()));
}

bool input_log_enabled() {
    return environment_flag_enabled("GAMEPAD_INPUT_LOG");
}

bool gamepad_perf_log_enabled(bool perf_log) {
    return perf_log && environment_flag_enabled("GAMEPAD_PERF_LOG");
}

unsigned int vision_log_interval_ticks() {
    return environment_uint_or("VISION_LOG_INTERVAL_TICKS", 1000u);
}

bool should_log_vision_tick(unsigned int tick_count) {
    const unsigned int interval = vision_log_interval_ticks();
    return tick_count == 1u || (interval > 0u && tick_count % interval == 0u);
}

void log_xinput_slot_table(const std::vector<controller_native::XInputUserSlot>& slots);
void log_sdl_joystick_table(const std::vector<controller_native::SdlJoystickDevice>& devices);
void log_vision_result(
    const vision_native::VisionResult* result,
    bool aiming,
    unsigned int tick_count);
std::unique_ptr<controller_native::SdlGamepadReader> open_sdl_input_reader();

unsigned int select_xinput_user_index(
    const controller_native::GamepadRuntimeConfig& config,
    bool enabled) {
    if (!enabled) {
        return 0;
    }

    const std::vector<controller_native::XInputUserSlot> slots =
        controller_native::scan_xinput_user_slots();
    if (input_log_enabled()) {
        log_xinput_slot_table(slots);
    }

    if (!config.xinput_auto_detect) {
        const unsigned int fixed_index = std::min<unsigned int>(config.xinput_user_index, 3u);
        if (input_log_enabled()) {
            std::cout << "[NativeRuntime] XInput fixed selection index=" << fixed_index << '\n';
        }
        return fixed_index;
    }

    const unsigned int fallback_index = std::min<unsigned int>(config.xinput_user_index, 3u);
    for (const controller_native::XInputUserSlot& slot : slots) {
        if (slot.connected) {
            if (input_log_enabled()) {
                std::cout << "[NativeRuntime] XInput auto selection index=" << slot.user_index
                          << " (lowest connected)\n";
            }
            return slot.user_index;
        }
    }

    if (input_log_enabled()) {
        std::cout << "[NativeRuntime] XInput auto selection found no connected slots; fallback index="
                  << fallback_index << '\n';
    }
    return fallback_index;
}

std::unique_ptr<controller_native::SdlGamepadReader> open_sdl_input_reader() {
    const std::vector<controller_native::SdlJoystickDevice> devices =
        controller_native::scan_sdl_joystick_devices();
    if (input_log_enabled()) {
        log_sdl_joystick_table(devices);
    }
    for (const controller_native::SdlJoystickDevice& device : devices) {
        if (!device.opened) {
            continue;
        }
        auto reader = std::make_unique<controller_native::SdlGamepadReader>(device.device_index);
        if (reader->available()) {
            if (input_log_enabled()) {
                std::cout << "[NativeRuntime] selected SDL joystick index="
                          << reader->device_index() << " name=\"" << reader->device_name() << "\"\n";
            }
            return reader;
        }
    }
    if (input_log_enabled()) {
        std::cout << "[NativeRuntime] no SDL joystick input selected; falling back to XInput\n";
    }
    return nullptr;
}

void log_sdl_joystick_table(const std::vector<controller_native::SdlJoystickDevice>& devices) {
    std::cout << "[NativeRuntime] SDL joystick scan:\n";
    if (devices.empty()) {
        std::cout << "[NativeRuntime]   no devices\n";
        return;
    }
    std::cout << "[NativeRuntime]   index | state | axes | buttons | hats | name\n";
    for (const controller_native::SdlJoystickDevice& device : devices) {
        std::cout << "[NativeRuntime]   " << device.device_index << "     | "
                  << (device.opened ? "open" : "closed") << "  | "
                  << device.axes << "    | " << device.buttons << "       | "
                  << device.hats << "    | " << device.name << '\n';
    }
}

void log_xinput_slot_table(const std::vector<controller_native::XInputUserSlot>& slots) {
    std::cout << "[NativeRuntime] XInput slot scan:\n";
    std::cout << "[NativeRuntime]   index | state\n";
    for (const controller_native::XInputUserSlot& slot : slots) {
        std::cout << "[NativeRuntime]   " << slot.user_index << "     | "
                  << (slot.connected ? "connected" : "empty") << '\n';
    }
}

void log_vision_result(
    const vision_native::VisionResult* result,
    bool aiming,
    unsigned int tick_count) {
    std::cout << "[Vision][CPP]"
              << " tick=" << tick_count
              << " aiming=" << (aiming ? 1 : 0);
    if (result == nullptr) {
        std::cout << " updated=0 frame=0 boxes=0 target=0 source=none stage=none"
                  << " conf=0 dx=0 dy=0 aim_auth=0 fire_auth=0"
                  << " cap=0ms copy=0ms pre=0ms infer=0ms decode=0ms"
                  << " selector=0ms enhance=0ms age=0ms\n";
        return;
    }

    std::cout
        << " updated=" << (result->frame_updated ? 1 : 0)
        << " frame=" << result->frame_id
        << " boxes=" << result->boxes_seen
        << " target=" << (result->has_target ? 1 : 0)
        << " tier=" << safe_c_string(result->target_tier, "none")
        << " source=" << safe_c_string(result->target_source, "none")
        << " stage=" << safe_c_string(result->association_stage, "none")
        << " conf=" << result->target_confidence
        << " dx=" << result->dx
        << " dy=" << result->dy
        << " aim_auth=" << (result->aim_authority ? 1 : 0)
        << " fire_auth=" << (result->fire_authority ? 1 : 0)
        << " cap=" << result->capture_acquire_ms
        << "ms copy=" << capture_transfer_ms(*result)
        << "ms map=" << result->cuda_map_ms
        << "ms pre=" << result->preprocess_ms
        << "ms infer=" << result->infer_ms
        << "ms gpu=" << result->gpu_total_ms
        << "ms wait=" << result->output_wait_ms
        << "ms decode=" << result->decode_ms
        << "ms selector=" << result->selector_ms
        << "ms enhance=" << result->enhance_ms
        << "ms post=" << result->post_ms
        << "ms age=" << result->age_ms
        << "ms\n";
}

}  // namespace

RuntimeLoop::RuntimeLoop(
    controller_native::RuntimeConfig config,
    bool perf_log,
    unsigned int max_ticks)
    : config_(std::move(config)),
      perf_logger_(gamepad_perf_log_enabled(perf_log)),
      downward_diagnostics_(DownwardPullDiagnostics::from_environment()),
      perf_log_(perf_log),
      gamepad_perf_log_(gamepad_perf_log_enabled(perf_log)),
      sdl_input_reader_(open_sdl_input_reader()),
      input_reader_(select_xinput_user_index(config_.gamepad, sdl_input_reader_ == nullptr)),
      controller_(config_.gamepad),
      virtual_gamepad_() {
    max_ticks_ = max_ticks;
    selected_xinput_user_index_ = input_reader_.user_index();
    if (input_log_enabled() && sdl_input_reader_ != nullptr) {
        std::cout << "[NativeRuntime] physical input backend=SDL joystick"
                  << " index=" << sdl_input_reader_->device_index()
                  << " name=\"" << sdl_input_reader_->device_name() << "\"\n";
    } else if (input_log_enabled()) {
        std::cout << "[NativeRuntime] XInput input index=" << selected_xinput_user_index_
                  << (config_.gamepad.xinput_auto_detect ? " auto" : " fixed")
                  << '\n';
    }
    if (
        config_.gamepad.recoil.enabled &&
        config_.gamepad.recoil.native_recognizer_enabled &&
        !config_.gamepad.recoil.recognizer_state_path.empty()) {
        recoil_weapon_recognizer_ =
            std::make_unique<controller_native::NativeRecoilWeaponRuntimeRecognizer>(
                config_.gamepad.recoil);
        if (config_.gamepad.recoil.recognizer_log_enabled) {
            std::cout << "[RecoilOCR][CPP] native_recognizer="
                      << (recoil_weapon_recognizer_->available() ? "ready" : "unavailable")
                      << " game=" << config_.gamepad.recoil.recognizer_game
                      << " state=\"" << config_.gamepad.recoil.recognizer_state_path << "\"\n";
        }
    }
    set_environment_variable("VISION_MODEL_PATH", config_.vision.model_path);
    vision_engine_ = std::make_unique<vision_native::VisionEngine>(
        config_.vision.capture_width,
        config_.vision.capture_height,
        0,
        -1,
        0);
}

int RuntimeLoop::run() {
    using controller_native::GamepadOutputState;

    const auto tick_interval = std::chrono::milliseconds(1);
    while (!should_stop_requested()) {
        const auto tick_started = std::chrono::steady_clock::now();
        run_once();
        if (max_ticks_ > 0 && tick_count_ >= max_ticks_) {
            break;
        }

        const auto elapsed = std::chrono::steady_clock::now() - tick_started;
        if (elapsed < tick_interval) {
            std::this_thread::sleep_for(tick_interval - elapsed);
        }
    }
    virtual_gamepad_.update(GamepadOutputState{});
    return 0;
}

void RuntimeLoop::request_stop() {
    stop_requested_.store(true);
}

void RuntimeLoop::run_once() {
    const auto tick_started = std::chrono::steady_clock::now();
    const controller_native::PhysicalGamepadState physical = read_physical_gamepad();
    update_recoil_recognizer_schedule(physical, tick_started);
    const bool aiming = is_aiming(physical);
    latest_vision_aiming_ = aiming;
    vision_engine_->set_aiming(aiming);
    if (should_poll_vision(tick_started)) {
        last_vision_poll_at_ = tick_started;
        vision_native::VisionResult result = vision_engine_->poll_once();
        const auto controller_consume_started = std::chrono::steady_clock::now();
        controller_.submit_vision_result(result);
        latest_vision_result_ = result;
        has_latest_vision_result_ = true;
        latest_result_timestamp_ns_ =
            result.captured_at_ns != 0 ? result.captured_at_ns : result.result_at_ns;
        latest_controller_consume_started_ns_ =
            steady_time_point_ns(controller_consume_started);
    }
    poll_due_recoil_recognizer(std::chrono::steady_clock::now());

    const auto controller_pipeline_started = std::chrono::steady_clock::now();
    controller_native::GamepadOutputState output = controller_.build_output(physical);
    downward_diagnostics_.record_if_triggered(
        physical,
        output,
        controller_.last_pipeline_traces(),
        has_latest_vision_result_ ? &latest_vision_result_ : nullptr,
        is_aiming(physical));
    const auto vigem_update_started = std::chrono::steady_clock::now();
    virtual_gamepad_.update(output);
    const auto vigem_update_finished = std::chrono::steady_clock::now();
    ++tick_count_;

    const bool log_vision = perf_log_ && should_log_vision_tick(tick_count_);
    const bool log_gamepad_perf = gamepad_perf_log_ && should_log_vision_tick(tick_count_);
    if (log_vision || log_gamepad_perf) {
        const auto elapsed = std::chrono::steady_clock::now() - tick_started;
        const auto controller_pipeline_elapsed = vigem_update_started - controller_pipeline_started;
        const auto vigem_update_elapsed = vigem_update_finished - vigem_update_started;
        const std::uint64_t output_sent_at_ns = steady_time_point_ns(vigem_update_finished);
        const controller_native::NativeAutoFireCounters fire = controller_.auto_fire_counters();
        const vision_native::VisionResult& result = latest_vision_result_;
        if (log_gamepad_perf) {
            PerfSnapshot snapshot;
            snapshot.loop_fps = 1000.0;
            snapshot.native_ms = has_latest_vision_result_ ? result.post_ms : 0.0;
            snapshot.consume_ms = has_latest_vision_result_
                ? elapsed_ms_between_ns(latest_result_timestamp_ns_, latest_controller_consume_started_ns_)
                : 0.0;
            snapshot.out_age_ms = has_latest_vision_result_
                ? elapsed_ms_between_ns(latest_result_timestamp_ns_, output_sent_at_ns)
                : 0.0;
            snapshot.gpu_total_ms = has_latest_vision_result_ ? result.gpu_total_ms : 0.0;
            snapshot.sync_wait_ms = has_latest_vision_result_ ? result.output_wait_ms : 0.0;
            snapshot.ctrl_loop_ms = std::chrono::duration<double, std::milli>(elapsed).count();
            snapshot.ctrl_pipeline_ms =
                std::chrono::duration<double, std::milli>(controller_pipeline_elapsed).count();
            snapshot.vigem_update_ms =
                std::chrono::duration<double, std::milli>(vigem_update_elapsed).count();
            snapshot.target_tier =
                has_latest_vision_result_ && result.target_tier != nullptr ? result.target_tier : "none";
            snapshot.fire_requested = fire.requested;
            snapshot.fire_allowed = fire.allowed;
            snapshot.fire_blocked = fire.blocked;
            snapshot.box_samples = has_latest_vision_result_ ? result.boxes_seen : 0.0;
            perf_logger_.record_sample(snapshot);
        }
        if (log_vision) {
            log_vision_result(
                has_latest_vision_result_ ? &latest_vision_result_ : nullptr,
                latest_vision_aiming_,
                tick_count_);
        }
    }
}

controller_native::PhysicalGamepadState RuntimeLoop::read_physical_gamepad() {
    if (sdl_input_reader_ != nullptr && sdl_input_reader_->available()) {
        return sdl_input_reader_->read();
    }
    return input_reader_.read();
}

bool RuntimeLoop::should_poll_vision(std::chrono::steady_clock::time_point now) const {
    if (last_vision_poll_at_ == std::chrono::steady_clock::time_point{}) {
        return true;
    }
    const auto capture_interval = capture_interval_for_fps(config_.vision.capture_fps);
    return capture_interval <= std::chrono::steady_clock::duration::zero() ||
        now - last_vision_poll_at_ >= capture_interval;
}

void RuntimeLoop::update_recoil_recognizer_schedule(
    const controller_native::PhysicalGamepadState& physical,
    std::chrono::steady_clock::time_point now) {
    if (recoil_weapon_recognizer_ == nullptr || !recoil_weapon_recognizer_->available()) {
        return;
    }
    const bool scheduled = recoil_switch_scheduler_.update_y_button(physical.y, now);
    if (scheduled && config_.gamepad.recoil.recognizer_log_enabled) {
        std::cout << "[RecoilOCR][CPP] Y switch detected; scheduled captures at 600ms and 760ms\n";
    }
}

void RuntimeLoop::poll_due_recoil_recognizer(std::chrono::steady_clock::time_point now) {
    if (recoil_weapon_recognizer_ == nullptr || !recoil_weapon_recognizer_->available()) {
        return;
    }
    while (recoil_switch_scheduler_.consume_due_capture(now)) {
        const bool updated = recoil_weapon_recognizer_->poll_once();
        if (updated) {
            const auto& event = recoil_weapon_recognizer_->last_event();
            if (event.has_value() && event->source != "switch_unresolved") {
                recoil_switch_scheduler_.clear_pending();
            }
        }
        if (config_.gamepad.recoil.recognizer_log_enabled) {
            std::cout << "[RecoilOCR][CPP] switch capture"
                      << " updated=" << (updated ? 1 : 0)
                      << " pending=" << recoil_switch_scheduler_.pending_count()
                      << '\n';
        }
    }
}

bool RuntimeLoop::should_stop_requested() const {
    if (stop_requested_.load()) {
        return true;
    }
    const int quit_key = virtual_key_from_quit_key(config_.vision.quit_key);
    return (GetAsyncKeyState(quit_key) & 0x8000) != 0;
}

bool RuntimeLoop::is_aiming(const controller_native::PhysicalGamepadState& physical) const {
    return physical.left_trigger > 0.05f || (
        config_.gamepad.rb_counts_as_aiming && physical.rb);
}

}  // namespace runtime_app
