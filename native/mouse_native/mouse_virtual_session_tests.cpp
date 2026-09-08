#include "mouse_native/mouse_controller_session.h"
#include "controller_native/incident_fixture_support.h"
#include "mouse_native/mouse_diagnostics.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace mouse_native;
constexpr wchar_t kPhysicalHardware[] = L"HID\\VID_1532&PID_00B8&MI_00";
constexpr int kPhysicalDevice = 13;
bool inject_source_leak = false;

void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

// This fake owns two independent receiving channels. source() delivers natively
// only when capture is off; every driver send() reaches the physical receiver.
// Neither receiver derives its expected movement from the transport's counters.
class SourceDriver final : public MousePacketDriver {
public:
    bool open() override { opened_ = true; return true; }
    void close() noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        opened_ = false;
        captured_ = false;
    }
    std::vector<MouseDeviceInfo> devices() override {
        return {{kPhysicalDevice, kPhysicalHardware}};
    }
    bool capture(int device, bool enabled) noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!opened_ || device != kPhysicalDevice) return false;
        captured_ = enabled;
        return true;
    }
    int receive(int& device, MouseDevicePacket& packet) noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pending_.empty()) return 0;
        device = kPhysicalDevice;
        packet = pending_.front();
        pending_.pop_front();
        return 1;
    }
    bool send(int, const MouseDevicePacket& packet) noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        physical_receiver_.push_back(packet);
        return true;
    }
    void source(MouseDevicePacket packet) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (captured_) pending_.push_back(packet);
        if (!captured_ || inject_source_leak) physical_receiver_.push_back(packet);
    }
    bool captured() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return captured_;
    }
    std::vector<MouseDevicePacket> physical_receiver() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return physical_receiver_;
    }
private:
    mutable std::mutex mutex_;
    bool opened_ = false;
    bool captured_ = false;
    std::deque<MouseDevicePacket> pending_;
    std::vector<MouseDevicePacket> physical_receiver_;
};

class Receiver final : public MouseVirtualHidSink {
public:
    bool open(const std::wstring& physical_hardware) override {
        opened_ = physical_hardware == kPhysicalHardware;
        return opened_;
    }
    void close() noexcept override { opened_ = false; }
    bool identities_valid() const noexcept override { return opened_; }
    bool buttons_released() const noexcept override { return true; }
    int axis_limit() const noexcept override { return 127; }
    bool send(const virtual_mouse::Report& report) noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        received_.push_back(report);
        return true;
    }
    unsigned long last_error() const noexcept override { return 0; }
    std::vector<virtual_mouse::Report> reports() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return received_;
    }
private:
    bool opened_ = false;
    mutable std::mutex mutex_;
    std::vector<virtual_mouse::Report> received_;
};

struct Fixture {
    SourceDriver* source = nullptr;
    Receiver* receiver = nullptr;
    MouseVirtualHidTransport* transport = nullptr;
    std::unique_ptr<MouseControllerSession> session;
    std::uint64_t tick_id = 0;
    std::uint64_t last_window_sequence = 0;
    std::uint64_t last_consumed_source = 0;
    std::size_t allowed_native_packets = 0;

    explicit Fixture(MouseRelayTestMode mode = MouseRelayTestMode::None, bool recoil = false) {
        auto driver = std::make_unique<SourceDriver>();
        auto sink = std::make_unique<Receiver>();
        source = driver.get();
        receiver = sink.get();
        auto virtual_transport = std::make_unique<MouseVirtualHidTransport>(
            std::move(driver), std::move(sink));
        transport = virtual_transport.get();
        // External F12 ownership leaves the test's session registering F11 only.
        // No key is pressed, real input is sampled, or device handle is opened.
        MouseControllerFacadeConfig controller_config;
        controller_config.tuning = {1.5f, 4.0f}; // The production runtime defaults.
        controller_config.recoil = {recoil, 100, true};
        session = std::make_unique<MouseControllerSession>(
            controller_config, MouseSensitivityCalibratorConfig{},
            MouseControllerTransport::VirtualHid, mode, MouseCodDefaultConfig{},
            kPhysicalDevice, kPhysicalHardware, true, std::move(virtual_transport));
    }
    void prepare() {
        check(session->prepare(91, [] {}), "offline session prepare failed (F11 may already be registered)");
        check(session->prepared() && !session->armed() && !source->captured(),
            "prepare must not capture the source");
        check(receiver->reports().empty(), "prepare must not emit a virtual report");
    }
    void arm() {
        prepare();
        check(session->arm() && session->armed() && source->captured(),
            "arm must activate the injected source and independent sink");
    }
    MouseControllerSessionTickResult tick(double now) {
        const std::size_t before = receiver->reports().size();
        const auto result = session->tick(now, ++tick_id);
        check(result.relay_ready && result.output_delivered && !result.transport_failed,
            "real session tick must commit its output successfully");
        const auto reports = receiver->reports();
        // A zero final with unchanged button/wheel state needs no HID write;
        // the logical input window must still commit exactly once below.
        std::int64_t x = 0, y = 0;
        for (std::size_t index = before; index < reports.size(); ++index) {
            x += reports[index].x;
            y += reports[index].y;
        }
        check(x == result.runtime.output.counts.dx && y == result.runtime.output.counts.dy,
            "receiver must see exactly runtime final T, never M+T or a stale final");
        check(source->physical_receiver().size() == allowed_native_packets,
            "RED: a captured source packet or final output escaped to the original physical receiver");
        const auto info = transport->window_info();
        check(info.committed && info.token.sequence > last_window_sequence,
            "each controller tick must commit a fresh input window once");
        if (info.source_begin != 0) {
            check(info.source_begin > last_consumed_source &&
                    info.source_end == result.through_source_sequence,
                "nonempty window must bind the newly consumed source sequence range");
        } else {
            check(info.source_end == 0 && result.through_source_sequence == last_consumed_source,
                "empty window must not claim or consume another physical source packet");
        }
        check(info.final_counts.dx == result.runtime.output.counts.dx &&
                info.final_counts.dy == result.runtime.output.counts.dy,
            "committed window must bind the consumed source sequence and this tick's final");
        last_window_sequence = info.token.sequence;
        last_consumed_source = result.through_source_sequence;
        return result;
    }
};

controller_native::incident_fixture::TargetSpec target_spec(bool fire = false) {
    controller_native::incident_fixture::TargetSpec target;
    target.observation_id = 77;
    target.selector_generation = 9;
    // This fixture exercises authorized assistance, including real AutoFire.
    // A bare detection without enemy evidence correctly receives no AI work.
    target.color_classified = true;
    target.has_enemy_cue = true;
    target.enemy_identity_confirmed = true;
    target.body_width = 48.0f;
    target.body_height = 112.0f;
    target.aim_height_ratio = 0.4f;
    target.fire_authority = fire;
    return target;
}

auto observed(std::uint64_t frame, double now, float error_x, bool fire = false) {
    auto result = controller_native::incident_fixture::observed_snapshot(
        target_spec(fire), frame, now, error_x, 0.0f);
    result.state.auto_fire_requested = fire;
    return result;
}

void test_diagnostic_modes_replace_one_source_once() {
    struct Case { MouseRelayTestMode mode; int x, y; };
    const Case cases[] = {
        {MouseRelayTestMode::Passthrough, 10, -4},
        {MouseRelayTestMode::Block, 0, 0},
        {MouseRelayTestMode::Invert, -10, 4},
    };
    for (const auto& item : cases) {
        Fixture f(item.mode);
        f.arm();
        const auto before = f.receiver->reports().size();
        f.source->source({0, 0, 0, 10, -4, 0});
        const auto result = f.tick(0.100);
        check(result.source_counts.dx == 10 && result.source_counts.dy == -4,
            "diagnostic source must be the original physical counts");
        check(result.runtime.output.counts.dx == item.x && result.runtime.output.counts.dy == item.y,
            "diagnostic mode must select the known pass/block/invert output");
        check(f.receiver->reports().size() == before +
                (item.mode == MouseRelayTestMode::Block ? 0 : 1),
            "small diagnostic movement must produce one report; block must produce no displacement");
        const auto empty = f.tick(0.110);
        check(empty.source_counts.dx == 0 && empty.source_counts.dy == 0 &&
                empty.runtime.output.counts.dx == 0 && empty.runtime.output.counts.dy == 0,
            "next controller window must not reuse consumed source counts");
    }
}

void test_prepared_emergency_and_shutdown_boundaries() {
    Fixture f;
    f.prepare();
    f.source->source({0, 0, 0, 5, -1, 0});
    f.allowed_native_packets = 1;
    const auto unarmed = f.session->tick(0.100, 1);
    check(!unarmed.relay_ready && !unarmed.output_delivered && f.receiver->reports().empty(),
        "prepared unarmed session must not create a second output alongside native input");
    check(f.source->physical_receiver().size() == 1,
        "unarmed input must reach its original receiver once");
    check(f.session->arm(), "prepared session must still arm after an unarmed tick");
    f.tick_id = 1;
    f.source->source({0, 0, 0, 2, 0, 0});
    f.tick(0.110);
    f.session->emergency_release();
    check(!f.session->armed() && !f.source->captured(),
        "emergency release must remove capture before another controller tick");
    const auto released_reports = f.receiver->reports().size();
    f.session->tick(0.120, 3);
    check(f.receiver->reports().size() == released_reports,
        "post-emergency controller tick must not resubmit the prior final");
    f.session->shutdown();
    check(!f.session->prepared() && !f.session->armed(), "shutdown must clear session ownership");
    f.source->source({0, 0, 0, 7, 0, 0});
    const auto native = f.source->physical_receiver();
    check(native.size() == 2 && native.back().x == 7,
        "source must resume native delivery exactly once after shutdown");
}

void test_same_burst_rmb_edges_reach_controller_in_order() {
    Fixture f;
    f.arm();
    // Both edges already exist before the first tick. Final-button-only
    // reduction would erase ADS entry and is therefore a failing mutation.
    f.source->source({0x004, 0, 0, 0, 0, 0});
    f.source->source({0x008, 0, 0, 0, 0, 0});
    f.session->submit_vision_snapshot(observed(1, 0.100, 80.0f));
    const auto down = f.tick(0.100);
    check(f.session->right_button_down() && down.runtime.mode == MouseAimMode::Ads &&
            down.runtime.controller.controller_used &&
            f.session->runtime().facade().controller().last_ai_aim_mode() == "ads_snap",
        "first burst edge must activate actual ADS control");
    check((f.receiver->reports().back().buttons & 2u) != 0,
        "ADS entry must accompany virtual RMB down");
    f.session->submit_vision_snapshot(observed(2, 0.110, 80.0f));
    const auto up = f.tick(0.110);
    check(!f.session->right_button_down() && up.runtime.mode == MouseAimMode::Hipfire &&
            !up.runtime.controller.auto_fire_active &&
            (f.receiver->reports().back().buttons & 3u) == 0,
        "next burst edge must revoke ADS and fire in the controller and receiver");
    check(up.through_source_sequence > down.through_source_sequence,
        "RMB down and up must occupy distinct consumed source sequences");
    check(up.runtime.output.counts.dx == 0 && up.runtime.output.counts.dy == 0,
        "ADS release without manual input must not deliver an old correction");
}

void test_actual_ads_bodylock_final_counts_are_the_only_output() {
    Fixture f;
    f.arm();
    // Begin the opposing gesture BEFORE target admission. A new gesture after
    // admission is a protected desired-point edit and correctly keeps M.
    f.source->source({0x004, 0, 0, -1, 0, 0});
    f.session->submit_vision_snapshot(
        controller_native::incident_fixture::empty_snapshot(target_spec(), 1, 0.090));
    const auto before_target = f.tick(0.090);
    check(before_target.runtime.controller.vision_intent.purpose ==
            pipeline_contract::UserAimIntentPurpose::AcquireTarget &&
            f.session->runtime().facade().controller().last_target_plan().target_id == 0,
        "opposing gesture must begin before target admission");
    const float errors[] = {80.0f, 45.0f, 12.0f, 5.0f, 3.0f, 2.0f, 2.0f, 2.0f};
    bool saw_ads = false, saw_bodylock = false;
    bool saw_ads_with_manual = false, saw_bodylock_with_manual = false;
    bool saw_changed_final = false;
    for (std::size_t index = 0; index < std::size(errors); ++index) {
        const double now = 0.100 + index * 0.010;
        f.source->source({0, 0, 0, -1, 0, 0});
        f.session->submit_vision_snapshot(observed(index + 2, now, errors[index]));
        const auto result = f.tick(now);
        check(result.runtime.controller.controller_used, "fixture must run the real shared controller");
        const auto& mode = f.session->runtime().facade().controller().last_ai_aim_mode();
        check(f.session->runtime().facade().controller().last_target_plan().visual_authority > 0.8f,
            "assistance fixture must supply strong current enemy evidence");
        const auto& components = f.session->runtime().facade().controller().last_output_components();
        check(!components.manual_correction_x,
            "continued pre-target gesture must not become a protected desired-point edit");
        std::cout << "[MouseVirtualSessionTests] control frame=" << index + 1
            << " error=" << errors[index] << " mode=" << mode
            << " source_x=" << result.source_counts.dx
            << " final_x=" << result.runtime.output.counts.dx
            << " authority=" << components.assist_authority
            << " reason=" << components.assist_authority_reason
            << " ai=" << components.ai_aim_stick.x
            << " correction=" << components.manual_correction_x << '\n';
        saw_ads = saw_ads || mode == "ads_snap";
        saw_bodylock = saw_bodylock || mode == "body_lock";
        saw_ads_with_manual = saw_ads_with_manual ||
            (mode == "ads_snap" && result.source_counts.dx != 0);
        saw_bodylock_with_manual = saw_bodylock_with_manual ||
            (mode == "body_lock" && result.source_counts.dx != 0);
        saw_changed_final = saw_changed_final ||
            result.runtime.output.counts.dx != result.source_counts.dx;
    }
    check(saw_ads && saw_bodylock && saw_ads_with_manual && saw_bodylock_with_manual && saw_changed_final,
        "fixture must exercise real ADS and BodyLock with nonzero M and a controller-edited final T");
    f.session->submit_vision_snapshot(
        controller_native::incident_fixture::empty_snapshot(target_spec(), 10, 0.180));
    const auto lost = f.tick(0.180);
    check(!lost.runtime.controller.auto_fire_active,
        "explicit target loss must revoke synthetic fire");
    f.source->source({0x008, 0, 0, 0, 0, 0});
    const auto released = f.tick(0.190);
    check(released.runtime.output.counts.dx == 0 && released.runtime.output.counts.dy == 0,
        "ADS release after target loss must not replay the last assist final");
}

void test_autofire_to_physical_left_handoff_and_release() {
    Fixture f(MouseRelayTestMode::None, true);
    f.arm();
    f.source->source({0x004, 0, 0, 0, 0, 0});
    bool saw_down = false, saw_gap = false, saw_recoil = false;
    std::uint64_t frame = 0;
    for (frame = 1; frame <= 80; ++frame) {
        const double now = frame * 0.010;
        f.session->submit_vision_snapshot(observed(frame, now, 0.0f, true));
        const auto result = f.tick(now);
        const bool fire = result.runtime.controller.auto_fire_active;
        saw_recoil |= result.runtime.controller.recoil.dy > 0;
        check(result.runtime.controller.recoil.active == fire &&
                result.runtime.output.counts.dy == result.runtime.controller.aim_counts.dy + result.runtime.controller.recoil.dy,
            "AutoFire recoil must use actual virtual button state and the same final movement");
        const auto record = mouse_diagnostic_record(*f.session, result, {});
        check(record.auto_fire == fire && record.recoil_dy == result.runtime.controller.recoil.dy &&
                record.window.committed && record.window.submitted_counts.dy == record.final.dy,
            "diagnostics must join real controller, recoil and committed transport window");
        check(!f.session->left_button_down(), "virtual AI left must never feed back as physical left");
        check(((f.receiver->reports().back().buttons & 1u) != 0) == fire,
            "same-tick controller AutoFire must reach the virtual left state");
        saw_down = saw_down || fire;
        saw_gap = saw_gap || (saw_down && !fire);
        if (saw_gap && fire) break;
    }
    check(saw_down && saw_gap && saw_recoil && frame <= 80,
        "fixture must see a real AutoFire pulse, gap, and renewed down before manual takeover");
    f.source->source({0x001, 0, 0, 0, 0, 0});
    double now = (++frame) * 0.010;
    f.session->submit_vision_snapshot(observed(frame, now, 0.0f, true));
    const auto manual = f.tick(now);
    check(f.session->left_button_down() && !manual.runtime.controller.auto_fire_active &&
            f.session->runtime().facade().controller().last_output_components().fire_button &&
            (f.receiver->reports().back().buttons & 1u) != 0,
        "physical left takes authority while AI revocation must leave virtual left held");
    now = (++frame) * 0.010;
    f.session->submit_vision_snapshot(
        controller_native::incident_fixture::empty_snapshot(target_spec(true), frame, now));
    const auto lost_while_held = f.tick(now);
    check(!lost_while_held.runtime.controller.auto_fire_active &&
            (f.receiver->reports().back().buttons & 1u) != 0,
        "target loss must revoke AI without lifting a physical left hold");
    f.source->source({0x002, 0, 0, 0, 0, 0});
    now = (++frame) * 0.010;
    const auto released = f.tick(now);
    check(!released.runtime.controller.recoil.active && released.runtime.controller.recoil.dy == 0,
        "physical left release must stop recoil in that same source window");
    check(!f.session->left_button_down() && !released.runtime.controller.auto_fire_active &&
            (f.receiver->reports().back().buttons & 1u) == 0,
        "physical release after target loss must not revive old AI fire");
}

double now_seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void test_queued_calibration_uses_newly_consumed_physical_rmb() {
    Fixture f;
    f.arm();
    double now = now_seconds();
    f.session->submit_vision_snapshot(observed(1, now, 80.0f));
    f.tick(now);
    check(!f.session->right_button_down(), "queued calibration fixture must begin in hipfire");
    // The request sees the old cached hipfire state; only read_source in the
    // next session tick establishes the actual physical state of this window.
    f.source->source({0x004, 0, 0, 1, 0, 0});
    f.session->request_calibration();
    bool stale_mode = false;
    check(!f.session->take_calibration_hotkey(&stale_mode),
        "outer hotkey polling must not steal the virtual session's queued request");
    now = std::max(now + 0.010, now_seconds() + 0.001);
    f.session->submit_vision_snapshot(observed(2, now, 80.0f));
    const auto before = f.receiver->reports().size();
    const auto probe = f.tick(now);
    check(probe.calibration_requested && probe.calibration_started &&
            probe.runtime.mode == MouseAimMode::Ads && f.session->right_button_down() &&
            probe.runtime.output.kind == MouseRuntimeOutputKind::CalibrationProbe &&
            probe.runtime.output.counts.dx == 41 && probe.runtime.output.counts.dy == 0,
        "queued probe must select current physical ADS and combine +40 with one jitter count");
    check(f.receiver->reports().size() == before + 1 &&
            f.receiver->reports().back().buttons == 2,
        "queued calibration and RMB down must share exactly one virtual report");
    now = std::max(now + 0.010, now_seconds() + 0.001);
    f.session->submit_vision_snapshot(observed(3, now, 60.0f));
    const auto returned = f.tick(now);
    check(!returned.calibration_requested && !returned.calibration_started &&
            returned.runtime.output.kind == MouseRuntimeOutputKind::CalibrationReturn &&
            returned.runtime.output.counts.dx == -40,
        "queued request must be consumed once and advance to the ADS return");
    now = std::max(now + 0.010, now_seconds() + 0.001);
    f.session->submit_vision_snapshot(observed(4, now, 80.0f));
    const auto completed = f.tick(now);
    check(completed.runtime.calibration_completed &&
            valid(f.session->runtime().profile(MouseAimMode::Ads)) &&
            !valid(f.session->runtime().profile(MouseAimMode::Hipfire)),
        "queued calibration must commit only the newly captured ADS profile");
}

void test_calibration_probe_return_window_and_next_controller_tick() {
    Fixture f;
    f.arm();
    double now = now_seconds();
    f.session->submit_vision_snapshot(observed(1, now, 80.0f));
    const auto initial_reports = f.receiver->reports().size();
    const auto begin = f.session->begin_calibration(false, static_cast<std::uint64_t>(now * 1.0e9));
    check(begin.started && begin.output.kind == MouseRuntimeOutputKind::CalibrationProbe &&
            begin.output.counts.dx == 40,
        "session calibration must start a real +40 probe without a controller source window");
    auto reports = f.receiver->reports();
    check(reports.size() == initial_reports + 1 && reports.back().x == 40 && reports.back().y == 0,
        "calibration probe must use only the same virtual endpoint");
    // Session acknowledges actual delivery with steady_clock. These synthetic
    // captures are stamped after that boundary without sleeping or real input.
    now = std::max(now + 0.010, now_seconds() + 0.001);
    f.source->source({0, 0, 0, 1, -1, 0});
    f.session->submit_vision_snapshot(observed(2, now, 60.0f));
    const auto returned = f.tick(now);
    check(returned.runtime.output.kind == MouseRuntimeOutputKind::CalibrationReturn &&
            returned.runtime.output.counts.dx == -39 && returned.runtime.output.counts.dy == -1 &&
            returned.runtime.calibration_failure == MouseCalibrationFailure::None,
        "return must consume this window and include allowed jitter once (-40+1,-1)");
    now = std::max(now + 0.010, now_seconds() + 0.001);
    f.source->source({0, 0, 0, 1, 0, 0});
    f.session->submit_vision_snapshot(observed(3, now, 80.0f));
    const auto completed = f.tick(now);
    check(completed.runtime.calibration_completed &&
            valid(f.session->runtime().profile(MouseAimMode::Hipfire)) &&
            std::abs(f.session->runtime().profile(MouseAimMode::Hipfire).px_per_count_x - 0.5f) < 1.0e-6f,
        "same-generation returned target must commit the measured profile through session delivery");
    f.session->submit_vision_snapshot(
        controller_native::incident_fixture::empty_snapshot(target_spec(), 4, now + 0.010));
    f.source->source({0, 0, 0, 2, -1, 0});
    const auto next = f.tick(now + 0.010);
    check(next.runtime.output.kind == MouseRuntimeOutputKind::ControllerFinal &&
            next.runtime.output.counts.dx == 2 && next.runtime.output.counts.dy == -1 &&
            f.session->armed(),
        "calibration return must leave the next controller window available with no duplicate jitter");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc > 1) {
            check(argc == 2 && std::string(argv[1]) == "--inject-source-leak",
                "usage: mouse_virtual_session_tests [--inject-source-leak]");
            inject_source_leak = true;
        }
        test_diagnostic_modes_replace_one_source_once();
        test_prepared_emergency_and_shutdown_boundaries();
        test_same_burst_rmb_edges_reach_controller_in_order();
        test_actual_ads_bodylock_final_counts_are_the_only_output();
        test_autofire_to_physical_left_handoff_and_release();
        test_queued_calibration_uses_newly_consumed_physical_rmb();
        test_calibration_probe_return_window_and_next_controller_tick();
        std::cout << "[MouseVirtualSessionTests] PASS: actual session final-T receiver, ADS/BodyLock, "
                     "ordered edges, AutoFire/manual ownership, calibration and release\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[MouseVirtualSessionTests] FAIL: " << error.what() << '\n';
        return 1;
    }
}
