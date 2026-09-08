#include "mouse_native/mouse_control_types.h"
#include "mouse_native/mouse_relay_contract.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef MOUSE_RELAY_FIXTURE_PATH
#define MOUSE_RELAY_FIXTURE_PATH ""
#endif

namespace {

using mouse_native::DeliveryPath;
using mouse_native::FinalReportDisposition;
using mouse_native::MouseButtonLeft;
using mouse_native::MouseButtonNone;
using mouse_native::MouseButtonRight;
using mouse_native::MouseFinalReportCommand;
using mouse_native::MousePhysicalPacket;
using mouse_native::MouseRelayContract;
using mouse_native::MouseRelayContractConfig;
using mouse_native::MouseRelayToken;
using mouse_native::RelayFailureReason;
using mouse_native::RelayState;

constexpr std::uint64_t kOwner = 0xA11CEu;
constexpr std::uint64_t kLease = 0xBEEFu;

void require_true(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

MouseRelayToken arm(MouseRelayContract& relay, std::uint64_t now_us = 1'000) {
    const auto token = relay.begin_probe(kOwner, kLease, now_us);
    require_true(token.has_value(), "begin_probe must create a lease");
    require_true(relay.state() == RelayState::Probing, "probe must remain physical bypass");
    require_true(
        relay.record_output_proof(kOwner, *token, true, now_us + 1),
        "virtual output proof must pass before relay");
    require_true(
        relay.confirm_relay(kOwner, *token, MouseButtonNone, now_us + 2),
        "neutral confirmation must arm relay");
    return *token;
}

bool packets_equal(const MousePhysicalPacket& left, const MousePhysicalPacket& right) {
    return left.sequence == right.sequence &&
        left.observed_at_monotonic_us == right.observed_at_monotonic_us &&
        left.dx_counts == right.dx_counts && left.dy_counts == right.dy_counts &&
        left.wheel_counts == right.wheel_counts &&
        left.button_down == right.button_down && left.button_up == right.button_up &&
        left.flags == right.flags;
}

MouseFinalReportCommand final_report(
    MouseRelayToken token,
    const MousePhysicalPacket& source) {
    MouseFinalReportCommand command;
    command.token = token;
    command.report_sequence = source.sequence;
    command.through_source_sequence = source.sequence;
    command.dx_counts = source.dx_counts;
    command.dy_counts = source.dy_counts;
    return command;
}

void test_protocol_and_profile_are_driver_neutral() {
    MouseFinalReportCommand command;
    require_true(
        mouse_native::valid_protocol_header(command.header, sizeof(command)),
        "final report must carry the current ABI header");
    mouse_native::MouseResponseProfile profile;
    profile.px_per_count_x = 0.5f;
    profile.px_per_count_y = 0.5f;
    profile.counts_per_u_second_x = 1'000.0f;
    profile.counts_per_u_second_y = 1'000.0f;
    profile.confidence = 1.0f;
    profile.generation = 1;
    profile.calibrated = true;
    require_true(mouse_native::valid(profile), "profile must not depend on Windows types");
}

void test_default_and_probe_paths_are_exact_physical_bypass() {
    MouseRelayContract relay;
    MousePhysicalPacket packet{1, 100, 3, -4, 120, MouseButtonLeft, 0, 0};
    auto delivery = relay.process_physical(packet, 100);
    require_true(delivery.path == DeliveryPath::PhysicalBypass, "startup must bypass");
    require_true(
        packets_equal(delivery.physical_packet, packet),
        "startup bypass must preserve the source packet");

    const auto token = relay.begin_probe(kOwner, kLease, 200);
    require_true(token.has_value(), "probe lease must be issued");
    packet.sequence = 2;
    packet.button_down = 0;
    delivery = relay.process_physical(packet, 201);
    require_true(delivery.path == DeliveryPath::PhysicalBypass, "probing must bypass");
    require_true(
        packets_equal(delivery.physical_packet, packet),
        "probing must not modify physical input");
}

void test_relay_splits_motion_from_native_buttons_and_wheel() {
    MouseRelayContract relay;
    arm(relay);
    MousePhysicalPacket packet{1, 1'003, 7, -5, 120, MouseButtonRight, 0, 0};
    const auto delivery = relay.process_physical(packet, 1'003);
    require_true(delivery.path == DeliveryPath::RelaySplit, "armed input must use split relay");
    require_true(delivery.captured_motion, "source X/Y must be captured for the controller");
    require_true(
        packets_equal(delivery.captured_packet, packet),
        "controller must receive the unmodified source packet");
    require_true(
        delivery.physical_packet.dx_counts == 0 && delivery.physical_packet.dy_counts == 0,
        "physical X/Y must be suppressed in Relay");
    require_true(
        delivery.physical_packet.button_down == MouseButtonRight &&
            delivery.physical_packet.wheel_counts == 120,
        "buttons and wheel must stay on the native physical path");
}

void test_fixture_round_trip_uses_one_final_report_not_addition() {
    MouseRelayContract relay;
    const auto token = arm(relay);
    std::ifstream input(MOUSE_RELAY_FIXTURE_PATH);
    require_true(input.good(), "transparent relay fixture must be readable");

    std::int64_t source_x = 0;
    std::int64_t source_y = 0;
    std::int64_t virtual_x = 0;
    std::int64_t virtual_y = 0;
    std::uint64_t now_us = 2'000;
    std::uint64_t sequence = 0;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::stringstream row(line);
        std::vector<std::int64_t> values;
        std::string cell;
        while (std::getline(row, cell, ',')) values.push_back(std::stoll(cell));
        require_true(values.size() == 7, "fixture row must contain seven fields");
        now_us += static_cast<std::uint64_t>(values[0]);
        MousePhysicalPacket packet{
            ++sequence,
            now_us,
            static_cast<std::int32_t>(values[1]),
            static_cast<std::int32_t>(values[2]),
            static_cast<std::int32_t>(values[3]),
            static_cast<std::uint32_t>(values[4]),
            static_cast<std::uint32_t>(values[5]),
            static_cast<std::uint32_t>(values[6])};
        const auto delivery = relay.process_physical(packet, now_us);
        require_true(delivery.path == DeliveryPath::RelaySplit, "fixture must stay in relay");
        require_true(
            delivery.physical_packet.dx_counts == 0 && delivery.physical_packet.dy_counts == 0,
            "source movement must not also leak through the physical path");
        const auto command = final_report(token, delivery.captured_packet);
        require_true(
            relay.submit_final_report(kOwner, command, now_us) ==
                FinalReportDisposition::Accepted,
            "one final report must be accepted for each captured source sequence");
        source_x += packet.dx_counts;
        source_y += packet.dy_counts;
        virtual_x += command.dx_counts;
        virtual_y += command.dy_counts;
    }
    require_true(sequence > 0, "fixture must contain input");
    require_true(
        source_x == virtual_x && source_y == virtual_y,
        "transparent final-T reports must conserve source counts exactly");
}

void test_one_million_packet_round_trip() {
    MouseRelayContract relay;
    const auto token = arm(relay, 10'000);
    std::uint64_t now_us = 10'002;
    std::int64_t source_x = 0;
    std::int64_t source_y = 0;
    std::int64_t output_x = 0;
    std::int64_t output_y = 0;

    for (std::uint64_t index = 1; index <= 1'000'000; ++index) {
        ++now_us;
        if (index % 25'000 == 0) {
            require_true(relay.heartbeat(kOwner, token, now_us), "heartbeat must retain lease");
        }
        MousePhysicalPacket packet;
        packet.sequence = index;
        packet.observed_at_monotonic_us = now_us;
        packet.dx_counts = static_cast<std::int32_t>((index * 17u) % 11u) - 5;
        packet.dy_counts = static_cast<std::int32_t>((index * 29u) % 13u) - 6;
        const auto delivery = relay.process_physical(packet, now_us);
        const auto command = final_report(token, delivery.captured_packet);
        require_true(
            relay.submit_final_report(kOwner, command, now_us) ==
                FinalReportDisposition::Accepted,
            "final report sequence must be accepted once");
        source_x += packet.dx_counts;
        source_y += packet.dy_counts;
        output_x += command.dx_counts;
        output_y += command.dy_counts;
    }
    require_true(
        source_x == output_x && source_y == output_y,
        "million-report final-T round trip must conserve counts");
}

void test_final_report_requires_current_owner_epoch_and_captured_sequence() {
    MouseRelayContract relay;
    const auto token = arm(relay, 1'000);
    MouseFinalReportCommand command;
    command.token = token;
    command.report_sequence = 1;
    command.through_source_sequence = 0;
    command.dx_counts = 1;
    require_true(
        relay.submit_final_report(kOwner, command, 1'003) ==
            FinalReportDisposition::Accepted,
        "controller must be able to emit AI output without a mouse packet");

    const MousePhysicalPacket packet{1, 1'004, 2, 3, 0, 0, 0, 0};
    relay.process_physical(packet, 1'004);
    command.report_sequence = 2;
    command.through_source_sequence = 1;
    require_true(
        relay.submit_final_report(kOwner + 1, command, 1'004) ==
            FinalReportDisposition::WrongOwner,
        "foreign owner must be rejected");
    ++command.token.capture_epoch;
    require_true(
        relay.submit_final_report(kOwner, command, 1'004) ==
            FinalReportDisposition::WrongEpoch,
        "foreign capture epoch must be rejected");
    command.token = token;
    command.through_source_sequence = 2;
    require_true(
        relay.submit_final_report(kOwner, command, 1'004) ==
            FinalReportDisposition::UnknownSourceSequence,
        "future source sequence must be rejected");
    command.through_source_sequence = 1;
    require_true(
        relay.submit_final_report(kOwner, command, 1'004) ==
            FinalReportDisposition::Accepted,
        "captured source sequence must accept one final report");
    require_true(
        relay.submit_final_report(kOwner, command, 1'004) ==
            FinalReportDisposition::NonMonotonicReport,
        "same output report must never replay");
    command.report_sequence = 3;
    require_true(
        relay.submit_final_report(kOwner, command, 1'005) ==
            FinalReportDisposition::Accepted,
        "a new controller tick may output AI with no new source packet");
    command.report_sequence = 4;
    command.through_source_sequence = 0;
    require_true(
        relay.submit_final_report(kOwner, command, 1'006) ==
            FinalReportDisposition::SourceSequenceRegressed,
        "a later output must not claim an older source-consumption point");
}

void test_timeout_and_owner_cleanup_restore_exact_physical_path() {
    MouseRelayContractConfig config;
    config.heartbeat_timeout_us = 100;
    MouseRelayContract relay(config);
    const auto old_token = arm(relay, 1'000);
    relay.tick(1'100);
    require_true(relay.state() == RelayState::FailOpen, "timeout must release relay");
    require_true(
        relay.failure_reason() == RelayFailureReason::HeartbeatExpired,
        "timeout reason must remain observable");
    require_true(!relay.heartbeat(kOwner, old_token, 1'101), "old lease must not reactivate");

    MousePhysicalPacket packet{1, 1'102, 5, -6, 120, MouseButtonRight, 0, 0};
    auto delivery = relay.process_physical(packet, 1'102);
    require_true(delivery.path == DeliveryPath::PhysicalBypass, "timeout must bypass");
    require_true(
        packets_equal(delivery.physical_packet, packet),
        "timeout must restore exact native input");

    MouseRelayContract owner_relay;
    arm(owner_relay, 2'000);
    owner_relay.owner_cleanup(kOwner, 2'003);
    require_true(owner_relay.state() == RelayState::FailOpen, "owner close must release relay");
    packet.sequence = 2;
    packet.observed_at_monotonic_us = 2'004;
    delivery = owner_relay.process_physical(packet, 2'004);
    require_true(
        packets_equal(delivery.physical_packet, packet),
        "owner close must restore exact physical packets");
}

void test_explicit_disarm_is_immediate_and_keeps_buttons_native() {
    MouseRelayContract relay;
    const auto token = arm(relay, 1'000);
    const MousePhysicalPacket down{1, 1'003, 4, 5, 0, MouseButtonLeft, 0, 0};
    auto delivery = relay.process_physical(down, 1'003);
    require_true(
        delivery.physical_packet.button_down == MouseButtonLeft,
        "relay must never own physical button state");
    require_true(relay.disarm(kOwner, token, 1'004), "emergency path must disarm current lease");
    const MousePhysicalPacket up{2, 1'005, 6, 7, 0, 0, MouseButtonLeft, 0};
    delivery = relay.process_physical(up, 1'005);
    require_true(delivery.path == DeliveryPath::PhysicalBypass, "disarm must be immediate");
    require_true(
        packets_equal(delivery.physical_packet, up),
        "button release and movement must remain native after disarm");
}

}  // namespace

int main() {
    try {
        test_protocol_and_profile_are_driver_neutral();
        test_default_and_probe_paths_are_exact_physical_bypass();
        test_relay_splits_motion_from_native_buttons_and_wheel();
        test_fixture_round_trip_uses_one_final_report_not_addition();
        test_one_million_packet_round_trip();
        test_final_report_requires_current_owner_epoch_and_captured_sequence();
        test_timeout_and_owner_cleanup_restore_exact_physical_path();
        test_explicit_disarm_is_immediate_and_keeps_buttons_native();
        std::cout << "[MouseRelayContractTests] PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[MouseRelayContractTests][FAIL] " << error.what() << '\n';
        return 1;
    }
}
