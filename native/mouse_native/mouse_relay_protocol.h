#pragma once

// Driver/user-mode ABI for the mouse input gate and virtual-mouse output.
// Keep this header independent from WDF and user-mode Windows types.

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace mouse_native {

inline constexpr std::uint32_t kMouseRelayProtocolMagic = 0x32524D43u;  // "CMR2"
inline constexpr std::uint16_t kMouseRelayProtocolVersion = 2;

enum class RelayState : std::uint32_t {
    Bypass = 0,
    Probing = 1,
    AwaitingConfirmation = 2,
    Relay = 3,
    FailOpen = 4,
};

enum class RelayFailureReason : std::uint32_t {
    None = 0,
    ExplicitDisarm = 1,
    OwnerClosed = 2,
    HeartbeatExpired = 3,
    OutputProofFailed = 4,
    VirtualSubmitFailed = 5,
    DeviceRemoved = 6,
    PowerTransition = 7,
    ProtocolViolation = 8,
};

enum MouseButtonBits : std::uint32_t {
    MouseButtonNone = 0,
    MouseButtonLeft = 1u << 0u,
    MouseButtonRight = 1u << 1u,
    MouseButtonMiddle = 1u << 2u,
    MouseButtonX1 = 1u << 3u,
    MouseButtonX2 = 1u << 4u,
};

enum MousePhysicalFlags : std::uint32_t {
    MousePhysicalNone = 0,
    MousePhysicalAbsolute = 1u << 0u,
    MousePhysicalHorizontalWheel = 1u << 1u,
};

#pragma pack(push, 8)

struct ProtocolHeader {
    std::uint32_t magic = kMouseRelayProtocolMagic;
    std::uint16_t version = kMouseRelayProtocolVersion;
    std::uint16_t size = 0;
};

struct MouseRelayToken {
    std::uint64_t lease_id = 0;
    std::uint64_t capture_epoch = 0;
};

struct BeginProbeRequest {
    ProtocolHeader header{kMouseRelayProtocolMagic, kMouseRelayProtocolVersion, 16};
    std::uint64_t client_nonce = 0;
};

struct LeaseReply {
    ProtocolHeader header{kMouseRelayProtocolMagic, kMouseRelayProtocolVersion, 40};
    MouseRelayToken token{};
    std::uint32_t state = static_cast<std::uint32_t>(RelayState::Bypass);
    std::uint32_t heartbeat_timeout_us = 0;
    std::uint64_t reserved = 0;
};

struct LeaseRequest {
    ProtocolHeader header{kMouseRelayProtocolMagic, kMouseRelayProtocolVersion, 24};
    MouseRelayToken token{};
};

struct OutputProofRequest {
    ProtocolHeader header{kMouseRelayProtocolMagic, kMouseRelayProtocolVersion, 32};
    MouseRelayToken token{};
    std::uint64_t proof_nonce = 0;
};

struct ConfirmRelayRequest {
    ProtocolHeader header{kMouseRelayProtocolMagic, kMouseRelayProtocolVersion, 32};
    MouseRelayToken token{};
    std::uint32_t observed_physical_buttons = 0;
    std::uint32_t reserved = 0;
};

// The one final X/Y report produced by MouseActuatorAdapter.  It is not a
// correction to be added to the physical packet.  Buttons and wheel stay on
// the physical path in V1.
struct MouseFinalReportCommand {
    ProtocolHeader header{kMouseRelayProtocolMagic, kMouseRelayProtocolVersion, 48};
    MouseRelayToken token{};
    std::uint64_t report_sequence = 0;
    std::uint64_t through_source_sequence = 0;
    std::int32_t dx_counts = 0;
    std::int32_t dy_counts = 0;
};

struct MouseRelayStatus {
    ProtocolHeader header{kMouseRelayProtocolMagic, kMouseRelayProtocolVersion, 80};
    MouseRelayToken token{};
    std::uint64_t last_heartbeat_monotonic_us = 0;
    std::uint64_t last_captured_sequence = 0;
    std::uint64_t last_report_sequence = 0;
    std::uint64_t last_reported_source_sequence = 0;
    std::uint64_t fail_open_count = 0;
    std::uint32_t state = static_cast<std::uint32_t>(RelayState::Bypass);
    std::uint32_t failure_reason = static_cast<std::uint32_t>(RelayFailureReason::None);
    std::uint32_t physical_buttons = 0;
    std::uint32_t reserved = 0;
};

#pragma pack(pop)

inline bool valid_protocol_header(const ProtocolHeader& header, std::size_t expected_size) {
    return header.magic == kMouseRelayProtocolMagic &&
        header.version == kMouseRelayProtocolVersion &&
        header.size == expected_size;
}

static_assert(sizeof(ProtocolHeader) == 8);
static_assert(sizeof(MouseRelayToken) == 16);
static_assert(sizeof(BeginProbeRequest) == 16);
static_assert(sizeof(LeaseReply) == 40);
static_assert(sizeof(LeaseRequest) == 24);
static_assert(sizeof(OutputProofRequest) == 32);
static_assert(sizeof(ConfirmRelayRequest) == 32);
static_assert(sizeof(MouseFinalReportCommand) == 48);
static_assert(sizeof(MouseRelayStatus) == 80);
static_assert(std::is_standard_layout_v<MouseFinalReportCommand>);
static_assert(std::is_trivially_copyable_v<MouseFinalReportCommand>);

}  // namespace mouse_native
