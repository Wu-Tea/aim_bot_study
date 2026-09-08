#pragma once

#include "mouse_native/mouse_relay_protocol.h"

#include <cstdint>
#include <optional>

namespace mouse_native {

struct MousePhysicalPacket {
    std::uint64_t sequence = 0;
    std::uint64_t observed_at_monotonic_us = 0;
    std::int32_t dx_counts = 0;
    std::int32_t dy_counts = 0;
    std::int32_t wheel_counts = 0;
    std::uint32_t button_down = 0;
    std::uint32_t button_up = 0;
    std::uint32_t flags = MousePhysicalNone;
};

enum class DeliveryPath : std::uint32_t {
    PhysicalBypass = 0,
    RelaySplit = 1,
};

enum class FinalReportDisposition : std::uint32_t {
    Accepted = 0,
    NotRelaying = 1,
    InvalidProtocol = 2,
    WrongOwner = 3,
    WrongLease = 4,
    WrongEpoch = 5,
    NonMonotonicReport = 6,
    SourceSequenceRegressed = 7,
    UnknownSourceSequence = 8,
};

struct MouseRelayContractConfig {
    std::uint64_t heartbeat_timeout_us = 100'000;
};

struct PacketDelivery {
    DeliveryPath path = DeliveryPath::PhysicalBypass;
    // The original device path. In Relay only X/Y are zeroed; physical
    // buttons and wheel remain native.
    MousePhysicalPacket physical_packet{};
    // The source packet supplied to user mode for final-T calculation.
    MousePhysicalPacket captured_packet{};
    bool captured_motion = false;
};

class MouseRelayContract {
public:
    explicit MouseRelayContract(MouseRelayContractConfig config = {});

    RelayState state() const;
    RelayFailureReason failure_reason() const;
    MouseRelayToken token() const;
    MouseRelayStatus status() const;

    std::optional<MouseRelayToken> begin_probe(
        std::uint64_t owner_file_id,
        std::uint64_t lease_id,
        std::uint64_t now_us);
    bool heartbeat(
        std::uint64_t owner_file_id,
        MouseRelayToken token,
        std::uint64_t now_us);
    bool record_output_proof(
        std::uint64_t owner_file_id,
        MouseRelayToken token,
        bool successful,
        std::uint64_t now_us);
    bool confirm_relay(
        std::uint64_t owner_file_id,
        MouseRelayToken token,
        std::uint32_t observed_physical_buttons,
        std::uint64_t now_us);
    bool disarm(
        std::uint64_t owner_file_id,
        MouseRelayToken token,
        std::uint64_t now_us);
    void owner_cleanup(std::uint64_t owner_file_id, std::uint64_t now_us);
    void report_fault(RelayFailureReason reason, std::uint64_t now_us);
    void tick(std::uint64_t now_us);

    PacketDelivery process_physical(
        const MousePhysicalPacket& packet,
        std::uint64_t now_us);
    FinalReportDisposition submit_final_report(
        std::uint64_t owner_file_id,
        const MouseFinalReportCommand& command,
        std::uint64_t now_us);

private:
    bool matches_owner(std::uint64_t owner_file_id) const;
    bool matches_token(MouseRelayToken token) const;
    bool lease_is_live(std::uint64_t now_us) const;
    void transition_to_bypass(RelayFailureReason reason);
    void transition_to_fail_open(RelayFailureReason reason);
    void update_physical_buttons(const MousePhysicalPacket& packet);
    void clear_relay_progress();

    MouseRelayContractConfig config_{};
    RelayState state_ = RelayState::Bypass;
    RelayFailureReason failure_reason_ = RelayFailureReason::None;
    std::uint64_t owner_file_id_ = 0;
    MouseRelayToken token_{};
    std::uint64_t next_capture_epoch_ = 1;
    std::uint64_t last_heartbeat_us_ = 0;
    std::uint64_t last_captured_sequence_ = 0;
    std::uint64_t last_report_sequence_ = 0;
    std::uint64_t last_reported_source_sequence_ = 0;
    std::uint64_t fail_open_count_ = 0;
    std::uint32_t physical_buttons_ = 0;
};

}  // namespace mouse_native
