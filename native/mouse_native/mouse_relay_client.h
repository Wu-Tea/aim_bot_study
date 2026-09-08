#pragma once

#include "mouse_native/cod_mouse_relay_ioctl.h"
#include "mouse_native/mouse_control_types.h"

#include <cstdint>
#include <vector>

namespace mouse_native {

class MouseRelayClient {
public:
    MouseRelayClient() = default;
    ~MouseRelayClient();

    MouseRelayClient(const MouseRelayClient&) = delete;
    MouseRelayClient& operator=(const MouseRelayClient&) = delete;

    bool open();
    void close() noexcept;
    bool begin(std::uint64_t lease_id);
    bool heartbeat();
    bool arm();
    bool disarm() noexcept;
    static bool emergency_disarm(
        void* native_handle,
        COD_MOUSE_RELAY_TOKEN token) noexcept;
    bool read_batch(std::vector<COD_MOUSE_SOURCE_PACKET>* packets);
    bool submit_final(
        std::uint64_t report_sequence,
        std::uint64_t through_source_sequence,
        MouseSourceCounts counts);
    bool submit_calibration(MouseSourceCounts counts);
    bool status(COD_MOUSE_RELAY_STATUS* relay_status);

    bool is_open() const noexcept;
    bool has_lease() const noexcept;
    bool armed() const noexcept;
    unsigned long last_error() const noexcept;
    COD_MOUSE_RELAY_TOKEN token() const noexcept;
    void* native_handle_for_emergency() const noexcept;

private:
    bool token_request(unsigned long ioctl_code);
    bool ioctl(
        unsigned long code,
        void* input,
        unsigned long input_size,
        void* output,
        unsigned long output_size,
        unsigned long* bytes_returned = nullptr);
    void clear_session() noexcept;

    void* handle_ = nullptr;
    COD_MOUSE_RELAY_TOKEN token_{};
    unsigned long last_error_ = 0;
    bool armed_ = false;
};

}  // namespace mouse_native
