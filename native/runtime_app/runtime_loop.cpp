#include "runtime_loop.h"

#include "runtime_timing.h"
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

RuntimeTelemetryOptions telemetry_options_from(
    const controller_native::RuntimeConfig& config,
    const std::filesystem::path& session_directory) {
    RuntimeTelemetryOptions options;
    // The legacy aim performance switch is a compatibility alias for the
    // asynchronous telemetry pipeline.  It must never resurrect the old
    // synchronous file writer on the 1 ms controller thread.
    options.enabled = config.telemetry.enabled || config.vision.aim_perf_file_log;
    options.directory = session_directory.empty()
        ? std::filesystem::path(config.vision.aim_perf_log_dir)
        : session_directory;
    options.queue_capacity = config.telemetry.queue_capacity;
    options.rotate_size_bytes =
        static_cast<std::size_t>(config.telemetry.rotate_size_mb) * 1024ull * 1024ull;
    options.max_files = config.telemetry.max_files;
    return options;
}

LogSessionOptions log_session_options_from(const controller_native::RuntimeConfig& config) {
    LogSessionOptions options;
    options.enabled = config.telemetry.enabled || config.vision.aim_perf_file_log;
    options.root = config.vision.aim_perf_log_dir;
    options.git_commit = config.build_commit;
    options.config_hash = config.source_config_sha256;
    options.engine_hash = config.engine_sha256;
    options.executable_sha256 = config.executable_sha256;
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

double elapsed_ms_or_invalid(std::uint64_t start_ns, std::uint64_t end_ns) {
    if (start_ns == 0 || end_ns <= start_ns) return -1.0;
    return static_cast<double>(end_ns - start_ns) / 1'000'000.0;
}

double elapsed_qpc_ms_or_invalid(
    std::uint64_t start_qpc,
    std::uint64_t end_qpc,
    std::uint64_t qpc_frequency) {
    if (start_qpc == 0 || end_qpc <= start_qpc || qpc_frequency == 0) {
        return -1.0;
    }
    return static_cast<double>(
        static_cast<long double>(end_qpc - start_qpc) * 1000.0L /
        static_cast<long double>(qpc_frequency));
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

    void set_controller_aiming(bool aiming) override {
        engine_->set_controller_aiming(aiming);
    }

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
      perf_summary_logger_(PerfSummaryOptions{
          config_.performance.enabled,
          config_.performance.interval_ms,
          std::filesystem::path(config_.performance.directory),
          config_.performance.stdout_enabled,
          config_.vision.cuda_submit_phase_us,
          config_.vision.cuda_submit_phase_mode}),
      log_session_manager_(log_session_options_from(config_)),
      telemetry_(telemetry_options_from(config_, log_session_manager_.session_directory())),
      telemetry_collectors_(
          config_.telemetry.enabled || config_.vision.aim_perf_file_log ||
              config_.control_learning.enabled,
          &telemetry_,
          TelemetrySessionContext{
              config_.build_commit.c_str(),
              config_.source_config_sha256.c_str(),
              config_.engine_sha256.c_str(),
              config_.executable_sha256.c_str(),
              tracking_native::tracker_backend_kind_name(config_.gamepad.tracker_backend).data(),
              config_.vision.capture_width,
              config_.vision.capture_height,
              config_.vision.gpu_service_active_fps,
              config_.vision.gpu_service_idle_fps,
              config_.scheduler.controller_tick_hz,
              config_.telemetry.manual_controller_hz}),
      aim_perf_file_logger_(
          false,
          config_.vision.aim_perf_log_dir,
          config_.vision.aim_perf_log_interval_ticks),
      downward_diagnostics_(DownwardPullDiagnostics::from_environment()),
      perf_log_(perf_log),
      gamepad_perf_log_(gamepad_perf_log_enabled(perf_log)),
      sdl_input_reader_(open_sdl_input_reader()),
      input_reader_(select_xinput_user_index(config_.gamepad, sdl_input_reader_ == nullptr)),
      controller_(config_.gamepad),
      viewport_controller_(viewport_controller_config_from(config_)),
      virtual_gamepad_(),
      vision_delivery_gate_(config_.gamepad.tracker.max_observation_age_ms) {
    telemetry_.start();
    if (config_.control_learning.enabled &&
        config_.control_learning.mode !=
            controller_native::ControlLearningMode::Disabled) {
        causal_response_learner_ =
            std::make_unique<control_learning::CausalOnlineResponseLearner>();
    }
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
        0,
        config_.vision.model_path,
        config_.vision.color_readback_mode,
        config_.vision.tensor_width,
        config_.vision.tensor_height,
        config_.vision.require_isotropic_resize,
        config_.vision.ego_motion_enabled,
        config_.vision.cuda_submit_phase_us,
        config_.vision.cuda_submit_phase_mode == "adaptive");
    std::cout << "[VisionGeometry][CPP]"
              << " capture=" << vision_engine->width() << 'x' << vision_engine->height()
              << " tensor=" << vision_engine->tensor_width() << 'x'
              << vision_engine->tensor_height()
              << " scale=" << vision_engine->resize_scale_x() << 'x'
              << vision_engine->resize_scale_y()
              << " isotropic=" << (vision_engine->resize_isotropic() ? 1 : 0)
              << " ego_motion=" << (vision_engine->ego_motion_enabled() ? "shadow" : "off")
              << " cuda_submit_phase_mode=" << vision_engine->cuda_submit_phase_mode()
              << " cuda_submit_phase_us=" << vision_engine->cuda_submit_phase_us()
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
        service_options.active_fps = static_cast<double>(config_.vision.gpu_service_active_fps);
        service_options.idle_fps = static_cast<double>(config_.vision.gpu_service_idle_fps);
        service_options.keepwarm_when_idle = config_.vision.gpu_service_keepwarm_when_idle;
        service_options.repeat_last_on_no_update =
            config_.vision.gpu_service_repeat_last_on_no_update;
        vision_service_ = std::make_unique<VisionService>(
            std::make_unique<VisionEngineServicePoller>(std::move(vision_engine)),
            service_options);
        vision_service_->set_viewport(initial_viewport);
        vision_service_->start();
        std::cout << "[VisionService][CPP] enabled"
                  << " active_fps=" << config_.vision.gpu_service_active_fps
                  << " idle_fps=" << config_.vision.gpu_service_idle_fps
                  << " keepwarm=" << (config_.vision.gpu_service_keepwarm_when_idle ? 1 : 0)
                  << " repeat_last=" << (config_.vision.gpu_service_repeat_last_on_no_update ? 1 : 0)
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
    using controller_native::GamepadOutputState;
    (void)set_current_thread_priority(RuntimeThreadPriority::AboveNormal);

    const int tick_hz = std::max(1, config_.scheduler.controller_tick_hz);
    const auto tick_interval = std::chrono::nanoseconds(1'000'000'000ll / tick_hz);
    AbsoluteDeadlineState deadlines(std::chrono::steady_clock::now(), tick_interval);
    PrecisionTickScheduler precision_scheduler(config_.scheduler.spin_tail_us);
    std::cout << "[NativeRuntime] controller_scheduler="
              << (config_.scheduler.mode == "legacy" ? "legacy" : precision_scheduler.mode_name())
              << " tick_hz=" << tick_hz << '\n';
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
    if (vision_service_ != nullptr) {
        vision_service_->stop();
    }
    perf_summary_logger_.stop();
    telemetry_collectors_.shutdown(steady_time_point_ns(std::chrono::steady_clock::now()));
    telemetry_.stop();
    log_session_manager_.close();
    if (config_.output.enabled) {
        virtual_gamepad_.update(GamepadOutputState{});
    }
    return 0;
}

void RuntimeLoop::request_stop() {
    stop_requested_.store(true);
}

void RuntimeLoop::run_once() {
    const auto tick_started = std::chrono::steady_clock::now();
    const controller_native::PhysicalGamepadState physical = read_physical_gamepad();
    const std::uint64_t physical_read_at_ns = steady_time_point_ns(std::chrono::steady_clock::now());
    const bool aiming = is_aiming(physical);
    latest_vision_aiming_ = aiming;
    const pipeline_contract::UserAimIntent user_aim_intent = build_user_aim_intent(
        physical,
        aiming,
        static_cast<std::uint64_t>(tick_count_) + 1u,
        tick_started);

    auto publish_fusion_if_updated = [&](const vision_native::VisionResult& result) {
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
    };

    bool telemetry_new_vision = false;
    bool viewport_fresh_vision = false;
    if (vision_service_ != nullptr) {
        const std::uint64_t expected_aim_transition_sequence =
            vision_service_->set_aiming(aiming);
        vision_service_->set_user_aim_intent(user_aim_intent);
        const VisionServiceSnapshot service_snapshot = vision_service_->latest_snapshot();
        if (
            service_snapshot.sequence != 0 &&
            service_snapshot.sequence != latest_vision_service_sequence_) {
            latest_vision_service_sequence_ = service_snapshot.sequence;
            vision_native::VisionResult result = service_snapshot.result;
            const auto controller_consume_started = std::chrono::steady_clock::now();
            const std::uint64_t controller_consume_ns =
                steady_time_point_ns(controller_consume_started);
            const bool current_control_epoch =
                service_snapshot.controller_aiming == aiming &&
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
                publish_fusion_if_updated(result);
            }
        }
    } else {
        vision_engine_->set_controller_aiming(aiming);
        vision_engine_->set_aiming(aiming);
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
                publish_fusion_if_updated(result);
            }
        }
    }
    if (telemetry_new_vision) {
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
        vision.projected = latest_vision_result_.has_target && !latest_vision_result_.frame_updated;
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
    const auto controller_pipeline_started = std::chrono::steady_clock::now();
    controller_native::GamepadOutputState output = controller_.build_output(physical);
    pipeline_contract::CommittedCaptureObservation viewport_observation;
    const pipeline_contract::CommittedCaptureObservation* viewport_observation_ptr = nullptr;
    if (viewport_fresh_vision) {
        viewport_observation = adapt_committed_capture_observation(
            latest_vision_result_,
            controller_.last_target_plan(),
            config_.gamepad.tracker.aim_height_ratio,
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
        is_aiming(physical));
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
    controller_.report_output_delivery(
        !config_.output.enabled || output_result.delivered,
        config_.output.enabled,
        steady_time_seconds(vigem_update_finished).value);
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
            vision_sample.cuda_submit_wait_applied =
                result.cuda_submit_wait_applied;
            vision_sample.cuda_submit_cycle_wrapped =
                result.cuda_submit_cycle_wrapped;
            vision_sample.cuda_submit_target_reached =
                result.cuda_submit_target_reached;
            vision_sample.cuda_submit_target_phase_us =
                result.cuda_submit_phase_us;
            vision_sample.cuda_submit_estimated_period_us =
                result.cuda_submit_estimated_period_us;
            vision_sample.cuda_submit_held_phase_us =
                result.cuda_submit_held_phase_us;
            vision_sample.cuda_submit_adaptation_epoch =
                result.cuda_submit_adaptation_epoch;
            vision_sample.cuda_submit_adaptive_state =
                result.cuda_submit_adaptive_state_code;
            vision_sample.cuda_submit_adaptive_reason =
                result.cuda_submit_adaptive_reason_code;
            vision_sample.cuda_submit_cadence_stable =
                result.cuda_submit_cadence_stable;
            vision_sample.accumulated_frames = result.accumulated_frames;
            vision_sample.source_present_steady_ns =
                result.source_present_steady_available
                ? result.source_present_steady_ns : 0;
            vision_sample.gpu_complete_at_ns = result.gpu_complete_at_ns;
            vision_sample.source_present_qpc = result.source_present_available
                ? result.source_present_qpc : 0;
            vision_sample.source_present_qpc_frequency =
                result.source_present_available
                ? result.source_present_qpc_frequency : 0;
            vision_sample.gpu_complete_qpc = result.gpu_complete_qpc;
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
            vision_sample.source_present_to_vigem_ms =
                result.source_present_steady_available
                ? elapsed_ms_or_invalid(result.source_present_steady_ns, output_sent_ns)
                : -1.0;
            vision_sample.source_present_to_cuda_map_begin_ms =
                result.source_present_available &&
                    result.cuda_map_begin_qpc != 0
                ? elapsed_qpc_ms_or_invalid(
                      result.source_present_qpc,
                      result.cuda_map_begin_qpc,
                      result.source_present_qpc_frequency)
                : (result.source_present_steady_available
                ? elapsed_ms_or_invalid(
                      result.source_present_steady_ns,
                      result.cuda_map_begin_ns)
                : -1.0);
            vision_sample.source_present_to_cuda_map_complete_ms =
                result.source_present_available &&
                    result.cuda_map_complete_qpc != 0
                ? elapsed_qpc_ms_or_invalid(
                      result.source_present_qpc,
                      result.cuda_map_complete_qpc,
                      result.source_present_qpc_frequency)
                : (result.source_present_steady_available
                ? elapsed_ms_or_invalid(
                      result.source_present_steady_ns,
                      result.cuda_map_complete_ns)
                : -1.0);
            vision_sample.source_present_to_cuda_submit_ms =
                result.source_present_available &&
                    result.cuda_submit_begin_qpc != 0
                ? elapsed_qpc_ms_or_invalid(
                      result.source_present_qpc,
                      result.cuda_submit_begin_qpc,
                      result.source_present_qpc_frequency)
                : (result.source_present_steady_available
                ? elapsed_ms_or_invalid(
                      result.source_present_steady_ns,
                      result.cuda_submit_begin_ns)
                : -1.0);
            vision_sample.source_present_to_gpu_complete_ms =
                result.source_present_available &&
                    result.gpu_complete_qpc != 0
                ? elapsed_qpc_ms_or_invalid(
                      result.source_present_qpc,
                      result.gpu_complete_qpc,
                      result.source_present_qpc_frequency)
                : (result.source_present_steady_available
                ? elapsed_ms_or_invalid(
                      result.source_present_steady_ns,
                      result.gpu_complete_at_ns)
                : -1.0);
            vision_sample.copy_to_cuda_submit_ms = elapsed_ms_or_invalid(
                capture_copy_complete_ns, result.cuda_submit_begin_ns);
            vision_sample.cuda_submit_wait_ms = result.cuda_submit_wait_ms;
            vision_sample.cuda_submit_phase_late_ms =
                result.cuda_submit_target_deadline_qpc != 0 &&
                    result.cuda_submit_begin_qpc != 0
                ? elapsed_qpc_ms_or_invalid(
                      result.cuda_submit_target_deadline_qpc,
                      result.cuda_submit_begin_qpc,
                      result.source_present_qpc_frequency)
                : (result.cuda_submit_target_deadline_ns != 0
                ? elapsed_ms_or_invalid(
                      result.cuda_submit_target_deadline_ns,
                      result.cuda_submit_begin_ns)
                : (vision_sample.source_present_to_cuda_submit_ms >= 0.0
                ? std::max(
                      0.0,
                      vision_sample.source_present_to_cuda_submit_ms -
                          static_cast<double>(result.cuda_submit_phase_us) /
                              1000.0)
                : -1.0));
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
            vision_sample.ego_stage_ms = result.ego_motion_stage_ms > 0.0f
                ? result.ego_motion_stage_ms : -1.0;
            vision_sample.ego_compute_ms = result.ego_motion_shadow.available &&
                    result.ego_motion_shadow.compute_ms > 0.0f
                ? result.ego_motion_shadow.compute_ms : -1.0;
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
        const auto& ego = latest_vision_result_.ego_motion_shadow;
        if (ego.available) {
            TelemetryEgoMotionShadowInput ego_input;
            ego_input.available = ego.available;
            ego_input.valid = ego.valid;
            ego_input.invalid_reason = static_cast<std::uint8_t>(ego.invalid_reason);
            ego_input.result_sequence = ego.result_sequence;
            ego_input.previous_frame_id = ego.previous_frame_id;
            ego_input.current_frame_id = ego.current_frame_id;
            ego_input.previous_present_qpc = ego.previous_present_qpc;
            ego_input.current_present_qpc = ego.current_present_qpc;
            ego_input.previous_present_qpc_frequency =
                ego.previous_present_qpc_frequency;
            ego_input.current_present_qpc_frequency =
                ego.current_present_qpc_frequency;
            ego_input.present_qpc_frequency = ego.present_qpc_frequency;
            ego_input.previous_present_steady_ns = ego.previous_present_steady_ns;
            ego_input.current_present_steady_ns = ego.current_present_steady_ns;
            ego_input.previous_present_calibration_id =
                ego.previous_present_calibration_id;
            ego_input.current_present_calibration_id =
                ego.current_present_calibration_id;
            ego_input.previous_present_calibration_uncertainty_ns =
                ego.previous_present_calibration_uncertainty_ns;
            ego_input.current_present_calibration_uncertainty_ns =
                ego.current_present_calibration_uncertainty_ns;
            ego_input.previous_present_steady_available =
                ego.previous_present_steady_available;
            ego_input.current_present_steady_available =
                ego.current_present_steady_available;
            ego_input.present_clock_valid = ego.present_clock_valid;
            ego_input.previous_capture_copy_complete_ns =
                ego.previous_capture_copy_complete_ns;
            ego_input.current_capture_copy_complete_ns =
                ego.current_capture_copy_complete_ns;
            ego_input.previous_result_ns = ego.previous_result_ns;
            ego_input.current_result_ns = ego.current_result_ns;
            ego_input.observer_completed_at_ns = ego.observer_completed_at_ns;
            ego_input.result_age_at_take_ns = ego.result_age_at_take_ns;
            ego_input.background_dx = ego.background_dx;
            ego_input.background_dy = ego.background_dy;
            ego_input.camera_dx = ego.camera_dx;
            ego_input.camera_dy = ego.camera_dy;
            ego_input.confidence = ego.confidence;
            ego_input.valid_background_ratio = ego.valid_background_ratio;
            ego_input.residual_px = ego.residual_px;
            ego_input.compute_ms = ego.compute_ms;
            ego_input.inlier_count = ego.inlier_count;
            ego_input.sample_count = ego.sample_count;
            ego_input.search_radius_px = ego.search_radius_px;
            ego_input.boundary_hit_count = ego.boundary_hit_count;
            ego_input.boundary_hit_rate = ego.boundary_hit_rate;
            ego_input.boundary_consistent_hit_count =
                ego.boundary_consistent_hit_count;
            ego_input.boundary_consistent_hit_rate =
                ego.boundary_consistent_hit_rate;
            ego_input.observer_lifecycle_generation =
                ego.observer_lifecycle_generation;
            ego_input.submitted_frame_count = ego.submitted_frame_count;
            ego_input.pending_frame_replaced_count =
                ego.pending_frame_replaced_count;
            ego_input.pairs_processed_count = ego.pairs_processed_count;
            ego_input.unread_result_replaced_count =
                ego.unread_result_replaced_count;
            ego_input.duplicate_or_out_of_order_rejected_count =
                ego.duplicate_or_out_of_order_rejected_count;
            telemetry_collectors_.observe_ego_motion_shadow(
                latest_vision_result_.frame_id, tick_count_, ego_input);
        }
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
    telemetry_tick.fresh_vision_validated_manual_proposal_x =
        telemetry_components.intent_fusion_fresh_validated_manual_proposal.x;
    telemetry_tick.fresh_vision_validated_manual_proposal_y =
        telemetry_components.intent_fusion_fresh_validated_manual_proposal.y;
    telemetry_tick.fresh_vision_validated_ai_proposal_x =
        telemetry_components.intent_fusion_fresh_validated_ai_proposal.x;
    telemetry_tick.fresh_vision_validated_ai_proposal_y =
        telemetry_components.intent_fusion_fresh_validated_ai_proposal.y;
    telemetry_tick.fresh_vision_manual_radial_scale =
        telemetry_components.intent_fusion_fresh_manual_radial_scale;
    telemetry_tick.fresh_vision_wrong_way_policy_applied =
        telemetry_components.intent_fusion_fresh_vision_policy_applied;
    telemetry_tick.fresh_vision_ai_radial_bound_applied =
        telemetry_components.intent_fusion_fresh_ai_radial_bound;
    telemetry_tick.fresh_vision_ai_radial_scale =
        telemetry_components.intent_fusion_fresh_ai_radial_scale;
    telemetry_tick.fresh_vision_predictive_envelope_applied =
        telemetry_components.intent_fusion_predictive_envelope_applied;
    telemetry_tick.fresh_vision_escape_latched =
        telemetry_components.intent_fusion_fresh_escape_latched;
    telemetry_tick.fresh_vision_authoritative_error_x =
        telemetry_components.intent_fusion_fresh_authoritative_error_px.x;
    telemetry_tick.fresh_vision_authoritative_error_y =
        telemetry_components.intent_fusion_fresh_authoritative_error_px.y;
    telemetry_tick.fresh_vision_predicted_error_x =
        telemetry_components.intent_fusion_fresh_predicted_error_px.x;
    telemetry_tick.fresh_vision_predicted_error_y =
        telemetry_components.intent_fusion_fresh_predicted_error_px.y;
    telemetry_tick.fresh_vision_raw_manual_radial =
        telemetry_components.intent_fusion_fresh_raw_manual_radial;
    telemetry_tick.fresh_vision_raw_ai_radial =
        telemetry_components.intent_fusion_fresh_raw_ai_radial;
    telemetry_tick.fresh_vision_strongest_valid_radial =
        telemetry_components.intent_fusion_fresh_strongest_valid_radial;
    telemetry_tick.fresh_vision_stopping_radial =
        telemetry_components.intent_fusion_fresh_stopping_radial;
    telemetry_tick.fresh_vision_permitted_radial =
        telemetry_components.intent_fusion_fresh_permitted_radial;
    telemetry_tick.fresh_vision_pre_slew_radial =
        telemetry_components.intent_fusion_fresh_pre_slew_radial;
    telemetry_tick.fresh_vision_final_radial =
        telemetry_components.intent_fusion_fresh_final_radial;
    telemetry_tick.fresh_vision_horizon_seconds =
        telemetry_components.intent_fusion_fresh_horizon_seconds;
    telemetry_tick.fresh_vision_horizon_y_seconds =
        telemetry_components.intent_fusion_fresh_horizon_y_seconds;
    telemetry_tick.fresh_vision_max_force_x =
        telemetry_components.intent_fusion_fresh_max_force.x;
    telemetry_tick.fresh_vision_max_force_y =
        telemetry_components.intent_fusion_fresh_max_force.y;
    telemetry_tick.fresh_vision_envelope_target_x =
        telemetry_components.intent_fusion_fresh_envelope_target_stick.x;
    telemetry_tick.fresh_vision_envelope_target_y =
        telemetry_components.intent_fusion_fresh_envelope_target_stick.y;
    telemetry_tick.fresh_vision_envelope_reason =
        telemetry_components.intent_fusion_fresh_envelope_reason.c_str();
    telemetry_tick.fresh_vision_envelope_source =
        telemetry_components.intent_fusion_fresh_envelope_source.c_str();
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
    telemetry_tick.post_ai_x = telemetry_components.post_ai_stick.x;
    telemetry_tick.post_ai_y = telemetry_components.post_ai_stick.y;
    telemetry_tick.dynamic_adjustment_x = telemetry_components.dynamic_adjustment_stick.x;
    telemetry_tick.dynamic_adjustment_y = telemetry_components.dynamic_adjustment_stick.y;
    telemetry_tick.post_dynamic_x = telemetry_components.post_dynamic_stick.x;
    telemetry_tick.post_dynamic_y = telemetry_components.post_dynamic_stick.y;
    telemetry_tick.ads_brake_x = telemetry_components.ads_brake_stick.x;
    telemetry_tick.ads_brake_y = telemetry_components.ads_brake_stick.y;
    telemetry_tick.post_ads_brake_x = telemetry_components.post_ads_brake_stick.x;
    telemetry_tick.post_ads_brake_y = telemetry_components.post_ads_brake_stick.y;
    telemetry_tick.ads_carry_brake_x = telemetry_components.ads_carry_brake_stick.x;
    telemetry_tick.ads_carry_brake_y = telemetry_components.ads_carry_brake_stick.y;
    telemetry_tick.post_ads_carry_brake_x = telemetry_components.post_ads_carry_brake_stick.x;
    telemetry_tick.post_ads_carry_brake_y = telemetry_components.post_ads_carry_brake_stick.y;
    telemetry_tick.ads_brake_active = telemetry_components.ads_brake_active;
    telemetry_tick.ads_carry_brake_active = telemetry_components.ads_carry_brake_active;
    telemetry_tick.ads_completion_active = telemetry_components.ads_completion_active;
    telemetry_tick.ads_completion_stable_frames = telemetry_components.ads_completion_stable_frames;
    telemetry_tick.ads_completion_radius_px = telemetry_components.ads_completion_radius_px;
    telemetry_tick.ads_completion_required_frames = telemetry_components.ads_completion_required_frames;
    telemetry_tick.ads_completion_max_ms = telemetry_components.ads_completion_max_ms;
    telemetry_tick.ads_completion_reason = telemetry_components.ads_completion_reason.c_str();
    telemetry_tick.manual_takeover_active = controller_.body_lock_manual_takeover_active();
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
    telemetry_tick.pre_recoil_x = telemetry_components.before_recoil_stick.x;
    telemetry_tick.pre_recoil_y = telemetry_components.before_recoil_stick.y;
    telemetry_tick.recoil_x = telemetry_components.recoil_stick.x;
    telemetry_tick.recoil_y = telemetry_components.recoil_stick.y;
    telemetry_tick.final_x = telemetry_components.final_stick.x;
    telemetry_tick.final_y = telemetry_components.final_stick.y;
    telemetry_tick.remaining_work_x = telemetry_components.remaining_work_px.x;
    telemetry_tick.remaining_work_y = telemetry_components.remaining_work_px.y;
    telemetry_tick.delivered_camera_work_x =
        telemetry_components.delivered_camera_work_px.x;
    telemetry_tick.delivered_camera_work_y =
        telemetry_components.delivered_camera_work_px.y;
    telemetry_tick.remaining_work_confidence =
        telemetry_components.remaining_work_confidence;
    telemetry_tick.remaining_work_valid =
        telemetry_components.remaining_work_valid;
    telemetry_tick.final_left_x = output.left_x;
    telemetry_tick.final_left_y = output.left_y;
    telemetry_tick.output_saturated =
        std::fabs(output.right_x) >= 0.999f ||
        std::fabs(output.right_y) >= 0.999f;
    telemetry_tick.selected_track_id = telemetry_vision_state.selected_track_id;
    telemetry_tick.selected_observation_id =
        telemetry_vision_state.selected_observation_id;
    telemetry_tick.backing_frame_id =
        telemetry_vision_state.selected_backing_frame_id;
    telemetry_tick.track_observation_age_ms =
        telemetry_vision_state.track_observation_age_ms;
    telemetry_tick.track_position_sigma =
        telemetry_vision_state.track_position_sigma;
    telemetry_tick.track_ambiguity = telemetry_vision_state.track_ambiguity;
    telemetry_tick.assist_authority = telemetry_components.assist_authority.c_str();
    telemetry_tick.assist_authority_reason =
        telemetry_components.assist_authority_reason.c_str();
    telemetry_tick.bodylock_lifecycle =
        telemetry_components.bodylock_lifecycle.c_str();
    telemetry_tick.bodylock_transition_reason =
        telemetry_components.bodylock_transition_reason.c_str();
    telemetry_tick.assist_limit_reason =
        telemetry_components.assist_limit_reason.c_str();
    telemetry_collectors_.observe_tick(telemetry_tick);
    if (telemetry_new_vision) {
        const auto committed = adapt_committed_capture_observation(
            latest_vision_result_,
            controller_.last_target_plan(),
            config_.gamepad.tracker.aim_height_ratio,
            controller_.ads_epoch(),
            latest_controller_consume_started_ns_);
        telemetry_collectors_.observe_committed_capture(committed);
        if (causal_response_learner_ != nullptr &&
            pipeline_contract::valid(committed)) {
            const auto* history = telemetry_collectors_.control_history();
            if (history != nullptr) {
                const auto assessment = causal_response_learner_->observe_vision(
                    committed, *history);
                const auto estimate = causal_response_learner_->estimate();
                control_learning::PendingMotionEstimate pending;
                control_learning::RolloutResult rollout;
                const auto& controller_trace = controller_.last_acquisition_trace();
                const bool causal_decision_available = controller_trace.valid &&
                    controller_trace.source_frame_id == committed.source_frame_id &&
                    controller_trace.plan_decision_ns != 0;
                const std::uint64_t causal_decision_ns =
                    causal_decision_available
                    ? controller_trace.plan_decision_ns
                    : 0;
                if (has_previous_learning_observation_) {
                    control_learning::PendingMotionRequest request;
                    request.previous_capture_ns =
                        previous_learning_observation_.captured_at_ns;
                    request.current_capture_ns = committed.captured_at_ns;
                    request.decision_ns = causal_decision_ns;
                    request.delay_ms = estimate.selected_delay_ms;
                    request.right_response = estimate.right_stable;
                    request.left_response = estimate.left_stable;
                    request.selected_delay_confidence =
                        estimate.selected_delay_confidence;
                    request.response_confidence = estimate.right_confidence;
                    request.identity_continuous =
                        committed.persistent_target_id ==
                        previous_learning_observation_.persistent_target_id;
                    request.ads_epoch_continuous =
                        committed.ads_epoch == previous_learning_observation_.ads_epoch;
                    request.stable_coordinates_valid =
                        committed.stable_coordinates_valid;
                    pending = control_learning::PendingMotionModel::estimate(
                        request, *history);
                }
                if (config_.control_learning.mode ==
                    controller_native::ControlLearningMode::RolloutShadow) {
                    const auto& plan = controller_.last_target_plan();
                    control_learning::RolloutSnapshot snapshot;
                    snapshot.decision_at_ns = causal_decision_ns;
                    snapshot.latest_evidence_at_ns = committed.result_at_ns;
                    snapshot.target_id = plan.target_id;
                    snapshot.mode = plan.mode;
                    snapshot.error_px = {plan.error_px.x, plan.error_px.y};
                    snapshot.predicted_terminal_error_px = {
                        plan.predicted_terminal_error_px.x,
                        plan.predicted_terminal_error_px.y};
                    snapshot.target_velocity_px_per_sec = {
                        plan.velocity_px_per_sec.x, plan.velocity_px_per_sec.y};
                    snapshot.target_acceleration_px_per_sec2 = {
                        plan.acceleration_px_per_sec2.x,
                        plan.acceleration_px_per_sec2.y};
                    // The fuser's single pre-recoil aim output is the only
                    // proposal the rollout may scale. Manual and shaped-AI
                    // components remain upstream diagnostics, not forces;
                    // recoil stays outside this shadow proposal.
                    snapshot.final_output = {
                        telemetry_components.before_recoil_stick.x,
                        telemetry_components.before_recoil_stick.y};
                    snapshot.pending_total_px = pending.pending_total_px;
                    snapshot.pending_motion_valid = pending.valid;
                    snapshot.right_response = estimate.right_stable;
                    snapshot.response_confidence = estimate.right_confidence;
                    snapshot.delay_confidence = estimate.selected_delay_confidence;
                    snapshot.has_target = plan.target_id != 0;
                    snapshot.single_strong_target =
                        pipeline_contract::single_strong_target(committed);
                    rollout = control_learning::ShortHorizonRollout::evaluate(snapshot);
                }
                if (config_.control_learning.telemetry_enabled)
                    telemetry_collectors_.observe_causal_shadow(
                        committed, assessment, estimate, pending, rollout,
                        {telemetry_components.before_recoil_stick.x,
                         telemetry_components.before_recoil_stick.y});
                previous_learning_observation_ = committed;
                has_previous_learning_observation_ = true;
            }
        }
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

bool RuntimeLoop::is_aiming(const controller_native::PhysicalGamepadState& physical) {
    return aim_activation_tracker_.update(physical, config_.gamepad.rb_counts_as_aiming);
}

}  // namespace runtime_app
