#include "mouse_native/mouse_controller_session.h"
#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <limits>
#include <utility>
#include <vector>

namespace mouse_native {
namespace {

constexpr std::uint64_t kHeartbeatIntervalNs = 25'000'000;

MouseCodDefaultConfig session_default_config(
    MouseCodDefaultConfig config, MouseRelayTestMode mode) {
    if (mode != MouseRelayTestMode::None) config.enabled = false;
    return config;
}

std::uint64_t delivery_time_ns() noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

std::int32_t bounded_count_sum(std::int64_t value) noexcept {
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(
        value,
        std::numeric_limits<std::int32_t>::min(),
        std::numeric_limits<std::int32_t>::max()));
}

std::uint64_t seconds_to_ns(double seconds) noexcept {
    if (!std::isfinite(seconds) || seconds <= 0.0) return 0;
    const double ns = seconds * 1.0e9;
    if (ns >= static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return static_cast<std::uint64_t>(ns);
}

}  // namespace

MouseControllerSession::MouseControllerSession(
    MouseControllerFacadeConfig controller_config,
    MouseSensitivityCalibratorConfig calibration_config,
    MouseControllerTransport transport,
    MouseRelayTestMode relay_test_mode,
    MouseCodDefaultConfig default_config, int mouse_device,
    std::wstring mouse_hardware, bool external_emergency_owner,
    std::unique_ptr<MouseVirtualHidTransport> virtual_transport)
    : runtime_(controller_config, calibration_config,
          session_default_config(default_config, relay_test_mode)), transport_(transport),
      relay_test_mode_(relay_test_mode),
      virtual_hid_(virtual_transport ? std::move(virtual_transport)
          : std::make_unique<MouseVirtualHidTransport>()),
      mouse_device_(mouse_device), mouse_hardware_(std::move(mouse_hardware)),
      external_emergency_owner_(external_emergency_owner) {}

MouseControllerSession::~MouseControllerSession() {
    shutdown();
}

bool MouseControllerSession::prepare(
    std::uint64_t lease_id,
    StopCallback request_application_stop) {
    shutdown();
    if (!request_application_stop) return false;

    if (using_packet_transport()) {
        emergency_exit_ = std::make_unique<MouseEmergencyExit>(
            [this]() { packet_transport().release_interception(); },
            std::move(request_application_stop));
        if (!emergency_hotkey_.start(
                [this]() { emergency_exit_->request(); },
                [this](bool right_button_down) {
                    if (transport_ == MouseControllerTransport::VirtualHid) {
                        request_calibration();
                        return;
                    }
                    pending_calibration_mode_.store(
                        right_button_down ? 2u : 1u,
                        std::memory_order_release);
                }, !external_emergency_owner_)) {
            emergency_exit_.reset();
            return false;
        }
        // Register escape before opening the transport. Capture starts only
        // after arm(); the driver route never installs a Win32 input hook.
        const bool started = transport_ == MouseControllerTransport::VirtualHid
            ? virtual_hid_->start(mouse_device_, mouse_hardware_)
            : transport_ == MouseControllerTransport::Interception
                ? interception_.start(mouse_device_) : win32_debug_.start();
        if (!started) {
            emergency_hotkey_.stop();
            emergency_exit_.reset();
            return false;
        }
        prepared_ = true;
        return true;
    }

    if (!relay_.open() || !relay_.begin(lease_id)) {
        relay_.close();
        return false;
    }

    void* const emergency_handle = relay_.native_handle_for_emergency();
    const COD_MOUSE_RELAY_TOKEN emergency_token = relay_.token();
    emergency_exit_ = std::make_unique<MouseEmergencyExit>(
        [emergency_handle, emergency_token]() {
            if (!MouseRelayClient::emergency_disarm(
                    emergency_handle, emergency_token)) {
                throw 1;
            }
        },
        std::move(request_application_stop));
    if (!emergency_hotkey_.start(
            [this]() { emergency_exit_->request(); },
            [this](bool right_button_down) {
                pending_calibration_mode_.store(
                    right_button_down ? 2u : 1u,
                    std::memory_order_release);
            })) {
        emergency_exit_.reset();
        relay_.close();
        return false;
    }

    prepared_ = true;
    last_heartbeat_ns_ = 0;
    return true;
}

bool MouseControllerSession::arm() {
    if (!prepared_ || !emergency_hotkey_.running()) return false;
    if (using_packet_transport()) {
        if (!packet_transport().enable_interception()) return false;
    } else if (!relay_.heartbeat() || !relay_.arm()) {
        return false;
    }
    last_source_sequence_ = 0;
    next_report_sequence_ = 1;
    left_button_down_ = false;
    right_button_down_ = false;
    return true;
}

void MouseControllerSession::shutdown() noexcept {
    if (using_packet_transport()) {
        packet_transport().release_interception();
    } else if (relay_.has_lease()) {
        (void)relay_.disarm();
    }
    emergency_hotkey_.stop();
    emergency_exit_.reset();
    win32_debug_.stop();
    interception_.stop();
    virtual_hid_->stop();
    relay_.close();
    runtime_.reset();
    pending_calibration_mode_.store(0, std::memory_order_release);
    prepared_ = false;
    left_button_down_ = false;
    right_button_down_ = false;
    last_source_sequence_ = 0;
    next_report_sequence_ = 1;
    last_heartbeat_ns_ = 0;
}

void MouseControllerSession::emergency_release() noexcept {
    if (using_packet_transport()) packet_transport().release_interception();
}

void MouseControllerSession::submit_vision_snapshot(
    const controller_native::ControllerVisionSnapshot& snapshot) {
    runtime_.submit_vision_snapshot(snapshot);
}

void MouseControllerSession::set_default_view_height(int height_px) noexcept {
    runtime_.set_default_view_height(height_px);
}

MouseCalibrationRequestResult MouseControllerSession::begin_calibration(
    bool right_button_down,
    std::uint64_t now_ns) {
    if (relay_test_mode_ != MouseRelayTestMode::None || !armed()) {
        MouseCalibrationRequestResult rejected{};
        rejected.failure = MouseCalibrationFailure::InvalidState;
        return rejected;
    }
    MouseCalibrationRequestResult result =
        runtime_.begin_calibration(right_button_down, now_ns);
    if (!result.started) return result;
    right_button_down_ = right_button_down;
    const bool delivered = deliver_calibration_output(result.output);
    const MouseCalibrationUpdate acknowledged =
        runtime_.acknowledge_calibration_output(delivered, delivery_time_ns());
    if (acknowledged.failed) {
        result.started = false;
        result.failure = acknowledged.failure;
    }
    return result;
}

bool MouseControllerSession::take_calibration_hotkey(
    bool* right_button_down) noexcept {
    if (transport_ == MouseControllerTransport::VirtualHid) return false;
    const unsigned int mode =
        pending_calibration_mode_.exchange(0, std::memory_order_acq_rel);
    if (mode == 0) return false;
    if (right_button_down != nullptr) *right_button_down = mode == 2u;
    return true;
}

void MouseControllerSession::request_calibration() noexcept {
    pending_calibration_mode_.store(1u, std::memory_order_release);
}

MouseControllerSessionTickResult MouseControllerSession::tick(
    double now_seconds,
    std::uint64_t tick_id) {
    MouseControllerSessionTickResult result{};
    if (!prepared_) return result;

    MouseWin32DebugSource debug_source{};
    if (using_packet_transport()) {
        if (!packet_transport().running() || packet_transport().last_error() == ERROR_TIMEOUT) {
            transport_failure();
            result.transport_failed = true;
            return result;
        }
        debug_source = packet_transport().read_source();
        if (!packet_transport().running() || packet_transport().last_error() != ERROR_SUCCESS) {
            transport_failure();
            result.transport_failed = true;
            return result;
        }
        left_button_down_ = debug_source.left_button_down;
        right_button_down_ = debug_source.right_button_down;
        last_source_sequence_ = debug_source.sequence;
    }
    const auto virtual_window = virtual_hid_->window_token();
    result.relay_ready = using_packet_transport()
        ? packet_transport().intercepting()
        : relay_.armed();

    if (!result.relay_ready) {
        MouseControllerRuntimeTickInput input{};
        if (using_packet_transport() &&
            runtime_.calibration_state() != MouseCalibrationState::Idle) {
            // Before arm(), normal physical movement remains native. It is
            // supplied here only so movement during a calibration probe aborts
            // that sample instead of contaminating the in-memory profile.
            input.source_counts = debug_source.counts;
        }
        input.now_seconds = now_seconds;
        input.tick_id = tick_id;
        input.right_button_down = right_button_down_;
        input.left_button_down = left_button_down_;
        result.runtime = runtime_.tick(input);
        if (result.runtime.output.kind != MouseRuntimeOutputKind::ControllerFinal) {
            const bool delivered = deliver_calibration_output(result.runtime.output);
            const MouseCalibrationUpdate acknowledged =
                runtime_.acknowledge_calibration_output(delivered, delivery_time_ns());
            result.output_delivered = delivered;
            if (acknowledged.failed && !delivered) {
                result.transport_failed = true;
            }
        }
        return result;
    }

    std::int64_t dx = 0;
    std::int64_t dy = 0;
    if (using_packet_transport()) {
        dx = debug_source.counts.dx;
        dy = debug_source.counts.dy;
    } else {
        const std::uint64_t now_ns = seconds_to_ns(now_seconds);
        if (!refresh_heartbeat(now_ns)) {
            result.transport_failed = true;
            return result;
        }

        std::vector<COD_MOUSE_SOURCE_PACKET> packets;
        if (!relay_.read_batch(&packets)) {
            transport_failure();
            result.transport_failed = true;
            return result;
        }
        for (const auto& packet : packets) {
            dx += packet.DxCounts;
            dy += packet.DyCounts;
            last_source_sequence_ = std::max(last_source_sequence_, packet.Sequence);
            apply_button_flags(
                packet.ButtonFlags, &left_button_down_, &right_button_down_);
        }
    }

    MouseControllerRuntimeTickInput input{};
    input.source_counts = {bounded_count_sum(dx), bounded_count_sum(dy)};
    result.source_counts = input.source_counts;
    input.now_seconds = now_seconds;
    input.tick_id = tick_id;
    input.right_button_down = right_button_down_;
    input.left_button_down = left_button_down_;
    MouseCalibrationRequestResult calibration_request{};
    if (transport_ == MouseControllerTransport::VirtualHid &&
        pending_calibration_mode_.exchange(0, std::memory_order_acq_rel) != 0) {
        result.calibration_requested = true;
        if (relay_test_mode_ != MouseRelayTestMode::None) {
            calibration_request.failure = MouseCalibrationFailure::InvalidState;
        } else if (std::abs(dx) + std::abs(dy) > 4) {
            calibration_request.failure = MouseCalibrationFailure::PhysicalMouseMoved;
        } else {
            calibration_request = runtime_.begin_calibration(
                right_button_down_, seconds_to_ns(now_seconds));
        }
    }
    result.runtime = runtime_.tick(input);
    if (calibration_request.started) {
        result.calibration_started = true;
        // Begin changed the runtime into calibration before this control tick.
        // The probe and this window's allowed sensor jitter have one commit.
        result.runtime.output = calibration_request.output;
        result.runtime.output.counts.dx += input.source_counts.dx;
        result.runtime.output.counts.dy += input.source_counts.dy;
    } else if (result.calibration_requested) {
        result.runtime.calibration_failure = calibration_request.failure;
    }
    if (relay_test_mode_ != MouseRelayTestMode::None) {
        result.runtime.output.counts = relay_test_output(relay_test_mode_, input.source_counts);
    }
    result.through_source_sequence = last_source_sequence_;

    if (result.runtime.output.kind != MouseRuntimeOutputKind::ControllerFinal) {
        const bool delivered = deliver_calibration_output(result.runtime.output);
        const MouseCalibrationUpdate acknowledged =
            runtime_.acknowledge_calibration_output(delivered, delivery_time_ns());
        result.output_delivered = delivered;
        if (acknowledged.failed && !delivered) {
            transport_failure();
            result.transport_failed = true;
        }
        return result;
    }

    result.report_sequence = next_report_sequence_++;
    if (next_report_sequence_ == 0) ++next_report_sequence_;
    const bool fire = relay_test_mode_ == MouseRelayTestMode::None &&
        result.runtime.controller.auto_fire_active;
    if (transport_ == MouseControllerTransport::VirtualHid) {
        // One atomic window commit owns movement AND button state. Never
        // issue a second synthetic click or replay the physical displacement.
        result.output_delivered = virtual_hid_->submit_frame(
            result.runtime.output.counts, fire, virtual_window);
    } else {
        result.output_delivered = submit_final_output(
            result.report_sequence, result.runtime.output.counts);
    }
    if (result.output_delivered && transport_ != MouseControllerTransport::VirtualHid) {
        // Both user-mode transports own AutoFire edges. The experimental
        // movement-only driver protocol cannot silently accept a fire command.
        result.output_delivered = using_packet_transport()
            ? packet_transport().submit_auto_fire(fire) : !fire;
    }
    if (!result.output_delivered) {
        transport_failure();
        result.transport_failed = true;
    }
    return result;
}

bool MouseControllerSession::prepared() const noexcept { return prepared_; }
MouseVirtualHidTransport::WindowInfo MouseControllerSession::transport_window() const noexcept {
    return transport_ == MouseControllerTransport::VirtualHid
        ? virtual_hid_->window_info() : MouseVirtualHidTransport::WindowInfo{};
}
bool MouseControllerSession::armed() const noexcept {
    return using_packet_transport() ? packet_transport().intercepting() : relay_.armed();
}
bool MouseControllerSession::right_button_down() const noexcept {
    return right_button_down_;
}
bool MouseControllerSession::left_button_down() const noexcept {
    return left_button_down_;
}
const MouseControllerRuntimeCore& MouseControllerSession::runtime() const noexcept {
    return runtime_;
}
const MouseRelayClient& MouseControllerSession::relay() const noexcept { return relay_; }
MouseWin32TransportStats MouseControllerSession::transport_stats() const noexcept {
    return using_packet_transport() ? packet_transport().stats() : MouseWin32TransportStats{};
}

unsigned long MouseControllerSession::transport_error() const noexcept {
    return using_packet_transport() ? packet_transport().last_error() : relay_.last_error();
}

void MouseControllerSession::apply_button_flags(
    std::uint32_t flags,
    bool* left_button_down,
    bool* right_button_down) noexcept {
    if (left_button_down != nullptr) {
        if ((flags & COD_MOUSE_LEFT_BUTTON_DOWN) != 0) *left_button_down = true;
        if ((flags & COD_MOUSE_LEFT_BUTTON_UP) != 0) *left_button_down = false;
    }
    if (right_button_down != nullptr) {
        if ((flags & COD_MOUSE_RIGHT_BUTTON_DOWN) != 0) *right_button_down = true;
        if ((flags & COD_MOUSE_RIGHT_BUTTON_UP) != 0) *right_button_down = false;
    }
}

bool MouseControllerSession::deliver_calibration_output(
    const MouseRuntimeOutput& output) noexcept {
    if (!output.requested) return false;
    if (using_packet_transport() && !packet_transport().submit_auto_fire(false)) return false;
    return using_packet_transport()
        ? packet_transport().submit_calibration(output.counts)
        : relay_.submit_calibration(output.counts);
}

bool MouseControllerSession::submit_final_output(
    std::uint64_t report_sequence,
    MouseSourceCounts counts) noexcept {
    if (using_packet_transport()) return packet_transport().submit_final(counts);
    return relay_.submit_final(
        report_sequence,
        last_source_sequence_,
        counts);
}

bool MouseControllerSession::refresh_heartbeat(std::uint64_t now_ns) noexcept {
    if (now_ns == 0) return false;
    if (last_heartbeat_ns_ != 0 && now_ns >= last_heartbeat_ns_ &&
        now_ns - last_heartbeat_ns_ < kHeartbeatIntervalNs) {
        return true;
    }
    if (!relay_.heartbeat()) {
        transport_failure();
        return false;
    }
    last_heartbeat_ns_ = now_ns;
    return true;
}

void MouseControllerSession::transport_failure() noexcept {
    if (using_packet_transport()) {
        packet_transport().release_interception();
    } else {
        (void)relay_.disarm();
    }
    prepared_ = false;
    runtime_.cancel_calibration();
}

bool MouseControllerSession::using_packet_transport() const noexcept {
    return transport_ != MouseControllerTransport::KmdfVhf;
}

MouseUserTransport& MouseControllerSession::packet_transport() noexcept {
    if (transport_ == MouseControllerTransport::VirtualHid) return *virtual_hid_;
    return transport_ == MouseControllerTransport::Interception
        ? static_cast<MouseUserTransport&>(interception_)
        : static_cast<MouseUserTransport&>(win32_debug_);
}
const MouseUserTransport& MouseControllerSession::packet_transport() const noexcept {
    if (transport_ == MouseControllerTransport::VirtualHid) return *virtual_hid_;
    return transport_ == MouseControllerTransport::Interception
        ? static_cast<const MouseUserTransport&>(interception_)
        : static_cast<const MouseUserTransport&>(win32_debug_);
}

}  // namespace mouse_native
