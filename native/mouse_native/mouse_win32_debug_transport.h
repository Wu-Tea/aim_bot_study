#pragma once

#include "mouse_native/mouse_control_types.h"

#include <cstdint>
#include <memory>

namespace mouse_native {

struct MouseWin32DebugSource {
    MouseSourceCounts counts{};
    std::uint64_t sequence = 0;
    bool left_button_down = false;
    bool right_button_down = false;
};

struct MouseWin32TransportStats {
    std::uint64_t source_packets = 0;
    std::uint64_t blocked_moves = 0;
    std::uint64_t submitted_reports = 0;
    std::uint64_t passed_replacements = 0;
    std::uint64_t auto_fire_downs = 0;
    std::uint64_t auto_fire_ups = 0;
};

class MouseUserTransport {
public:
    virtual ~MouseUserTransport() = default;
    virtual bool enable_interception() noexcept = 0;
    virtual void release_interception() noexcept = 0;
    virtual MouseWin32DebugSource read_source() noexcept = 0;
    virtual bool submit_final(MouseSourceCounts counts) noexcept = 0;
    virtual bool submit_calibration(MouseSourceCounts counts) noexcept = 0;
    virtual bool submit_auto_fire(bool pressed) noexcept = 0;
    virtual bool running() const noexcept = 0;
    virtual bool intercepting() const noexcept = 0;
    virtual unsigned long last_error() const noexcept = 0;
    virtual MouseWin32TransportStats stats() const noexcept = 0;
};

// Extends the existing demo's WH_MOUSE_LL interception and independent escape
// mechanism with relative Raw Input capture and tagged replacement movement.
// Buttons/wheel stay native. Game receiver compatibility is a live check.
// The historical class name is retained for source compatibility.
class MouseWin32DebugTransport : public MouseUserTransport {
public:
    MouseWin32DebugTransport();
    ~MouseWin32DebugTransport();

    MouseWin32DebugTransport(const MouseWin32DebugTransport&) = delete;
    MouseWin32DebugTransport& operator=(const MouseWin32DebugTransport&) = delete;

    // Starts the message thread and installs an initially transparent hook.
    bool start();
    void stop() noexcept;

    // Arm only after registering the independent escape key. read_source()
    // renews the 100 ms consumer lease; a stalled consumer releases the hook.
    // Default sensitivity is usable before optional calibration.
    bool enable_interception() noexcept;
    void release_interception() noexcept;

    MouseWin32DebugSource read_source() noexcept;
    bool submit_final(MouseSourceCounts counts) noexcept;
    bool submit_calibration(MouseSourceCounts counts) noexcept;
    bool submit_auto_fire(bool pressed) noexcept;

    bool running() const noexcept;
    bool intercepting() const noexcept;
    unsigned long last_error() const noexcept;
    MouseWin32TransportStats stats() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mouse_native
