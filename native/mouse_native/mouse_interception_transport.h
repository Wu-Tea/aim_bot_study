#pragma once

#include "mouse_native/mouse_win32_debug_transport.h"
#include <string>
#include <vector>

namespace mouse_native {

// Same packet layout as the existing mouse_link backend, kept independent of
// the optional vendor SDK so offline builds do not require a driver or DLL.
struct MouseDevicePacket {
    unsigned short state = 0, flags = 0;
    short rolling = 0;
    int x = 0, y = 0;
    unsigned int information = 0;
};
struct MouseDeviceInfo { int id; std::wstring hardware_id; };

// Driver boundary is injectable for an independent downstream receiver oracle.
class MousePacketDriver {
public:
    virtual ~MousePacketDriver() = default;
    virtual bool open() = 0;
    virtual void close() noexcept = 0;
    virtual std::vector<MouseDeviceInfo> devices() = 0;
    virtual unsigned int initial_buttons() const noexcept { return 0; }
    virtual bool capture(int device, bool enabled) noexcept = 0;
    // Nonblocking receive: 1 packet, 0 empty, -1 failure.
    virtual int receive(int& device, MouseDevicePacket& packet) noexcept = 0;
    // Optional event wait for standalone relays. receive() remains nonblocking;
    // fake/offline drivers may keep the default no-op implementation.
    virtual void wait_for_input(unsigned int timeout_ms) noexcept { (void)timeout_ms; }
    virtual bool send(int device, const MouseDevicePacket& packet) noexcept = 0;
};
std::unique_ptr<MousePacketDriver> make_interception_driver();

class MouseInterceptionTransport : public MouseUserTransport {
public:
    explicit MouseInterceptionTransport(std::unique_ptr<MousePacketDriver> driver = {});
    ~MouseInterceptionTransport();
    bool start(int device = 0);
    void stop() noexcept;
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
    int selected_device() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace mouse_native
