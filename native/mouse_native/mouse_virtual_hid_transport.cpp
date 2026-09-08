#include "mouse_native/mouse_virtual_hid_transport.h"
#include "mouse_link/fakerinput_output.h"

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cwctype>
#include <deque>
#include <limits>
#include <mutex>
#include <utility>

namespace mouse_native {
namespace {
constexpr std::size_t kQueueCapacity = 256;
constexpr std::uint64_t kIdentityCheckIntervalNs = 100'000'000;
constexpr std::uint64_t kMaximumSourceAgeNs = 100'000'000;

std::uint64_t monotonic_ns() noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
bool physical_hardware(const std::wstring& value) {
    std::wstring upper = value;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](wchar_t c) { return std::towupper(c); });
    return upper.find(L"HID\\VID_") == 0 && upper.find(L"VID_FE0F") == std::wstring::npos;
}
HANDLE raw_handle(const std::wstring& path) {
    UINT count = 0;
    if (GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST)) == UINT(-1)) return nullptr;
    std::vector<RAWINPUTDEVICELIST> devices(count);
    const UINT found = GetRawInputDeviceList(devices.data(), &count, sizeof(RAWINPUTDEVICELIST));
    if (found == UINT(-1)) return nullptr;
    HANDLE match = nullptr;
    for (UINT i = 0; i < found; ++i) {
        if (devices[i].dwType != RIM_TYPEMOUSE) continue;
        UINT size = 0;
        if (GetRawInputDeviceInfoW(devices[i].hDevice, RIDI_DEVICENAME, nullptr, &size) == UINT(-1)) continue;
        std::vector<wchar_t> name(size + 1);
        if (GetRawInputDeviceInfoW(devices[i].hDevice, RIDI_DEVICENAME, name.data(), &size) != UINT(-1) &&
            _wcsicmp(path.c_str(), name.data()) == 0) {
            if (match) return nullptr;
            match = devices[i].hDevice;
        }
    }
    return match;
}
bool raw_identity_valid(HANDLE device, const std::wstring& expected) noexcept {
    wchar_t path[4096]{};
    UINT size = static_cast<UINT>(std::size(path));
    return device && GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, path, &size) != UINT(-1) &&
        _wcsicmp(path, expected.c_str()) == 0;
}
class FakerSink final : public MouseVirtualHidSink {
    virtual_mouse::FakerOutput output_;
    HANDLE physical_ = nullptr, virtual_ = nullptr;
    std::wstring physical_path_, virtual_path_;
    unsigned long error_ = 0;
public:
    bool open(const std::wstring& hardware) override {
        close(); error_ = ERROR_DEVICE_NOT_CONNECTED;
        physical_path_ = virtual_mouse::raw_path_for_hardware(hardware, true);
        if (physical_path_.empty()) return false;
        physical_ = raw_handle(physical_path_);
        if (!physical_ || !output_.open()) {
            if (output_.error()) error_ = output_.error();
            close(); return false;
        }
        virtual_path_ = output_.raw_path();
        virtual_ = raw_handle(virtual_path_);
        if (!virtual_ || physical_ == virtual_ || !identities_valid()) { close(); return false; }
        error_ = ERROR_SUCCESS; return true;
    }
    void close() noexcept override {
        output_.close(); physical_ = virtual_ = nullptr;
        physical_path_.clear(); virtual_path_.clear();
    }
    bool identities_valid() const noexcept override {
        return physical_ != virtual_ && raw_identity_valid(physical_, physical_path_) &&
            raw_identity_valid(virtual_, virtual_path_);
    }
    bool buttons_released() const noexcept override {
        for (int key : {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2})
            if (GetAsyncKeyState(key) & 0x8000) return false;
        return true;
    }
    int axis_limit() const noexcept override { return output_.axis_limit(); }
    bool send(const virtual_mouse::Report& report) noexcept override {
        const bool sent = output_.send(report);
        if (!sent) error_ = output_.error();
        return sent;
    }
    unsigned long last_error() const noexcept override { return error_; }
};
bool token_equal(MouseVirtualHidTransport::WindowToken a, MouseVirtualHidTransport::WindowToken b) noexcept {
    return a.epoch == b.epoch && a.sequence == b.sequence && a.epoch && a.sequence;
}
} // namespace

struct MouseVirtualHidTransport::Impl {
    struct Captured {
        MouseDevicePacket packet{};
        std::uint64_t sequence = 0, received_ns = 0;
    };
    std::unique_ptr<MousePacketDriver> driver;
    std::unique_ptr<MouseVirtualHidSink> sink;
    mutable std::mutex mutex;
    // Escape can revoke an in-progress split without waiting for its mutex.
    // The current OS write may finish; no later piece can start after revocation.
    std::atomic<std::uint64_t> release_serial{0};
    std::uint64_t accepted_release_serial = 0;
    std::deque<Captured> queue;
    bool ready = false, armed = false, capture_owned = false, window_open = false;
    bool synthetic = false;
    unsigned char physical_buttons = 0, delivered_buttons = 0;
    int device = 0;
    std::wstring hardware;
    unsigned long error = ERROR_SUCCESS;
    std::uint64_t epoch = 0, next_window = 0, last_consumed_source = 0, checked_at_ns = 0;
    WindowInfo window{};
    MouseWin32TransportStats counters{};

    Impl(std::unique_ptr<MousePacketDriver> d, std::unique_ptr<MouseVirtualHidSink> s)
        : driver(std::move(d)), sink(std::move(s)) {}
    void record_error(unsigned long value) noexcept {
        if (!error) error = value ? value : ERROR_GEN_FAILURE;
    }
    void release() noexcept {
        // Revoke commands first. Restore the filter before potentially blocking
        // output cleanup; neither cleanup failure prevents the other attempt.
        armed = false; window_open = false; queue.clear(); synthetic = false;
        if (window.token.epoch && !window.committed) window.cancelled = true;
        if (capture_owned) {
            if (!driver->capture(device, false)) record_error(ERROR_WRITE_FAULT);
            driver->close();
            if (!sink->send({})) record_error(sink->last_error());
        } else {
            driver->close();
        }
        capture_owned = false; sink->close(); ready = false;
        physical_buttons = delivered_buttons = 0;
    }
    void fail(unsigned long reason) noexcept { record_error(reason); release(); }
    bool commit(MouseSourceCounts counts, bool requested, WindowToken token) noexcept {
        if (!armed || error || !window_open || !token_equal(token, window.token)) return false;
        if (release_serial.load(std::memory_order_acquire) != accepted_release_serial) {
            fail(ERROR_OPERATION_ABORTED); return false;
        }
        const auto origin_ns = window.first_source_ns ? window.first_source_ns : window.cutoff_ns;
        if (monotonic_ns() - origin_ns > kMaximumSourceAgeNs) {
            fail(ERROR_TIMEOUT); return false;
        }
        // Irrevocably claim this logical result before the first HID write.
        // Partial output failure may cancel remaining pieces, never replay them.
        window_open = false;
        const bool next_synthetic = requested && !(physical_buttons & 1u);
        const unsigned char buttons = static_cast<unsigned char>(physical_buttons | (next_synthetic ? 1u : 0u));
        virtual_mouse::Pending pending;
        pending.buttons = buttons;
        pending.x = counts.dx; pending.y = counts.dy;
        pending.wheel = window.wheel; pending.hwheel = window.hwheel;
        const int limit = sink->axis_limit();
        if (limit <= 0 || limit > 32767) { fail(ERROR_INVALID_DATA); return false; }
        window.final_counts = counts;
        window.cancelled_counts = counts;
        window.cancelled_wheel = window.wheel; window.cancelled_hwheel = window.hwheel;
        while (!pending.empty()) {
            if (release_serial.load(std::memory_order_acquire) != accepted_release_serial) {
                fail(ERROR_OPERATION_ABORTED); return false;
            }
            if (monotonic_ns() - origin_ns > kMaximumSourceAgeNs) {
                fail(ERROR_TIMEOUT); return false;
            }
            const auto report = pending.next(limit);
            // State-only edges are reports too; identical idle state is not.
            if (!report.x && !report.y && !report.wheel && !report.hwheel &&
                report.buttons == delivered_buttons) continue;
            if (!sink->send(report)) { fail(sink->last_error()); return false; }
            delivered_buttons = report.buttons;
            ++counters.submitted_reports;
            ++window.reports_submitted;
            window.submitted_counts.dx += report.x; window.submitted_counts.dy += report.y;
            window.cancelled_counts.dx -= report.x; window.cancelled_counts.dy -= report.y;
            window.submitted_wheel += report.wheel; window.submitted_hwheel += report.hwheel;
            window.cancelled_wheel -= report.wheel; window.cancelled_hwheel -= report.hwheel;
            window.submitted_ns = monotonic_ns();
        }
        if (next_synthetic && !synthetic) ++counters.auto_fire_downs;
        if (!next_synthetic && synthetic) ++counters.auto_fire_ups;
        synthetic = next_synthetic;
        window.submitted_ns = monotonic_ns(); window.committed = true;
        return true;
    }
};

MouseVirtualHidTransport::MouseVirtualHidTransport(std::unique_ptr<MousePacketDriver> driver,
    std::unique_ptr<MouseVirtualHidSink> sink)
    : impl_(std::make_unique<Impl>(driver ? std::move(driver) : make_interception_driver(),
          sink ? std::move(sink) : std::make_unique<FakerSink>())) {}
MouseVirtualHidTransport::~MouseVirtualHidTransport() { stop(); }

bool MouseVirtualHidTransport::start(int device, const std::wstring& hardware) {
    stop();
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto& s = *impl_;
    s.error = ERROR_SUCCESS; s.counters = {}; s.window = {};
    s.device = 0; s.hardware.clear();
    s.accepted_release_serial = s.release_serial.load(std::memory_order_acquire);
    s.last_consumed_source = 0; s.next_window = 0;
    if (!s.driver->open()) { s.record_error(ERROR_OPEN_FAILED); return false; }
    const auto devices = s.driver->devices();
    const MouseDeviceInfo* selected = nullptr;
    for (const auto& candidate : devices) {
        if (!physical_hardware(candidate.hardware_id)) continue;
        if (device && candidate.id != device) continue;
        if (!hardware.empty() && _wcsicmp(hardware.c_str(), candidate.hardware_id.c_str()) != 0) continue;
        if (selected) { s.fail(ERROR_DUP_NAME); return false; }
        selected = &candidate;
    }
    if (!selected) { s.fail(ERROR_DEVICE_NOT_CONNECTED); return false; }
    const auto matching_identity = std::count_if(devices.begin(), devices.end(), [&](const auto& candidate) {
        return _wcsicmp(candidate.hardware_id.c_str(), selected->hardware_id.c_str()) == 0;
    });
    if (matching_identity != 1) { s.fail(ERROR_DUP_NAME); return false; }
    s.device = selected->id; s.hardware = selected->hardware_id;
    if (!s.sink->open(s.hardware) || !s.sink->identities_valid() ||
        s.sink->axis_limit() <= 0 || s.sink->axis_limit() > 32767) {
        s.fail(s.sink->last_error() ? s.sink->last_error() : ERROR_DEVICE_NOT_CONNECTED); return false;
    }
    s.ready = true; return true;
}
void MouseVirtualHidTransport::stop() noexcept { release_interception(); }
bool MouseVirtualHidTransport::enable_interception() noexcept {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto& s = *impl_;
    if (!s.ready || s.armed || s.error) return false;
    if (s.release_serial.load(std::memory_order_acquire) != s.accepted_release_serial) return false;
    if (!s.sink->identities_valid()) { s.fail(ERROR_DEVICE_NOT_CONNECTED); return false; }
    if (!s.sink->buttons_released()) { s.record_error(ERROR_BUSY); return false; }
    // Mark ownership even if the vendor setter reports failure: its partial
    // effect is uncertain and must be undone when start is abandoned.
    s.capture_owned = true;
    if (!s.driver->capture(s.device, true)) { s.fail(ERROR_ACCESS_DENIED); return false; }
    if (!s.sink->buttons_released()) { s.fail(ERROR_BUSY); return false; }
    if (++s.epoch == 0) ++s.epoch;
    s.armed = true; s.next_window = 0; s.window = {};
    s.physical_buttons = s.delivered_buttons = 0; s.synthetic = false;
    s.checked_at_ns = monotonic_ns(); return true;
}
void MouseVirtualHidTransport::release_interception() noexcept {
    impl_->release_serial.fetch_add(1, std::memory_order_acq_rel);
    std::lock_guard<std::mutex> lock(impl_->mutex); impl_->release();
}
MouseWin32DebugSource MouseVirtualHidTransport::read_source() noexcept {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto& s = *impl_;
    if (!s.armed || s.error) return {};
    if (s.release_serial.load(std::memory_order_acquire) != s.accepted_release_serial) {
        s.fail(ERROR_OPERATION_ABORTED); return {};
    }
    if (s.window_open) { s.fail(ERROR_INVALID_STATE); return {}; }
    const auto now = monotonic_ns();
    if (now - s.checked_at_ns >= kIdentityCheckIntervalNs) {
        if (!s.sink->identities_valid()) { s.fail(ERROR_DEVICE_NOT_CONNECTED); return {}; }
        s.checked_at_ns = now;
    }
    // Drain into a finite FIFO, then freeze only the earliest semantic window.
    // Edge bursts cannot overwrite a previous Down or reorder preceding motion.
    for (;;) {
        int device = 0; MouseDevicePacket packet{};
        const int status = s.driver->receive(device, packet);
        if (status == 0) break;
        if (status < 0) { s.fail(ERROR_READ_FAULT); return {}; }
        if (device != s.device) { s.fail(ERROR_INVALID_DATA); return {}; }
        if (s.queue.size() >= kQueueCapacity) { s.fail(ERROR_BUFFER_OVERFLOW); return {}; }
        // Validate format independently of held state before queue ownership.
        virtual_mouse::Pending parsed;
        if (!virtual_mouse::translate({packet.state, packet.flags, packet.rolling, packet.x, packet.y},
                virtual_mouse::Mode::Pass, 0, parsed)) { s.fail(ERROR_NOT_SUPPORTED); return {}; }
        ++s.counters.source_packets;
        if (packet.x || packet.y) ++s.counters.blocked_moves;
        s.queue.push_back({packet, s.counters.source_packets, monotonic_ns()});
    }
    s.window = {};
    if (++s.next_window == 0) ++s.next_window;
    s.window.token = {s.epoch, s.next_window};
    std::int64_t x = 0, y = 0;
    while (!s.queue.empty()) {
        const auto& front = s.queue.front();
        if (now >= front.received_ns && now - front.received_ns > kMaximumSourceAgeNs) {
            s.fail(ERROR_TIMEOUT); return {};
        }
        const bool eventful = front.packet.state || front.packet.rolling;
        if (eventful && s.window.source_begin) break;
        virtual_mouse::Pending parsed;
        if (!virtual_mouse::translate({front.packet.state, front.packet.flags, front.packet.rolling,
                front.packet.x, front.packet.y}, virtual_mouse::Mode::Pass, s.physical_buttons, parsed)) {
            s.fail(ERROR_NOT_SUPPORTED); return {};
        }
        x += parsed.x; y += parsed.y;
        if (x < INT_MIN || x > INT_MAX || y < INT_MIN || y > INT_MAX) {
            s.fail(ERROR_ARITHMETIC_OVERFLOW); return {};
        }
        if (!s.window.source_begin) {
            s.window.source_begin = front.sequence;
            s.window.first_source_ns = front.received_ns;
        }
        s.window.source_end = s.last_consumed_source = front.sequence;
        s.physical_buttons = parsed.buttons;
        if (front.packet.state & 1u) s.synthetic = false;
        s.window.wheel = parsed.wheel; s.window.hwheel = parsed.hwheel;
        s.queue.pop_front();
        if (eventful) break;
    }
    s.window.source_counts = {static_cast<int>(x), static_cast<int>(y)};
    s.window.physical_buttons = s.physical_buttons;
    s.window.cutoff_ns = monotonic_ns(); s.window_open = true;
    return {s.window.source_counts, s.last_consumed_source,
        (s.physical_buttons & 1u) != 0, (s.physical_buttons & 2u) != 0};
}
MouseVirtualHidTransport::WindowToken MouseVirtualHidTransport::window_token() const noexcept {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->armed && impl_->window_open ? impl_->window.token : WindowToken{};
}
MouseVirtualHidTransport::WindowInfo MouseVirtualHidTransport::window_info() const noexcept {
    std::lock_guard<std::mutex> lock(impl_->mutex); return impl_->window;
}
unsigned int MouseVirtualHidTransport::physical_buttons() const noexcept {
    std::lock_guard<std::mutex> lock(impl_->mutex); return impl_->physical_buttons;
}
int MouseVirtualHidTransport::selected_device() const noexcept {
    std::lock_guard<std::mutex> lock(impl_->mutex); return impl_->device;
}
bool MouseVirtualHidTransport::submit_frame(MouseSourceCounts counts, bool fire) noexcept {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->commit(counts, fire, impl_->window.token);
}
bool MouseVirtualHidTransport::submit_frame(MouseSourceCounts counts, bool fire, WindowToken token) noexcept {
    std::lock_guard<std::mutex> lock(impl_->mutex); return impl_->commit(counts, fire, token);
}
bool MouseVirtualHidTransport::submit_final(MouseSourceCounts counts) noexcept { return submit_frame(counts, false); }
bool MouseVirtualHidTransport::submit_calibration(MouseSourceCounts counts) noexcept {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto& s = *impl_;
    if (!s.armed || s.error) return false;
    if (!s.window_open) {
        // The explicit F11 probe is the only non-tick producer. It owns a new
        // zero-source window; queued physical reports remain for the next tick.
        s.window = {};
        if (++s.next_window == 0) ++s.next_window;
        s.window.token = {s.epoch, s.next_window};
        s.window.physical_buttons = s.physical_buttons;
        s.window.cutoff_ns = monotonic_ns(); s.window_open = true;
    }
    return s.commit(counts, false, s.window.token);
}
bool MouseVirtualHidTransport::submit_auto_fire(bool pressed) noexcept {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->armed && !impl_->error && !pressed;
}
bool MouseVirtualHidTransport::running() const noexcept {
    std::lock_guard<std::mutex> lock(impl_->mutex); return impl_->ready;
}
bool MouseVirtualHidTransport::intercepting() const noexcept {
    std::lock_guard<std::mutex> lock(impl_->mutex); return impl_->armed;
}
unsigned long MouseVirtualHidTransport::last_error() const noexcept {
    std::lock_guard<std::mutex> lock(impl_->mutex); return impl_->error;
}
MouseWin32TransportStats MouseVirtualHidTransport::stats() const noexcept {
    std::lock_guard<std::mutex> lock(impl_->mutex); return impl_->counters;
}
} // namespace mouse_native
