#include "runtime_loop.h"

#include "runtime_timing.h"
#include "vision_controller_adapter.h"

#include <Windows.h>

#include <chrono>
#include <cstdlib>
#include <exception>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace runtime_app {

namespace {

// A 200 Hz service can poll just before a 180 Hz game presents its next frame.
// Give DXGI one bounded millisecond to receive that frame instead of returning
// empty and waiting another full service period. Keep direct/controller-thread
// polling non-blocking so this never stalls the 1 kHz output loop.
constexpr int kGpuServiceCaptureWaitMs = 1;

RuntimeTelemetryOptions telemetry_options_from(
    const controller_native::RuntimeConfig& config,
    const std::filesystem::path& session_directory) {
    RuntimeTelemetryOptions options;
    options.enabled = config.telemetry.enabled;
    options.directory = session_directory.empty()
        ? std::filesystem::path(config.telemetry.directory)
        : session_directory;
    options.queue_capacity = config.telemetry.queue_capacity;
    options.rotate_size_bytes =
        static_cast<std::size_t>(config.telemetry.rotate_size_mb) * 1024ull * 1024ull;
    options.max_files = config.telemetry.max_files;
    return options;
}

LogSessionOptions log_session_options_from(const controller_native::RuntimeConfig& config) {
    LogSessionOptions options;
    options.enabled = config.telemetry.enabled;
    options.root = config.telemetry.directory;
    options.git_commit = config.build_commit;
    options.config_hash = config.source_config_sha256;
    options.engine_hash = config.engine_sha256;
    options.executable_sha256 = config.executable_sha256;
    options.control_contract_sha256 = config.control_contract_sha256;
    options.control_architecture_version = config.control_architecture_version;
    options.control_event_schema_version = config.control_event_schema_version;
    options.capture_width = config.vision.capture_width;
    options.capture_height = config.vision.capture_height;
    options.tensor_width = config.vision.tensor_width;
    options.tensor_height = config.vision.tensor_height;
    options.require_isotropic_resize = config.vision.require_isotropic_resize;
    options.model_path = config.vision.model_path;
    return options;
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
    const pipeline_contract::IntentState& controller_intent,
    bool aiming,
    std::uint64_t intent_id,
    std::chrono::steady_clock::time_point timestamp) {
    pipeline_contract::UserAimIntent intent;
    intent.intent_id = intent_id;
    intent.timestamp = steady_time_seconds(timestamp);
    intent.aiming = aiming;
    intent.purpose = controller_intent.right_purpose;
    if (!aiming) {
        return intent;
    }

    const float strength = std::hypot(
        controller_intent.filtered_right.x,
        controller_intent.filtered_right.y);
    intent.strength = std::min(1.0f, strength);
    if (strength <= 0.0f) {
        return intent;
    }

    intent.valid = true;
    intent.has_direction = true;
    intent.direction.x = controller_intent.filtered_right.x / strength;
    intent.direction.y = -controller_intent.filtered_right.y / strength;
    return intent;
}

double elapsed_ms_between_ns(std::uint64_t start_ns, std::uint64_t end_ns) {
    if (start_ns == 0 || end_ns <= start_ns) {
        return 0.0;
    }
    return static_cast<double>(end_ns - start_ns) / 1'000'000.0;
}

double elapsed_ms_or_invalid(std::uint64_t start_ns, std::uint64_t end_ns) {
    if (start_ns == 0 || end_ns <= start_ns) return -1.0;
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

ViewportControllerConfig viewport_controller_config_from(
    const controller_native::RuntimeConfig& config) {
    ViewportControllerConfig value;
    value.enabled = config.vision.dynamic_viewport_enabled;
    if (value.enabled) {
        value.precision = {
            config.vision.viewport_precision_width,
            config.vision.viewport_precision_height};
        value.normal = {
            config.vision.viewport_normal_width,
            config.vision.viewport_normal_height};
        value.rescue = {
            config.vision.viewport_rescue_width,
            config.vision.viewport_rescue_height};
        value.prediction_seconds =
            config.vision.viewport_prediction_ms / 1000.0f;
    } else {
        const ViewportDimensions full_capture{
            config.vision.capture_width,
            config.vision.capture_height};
        value.precision = full_capture;
        value.normal = full_capture;
        value.rescue = full_capture;
    }
    value.aim_height_ratio = config.gamepad.tracker.aim_height_ratio;
    return value;
}

class VisionEngineServicePoller final : public IVisionServicePoller {
public:
    explicit VisionEngineServicePoller(std::unique_ptr<vision_native::VisionEngine> engine)
        : engine_(std::move(engine)) {}

    void set_aiming(bool aiming) override {
        engine_->set_aiming(aiming);
    }

    void set_user_aim_intent(const pipeline_contract::UserAimIntent& intent) override {
        engine_->set_user_aim_intent(intent);
    }

    void set_viewport(const ViewportRequest& request) override {
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
                  << " service=none service_state=unknown service_seq=0"
                  << " mode=none"
                  << " cap=0ms copy=0ms pre=0ms infer=0ms enqueue=0ms decode=0ms"
                  << " selector=0ms age=0ms\n";
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
        << " service=" << safe_c_string(result->service_freshness, "none")
        << " service_state=" << safe_c_string(result->service_source_state, "unknown")
        << " service_seq=" << result->service_sequence
        << " viewport=" << safe_c_string(result->viewport_level, "normal")
        << ':' << result->viewport_width << 'x' << result->viewport_height
        << '@' << result->viewport_left << ',' << result->viewport_top
        << " viewport_seq=" << result->viewport_sequence
        << " mode=" << vision_native::preprocess_mode_name(result->preprocess_mode)
        << " cap=" << result->capture_acquire_ms
        << "ms copy=" << capture_transfer_ms(*result)
        << "ms map=" << result->cuda_map_ms
        << "ms pre=" << result->preprocess_ms
        << "ms infer=" << result->infer_ms
        << "ms enqueue=" << result->enqueue_cpu_ms
        << "ms gpu=" << result->gpu_total_ms
        << "ms wait=" << result->output_wait_ms
        << "ms decode=" << result->decode_ms
        << "ms selector=" << result->selector_ms
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
    dst.direct_observation =
        src.frame_updated && src.has_selected_detection && src.has_body_box;
    dst.enemy_identity_confirmed = src.enemy_identity_confirmed;
    dst.selector_target_generation = src.selector_target_generation;

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
      perf_summary_logger_(PerfSummaryOptions{
          config_.performance.enabled,
          config_.performance.interval_ms,
          std::filesystem::path(config_.performance.directory),
          config_.performance.stdout_enabled,
          config_.build_commit,
          config_.source_config_sha256,
          config_.engine_sha256}),
      log_session_manager_(log_session_options_from(config_)),
      telemetry_(telemetry_options_from(config_, log_session_manager_.session_directory())),
      telemetry_collectors_(
          config_.telemetry.enabled,
          &telemetry_,
          TelemetrySessionContext{
              config_.build_commit.c_str(),
               config_.source_config_sha256.c_str(),
               config_.engine_sha256.c_str(),
               config_.executable_sha256.c_str(),
               config_.vision.capture_width,
              config_.vision.capture_height,
              config_.vision.capture_fps,
               config_.vision.idle_capture_fps,
               config_.scheduler.controller_tick_hz,
               config_.telemetry.manual_controller_hz}),
      downward_diagnostics_(DownwardPullDiagnostics::from_environment()),
      perf_log_(perf_log),
      gamepad_perf_log_(gamepad_perf_log_enabled(perf_log)),
      sdl_input_reader_(open_sdl_input_reader()),
      input_reader_(select_xinput_user_index(config_.gamepad, sdl_input_reader_ == nullptr)),
      controller_(config_.gamepad),
      viewport_controller_(viewport_controller_config_from(config_)),
      virtual_gamepad_(),
      person_detection_gesture_(
          std::chrono::milliseconds(
              config_.gamepad.enemy_mark.l3_cooldown_ms),
          std::chrono::milliseconds(
              config_.gamepad.enemy_mark.lt_cooldown_ms)),
      vision_delivery_gate_(config_.gamepad.tracker.max_observation_age_ms) {
    telemetry_.start();
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
    auto vision_engine = std::make_unique<vision_native::VisionEngine>(
        config_.vision.capture_width,
        config_.vision.capture_height,
        0,
        -1,
        config_.vision.gpu_service_enabled ? kGpuServiceCaptureWaitMs : 0,
        config_.vision.model_path,
        config_.vision.color_readback_mode,
        config_.vision.tensor_width,
        config_.vision.tensor_height,
        config_.vision.require_isotropic_resize,
        config_.gamepad.ai_aim.ads_pickup_base_radius_px);
    std::cout << "[VisionGeometry][CPP]"
              << " capture=" << vision_engine->width() << 'x' << vision_engine->height()
              << " tensor=" << vision_engine->tensor_width() << 'x'
              << vision_engine->tensor_height()
              << " scale=" << vision_engine->resize_scale_x() << 'x'
              << vision_engine->resize_scale_y()
              << " isotropic=" << (vision_engine->resize_isotropic() ? 1 : 0)
              << '\n';
    const ViewportRequest initial_viewport = viewport_controller_.current();
    vision_engine->set_viewport(
        static_cast<int>(initial_viewport.level),
        initial_viewport.width,
        initial_viewport.height,
        initial_viewport.sequence,
        initial_viewport.source_frame_id);
    if (config_.vision.gpu_service_enabled) {
        VisionServiceOptions service_options;
        service_options.capture_fps = static_cast<double>(config_.vision.capture_fps);
        service_options.idle_fps = static_cast<double>(config_.vision.idle_capture_fps);
        service_options.keepwarm_when_idle = config_.vision.keepwarm_when_idle;
        vision_service_ = std::make_unique<VisionService>(
            std::make_unique<VisionEngineServicePoller>(std::move(vision_engine)),
            service_options);
        vision_service_->set_viewport(initial_viewport);
        vision_service_->start();
        std::cout << "[VisionService][CPP] enabled"
                  << " capture_fps=" << config_.vision.capture_fps
                  << " idle_fps=" << config_.vision.idle_capture_fps
                  << " keepwarm=" << (config_.vision.keepwarm_when_idle ? 1 : 0)
                  << '\n';
    } else {
        vision_engine_ = std::move(vision_engine);
    }

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
    (void)set_current_thread_priority(RuntimeThreadPriority::AboveNormal);

    const int tick_hz = std::max(1, config_.scheduler.controller_tick_hz);
    const auto tick_interval = std::chrono::nanoseconds(1'000'000'000ll / tick_hz);
    AbsoluteDeadlineState deadlines(std::chrono::steady_clock::now(), tick_interval);
    PrecisionTickScheduler precision_scheduler(config_.scheduler.spin_tail_us);
    std::cout << "[NativeRuntime] controller_scheduler="
              << (config_.scheduler.mode == "legacy" ? "legacy" : precision_scheduler.mode_name())
              << " tick_hz=" << tick_hz << '\n';
    std::exception_ptr failure;
    try {
        while (!should_stop_requested()) {
            const auto tick_started = std::chrono::steady_clock::now();
            run_once();
            if (max_ticks_ > 0 && tick_count_ >= max_ticks_) {
                break;
            }

            if (config_.scheduler.mode == "legacy") {
                sleep_until_precise(deadlines.next_deadline());
            } else {
                precision_scheduler.wait_until(deadlines.next_deadline());
            }
            deadlines.advance_after_tick(std::chrono::steady_clock::now());
        }
    } catch (...) {
        failure = std::current_exception();
    }
    // Revoke controller authority and neutralize before any potentially slow
    // worker join or telemetry drain, including a Vision worker failure.
    controller_.reset();
    if (config_.output.enabled) {
        controller_native::PhysicalGamepadState neutral_physical{};
        auto neutral_frame = controller_native::ControlFrame::begin(
            neutral_physical,
            pipeline_contract::ControllerTickId::from(1),
            pipeline_contract::EventSequence::from(1));
        if (output_composer_.compose(neutral_frame) ==
                controller_native::OutputComposeStatus::Ok &&
            output_composer_.finalized_output() != nullptr) {
            virtual_gamepad_.update(*output_composer_.finalized_output());
        }
    }
    if (vision_service_ != nullptr) {
        vision_service_->stop();
    }
    perf_summary_logger_.stop();
    telemetry_collectors_.shutdown(steady_time_point_ns(std::chrono::steady_clock::now()));
    telemetry_.stop();
    log_session_manager_.close();
    if (failure) std::rethrow_exception(failure);
    return 0;
}

void RuntimeLoop::request_stop() {
    stop_requested_.store(true);
}

void RuntimeLoop::run_once() {
    const auto tick_started = std::chrono::steady_clock::now();
    const controller_native::PhysicalGamepadState physical = read_physical_gamepad();
    const std::uint64_t physical_read_at_ns = steady_time_point_ns(std::chrono::steady_clock::now());
    const auto& tick_preparation = controller_.begin_tick(
        physical,
        static_cast<std::uint64_t>(tick_count_) + 1u);
    const bool aiming = tick_preparation.scope.physical_ads_active;
    // Physical ADS and manual-fire activation are independent input events,
    // reduced into one scope snapshot without synthesizing LT.
    const bool assist_aiming = tick_preparation.scope.assist_active;
    latest_vision_aiming_ = assist_aiming;
    const auto& controller_intent = tick_preparation.intent;
    const pipeline_contract::UserAimIntent user_aim_intent = build_user_aim_intent(
        controller_intent,
        assist_aiming,
        static_cast<std::uint64_t>(tick_count_) + 1u,
        tick_started);

    // L3 is a mark request, not an aim request. It may temporarily wake the
    // Vision/selector path, but controller intent and aim authority continue
    // to use the real LT/RB aiming state above.
    bool vision_requested = assist_aiming;
    if (config_.gamepad.enemy_mark.enabled) {
        person_detection_gesture_.update_activation(
            physical.left_thumb,
            physical.left_trigger,
            tick_started);
        vision_requested = vision_requested ||
            person_detection_gesture_.status(tick_started).request_pending;
        if (vision_requested && !enemy_mark_vision_active_) {
            ++enemy_mark_target_scope_;
            if (enemy_mark_target_scope_ == 0) {
                ++enemy_mark_target_scope_;
            }
        }
        enemy_mark_vision_active_ = vision_requested;
    }

    auto publish_fusion_if_updated = [&](const vision_native::VisionResult& result) {
        // --- fusion visual overlay publish (best-effort, no hot-path wait) ---
        if (fusion_enabled_ && result.frame_updated) {
            const int fw = config_.vision.capture_width;
            const int fh = config_.vision.capture_height;

            shared_fusion::FusionTarget ftarget{};
            populate_fusion_target(result, fw, fh, ftarget);

            const shared_fusion::FusionFrameGeometry fgeometry{
                result.capture_output_left,
                result.capture_output_top,
                result.capture_output_width,
                result.capture_output_height,
                result.capture_roi_left,
                result.capture_roi_top,
            };

            shared_fusion::FusionDetection fdetections[shared_fusion::FUSION_CHANNEL_MAX_DETECTIONS];
            std::uint32_t fcount = 0;
            populate_fusion_detections(result, fw, fh, fdetections, fcount);

            fusion_publisher_.publish(
                result.frame_id, fw, fh, fgeometry, ftarget, fdetections, fcount);

            if (!fusion_publisher_.enabled()) {
                fusion_enabled_ = false;
                std::cout << "[Fusion][CPP] self-disabled after repeated publish failures\n";
            }
        }
    };

    bool telemetry_new_vision = false;
    bool viewport_fresh_vision = false;
    if (vision_service_ != nullptr) {
        const std::uint64_t expected_aim_transition_sequence =
            vision_service_->set_aiming(vision_requested);
        vision_service_->set_user_aim_intent(user_aim_intent);
        VisionServiceSnapshot service_snapshot =
            vision_service_->latest_snapshot(latest_vision_service_sequence_);
        if (
            service_snapshot.sequence != 0 &&
            service_snapshot.sequence != latest_vision_service_sequence_) {
            latest_vision_service_sequence_ = service_snapshot.sequence;
            vision_native::VisionResult result = std::move(service_snapshot.result);
            const auto controller_consume_started = std::chrono::steady_clock::now();
            const std::uint64_t controller_consume_ns =
                steady_time_point_ns(controller_consume_started);
            const bool current_control_epoch =
                service_snapshot.controller_aiming == vision_requested &&
                service_snapshot.aim_transition_sequence ==
                    expected_aim_transition_sequence;
            if (service_snapshot.freshness == VisionSnapshotFreshness::Fresh &&
                current_control_epoch &&
                vision_delivery_gate_.accept(result, controller_consume_ns)) {
                controller_.submit_vision_snapshot(adapt_vision_result(result));
                latest_vision_publish_ns_ = service_snapshot.published_at_ns;
                latest_vision_publish_available_ =
                    service_snapshot.published_at_ns != 0;
                latest_controller_submit_complete_ns_ =
                    steady_time_point_ns(std::chrono::steady_clock::now());
                latest_vision_result_ = result;
                has_latest_vision_result_ = true;
                latest_result_timestamp_ns_ =
                    result.captured_at_ns != 0 ? result.captured_at_ns : result.result_at_ns;
                latest_controller_consume_started_ns_ = controller_consume_ns;
                telemetry_new_vision = true;
                viewport_fresh_vision = true;
            }
        }
    } else {
        vision_engine_->set_aiming(vision_requested);
        vision_engine_->set_user_aim_intent(user_aim_intent);
        if (should_poll_vision(tick_started)) {
            last_vision_poll_at_ = tick_started;
            vision_native::VisionResult result = vision_engine_->poll_once();
            const auto controller_consume_started = std::chrono::steady_clock::now();
            const std::uint64_t controller_consume_ns =
                steady_time_point_ns(controller_consume_started);
            if (vision_delivery_gate_.accept(result, controller_consume_ns)) {
                controller_.submit_vision_snapshot(adapt_vision_result(result));
                // Direct polling has no VisionService mailbox publish stage.
                // Keep that stage explicitly unavailable and expose the
                // controller submit completion separately.
                latest_vision_publish_ns_ = 0;
                latest_vision_publish_available_ = false;
                latest_controller_submit_complete_ns_ =
                    steady_time_point_ns(std::chrono::steady_clock::now());
                latest_vision_result_ = result;
                has_latest_vision_result_ = true;
                latest_result_timestamp_ns_ =
                    result.captured_at_ns != 0 ? result.captured_at_ns : result.result_at_ns;
                latest_controller_consume_started_ns_ = controller_consume_ns;
                telemetry_new_vision = true;
                viewport_fresh_vision = true;
            }
        }
    }
    const auto controller_pipeline_started = std::chrono::steady_clock::now();
    controller_native::ControlFrame control_frame =
        controller_.resolve_control_frame();
    if (config_.gamepad.enemy_mark.enabled) {
        if (telemetry_new_vision) {
            person_detection_gesture_.observe_fresh_plan(
                controller_.last_target_plan(),
                tick_started,
                enemy_mark_target_scope_);
        }
        if (person_detection_gesture_.dpad_up_requested(false, tick_started)) {
            auto& dpad = control_frame.auxiliary_dpad();
            dpad.header.controller_tick = control_frame.controller_tick();
            dpad.header.sequence = control_frame.sample_sequence();
            dpad.header.cause_event = control_frame.sample_sequence();
            dpad.up = true;
        }
    }
    const auto compose_status = output_composer_.compose(control_frame);
    const auto* composed_output = output_composer_.finalized_output();
    if (compose_status != controller_native::OutputComposeStatus::Ok ||
        composed_output == nullptr) {
        throw std::runtime_error("control output composition failed");
    }
    const controller_native::GamepadOutputState output = *composed_output;
    controller_.observe_composed_output(output);
    const auto vigem_update_started = std::chrono::steady_clock::now();
    controller_native::VirtualGamepadUpdateResult output_result;
    if (config_.output.enabled) {
        output_result = virtual_gamepad_.update(output);
        if (output_result.reconnect_attempted && output_result.delivered) {
            std::cout << "[NativeRuntime][Output] ViGEm recovered"
                      << " reconnect_count=" << output_result.reconnect_count << '\n';
        } else if (output_result.reconnect_attempted && !output_result.delivered) {
            std::cerr << "[NativeRuntime][Output] ViGEm recovery pending"
                      << " error=0x" << std::hex << output_result.error_code << std::dec << '\n';
        }
    }
    const auto vigem_update_finished = std::chrono::steady_clock::now();
    // Everything below is observation, diagnostics, or future-frame setup.
    // It must never delay the output calculated from a newly consumed result.
    if (telemetry_new_vision) {
        publish_fusion_if_updated(latest_vision_result_);

        TelemetryVisionInput vision;
        vision.frame_id = latest_vision_result_.frame_id;
        vision.captured_at_ns = latest_vision_result_.captured_at_ns;
        vision.inferred_at_ns = latest_vision_result_.inferred_at_ns;
        vision.result_at_ns = latest_vision_result_.result_at_ns;
        vision.controller_consume_ns = latest_controller_consume_started_ns_;
        vision.frame_width = config_.vision.capture_width;
        vision.frame_height = config_.vision.capture_height;
        vision.has_target = latest_vision_result_.has_target;
        vision.live = latest_vision_result_.has_target && latest_vision_result_.has_body_box &&
            latest_vision_result_.frame_updated;
        vision.aiming = aiming;
        vision.x1 = latest_vision_result_.body_x1;
        vision.y1 = latest_vision_result_.body_y1;
        vision.x2 = latest_vision_result_.body_x2;
        vision.y2 = latest_vision_result_.body_y2;
        vision.target_x = latest_vision_result_.target_x;
        vision.target_y = latest_vision_result_.target_y;
        vision.screen_center_x = latest_vision_result_.screen_center_x;
        vision.screen_center_y = latest_vision_result_.screen_center_y;
        vision.detector_box_count = static_cast<std::uint32_t>(latest_vision_result_.boxes_seen);
        vision.target_source = latest_vision_result_.target_source;
        vision.target_tier = latest_vision_result_.target_tier;
        vision.target_confidence = latest_vision_result_.target_confidence;
        telemetry_collectors_.observe_new_vision(vision);
    }

    pipeline_contract::CommittedCaptureObservation viewport_observation;
    const pipeline_contract::CommittedCaptureObservation* viewport_observation_ptr = nullptr;
    if (viewport_fresh_vision) {
        viewport_observation = adapt_committed_capture_observation(
            latest_vision_result_,
            controller_.last_target_plan(),
            controller_.ads_epoch(),
            latest_controller_consume_started_ns_);
        if (pipeline_contract::valid(viewport_observation)) {
            viewport_observation_ptr = &viewport_observation;
        }
    }
    const ViewportRequest viewport_request = viewport_controller_.update(
        controller_.last_target_plan(),
        viewport_observation_ptr,
        steady_time_point_ns(std::chrono::steady_clock::now()));
    if (viewport_request.changed) {
        if (vision_service_ != nullptr) {
            vision_service_->set_viewport(viewport_request);
        } else {
            vision_engine_->set_viewport(
                static_cast<int>(viewport_request.level),
                viewport_request.width,
                viewport_request.height,
                viewport_request.sequence,
                viewport_request.source_frame_id);
        }
        std::cout << "[VisionViewport][CPP]"
                  << " level=" << viewport_level_name(viewport_request.level)
                  << " size=" << viewport_request.width << 'x'
                  << viewport_request.height
                  << " sequence=" << viewport_request.sequence
                  << " source_frame=" << viewport_request.source_frame_id
                  << '\n';
    }
    downward_diagnostics_.record_if_triggered(
        physical,
        output,
        controller_.last_pipeline_traces(),
        has_latest_vision_result_ ? &latest_vision_result_ : nullptr,
        aiming);
    if (perf_summary_logger_.enabled()) {
        const std::uint64_t output_sent_ns = steady_time_point_ns(vigem_update_finished);
        PerfControllerWindowSample controller_sample;
        controller_sample.timestamp_ns = output_sent_ns;
        controller_sample.aiming = aiming;
        controller_sample.output_delivered =
            !config_.output.enabled || output_result.delivered;
        controller_sample.tick_ms = std::chrono::duration<double, std::milli>(
            vigem_update_finished - tick_started).count();
        controller_sample.pipeline_ms = std::chrono::duration<double, std::milli>(
            vigem_update_started - controller_pipeline_started).count();
        controller_sample.vigem_ms = std::chrono::duration<double, std::milli>(
            vigem_update_finished - vigem_update_started).count();
        perf_summary_logger_.record_controller(controller_sample);

        if (telemetry_new_vision && latest_vision_result_.frame_updated) {
            const auto& result = latest_vision_result_;
            const std::uint64_t capture_copy_complete_ns =
                result.capture_copy_complete_ns != 0
                ? result.capture_copy_complete_ns : result.captured_at_ns;
            PerfVisionWindowSample vision_sample;
            vision_sample.aiming = aiming;
            vision_sample.accumulated_frames = result.accumulated_frames;
            vision_sample.capture_to_result_ms = elapsed_ms_or_invalid(
                result.capture_acquire_begin_ns, result.result_at_ns);
            vision_sample.copy_to_result_ms = elapsed_ms_or_invalid(
                capture_copy_complete_ns, result.result_at_ns);
            vision_sample.source_present_to_result_ms =
                result.source_present_steady_available
                ? elapsed_ms_or_invalid(
                      result.source_present_steady_ns, result.result_at_ns)
                : -1.0;
            vision_sample.result_to_controller_ms = elapsed_ms_or_invalid(
                result.result_at_ns, latest_controller_consume_started_ns_);
            vision_sample.result_to_vigem_ms = elapsed_ms_or_invalid(
                result.result_at_ns, output_sent_ns);
            vision_sample.vision_publish_to_vigem_ms =
                latest_vision_publish_available_
                ? elapsed_ms_or_invalid(latest_vision_publish_ns_, output_sent_ns)
                : -1.0;
            vision_sample.controller_consume_to_vigem_ms = elapsed_ms_or_invalid(
                latest_controller_consume_started_ns_, output_sent_ns);
            const auto& control_trace = controller_.last_acquisition_trace();
            vision_sample.controller_submit_to_final_output_ms = elapsed_ms_or_invalid(
                latest_controller_submit_complete_ns_,
                control_trace.final_output_ready_ns);
            vision_sample.final_output_to_vigem_ms = elapsed_ms_or_invalid(
                control_trace.final_output_ready_ns, output_sent_ns);
            vision_sample.source_present_to_vigem_ms =
                result.source_present_steady_available
                ? elapsed_ms_or_invalid(result.source_present_steady_ns, output_sent_ns)
                : -1.0;
            vision_sample.cuda_map_ms = result.cuda_map_ms;
            vision_sample.preprocess_ms = result.preprocess_ms;
            vision_sample.infer_ms = result.infer_ms;
            vision_sample.gpu_total_ms = result.gpu_total_ms;
            vision_sample.output_copy_sync_ms = result.output_copy_sync_ms;
            vision_sample.output_copy_ms = result.output_copy_ms;
            vision_sample.output_wait_ms = result.output_wait_ms;
            vision_sample.sync_queue_residual_ms = std::max(
                0.0,
                static_cast<double>(result.output_wait_ms) -
                    static_cast<double>(result.gpu_total_ms));
            vision_sample.color_copy_ms = result.color_copy_required
                ? result.color_copy_ms : -1.0;
            vision_sample.cuda_unmap_ms = result.cuda_unmap_ms;
            perf_summary_logger_.record_vision(vision_sample);
        }
    }
    ++tick_count_;
    if (telemetry_new_vision) {
        const auto& trace = controller_.last_acquisition_trace();
        TelemetryAcquisitionTraceInput acquisition_trace;
        acquisition_trace.source_frame_id = trace.source_frame_id;
        acquisition_trace.source_observation_id = trace.source_observation_id;
        acquisition_trace.persistent_target_id = trace.persistent_target_id;
        acquisition_trace.physical_ads_epoch = trace.physical_ads_epoch;
        acquisition_trace.target_acquisition_id = trace.target_acquisition_id;
        acquisition_trace.controller_tick_id = tick_count_;
        acquisition_trace.capture_acquire_begin_ns =
            latest_vision_result_.capture_acquire_begin_ns;
        acquisition_trace.capture_acquire_complete_ns =
            latest_vision_result_.capture_acquire_complete_ns;
        acquisition_trace.capture_copy_complete_ns =
            latest_vision_result_.capture_copy_complete_ns != 0
            ? latest_vision_result_.capture_copy_complete_ns
            : latest_vision_result_.captured_at_ns;
        acquisition_trace.accumulated_frames =
            latest_vision_result_.accumulated_frames;
        acquisition_trace.ads_acquisition_begin_ns =
            trace.ads_acquisition_begin_ns;
        acquisition_trace.ads_acquisition_complete_ns =
            trace.ads_acquisition_complete_ns;
        acquisition_trace.result_ready_ns = latest_vision_result_.result_at_ns;
        acquisition_trace.vision_publish_ns = latest_vision_publish_ns_;
        acquisition_trace.vision_publish_available =
            latest_vision_publish_available_;
        acquisition_trace.controller_submit_complete_ns =
            latest_controller_submit_complete_ns_;
        acquisition_trace.controller_consume_ns =
            latest_controller_consume_started_ns_;
        acquisition_trace.plan_decision_ns = trace.plan_decision_ns;
        acquisition_trace.final_output_ready_ns = trace.final_output_ready_ns;
        acquisition_trace.vigem_submit_complete_ns =
            steady_time_point_ns(vigem_update_finished);
        acquisition_trace.source_present_qpc =
            latest_vision_result_.source_present_qpc;
        acquisition_trace.source_present_qpc_frequency =
            latest_vision_result_.source_present_qpc_frequency;
        acquisition_trace.source_present_available =
            latest_vision_result_.source_present_available;
        acquisition_trace.source_present_steady_ns =
            latest_vision_result_.source_present_steady_ns;
        acquisition_trace.source_present_calibration_id =
            latest_vision_result_.source_present_calibration_id;
        acquisition_trace.source_present_calibration_uncertainty_ns =
            latest_vision_result_.source_present_calibration_uncertainty_ns;
        acquisition_trace.source_present_steady_available =
            latest_vision_result_.source_present_steady_available;
        acquisition_trace.plan_admitted = trace.plan_admitted;
        acquisition_trace.acquisition_active = trace.acquisition_active;
        acquisition_trace.acquisition_exists = trace.acquisition_exists;
        acquisition_trace.preferred_source_id = trace.preferred_source_id;
        acquisition_trace.selected_source_id = trace.selected_source_id;
        acquisition_trace.candidate_count = trace.candidate_count;
        acquisition_trace.acquisition_state =
            static_cast<std::uint8_t>(trace.acquisition_state);
        acquisition_trace.decision_reason =
            static_cast<std::uint8_t>(trace.decision_reason);
        acquisition_trace.source_decision_available =
            trace.source_decision_available;
        acquisition_trace.source_decision_outcome =
            static_cast<std::uint8_t>(trace.source_decision_outcome);
        acquisition_trace.source_decision_reason =
            static_cast<std::uint8_t>(trace.source_decision_reason);
        acquisition_trace.acquisition_terminal_reason =
            static_cast<std::uint8_t>(trace.acquisition_terminal_reason);
        acquisition_trace.selector_target_generation =
            trace.selector_target_generation;
        acquisition_trace.selector_target_changed =
            trace.selector_target_changed;
        acquisition_trace.effective_activation_radius_px =
            trace.effective_activation_radius_px;
        acquisition_trace.raw_error_x = trace.raw_error_px.x;
        acquisition_trace.raw_error_y = trace.raw_error_px.y;
        acquisition_trace.target_size_x = trace.target_size_px.x;
        acquisition_trace.target_size_y = trace.target_size_px.y;
        acquisition_trace.requested_ai_x = trace.requested_ai.x;
        acquisition_trace.requested_ai_y = trace.requested_ai.y;
        acquisition_trace.shaped_ai_x = trace.shaped_ai.x;
        acquisition_trace.shaped_ai_y = trace.shaped_ai.y;
        acquisition_trace.fused_output_x = trace.fused_output.x;
        acquisition_trace.fused_output_y = trace.fused_output.y;
        acquisition_trace.post_output_x = trace.post_output.x;
        acquisition_trace.post_output_y = trace.post_output.y;
        acquisition_trace.has_first_requested_ai =
            trace.has_first_requested_ai;
        acquisition_trace.has_first_shaped_ai = trace.has_first_shaped_ai;
        acquisition_trace.has_first_fused_output =
            trace.has_first_fused_output;
        acquisition_trace.first_requested_ai_ns =
            trace.first_requested_ai_ns;
        acquisition_trace.first_shaped_ai_ns = trace.first_shaped_ai_ns;
        acquisition_trace.first_fused_output_ns =
            trace.first_fused_output_ns;
        acquisition_trace.first_requested_ai_x =
            trace.first_requested_ai.x;
        acquisition_trace.first_requested_ai_y =
            trace.first_requested_ai.y;
        acquisition_trace.first_shaped_ai_x = trace.first_shaped_ai.x;
        acquisition_trace.first_shaped_ai_y = trace.first_shaped_ai.y;
        acquisition_trace.first_fused_output_x = trace.first_fused_output.x;
        acquisition_trace.first_fused_output_y = trace.first_fused_output.y;
        telemetry_collectors_.observe_acquisition_trace(acquisition_trace);
    }
    const auto& telemetry_components = controller_.last_output_components();
    TelemetryTickInput telemetry_tick;
    telemetry_tick.tick_id = tick_count_;
    telemetry_tick.physical_read_ns = physical_read_at_ns;
    telemetry_tick.controller_consume_ns = latest_controller_consume_started_ns_;
    telemetry_tick.output_sent_ns = steady_time_point_ns(vigem_update_finished);
    telemetry_tick.sample_ns = steady_time_point_ns(vigem_update_finished);
    telemetry_tick.aiming = aiming;
    const auto& telemetry_vision_state = controller_.last_frame_vision_state();
    telemetry_tick.physical_connected = physical.connected;
    telemetry_tick.current_observed_target_present =
        telemetry_vision_state.current_observed_target_present;
    telemetry_tick.output_delivered = !config_.output.enabled || output_result.delivered;
    telemetry_tick.output_disabled = !config_.output.enabled;
    telemetry_tick.output_backend_connected =
        !config_.output.enabled || output_result.backend_connected;
    telemetry_tick.output_error_code = output_result.error_code;
    telemetry_tick.input_reconnect_count = sdl_reconnect_count_;
    telemetry_tick.output_reconnect_count = output_result.reconnect_count;
    telemetry_tick.aim_authority = telemetry_vision_state.aim_authority;
    telemetry_tick.fire_authority = telemetry_vision_state.fire_authority;
    telemetry_tick.aim_mode = controller_.last_ai_aim_mode().c_str();
    telemetry_tick.left_trigger = physical.left_trigger;
    telemetry_tick.right_trigger = physical.right_trigger;
    telemetry_tick.physical_x = physical.right_x;
    telemetry_tick.physical_y = physical.right_y;
    telemetry_tick.physical_left_x = physical.left_x;
    telemetry_tick.physical_left_y = physical.left_y;
    telemetry_tick.manual_x = telemetry_components.manual_stick.x;
    telemetry_tick.manual_y = telemetry_components.manual_stick.y;
    telemetry_tick.filtered_manual_x =
        telemetry_components.filtered_manual_stick.x;
    telemetry_tick.filtered_manual_y =
        telemetry_components.filtered_manual_stick.y;
    telemetry_tick.manual_confidence = telemetry_components.manual_confidence;
    telemetry_tick.ai_x = telemetry_components.ai_aim_stick.x;
    telemetry_tick.ai_y = telemetry_components.ai_aim_stick.y;
    telemetry_tick.target_final_x = telemetry_components.target_final_stick.x;
    telemetry_tick.target_final_y = telemetry_components.target_final_stick.y;
    telemetry_tick.ai_correction_x = telemetry_components.ai_correction_stick.x;
    telemetry_tick.ai_correction_y = telemetry_components.ai_correction_stick.y;
    telemetry_tick.manual_authority_mode =
        telemetry_components.manual_authority_mode;
    telemetry_tick.assist_control_phase =
        telemetry_components.assist_control_phase.c_str();
    telemetry_tick.operation_class =
        telemetry_components.operation_class.c_str();
    telemetry_tick.operation_confidence =
        telemetry_components.operation_confidence;
    telemetry_tick.direction_trust = telemetry_components.direction_trust;
    telemetry_tick.recoil_pull_strength =
        telemetry_components.recoil_pull_strength;
    telemetry_tick.manual_passthrough_x =
        telemetry_components.manual_passthrough_x;
    telemetry_tick.manual_passthrough_y =
        telemetry_components.manual_passthrough_y;
    telemetry_tick.manual_correction_x =
        telemetry_components.manual_correction_x;
    telemetry_tick.manual_correction_y =
        telemetry_components.manual_correction_y;
    telemetry_tick.manual_boundary_x = telemetry_components.manual_boundary_x;
    telemetry_tick.manual_boundary_y = telemetry_components.manual_boundary_y;
    telemetry_tick.manual_exit_requested =
        telemetry_components.manual_exit_requested;
    telemetry_tick.handover_requested =
        telemetry_components.handover_requested;
    telemetry_tick.handover_braking = telemetry_components.handover_braking;
    telemetry_tick.bodylock_error_rate_x =
        telemetry_components.bodylock_error_rate_px_per_sec.x;
    telemetry_tick.bodylock_error_rate_y =
        telemetry_components.bodylock_error_rate_px_per_sec.y;
    telemetry_tick.bodylock_position_stick_x =
        telemetry_components.bodylock_position_stick.x;
    telemetry_tick.bodylock_position_stick_y =
        telemetry_components.bodylock_position_stick.y;
    telemetry_tick.bodylock_motion_stick_x =
        telemetry_components.bodylock_motion_stick.x;
    telemetry_tick.bodylock_motion_stick_y =
        telemetry_components.bodylock_motion_stick.y;
    telemetry_tick.bodylock_effective_motion_stick_x =
        telemetry_components.bodylock_effective_motion_stick.x;
    telemetry_tick.bodylock_effective_motion_stick_y =
        telemetry_components.bodylock_effective_motion_stick.y;
    telemetry_tick.bodylock_radial_motion_bound =
        telemetry_components.bodylock_radial_motion_bound;
    telemetry_tick.bodylock_constraint_reason =
        telemetry_components.bodylock_constraint_reason.c_str();
    telemetry_tick.requested_assist_x = telemetry_components.requested_assist_stick.x;
    telemetry_tick.requested_assist_y = telemetry_components.requested_assist_stick.y;
    telemetry_tick.shaped_assist_x = telemetry_components.shaped_assist_stick.x;
    telemetry_tick.shaped_assist_y = telemetry_components.shaped_assist_stick.y;
    telemetry_tick.auto_fire_requested = telemetry_components.auto_fire_requested;
    telemetry_tick.auto_fire_aim_ready = telemetry_components.auto_fire_aim_ready;
    telemetry_tick.auto_fire_allowed = telemetry_components.auto_fire_allowed;
    telemetry_tick.auto_fire_active = telemetry_components.auto_fire_active;
    telemetry_tick.auto_fire_pulse_starts =
        telemetry_components.auto_fire_pulse_starts;
    telemetry_tick.auto_fire_pulse_pressed =
        telemetry_components.auto_fire_pulse_pressed;
    telemetry_tick.auto_fire_cadence_wait =
        telemetry_components.auto_fire_cadence_wait;
    telemetry_tick.final_fire_button = telemetry_components.fire_button;
    telemetry_tick.auto_fire_block_reason =
        telemetry_components.auto_fire_block_reason.c_str();
    const auto enemy_mark_status =
        person_detection_gesture_.status(tick_started);
    telemetry_tick.enemy_mark_request_pending =
        config_.gamepad.enemy_mark.enabled &&
        enemy_mark_status.request_pending;
    telemetry_tick.enemy_mark_synthetic_pressed =
        config_.gamepad.enemy_mark.enabled &&
        enemy_mark_status.synthetic_pressed;
    telemetry_tick.enemy_mark_fired =
        config_.gamepad.enemy_mark.enabled &&
        enemy_mark_status.fired_this_tick;
    telemetry_tick.enemy_mark_canceled =
        config_.gamepad.enemy_mark.enabled &&
        enemy_mark_status.canceled_this_tick;
    telemetry_tick.enemy_mark_confirmation_frames =
        enemy_mark_status.confirmation_frames;
    telemetry_tick.enemy_mark_target_scope =
        enemy_mark_status.evaluated_scope;
    telemetry_tick.enemy_mark_target_generation =
        enemy_mark_status.evaluated_generation;
    telemetry_tick.enemy_mark_last_scope =
        enemy_mark_status.last_marked_scope;
    telemetry_tick.enemy_mark_last_generation =
        enemy_mark_status.last_marked_generation;
    telemetry_tick.enemy_mark_block_reason =
        config_.gamepad.enemy_mark.enabled
        ? person_mark_block_reason_name(enemy_mark_status.block_reason)
        : "disabled";
    telemetry_tick.pre_recoil_x = telemetry_components.before_recoil_stick.x;
    telemetry_tick.pre_recoil_y = telemetry_components.before_recoil_stick.y;
    telemetry_tick.recoil_x = telemetry_components.recoil_stick.x;
    telemetry_tick.recoil_y = telemetry_components.recoil_stick.y;
    telemetry_tick.final_x = telemetry_components.final_stick.x;
    telemetry_tick.final_y = telemetry_components.final_stick.y;
    telemetry_tick.observed_error_x = telemetry_components.observed_error_px.x;
    telemetry_tick.observed_error_y = telemetry_components.observed_error_px.y;
    telemetry_tick.control_error_x = telemetry_components.control_error_px.x;
    telemetry_tick.control_error_y = telemetry_components.control_error_px.y;
    telemetry_tick.source_aim_x = telemetry_components.source_aim_px.x;
    telemetry_tick.source_aim_y = telemetry_components.source_aim_px.y;
    telemetry_tick.desired_aim_x = telemetry_components.desired_aim_px.x;
    telemetry_tick.desired_aim_y = telemetry_components.desired_aim_px.y;
    telemetry_tick.desired_point_u =
        telemetry_components.desired_point_normalized.x;
    telemetry_tick.desired_point_v =
        telemetry_components.desired_point_normalized.y;
    telemetry_tick.aim_region_x1 = telemetry_components.aim_region_px.x;
    telemetry_tick.aim_region_y1 = telemetry_components.aim_region_px.y;
    telemetry_tick.aim_region_x2 = telemetry_components.aim_region_px.x +
        telemetry_components.aim_region_px.w;
    telemetry_tick.aim_region_y2 = telemetry_components.aim_region_px.y +
        telemetry_components.aim_region_px.h;
    telemetry_tick.has_aim_region = telemetry_components.has_aim_region;
    telemetry_tick.visual_authority = telemetry_components.visual_authority;
    telemetry_tick.enemy_cue_current = telemetry_components.enemy_cue_current;
    telemetry_tick.enemy_identity_confirmed =
        telemetry_components.enemy_identity_confirmed;
    telemetry_tick.enemy_cue_checked = telemetry_components.enemy_cue_checked;
    telemetry_tick.aim_region_source =
        telemetry_components.aim_region_source.c_str();
    telemetry_tick.desired_point_source =
        telemetry_components.desired_point_source.c_str();
    telemetry_tick.final_left_x = output.left_x;
    telemetry_tick.final_left_y = output.left_y;
    telemetry_tick.output_saturated =
        std::fabs(output.right_x) >= 0.999f ||
        std::fabs(output.right_y) >= 0.999f;
    telemetry_tick.selected_track_id = telemetry_vision_state.selected_track_id;
    telemetry_tick.selected_observation_id =
        telemetry_vision_state.selected_observation_id;
    telemetry_tick.assist_authority = telemetry_components.assist_authority.c_str();
    telemetry_tick.assist_authority_reason =
        telemetry_components.assist_authority_reason.c_str();
    telemetry_tick.bodylock_lifecycle =
        telemetry_components.bodylock_lifecycle.c_str();
    telemetry_tick.assist_limit_reason =
        telemetry_components.assist_limit_reason.c_str();
    telemetry_collectors_.observe_tick(telemetry_tick);
    if (telemetry_new_vision) {
        const auto committed = adapt_committed_capture_observation(
            latest_vision_result_,
            controller_.last_target_plan(),
            controller_.ads_epoch(),
            latest_controller_consume_started_ns_);
        telemetry_collectors_.observe_committed_capture(committed);
    }
    const bool log_vision = perf_log_ && should_log_vision_tick(tick_count_);
    const bool log_gamepad_perf = gamepad_perf_log_ && should_log_vision_tick(tick_count_);
    if (log_vision || log_gamepad_perf) {
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
        if (log_vision) {
            log_vision_result(
                result,
                latest_vision_aiming_,
                tick_count_);
        }
    }
}

controller_native::PhysicalGamepadState RuntimeLoop::read_physical_gamepad() {
    if (sdl_input_reader_ != nullptr) {
        controller_native::PhysicalGamepadState state = sdl_input_reader_->read();
        if (state.connected) {
            sdl_reconnect_throttle_.record_success();
            return state;
        }

        const auto now = std::chrono::steady_clock::now();
        if (sdl_reconnect_throttle_.should_attempt(now)) {
            if (sdl_input_reader_->reconnect()) {
                ++sdl_reconnect_count_;
                sdl_reconnect_throttle_.record_success();
                std::cout << "[NativeRuntime][Input] SDL physical gamepad recovered"
                          << " index=" << sdl_input_reader_->device_index()
                          << " name=\"" << sdl_input_reader_->device_name() << "\""
                          << " reconnect_count=" << sdl_reconnect_count_ << '\n';
                return sdl_input_reader_->read();
            }
            sdl_reconnect_throttle_.record_failure(now);
            const unsigned int failures = sdl_reconnect_throttle_.failure_count();
            if (failures == 1 || failures % 10 == 0) {
                std::cerr << "[NativeRuntime][Input] SDL physical gamepad detached;"
                          << " waiting for the original device"
                          << " name=\"" << sdl_input_reader_->device_name() << "\""
                          << " attempts=" << failures << '\n';
            }
        }
        return state;
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

bool RuntimeLoop::should_stop_requested() const {
    return stop_requested_.load();
}

}  // namespace runtime_app
