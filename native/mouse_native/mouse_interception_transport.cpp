#include "mouse_native/mouse_interception_transport.h"
#include "mouse_native/mouse_auto_fire_button.h"
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <limits>
#include <mutex>
#include <thread>

namespace mouse_native {
namespace {
constexpr unsigned int kMarker = 0x434F444D;
constexpr unsigned short kAll = 0xffff;
static_assert(sizeof(MouseDevicePacket) == 20, "Interception v1.0.1 ABI");
int selected_predicate(int device) { return device >= 11 && device <= 20; }

class InterceptionDriver final : public MousePacketDriver {
    HMODULE dll_ = nullptr;
    void* context_ = nullptr;
    using Create = void* (__cdecl*)();
    using Destroy = void (__cdecl*)(void*);
    using SetFilter = void (__cdecl*)(void*, int (__cdecl*)(int), unsigned short);
    using Wait = int (__cdecl*)(void*, unsigned long);
    using Receive = int (__cdecl*)(void*, int, MouseDevicePacket*, unsigned int);
    using Send = int (__cdecl*)(void*, int, const MouseDevicePacket*, unsigned int);
    using Hardware = unsigned int (__cdecl*)(void*, int, void*, unsigned int);
    Create create_ = nullptr; Destroy destroy_ = nullptr;
    SetFilter set_filter_ = nullptr;
    Wait wait_ = nullptr; Receive receive_ = nullptr; Send send_ = nullptr; Hardware hardware_ = nullptr;
    // The vendor API takes a function pointer rather than caller context.
    // Calls are serialized; thread-local selection avoids a global device race.
    static thread_local int filter_device_;
    static int predicate(int device) { return device == filter_device_; }
public:
    ~InterceptionDriver() override { close(); }
    bool open() override {
        close();
        wchar_t executable[32768]{};
        if (!GetModuleFileNameW(nullptr, executable, 32768)) return false;
        const auto path = std::filesystem::path(executable).parent_path() / L"interception.dll";
        dll_ = LoadLibraryExW(path.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!dll_) return false;
#define LOAD(member, type, name) member = reinterpret_cast<type>(GetProcAddress(dll_, name)); if (!member) { close(); return false; }
        LOAD(create_, Create, "interception_create_context")
        LOAD(destroy_, Destroy, "interception_destroy_context")
        LOAD(set_filter_, SetFilter, "interception_set_filter")
        LOAD(wait_, Wait, "interception_wait_with_timeout")
        LOAD(receive_, Receive, "interception_receive")
        LOAD(send_, Send, "interception_send")
        LOAD(hardware_, Hardware, "interception_get_hardware_id")
#undef LOAD
        context_ = create_();
        if (!context_) { close(); return false; }
        return true;
    }
    void close() noexcept override {
        if (context_) {
            set_filter_(context_, selected_predicate, 0);
            destroy_(context_); context_ = nullptr;
        }
        if (dll_) { FreeLibrary(dll_); dll_ = nullptr; }
    }
    std::vector<MouseDeviceInfo> devices() override {
        std::vector<MouseDeviceInfo> result;
        if (context_) for (int id = 11; id <= 20; ++id) {
            wchar_t hardware[512]{};
            if (hardware_(context_, id, hardware, sizeof(hardware))) {
                hardware[511] = 0; result.push_back({id, hardware});
            }
        }
        return result;
    }
    unsigned int initial_buttons() const noexcept override {
        return ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) ? 1u : 0u) |
            ((GetAsyncKeyState(VK_RBUTTON) & 0x8000) ? 4u : 0u);
    }
    bool capture(int device, bool enabled) noexcept override {
        if (!context_ || device < 11 || device > 20) return false;
        filter_device_ = device;
        // The pinned SDK setter synchronously calls DeviceIoControl for the
        // selected device. Its GET_FILTER path returned success with zero
        // output bytes on the live system and stranded process teardown. Do
        // not issue that query to infer whether capture is active. Check the
        // setter's Win32 failure and prove capture with received source data
        // plus an independent observer at the relay boundary.
        SetLastError(ERROR_SUCCESS);
        set_filter_(context_, predicate, enabled ? kAll : 0);
        return GetLastError() == ERROR_SUCCESS;
    }
    int receive(int& device, MouseDevicePacket& packet) noexcept override {
        if (!context_) return -1;
        device = wait_(context_, 0);
        return device == 0 ? 0 : receive_(context_, device, &packet, 1) == 1 ? 1 : -1;
    }
    void wait_for_input(unsigned int timeout_ms) noexcept override {
        if (context_) wait_(context_, timeout_ms);
    }
    bool send(int device, const MouseDevicePacket& packet) noexcept override {
        return context_ && send_(context_, device, &packet, 1) == 1;
    }
};
thread_local int InterceptionDriver::filter_device_ = 0;
}

std::unique_ptr<MousePacketDriver> make_interception_driver() { return std::make_unique<InterceptionDriver>(); }

struct MouseInterceptionTransport::Impl {
    std::unique_ptr<MousePacketDriver> driver;
    mutable std::mutex mutex;
    std::thread worker;
    std::atomic_bool stop{false};
    bool ready = false, armed = false, right = false;
    int device = 0;
    unsigned long error = 0;
    std::int64_t dx = 0, dy = 0;
    ULONGLONG heartbeat = 0;
    MouseAutoFireButton fire;
    MouseWin32TransportStats counters{};
    explicit Impl(std::unique_ptr<MousePacketDriver> d) : driver(std::move(d)) {}

    bool fire_output(bool requested) {
        const auto edge = fire.next_edge(requested);
        if (edge == MouseAutoFireEdge::None) return true;
        MouseDevicePacket p{}; p.information = kMarker;
        p.state = edge == MouseAutoFireEdge::Down ? 1 : 2;
        if (!driver->send(device, p)) return false;
        if (edge == MouseAutoFireEdge::Down) { fire.accept_down(armed, requested); ++counters.auto_fire_downs; }
        else { fire.accept_up(); ++counters.auto_fire_ups; }
        return true;
    }
    // All operations are serialized with send: no final output can slip past
    // disarm. Captured-but-unconsumed movement is returned once on release.
    void release() {
        armed = false;
        if (!ready) return;
        if (!driver->capture(device, false)) error = ERROR_WRITE_FAULT;
        if (!fire_output(false)) error = ERROR_WRITE_FAULT;
        if (dx || dy) {
            // Accumulation is checked at input, so conversion is exact.
            MouseDevicePacket p{}; p.x = static_cast<int>(dx); p.y = static_cast<int>(dy);
            if (!driver->send(device, p)) error = ERROR_WRITE_FAULT;
            dx = dy = 0;
        }
        // Filter has been removed; only the already captured queue remains.
        int source = 0; MouseDevicePacket p{};
        for (int count = 0; count < 4096; ++count) {
            const int status = driver->receive(source, p);
            if (status == 0) break;
            if (status < 0 || !driver->send(source, p)) { error = ERROR_READ_FAULT; break; }
            if (count == 4095) error = ERROR_BUFFER_OVERFLOW;
        }
        // Close the owning context on release. This also clears filters if
        // filter removal failed and prevents rearming with stale packet state.
        driver->close(); ready = false;
    }
    void consume() {
        int source = 0; MouseDevicePacket packet{};
        // Bound work so a continuous high-rate device cannot starve F12.
        for (int count = 0; count < 256; ++count) {
            const int status = driver->receive(source, packet);
            if (status == 0) return;
            if (status < 0) { error = ERROR_READ_FAULT; release(); return; }
            if (source != device || (packet.flags & ~0x08u) != 0 || packet.information == kMarker) {
                // Unsupported/echoed source cannot be silently interpreted as
                // relative physical counts. Restore it once, then disarm.
                driver->send(source, packet); error = ERROR_INVALID_DATA; release(); return;
            }
            const auto next_x = dx + packet.x, next_y = dy + packet.y;
            if (next_x < INT_MIN || next_x > INT_MAX || next_y < INT_MIN || next_y > INT_MAX) {
                driver->send(source, packet); error = ERROR_ARITHMETIC_OVERFLOW; release(); return;
            }
            dx = next_x; dy = next_y;
            ++counters.source_packets;
            if (packet.x || packet.y) ++counters.blocked_moves;
            if (packet.state & 1) fire.observe_physical(true);
            if (packet.state & 2) fire.observe_physical(false);
            if (packet.state & 4) right = true;
            if (packet.state & 8) right = false;
            // A mixed packet's original movement MUST NOT accompany its native
            // button/wheel. Only the controller's later final packet owns X/Y.
            packet.x = packet.y = 0;
            if (packet.state || packet.rolling) {
                if (!driver->send(source, packet)) { error = ERROR_WRITE_FAULT; release(); return; }
            }
        }
    }
};

MouseInterceptionTransport::MouseInterceptionTransport(std::unique_ptr<MousePacketDriver> driver)
    : impl_(std::make_unique<Impl>(driver ? std::move(driver) : make_interception_driver())) {}
MouseInterceptionTransport::~MouseInterceptionTransport() { stop(); }
bool MouseInterceptionTransport::start(int device) {
    stop();
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto& s = *impl_; s.error = 0; s.counters = {}; s.dx = s.dy = 0;
    if (!s.driver->open()) { s.error = ERROR_OPEN_FAILED; return false; }
    const auto devices = s.driver->devices();
    if (device == 0 && devices.size() == 1) device = devices.front().id;
    if (std::none_of(devices.begin(), devices.end(), [device](const auto& d) { return d.id == device; })) {
        s.error = ERROR_DEVICE_NOT_CONNECTED; s.driver->close(); return false;
    }
    s.device = device; s.ready = true; s.stop = false;
    s.worker = std::thread([this] {
        while (!impl_->stop.load()) {
            {
                std::lock_guard<std::mutex> guard(impl_->mutex);
                if (impl_->armed) {
                    if (GetTickCount64() - impl_->heartbeat > 100) {
                        impl_->error = ERROR_TIMEOUT; impl_->release();
                    } else impl_->consume();
                }
            }
            // The runtime requests a 1 ms timer period. This receiver
            // drains physical reports without limiting the controller to 250 Hz.
            Sleep(1);
        }
    });
    return true;
}
void MouseInterceptionTransport::stop() noexcept {
    release_interception(); impl_->stop = true;
    if (impl_->worker.joinable()) impl_->worker.join();
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->driver->close(); impl_->ready = false;
}
bool MouseInterceptionTransport::enable_interception() noexcept {
    std::lock_guard<std::mutex> lock(impl_->mutex); auto& s = *impl_;
    if (!s.ready || s.error || s.armed) return false;
    if (!s.driver->capture(s.device, true)) { s.error = ERROR_ACCESS_DENIED; s.release(); return false; }
    const unsigned int buttons = s.driver->initial_buttons();
    s.fire.observe_physical((buttons & 1) != 0);
    s.right = (buttons & 4) != 0;
    s.heartbeat = GetTickCount64(); s.armed = true; return true;
}
void MouseInterceptionTransport::release_interception() noexcept {
    std::lock_guard<std::mutex> lock(impl_->mutex); impl_->release();
}
MouseWin32DebugSource MouseInterceptionTransport::read_source() noexcept {
    std::lock_guard<std::mutex> lock(impl_->mutex); auto& s = *impl_;
    s.heartbeat = GetTickCount64();
    MouseWin32DebugSource result{{static_cast<int>(s.dx), static_cast<int>(s.dy)},
        s.counters.source_packets, s.fire.physical_down(), s.right};
    s.dx = s.dy = 0; return result;
}
bool MouseInterceptionTransport::submit_final(MouseSourceCounts counts) noexcept {
    std::lock_guard<std::mutex> lock(impl_->mutex); auto& s = *impl_;
    if (!s.armed || s.error) return false;
    if (!counts.dx && !counts.dy) return true;
    MouseDevicePacket p{}; p.x = counts.dx; p.y = counts.dy;
    p.flags = 0x08; p.information = kMarker;
    if (!s.driver->send(s.device, p)) { s.error = ERROR_WRITE_FAULT; s.release(); return false; }
    ++s.counters.submitted_reports;
    // passed_replacements stays zero: there is no Win32 hook and no game ACK.
    return true;
}
bool MouseInterceptionTransport::submit_calibration(MouseSourceCounts counts) noexcept { return submit_final(counts); }
bool MouseInterceptionTransport::submit_auto_fire(bool pressed) noexcept {
    std::lock_guard<std::mutex> lock(impl_->mutex); auto& s = *impl_;
    if (!s.armed || s.error) return false;
    if (s.fire_output(pressed)) return true;
    s.error = ERROR_WRITE_FAULT; s.release(); return false;
}
bool MouseInterceptionTransport::running() const noexcept { std::lock_guard<std::mutex> lock(impl_->mutex); return impl_->ready; }
bool MouseInterceptionTransport::intercepting() const noexcept { std::lock_guard<std::mutex> lock(impl_->mutex); return impl_->armed; }
unsigned long MouseInterceptionTransport::last_error() const noexcept { std::lock_guard<std::mutex> lock(impl_->mutex); return impl_->error; }
MouseWin32TransportStats MouseInterceptionTransport::stats() const noexcept { std::lock_guard<std::mutex> lock(impl_->mutex); return impl_->counters; }
int MouseInterceptionTransport::selected_device() const noexcept { std::lock_guard<std::mutex> lock(impl_->mutex); return impl_->device; }
} // namespace mouse_native
