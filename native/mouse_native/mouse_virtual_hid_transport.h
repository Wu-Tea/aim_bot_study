#pragma once

#include "mouse_native/mouse_interception_transport.h"
#include "mouse_link/virtual_mouse_packets.h"
#include <string>

namespace mouse_native {

// Injectable output boundary. The real implementation proves distinct physical
// and virtual Raw Input identities before capture; offline tests never open HID.
class MouseVirtualHidSink {
public:
    virtual ~MouseVirtualHidSink() = default;
    virtual bool open(const std::wstring& physical_hardware) = 0;
    virtual void close() noexcept = 0;
    virtual bool identities_valid() const noexcept = 0;
    // A conservative all-buttons-up activation boundary, not source sampling.
    virtual bool buttons_released() const noexcept = 0;
    virtual int axis_limit() const noexcept = 0;
    virtual bool send(const virtual_mouse::Report& report) noexcept = 0;
    virtual unsigned long last_error() const noexcept = 0;
};

class MouseVirtualHidTransport final : public MouseUserTransport {
public:
    struct WindowToken {
        std::uint64_t epoch = 0;
        std::uint64_t sequence = 0;
    };
    struct WindowInfo {
        WindowToken token{};
        std::uint64_t source_begin = 0, source_end = 0;
        std::uint64_t first_source_ns = 0, cutoff_ns = 0, submitted_ns = 0;
        MouseSourceCounts source_counts{}, final_counts{};
        MouseSourceCounts submitted_counts{}, cancelled_counts{};
        std::uint64_t reports_submitted = 0;
        unsigned int physical_buttons = 0;
        int wheel = 0, hwheel = 0;
        int submitted_wheel = 0, submitted_hwheel = 0;
        int cancelled_wheel = 0, cancelled_hwheel = 0;
        bool committed = false, cancelled = false;
    };

    explicit MouseVirtualHidTransport(std::unique_ptr<MousePacketDriver> driver = {},
        std::unique_ptr<MouseVirtualHidSink> sink = {});
    ~MouseVirtualHidTransport();
    MouseVirtualHidTransport(const MouseVirtualHidTransport&) = delete;
    MouseVirtualHidTransport& operator=(const MouseVirtualHidTransport&) = delete;

    // Preflight only: identity/output handles are opened, no capture or reports.
    bool start(int device = 0, const std::wstring& hardware = {});
    void stop() noexcept;
    bool enable_interception() noexcept override;
    void release_interception() noexcept override;
    MouseWin32DebugSource read_source() noexcept override;
    WindowToken window_token() const noexcept;
    WindowInfo window_info() const noexcept;
    unsigned int physical_buttons() const noexcept;
    int selected_device() const noexcept;

    bool submit_frame(MouseSourceCounts counts, bool auto_fire) noexcept;
    bool submit_frame(MouseSourceCounts counts, bool auto_fire, WindowToken token) noexcept;
    bool submit_final(MouseSourceCounts counts) noexcept override;
    // Probe may have no source window; return consumes the existing window.
    bool submit_calibration(MouseSourceCounts counts) noexcept override;
    // No separate synthetic edge is allowed. false is a compatibility no-op;
    // true is rejected: production fire intent must accompany submit_frame.
    bool submit_auto_fire(bool pressed) noexcept override;
    bool running() const noexcept override;
    bool intercepting() const noexcept override;
    unsigned long last_error() const noexcept override;
    MouseWin32TransportStats stats() const noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace mouse_native
