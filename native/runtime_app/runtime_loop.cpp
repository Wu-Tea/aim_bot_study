#include "runtime_reload_policy.h"
#include "vision_engine_service_poller.h"
#include "runtime_loop.h"
#include "runtime_control_bridge.h"

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


bool input_log_enabled() {
    return environment_flag_enabled("GAMEPAD_INPUT_LOG");
}

bool gamepad_perf_log_enabled(bool perf_log) {
    return perf_log && environment_flag_enabled("GAMEPAD_PERF_LOG");
}

void log_xinput_slot_table(const std::vector<controller_native::XInputUserSlot>& slots);
void log_sdl_joystick_table(const std::vector<controller_native::SdlJoystickDevice>& devices);

std::unique_ptr<controller_native::SdlGamepadReader> open_sdl_input_reader(
    const controller_native::GamepadRuntimeConfig& config);

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

std::unique_ptr<controller_native::SdlGamepadReader> open_sdl_input_reader(
    const controller_native::GamepadRuntimeConfig& config) {
    const std::vector<controller_native::SdlJoystickDevice> devices =
        controller_native::scan_sdl_joystick_devices();
    if (input_log_enabled()) {
        log_sdl_joystick_table(devices);
    }
    if (!config.input_device_id.empty()) {
        const int selected = controller_native::select_sdl_reconnect_device(
            devices, {}, 4, 0, config.input_device_id);
        if (selected >= 0) {
            auto reader = std::make_unique<controller_native::SdlGamepadReader>(selected, config.input_device_id);
            if (reader->available()) return reader;
        }
        throw std::runtime_error("selected input device is unavailable or ambiguous: " + config.input_device_name);
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

}  // namespace

int probe_physical_input(const controller_native::GamepadRuntimeConfig& config) {
    auto sdl = open_sdl_input_reader(config);
    controller_native::XInputReader xinput(select_xinput_user_index(config, sdl == nullptr));
    std::cout << "[InputProbe] backend=" << (sdl ? "SDL" : "XInput")
              << " name=" << (sdl ? sdl->device_name() : "XInput") << '\n';
    int connected_samples = 0;
    float min_x = 1, max_x = -1, min_y = 1, max_y = -1;
    constexpr int kSamples = 200;
    for (int sample = 0; sample < kSamples; ++sample) {
        const auto state = sdl ? sdl->read() : xinput.read();
        if (state.connected) {
            ++connected_samples;
            min_x = std::min(min_x, state.right_x);
            max_x = std::max(max_x, state.right_x);
            min_y = std::min(min_y, state.right_y);
            max_y = std::max(max_y, state.right_y);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::cout << "[InputProbe] connected_samples=" << connected_samples
              << " total_samples=" << kSamples << " right_x_min=" << min_x
              << " right_x_max=" << max_x << " right_y_min=" << min_y
              << " right_y_max=" << max_y << '\n';
    if (connected_samples != kSamples) {
        std::cerr << "[InputProbe] Physical controller unavailable or disconnected. "
                     "Check device connection and HidHide application access.\n";
        return 4;
    }
    return 0;
}

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
          config_.build_commit}),
      log_session_manager_(log_session_options_from(config_)),
      telemetry_(telemetry_options_from(config_, log_session_manager_.session_directory())),
      telemetry_collectors_(
          config_.telemetry.enabled,
          &telemetry_,
          TelemetrySessionContext{
              config_.build_commit.c_str(),
               config_.vision.capture_width,
              config_.vision.capture_height,
              config_.vision.capture_fps,
               config_.vision.idle_capture_fps,
               config_.scheduler.controller_tick_hz,
               config_.telemetry.manual_controller_hz}),
      downward_diagnostics_(DownwardPullDiagnostics::from_environment()),
      perf_log_(perf_log),
      gamepad_perf_log_(gamepad_perf_log_enabled(perf_log)),
      sdl_input_reader_(open_sdl_input_reader(config_.gamepad)),
      input_reader_(select_xinput_user_index(config_.gamepad, sdl_input_reader_ == nullptr)),
      controller_(config_.gamepad),
      output_composer_(config_.gamepad.output_transfer),
      viewport_controller_(viewport_controller_config_from(config_)),
      virtual_gamepad_(config_.output.enabled
          ? std::make_unique<controller_native::VirtualGamepad>() : nullptr),
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
        config_.gamepad.ai_aim.ads_pickup_base_radius_px,
        config_.vision.friendly_filter_enabled,
        config_.vision.target_height_ratio,
        config_.vision.target_wide_low_height_ratio);
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
        service_options.aim_release_hold_ms = config_.vision.aim_release_hold_ms;
        vision_service_ = std::make_unique<VisionService>(
            std::make_unique<VisionEngineServicePoller>(std::move(vision_engine)),
            service_options);
        vision_service_->set_viewport(initial_viewport);
        vision_service_->start();
        std::cout << "[VisionService][CPP] enabled"
                  << " capture_fps=" << config_.vision.capture_fps
                  << " idle_fps=" << config_.vision.idle_capture_fps
                  << " aim_release_hold_ms=" << config_.vision.aim_release_hold_ms
                  << " keepwarm=" << (config_.vision.keepwarm_when_idle ? 1 : 0)
                  << '\n';
    } else {
        vision_engine_ = std::move(vision_engine);
    }

    // --- fusion visual overlay channel (disabled by default) ---
    if (config_.vision.fusion_enabled) {
        const bool fusion_opened = fusion_publisher_.open(
            config_.vision.fusion_session.c_str(),
            config_.vision.fusion_show_all_detections);
        if (fusion_opened) {
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
            virtual_gamepad_->update(*output_composer_.finalized_output());
        }
    }
    frame_rates_.finish(steady_time_point_ns(std::chrono::steady_clock::now()));
    if (control_bridge_) control_bridge_->finish_frame_rates(frame_rates_.snapshot());
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

void RuntimeLoop::apply_pending_config() {
    if (!control_bridge_) return;
    if (!pending_hot_config_) pending_hot_config_ = control_bridge_->take_prepared();
    if (!pending_hot_config_) return;
    const auto& next = *pending_hot_config_;
    const bool vision_changed = config_.vision.friendly_filter_enabled != next.vision.friendly_filter_enabled ||
        config_.vision.target_height_ratio != next.vision.target_height_ratio ||
        config_.vision.target_wide_low_height_ratio != next.vision.target_wide_low_height_ratio;
    if (vision_changed) {
        if (!policy_requested_) {
            vision_service_->set_detection_policy({next.vision.friendly_filter_enabled, next.vision.target_height_ratio,
                next.vision.target_wide_low_height_ratio, vision_policy_revision_ + 1});
            policy_requested_ = true;
        }
        const auto snapshot = vision_service_->latest_snapshot();
        if (snapshot.policy_revision != vision_policy_revision_ + 1 || snapshot.freshness != VisionSnapshotFreshness::Fresh) return;
        ++vision_policy_revision_;
    }
    const bool preserve_learning = preserve_response_learning_on_reload(config_, next);
    controller_.apply_hot_config(next.gamepad, preserve_learning);
    config_.gamepad = next.gamepad;
    config_.ads = next.ads;
    config_.vision.friendly_filter_enabled = next.vision.friendly_filter_enabled;
    config_.vision.target_height_ratio = next.vision.target_height_ratio;
    config_.vision.target_wide_low_height_ratio = next.vision.target_wide_low_height_ratio;
    config_.effective_values = next.effective_values;
    config_.effective_sources = next.effective_sources;
    control_bridge_->complete(controller_.learning_snapshot(), preserve_learning);
    pending_hot_config_.reset();
    policy_requested_ = false;
}

// Called only after the producer-specific epoch/policy and capture fences.
// Both direct polling and mailbox delivery commit the same controller state.
void RuntimeLoop::submit_vision_result(const vision_native::VisionResult& result,
                                      std::uint64_t controller_consume_ns,
                                      std::uint64_t published_at_ns) {
    controller_.submit_vision_snapshot(adapt_vision_result(result));
    latest_vision_publish_ns_ = published_at_ns;
    latest_controller_submit_complete_ns_ =
        steady_time_point_ns(std::chrono::steady_clock::now());
    latest_vision_result_ = result;
    has_latest_vision_result_ = true;
    latest_controller_consume_started_ns_ = controller_consume_ns;
}

void RuntimeLoop::run_once() {
    apply_pending_config();
    const auto tick_started = std::chrono::steady_clock::now();
    const controller_native::PhysicalGamepadState physical = read_physical_gamepad();
    const std::uint64_t physical_read_at_ns = steady_time_point_ns(std::chrono::steady_clock::now());
    const auto& tick_preparation = controller_.begin_tick(
        physical,
        static_cast<std::uint64_t>(tick_count_) + 1u);
    const bool aiming = tick_preparation.scope.physical_ads_active;
    // Physical ADS and manual-fire activation are independent input events,
    // reduced into one scope snapshot without synthesizing LT.
    const bool assist_aiming = pipeline_contract::requests_target_search(tick_preparation.activation);
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

    bool fresh_vision = false;
    if (vision_service_ != nullptr) {
        const std::uint64_t expected_aim_transition_sequence =
            vision_service_->set_request(pipeline_contract::vision_request(tick_preparation.activation, vision_requested), tick_started);
        vision_service_->set_user_aim_intent(user_aim_intent);
        VisionServiceSnapshot service_snapshot =
            vision_service_->latest_snapshot(latest_vision_service_sequence_);
        if (
            service_snapshot.sequence != 0 && service_snapshot.policy_revision == vision_policy_revision_ &&
            service_snapshot.sequence != latest_vision_service_sequence_) {
            latest_vision_service_sequence_ = service_snapshot.sequence;
            vision_native::VisionResult result = std::move(service_snapshot.result);
            const auto controller_consume_started = std::chrono::steady_clock::now();
            const std::uint64_t controller_consume_ns =
                steady_time_point_ns(controller_consume_started);
            const bool current_control_epoch =
                service_snapshot.request == pipeline_contract::vision_request(tick_preparation.activation, vision_requested) &&
                service_snapshot.aim_transition_sequence ==
                    expected_aim_transition_sequence;
            if (service_snapshot.freshness == VisionSnapshotFreshness::Fresh &&
                current_control_epoch &&
                vision_delivery_gate_.accept(result, controller_consume_ns)) {
                submit_vision_result(result, controller_consume_ns, service_snapshot.published_at_ns);
                fresh_vision = true;
            }
        }
    } else {
        vision_engine_->set_request(pipeline_contract::vision_request(tick_preparation.activation, vision_requested));
        vision_engine_->set_user_aim_intent(user_aim_intent);
        if (should_poll_vision(tick_started)) {
            last_vision_poll_at_ = tick_started;
            vision_native::VisionResult result = vision_engine_->poll_once();
            const auto controller_consume_started = std::chrono::steady_clock::now();
            const std::uint64_t controller_consume_ns =
                steady_time_point_ns(controller_consume_started);
            if (vision_delivery_gate_.accept(result, controller_consume_ns)) {
                // Direct polling has no mailbox publication timestamp.
                submit_vision_result(result, controller_consume_ns, 0);
                fresh_vision = true;
            }
        }
    }
    const auto controller_pipeline_started = std::chrono::steady_clock::now();
    controller_native::ControlFrame control_frame =
        controller_.resolve_control_frame();
    if (config_.gamepad.enemy_mark.enabled) {
        if (fresh_vision) {
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
        output_result = virtual_gamepad_->update(output);
        if (output_result.reconnect_attempted && output_result.delivered) {
            std::cout << "[NativeRuntime][Output] ViGEm recovered"
                      << " reconnect_count=" << output_result.reconnect_count << '\n';
        } else if (output_result.reconnect_attempted && !output_result.delivered) {
            std::cerr << "[NativeRuntime][Output] ViGEm recovery pending"
                      << " error=0x" << std::hex << output_result.error_code << std::dec << '\n';
        }
    }
    auto delivered_output = output;
    delivered_output.right_x = output_result.delivered_right_x;
    delivered_output.right_y = output_result.delivered_right_y;
    if (output_result.reconnect_attempted) {
        // Recreating the device publishes a neutral report before this report.
        // Even a successful same-tick reconnect breaks camera-work continuity.
        controller_.observe_delivered_output({}, false, 0.0);
    }
    controller_.observe_delivered_output(delivered_output,
        output_result.delivered, output_result.submitted_at_seconds);
    const auto vigem_update_finished = std::chrono::steady_clock::now();
    // Everything below is observation, diagnostics, or future-frame setup.
    // It must never delay the output calculated from a newly consumed result.
    frame_rates_.record_tick(physical_read_at_ns, aiming,
        fresh_vision && latest_vision_result_.frame_updated);
    if (control_bridge_ && tick_count_ % 128 == 0 && GetTickCount64() - last_learning_publish_ms_ >= 500) {
        control_bridge_->offer_learning(controller_.learning_snapshot());
        control_bridge_->offer_frame_rates(frame_rates_.snapshot());
        last_learning_publish_ms_ = GetTickCount64();
    }
    if (fresh_vision) {
        if (fusion_publisher_.enabled() && latest_vision_result_.frame_updated) {
            fusion_publisher_.publish_vision_result(latest_vision_result_,
                config_.vision.capture_width, config_.vision.capture_height);
            if (!fusion_publisher_.enabled())
                std::cout << "[Fusion][CPP] self-disabled after repeated publish failures\n";
        }

        record_vision_diagnostics(aiming);
    }

    pipeline_contract::CommittedCaptureObservation viewport_observation;
    const pipeline_contract::CommittedCaptureObservation* viewport_observation_ptr = nullptr;
    if (fresh_vision) {
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
    record_tick_diagnostics(physical, output, output_result,
        {tick_started, controller_pipeline_started, vigem_update_started,
         vigem_update_finished, physical_read_at_ns},
        aiming, assist_aiming, fresh_vision ? &viewport_observation : nullptr);
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
