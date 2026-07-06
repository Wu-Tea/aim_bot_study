#include "runtime_loop.h"

#include "vision_controller_adapter.h"

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

common_native::TimeSeconds steady_time_seconds(const std::chrono::steady_clock::time_point& value) {
    return common_native::TimeSeconds{
        static_cast<double>(steady_time_point_ns(value)) / 1'000'000'000.0,
    };
}

pipeline_contract::UserAimIntent build_user_aim_intent(
    const controller_native::PhysicalGamepadState& physical,
    bool aiming,
    std::uint64_t intent_id,
    std::chrono::steady_clock::time_point timestamp) {
    pipeline_contract::UserAimIntent intent;
    intent.intent_id = intent_id;
    intent.timestamp = steady_time_seconds(timestamp);
    intent.aiming = aiming;
    if (!aiming) {
        return intent;
    }

    const float strength = std::hypot(physical.right_x, physical.right_y);
    intent.strength = std::min(1.0f, strength);
    if (strength <= 0.05f) {
        return intent;
    }

    intent.valid = true;
    intent.has_direction = true;
    intent.direction.x = physical.right_x / strength;
    intent.direction.y = -physical.right_y / strength;
    return intent;
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
                  << " mode=none"
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
        << " mode=" << vision_native::preprocess_mode_name(result->preprocess_mode)
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

void populate_fusion_target(
    const vision_native::VisionResult& src,
    int frame_width,
    int frame_height,
    shared_fusion::FusionTarget& dst) {
    dst.has_target = src.has_target;
    dst.auto_fire  = src.auto_fire;
    dst.confidence = src.target_confidence;
    dst.has_body_box = src.has_body_box;

    if (frame_width > 0 && frame_height > 0) {
        const float iw = 1.0f / static_cast<float>(frame_width);
        const float ih = 1.0f / static_cast<float>(frame_height);
        dst.target_x = src.target_x * iw;
        dst.target_y = src.target_y * ih;
        dst.dx = src.dx * iw;
        dst.dy = src.dy * ih;
        if (src.has_body_box) {
            dst.body_x1 = src.body_x1 * iw;
            dst.body_y1 = src.body_y1 * ih;
            dst.body_x2 = src.body_x2 * iw;
            dst.body_y2 = src.body_y2 * ih;
        }
    }
}

void populate_fusion_detections(
    const vision_native::VisionResult& src,
    int frame_width,
    int frame_height,
    shared_fusion::FusionDetection* dst,
    std::uint32_t& count) {
    count = 0;
    const std::size_t src_count = src.detections.size();
    if (src_count == 0 || dst == nullptr) {
        return;
    }

    const float iw = (frame_width > 0)
        ? 1.0f / static_cast<float>(frame_width) : 0.0f;
    const float ih = (frame_height > 0)
        ? 1.0f / static_cast<float>(frame_height) : 0.0f;

    const std::uint32_t limit = std::min(
        static_cast<std::uint32_t>(src_count),
        shared_fusion::FUSION_CHANNEL_MAX_DETECTIONS);

    for (std::uint32_t i = 0; i < limit; ++i) {
        const auto& d = src.detections[i];
        dst[i].x1          = d.x1 * iw;
        dst[i].y1          = d.y1 * ih;
        dst[i].x2          = d.x2 * iw;
        dst[i].y2          = d.y2 * ih;
        dst[i].conf        = d.conf;
        dst[i].class_id    = d.class_id;
        dst[i].color_bonus = d.color_bonus;
        dst[i].is_friendly = d.is_friendly;
    }
    count = limit;
}

}  // namespace

RuntimeLoop::RuntimeLoop(
    controller_native::RuntimeConfig config,
    bool perf_log,
    unsigned int max_ticks)
    : config_(std::move(config)),
      perf_logger_(gamepad_perf_log_enabled(perf_log)),
      aim_perf_file_logger_(
          config_.vision.aim_perf_file_log,
          config_.vision.aim_perf_log_dir,
          config_.vision.aim_perf_log_interval_ticks),
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
    vision_engine_ = std::make_unique<vision_native::VisionEngine>(
        config_.vision.capture_width,
        config_.vision.capture_height,
        0,
        -1,
        0,
        config_.vision.model_path);

    // --- fusion visual overlay channel (disabled by default) ---
    if (config_.vision.fusion_enabled) {
        fusion_enabled_ = fusion_publisher_.open(
            config_.vision.fusion_session.c_str(),
            config_.vision.fusion_show_all_detections);
        if (fusion_enabled_) {
            std::cout << "[Fusion][CPP] channel=ready"
                      << " session=\"" << config_.vision.fusion_session << "\""
                      << " show_all=" << (config_.vision.fusion_show_all_detections ? 1 : 0)
                      << '\n';
        } else {
            std::cout << "[Fusion][CPP] channel=unavailable (check FUSION_FORCE_OFF or kernel objects)"
                      << '\n';
        }
    }
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
    vision_engine_->set_user_aim_intent(build_user_aim_intent(
        physical,
        aiming,
        static_cast<std::uint64_t>(tick_count_) + 1u,
        tick_started));
    if (should_poll_vision(tick_started)) {
        last_vision_poll_at_ = tick_started;
        vision_native::VisionResult result = vision_engine_->poll_once();
        const auto controller_consume_started = std::chrono::steady_clock::now();
        controller_.submit_vision_snapshot(
            adapt_vision_result(result));
        latest_vision_result_ = result;
        has_latest_vision_result_ = true;
        latest_result_timestamp_ns_ =
            result.captured_at_ns != 0 ? result.captured_at_ns : result.result_at_ns;
        latest_controller_consume_started_ns_ =
            steady_time_point_ns(controller_consume_started);

        // --- fusion visual overlay publish (best-effort, no hot-path wait) ---
        if (fusion_enabled_ && result.frame_updated) {
            const int fw = config_.vision.capture_width;
            const int fh = config_.vision.capture_height;

            shared_fusion::FusionTarget ftarget{};
            populate_fusion_target(result, fw, fh, ftarget);

            shared_fusion::FusionDetection fdetections[shared_fusion::FUSION_CHANNEL_MAX_DETECTIONS];
            std::uint32_t fcount = 0;
            populate_fusion_detections(result, fw, fh, fdetections, fcount);

            fusion_publisher_.publish(
                result.frame_id, fw, fh, ftarget, fdetections, fcount);

            if (!fusion_publisher_.enabled()) {
                fusion_enabled_ = false;
                std::cout << "[Fusion][CPP] self-disabled after repeated publish failures\n";
            }
        }
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
    const bool log_aim_perf_file = config_.vision.aim_perf_file_log && aiming;
    if (log_vision || log_gamepad_perf || log_aim_perf_file) {
        const auto elapsed = std::chrono::steady_clock::now() - tick_started;
        const auto controller_pipeline_elapsed = vigem_update_started - controller_pipeline_started;
        const auto vigem_update_elapsed = vigem_update_finished - vigem_update_started;
        const double ctrl_loop_ms = std::chrono::duration<double, std::milli>(elapsed).count();
        const std::uint64_t output_sent_at_ns = steady_time_point_ns(vigem_update_finished);
        const controller_native::NativeAutoFireCounters fire = controller_.auto_fire_counters();
        const vision_native::VisionResult* result =
            has_latest_vision_result_ ? &latest_vision_result_ : nullptr;
        PerfSnapshot snapshot;
        snapshot.loop_fps = loop_fps_from_elapsed_ms(ctrl_loop_ms);
        snapshot.native_ms = result != nullptr ? result->post_ms : 0.0;
        snapshot.consume_ms = result != nullptr
            ? elapsed_ms_between_ns(latest_result_timestamp_ns_, latest_controller_consume_started_ns_)
            : 0.0;
        snapshot.out_age_ms = result != nullptr
            ? elapsed_ms_between_ns(latest_result_timestamp_ns_, output_sent_at_ns)
            : 0.0;
        snapshot.gpu_total_ms = result != nullptr ? result->gpu_total_ms : 0.0;
        snapshot.sync_wait_ms = result != nullptr ? result->output_wait_ms : 0.0;
        snapshot.ctrl_loop_ms = ctrl_loop_ms;
        snapshot.ctrl_pipeline_ms =
            std::chrono::duration<double, std::milli>(controller_pipeline_elapsed).count();
        snapshot.vigem_update_ms =
            std::chrono::duration<double, std::milli>(vigem_update_elapsed).count();
        snapshot.target_tier =
            result != nullptr && result->target_tier != nullptr ? result->target_tier : "none";
        snapshot.fire_requested = fire.requested;
        snapshot.fire_allowed = fire.allowed;
        snapshot.fire_blocked = fire.blocked;
        snapshot.box_samples = result != nullptr ? result->boxes_seen : 0.0;
        if (log_gamepad_perf) {
            perf_logger_.record_sample(snapshot);
        }
        if (log_aim_perf_file) {
            const controller_native::NativeControllerOutputComponents& output_components =
                controller_.last_output_components();
            const controller_native::NativeControllerVisionState& controller_vision_state =
                controller_.last_frame_vision_state();
            const controller_native::GamepadOutputState tracker_motion_output =
                controller_.last_tracker_motion_output();
            aim_perf_file_logger_.record_aim_sample(
                tick_count_,
                aiming,
                snapshot,
                result,
                &controller_vision_state,
                &output_components,
                &tracker_motion_output);
        }
        if (log_vision) {
            log_vision_result(
                result,
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

bool RuntimeLoop::is_aiming(const controller_native::PhysicalGamepadState& physical) {
    return aim_activation_tracker_.update(physical, config_.gamepad.rb_counts_as_aiming);
}

}  // namespace runtime_app
