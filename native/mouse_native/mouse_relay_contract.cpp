#include "mouse_native/mouse_relay_contract.h"

#include <algorithm>

namespace mouse_native {
namespace {

bool token_equal(MouseRelayToken left, MouseRelayToken right) {
    return left.lease_id == right.lease_id &&
        left.capture_epoch == right.capture_epoch;
}

}  // namespace

MouseRelayContract::MouseRelayContract(MouseRelayContractConfig config)
    : config_(config) {
    config_.heartbeat_timeout_us = std::max<std::uint64_t>(1, config_.heartbeat_timeout_us);
}

RelayState MouseRelayContract::state() const { return state_; }
RelayFailureReason MouseRelayContract::failure_reason() const { return failure_reason_; }
MouseRelayToken MouseRelayContract::token() const { return token_; }

MouseRelayStatus MouseRelayContract::status() const {
    MouseRelayStatus value;
    value.token = token_;
    value.last_heartbeat_monotonic_us = last_heartbeat_us_;
    value.last_captured_sequence = last_captured_sequence_;
    value.last_report_sequence = last_report_sequence_;
    value.last_reported_source_sequence = last_reported_source_sequence_;
    value.fail_open_count = fail_open_count_;
    value.state = static_cast<std::uint32_t>(state_);
    value.failure_reason = static_cast<std::uint32_t>(failure_reason_);
    value.physical_buttons = physical_buttons_;
    return value;
}

std::optional<MouseRelayToken> MouseRelayContract::begin_probe(
    std::uint64_t owner_file_id,
    std::uint64_t lease_id,
    std::uint64_t now_us) {
    if (owner_file_id == 0 || lease_id == 0 ||
        (state_ != RelayState::Bypass && state_ != RelayState::FailOpen)) {
        return std::nullopt;
    }
    owner_file_id_ = owner_file_id;
    token_.lease_id = lease_id;
    token_.capture_epoch = next_capture_epoch_++;
    last_heartbeat_us_ = now_us;
    failure_reason_ = RelayFailureReason::None;
    state_ = RelayState::Probing;
    clear_relay_progress();
    return token_;
}

bool MouseRelayContract::heartbeat(
    std::uint64_t owner_file_id,
    MouseRelayToken token,
    std::uint64_t now_us) {
    if (!matches_owner(owner_file_id) || !matches_token(token) ||
        state_ == RelayState::Bypass || state_ == RelayState::FailOpen) {
        return false;
    }
    if (!lease_is_live(now_us)) {
        transition_to_fail_open(RelayFailureReason::HeartbeatExpired);
        return false;
    }
    last_heartbeat_us_ = now_us;
    return true;
}

bool MouseRelayContract::record_output_proof(
    std::uint64_t owner_file_id,
    MouseRelayToken token,
    bool successful,
    std::uint64_t now_us) {
    tick(now_us);
    if (state_ != RelayState::Probing || !matches_owner(owner_file_id) ||
        !matches_token(token)) {
        return false;
    }
    if (!successful) {
        transition_to_fail_open(RelayFailureReason::OutputProofFailed);
        return false;
    }
    state_ = RelayState::AwaitingConfirmation;
    return true;
}

bool MouseRelayContract::confirm_relay(
    std::uint64_t owner_file_id,
    MouseRelayToken token,
    std::uint32_t observed_physical_buttons,
    std::uint64_t now_us) {
    tick(now_us);
    if (state_ != RelayState::AwaitingConfirmation ||
        !matches_owner(owner_file_id) || !matches_token(token) ||
        observed_physical_buttons != MouseButtonNone ||
        physical_buttons_ != MouseButtonNone) {
        return false;
    }
    state_ = RelayState::Relay;
    clear_relay_progress();
    return true;
}

bool MouseRelayContract::disarm(
    std::uint64_t owner_file_id,
    MouseRelayToken token,
    std::uint64_t now_us) {
    tick(now_us);
    if (!matches_owner(owner_file_id) || !matches_token(token) ||
        state_ == RelayState::Bypass || state_ == RelayState::FailOpen) {
        return false;
    }
    transition_to_bypass(RelayFailureReason::ExplicitDisarm);
    return true;
}

void MouseRelayContract::owner_cleanup(std::uint64_t owner_file_id, std::uint64_t now_us) {
    tick(now_us);
    if (matches_owner(owner_file_id) && state_ != RelayState::Bypass &&
        state_ != RelayState::FailOpen) {
        transition_to_fail_open(RelayFailureReason::OwnerClosed);
    }
}

void MouseRelayContract::report_fault(RelayFailureReason reason, std::uint64_t /*now_us*/) {
    if (reason == RelayFailureReason::None || reason == RelayFailureReason::ExplicitDisarm) {
        reason = RelayFailureReason::ProtocolViolation;
    }
    transition_to_fail_open(reason);
}

void MouseRelayContract::tick(std::uint64_t now_us) {
    if (state_ != RelayState::Bypass && state_ != RelayState::FailOpen &&
        !lease_is_live(now_us)) {
        transition_to_fail_open(RelayFailureReason::HeartbeatExpired);
    }
}

PacketDelivery MouseRelayContract::process_physical(
    const MousePhysicalPacket& packet,
    std::uint64_t now_us) {
    tick(now_us);
    update_physical_buttons(packet);

    PacketDelivery delivery;
    delivery.physical_packet = packet;
    if (state_ != RelayState::Relay) {
        delivery.path = DeliveryPath::PhysicalBypass;
        return delivery;
    }

    delivery.path = DeliveryPath::RelaySplit;
    delivery.captured_packet = packet;
    delivery.captured_motion = packet.dx_counts != 0 || packet.dy_counts != 0;
    delivery.physical_packet.dx_counts = 0;
    delivery.physical_packet.dy_counts = 0;
    last_captured_sequence_ = std::max(last_captured_sequence_, packet.sequence);
    return delivery;
}

FinalReportDisposition MouseRelayContract::submit_final_report(
    std::uint64_t owner_file_id,
    const MouseFinalReportCommand& command,
    std::uint64_t now_us) {
    tick(now_us);
    if (state_ != RelayState::Relay) return FinalReportDisposition::NotRelaying;
    if (!valid_protocol_header(command.header, sizeof(MouseFinalReportCommand))) {
        return FinalReportDisposition::InvalidProtocol;
    }
    if (!matches_owner(owner_file_id)) return FinalReportDisposition::WrongOwner;
    if (command.token.lease_id != token_.lease_id) return FinalReportDisposition::WrongLease;
    if (command.token.capture_epoch != token_.capture_epoch) {
        return FinalReportDisposition::WrongEpoch;
    }
    if (command.report_sequence == 0 ||
        command.report_sequence <= last_report_sequence_) {
        return FinalReportDisposition::NonMonotonicReport;
    }
    if (command.through_source_sequence < last_reported_source_sequence_) {
        return FinalReportDisposition::SourceSequenceRegressed;
    }
    if (command.through_source_sequence > last_captured_sequence_) {
        return FinalReportDisposition::UnknownSourceSequence;
    }
    last_report_sequence_ = command.report_sequence;
    last_reported_source_sequence_ = command.through_source_sequence;
    return FinalReportDisposition::Accepted;
}

bool MouseRelayContract::matches_owner(std::uint64_t owner_file_id) const {
    return owner_file_id_ != 0 && owner_file_id == owner_file_id_;
}

bool MouseRelayContract::matches_token(MouseRelayToken token) const {
    return token_equal(token, token_);
}

bool MouseRelayContract::lease_is_live(std::uint64_t now_us) const {
    return now_us >= last_heartbeat_us_ &&
        now_us - last_heartbeat_us_ < config_.heartbeat_timeout_us;
}

void MouseRelayContract::transition_to_bypass(RelayFailureReason reason) {
    state_ = RelayState::Bypass;
    failure_reason_ = reason;
    owner_file_id_ = 0;
    token_ = {};
    last_heartbeat_us_ = 0;
    clear_relay_progress();
}

void MouseRelayContract::transition_to_fail_open(RelayFailureReason reason) {
    if (state_ == RelayState::FailOpen) return;
    state_ = RelayState::FailOpen;
    failure_reason_ = reason;
    ++fail_open_count_;
    owner_file_id_ = 0;
    token_ = {};
    last_heartbeat_us_ = 0;
    clear_relay_progress();
}

void MouseRelayContract::update_physical_buttons(const MousePhysicalPacket& packet) {
    physical_buttons_ |= packet.button_down;
    physical_buttons_ &= ~packet.button_up;
}

void MouseRelayContract::clear_relay_progress() {
    last_captured_sequence_ = 0;
    last_report_sequence_ = 0;
    last_reported_source_sequence_ = 0;
}

}  // namespace mouse_native
