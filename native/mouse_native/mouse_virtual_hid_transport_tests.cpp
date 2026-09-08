#include "mouse_native/mouse_interception_transport.h"
#ifndef MOUSE_VIRTUAL_LEGACY_RED
#include "mouse_native/mouse_virtual_hid_transport.h"
#endif
#include <Windows.h>
#include <chrono>
#include <deque>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace mouse_native;
namespace {
constexpr wchar_t hardware[] = L"HID\\VID_1532&PID_00B8&MI_00";
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Driver final : MousePacketDriver {
    std::mutex mutex;
    bool opened = false, captured = false, fail_capture_off = false, fail_receive = false;
    bool available = true;
    std::vector<MouseDeviceInfo> inventory{{13, hardware}};
    std::deque<std::pair<int, MouseDevicePacket>> pending;
    std::vector<MouseDevicePacket> physical_receiver;
    bool open() override { return opened = available; }
    void close() noexcept override { opened = captured = false; }
    std::vector<MouseDeviceInfo> devices() override { return inventory; }
    bool capture(int id, bool enabled) noexcept override {
        std::lock_guard<std::mutex> lock(mutex);
        if (id != 13 || (!enabled && fail_capture_off)) return false;
        captured = enabled; return true;
    }
    int receive(int& id, MouseDevicePacket& packet) noexcept override {
        std::lock_guard<std::mutex> lock(mutex);
        if (fail_receive) return -1;
        if (pending.empty()) return 0;
        id = pending.front().first; packet = pending.front().second; pending.pop_front(); return 1;
    }
    bool send(int, const MouseDevicePacket& packet) noexcept override {
        std::lock_guard<std::mutex> lock(mutex);
        physical_receiver.push_back(packet); return true;
    }
    void source(MouseDevicePacket packet, int id = 13) {
        std::lock_guard<std::mutex> lock(mutex);
        if (captured) pending.push_back({id, packet}); else physical_receiver.push_back(packet);
    }
};

#ifdef MOUSE_VIRTUAL_LEGACY_RED
void legacy_red() {
    auto driver = std::make_unique<Driver>(); auto* raw = driver.get();
    MouseInterceptionTransport old(std::move(driver));
    check(old.start(13) && old.enable_interception(), "legacy fixture setup");
    raw->source({0, 0, 0, 10, 0, 0});
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!old.stats().source_packets && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    check(old.read_source().counts.dx == 10, "legacy source received");
    check(old.submit_final({3, 0}), "legacy final accepted");
    check(raw->physical_receiver.empty(),
        "RED: final still reaches original device channel; independent virtual endpoint receives nothing");
}
#else
struct Sink final : MouseVirtualHidSink {
    bool opened = false, identity = true, released = true, fail_neutral = false;
    int limit = 127, fail_after = -1, send_delay_ms = 0;
    unsigned long error = 0;
    std::vector<virtual_mouse::Report> received;
    bool open(const std::wstring& id) override { opened = id == hardware && identity; return opened; }
    void close() noexcept override { opened = false; }
    bool identities_valid() const noexcept override { return opened && identity; }
    bool buttons_released() const noexcept override { return released; }
    int axis_limit() const noexcept override { return limit; }
    bool send(const virtual_mouse::Report& report) noexcept override {
        if (send_delay_ms) std::this_thread::sleep_for(std::chrono::milliseconds(send_delay_ms));
        if ((fail_after >= 0 && static_cast<int>(received.size()) >= fail_after) ||
            (fail_neutral && !report.buttons && !report.x && !report.y && !report.wheel && !report.hwheel)) {
            error = ERROR_WRITE_FAULT; return false;
        }
        received.push_back(report); return true;
    }
    unsigned long last_error() const noexcept override { return error; }
};
struct Fixture {
    Driver* driver; Sink* sink;
    MouseVirtualHidTransport transport;
    Fixture() : Fixture(std::make_unique<Driver>(), std::make_unique<Sink>()) {}
    Fixture(std::unique_ptr<Driver> d, std::unique_ptr<Sink> s)
        : driver(d.get()), sink(s.get()), transport(std::move(d), std::move(s)) {}
    void start() { check(transport.start(13, hardware), "preflight"); }
    void arm() { start(); check(transport.enable_interception(), "arm"); }
};
void exact_replacement(bool leak) {
    Fixture f; f.start();
    check(!f.driver->captured && f.sink->received.empty(), "preflight never captures or sends");
    check(f.transport.enable_interception(), "arm");
    check(!f.transport.submit_frame({1, 0}, false), "no source window cannot submit");
    f.driver->source({0, 0, 0, 10, 0, 0});
    auto source = f.transport.read_source(); const auto token = f.transport.window_token();
    check(source.counts.dx == 10, "source window counts");
    if (leak) f.driver->send(13, {0, 0, 0, 10, 0, 0});
    check(f.transport.submit_frame({3, 0}, false, token), "final commit");
    check(f.driver->physical_receiver.empty(), "RED: original physical report escaped to receiver");
    check(f.sink->received.size() == 1 && f.sink->received[0].x == 3, "receiver sees 3, never M+T or physical final");
    check(!f.transport.submit_frame({3, 0}, false, token), "duplicate rejected");
    f.transport.read_source(); const auto current = f.transport.window_token();
    check(!f.transport.submit_frame({9, 0}, false, token), "old window rejected");
    check(f.transport.submit_frame({-3, 0}, false, current), "empty source AI window");
    check(f.sink->received.back().x == -3, "no stale source addition");
    const auto info = f.transport.window_info();
    check(info.committed && info.final_counts.dx == -3 && info.cutoff_ns && info.submitted_ns >= info.cutoff_ns,
        "window evidence binds final and time");
    f.transport.release_interception();
    check(!f.transport.submit_frame({5, 0}, true, current), "late output rejected after release");
    f.driver->source({0, 0, 0, 7, 0, 0});
    check(f.driver->physical_receiver.size() == 1 && f.driver->physical_receiver[0].x == 7, "native input resumes once");
    check(f.transport.start(13, hardware) && f.transport.enable_interception(), "explicit new epoch");
    f.transport.read_source();
    check(!f.transport.submit_frame({7, 0}, false, current), "old epoch cannot control new session");
    check(f.transport.submit_frame({}, false), "new epoch current window remains valid");
}
void ordered_edges_and_wheels() {
    Fixture f; f.arm();
    f.driver->source({0, 0, 0, 7, 2, 0});
    f.driver->source({0x004 | 0x400, 0, 240, 5, -1, 0});
    f.driver->source({0x008, 0, 0, 0, 0, 0});
    auto a = f.transport.read_source();
    check(a.counts.dx == 7 && !a.right_button_down, "movement before ADS edge keeps old authority");
    check(f.transport.submit_frame(a.counts, false), "first movement");
    auto b = f.transport.read_source();
    check(b.counts.dx == 5 && b.right_button_down, "mixed edge owns its movement window");
    check(f.transport.submit_frame({-5, 1}, false), "mixed replacement");
    auto c = f.transport.read_source();
    check(!c.right_button_down, "same burst ADS release not coalesced away");
    check(f.transport.submit_frame(c.counts, false), "up window");
    check(f.sink->received.size() == 3 && f.sink->received[0].x == 7 && f.sink->received[0].buttons == 0,
        "first phase at receiver");
    check(f.sink->received[1].x == -5 && f.sink->received[1].buttons == 2 && f.sink->received[1].wheel == 2,
        "mixed report has only final movement, correct button and wheel");
    check(f.sink->received[2].buttons == 0, "up delivered");
    f.driver->source({0x100 | 0x800, 0, -120, 0, 0, 0});
    f.transport.read_source(); check(f.transport.physical_buttons() == 16, "fifth physical button snapshot");
    check(f.transport.submit_frame({}, false), "side and horizontal wheel");
    check(f.sink->received.back().buttons == 16 && f.sink->received.back().hwheel == -1,
        "fifth button and horizontal wheel preserved");
    check(f.driver->physical_receiver.empty(), "all normal kinds stay off physical channel");
}
void fire_ownership_and_calibration() {
    Fixture f; f.arm(); f.transport.read_source();
    check(f.transport.submit_frame({}, true), "fresh AI down");
    check(f.sink->received.back().buttons == 1 && f.transport.physical_buttons() == 0, "synthetic is never physical");
    f.driver->source({1, 0, 0, 0, 0, 0});
    f.transport.read_source(); check(f.transport.submit_frame({}, false), "physical takes over AI");
    check(f.sink->received.back().buttons == 1, "AI revoke cannot lift physical");
    f.driver->source({2, 0, 0, 0, 0, 0});
    f.transport.read_source(); check(f.transport.submit_frame({}, false), "physical release");
    check(f.sink->received.back().buttons == 0, "old AI press does not revive");
    check(!f.transport.submit_auto_fire(true), "separate synthetic writer rejected");
    check(f.transport.submit_auto_fire(false), "legacy false has no independent output");
    check(f.transport.submit_calibration({20, 0}), "probe owns same virtual sender");
    f.driver->source({0, 0, 0, 1, 0, 0});
    f.transport.read_source();
    check(f.transport.submit_calibration({-19, 0}), "return consumes window with its physical jitter already included");
    check(!f.transport.submit_frame({1, 0}, false), "return cannot duplicate window");
    check(f.driver->physical_receiver.empty(), "calibration has no source-channel writes");
}
void split_and_partial_failure() {
    Fixture f; f.arm();
    f.driver->source({0x400, 0, 120, 1000, -600, 0});
    f.transport.read_source(); check(f.transport.submit_frame({1000, -600}, false), "descriptor splitting");
    int x = 0, y = 0, wheel = 0;
    for (auto r : f.sink->received) {
        check(std::abs(r.x) <= 127 && std::abs(r.y) <= 127, "actual descriptor bounds");
        x += r.x; y += r.y; wheel += r.wheel;
    }
    check(x == 1000 && y == -600 && wheel == 1, "all pieces preserve totals without repeated wheel");
    f.transport.read_source(); f.sink->fail_after = static_cast<int>(f.sink->received.size()) + 1;
    check(!f.transport.submit_frame({400, 0}, true), "second-piece failure is reported");
    check(!f.transport.running() && !f.transport.intercepting() && !f.driver->opened,
        "partial failure releases input despite failed neutral");
    const auto failed = f.transport.window_info();
    check(failed.cancelled && !failed.committed && failed.submitted_counts.dx == 127 &&
        failed.cancelled_counts.dx == 273 && failed.reports_submitted == 1,
        "partial-failure evidence identifies committed prefix and cancelled tail");
    check(f.driver->physical_receiver.empty(), "partial final is never replayed onto physical path");
}
void slow_split_freshness() {
    Fixture f; f.arm(); f.transport.read_source();
    f.sink->send_delay_ms = 40;
    check(!f.transport.submit_frame({700, 0}, false),
        "RED: successful slow HID pieces must not keep starting stale final movement after its deadline");
    int x = 0;
    for (const auto& report : f.sink->received) x += report.x;
    check(x > 0 && x < 700 && !f.transport.intercepting(), "expired split cancels only unsent tail");
    const auto info = f.transport.window_info();
    check(info.submitted_counts.dx == x && info.cancelled_counts.dx == 700 - x && info.cancelled,
        "expired split has exact submitted/cancelled accounting");
}
void trust_boundary_failures() {
    { Fixture f; f.sink->identity = false; check(!f.transport.start(13, hardware), "identity proof required");
      check(!f.driver->captured && f.sink->received.empty(), "identity failure leaves input native"); }
    { Fixture f; f.driver->inventory.push_back({14, hardware});
      check(!f.transport.start(13, hardware), "duplicate full hardware identity rejected even with slot"); }
    { Fixture f; f.driver->inventory = {{13, L"HID\\VID_FE0F&PID_00FF"}};
      check(!f.transport.start(13), "FakerInput cannot be source"); }
    { Fixture f; f.start(); f.sink->released = false;
      check(!f.transport.enable_interception() && !f.driver->captured, "held buttons require clean handoff"); }
    { Fixture f; f.arm(); f.driver->source({0, 1, 0, 9, 0, 0});
      f.transport.read_source(); check(f.transport.last_error() && !f.transport.running(), "absolute source fails explicitly");
      check(f.driver->physical_receiver.empty(), "unsupported report not replayed as a normal output"); }
    { Fixture f; f.arm(); f.driver->source({0x400, 0, 1, 0, 0, 0});
      f.transport.read_source(); check(f.transport.last_error() && !f.transport.intercepting(), "high-resolution wheel rejected"); }
    { Fixture f; f.arm(); f.driver->source({0, 0, 0, 1, 0, 0}, 14);
      f.transport.read_source(); check(f.transport.last_error() && !f.transport.intercepting(), "foreign source rejected"); }
    { Fixture f; f.arm(); f.transport.read_source(); f.transport.read_source();
      check(f.transport.last_error() && !f.transport.intercepting(), "uncommitted input window cannot be overwritten"); }
    { Fixture f; f.arm(); for (int i = 0; i < 258; ++i) f.driver->source({0, 0, 0, 1, 0, 0});
      f.transport.read_source(); check(f.transport.last_error() && !f.transport.intercepting(), "bounded queue overload fails"); }
    { Fixture f; f.arm(); f.driver->fail_capture_off = true; f.sink->fail_neutral = true;
      f.transport.release_interception(); check(!f.driver->opened && !f.sink->opened && f.transport.last_error(),
          "both cleanup branches attempted despite individual failures"); }
    { Fixture f; f.arm(); f.driver->fail_receive = true; f.transport.read_source();
      check(f.transport.last_error() && !f.transport.running(), "receive failure releases"); }
    { Fixture f; f.arm(); f.transport.read_source();
      std::this_thread::sleep_for(std::chrono::milliseconds(110));
      check(!f.transport.submit_frame({20, 0}, true) && !f.transport.intercepting() && f.transport.last_error(),
          "stalled controller cannot submit an expired physical window"); }
    { Fixture f; f.arm(); f.sink->identity = false;
      std::this_thread::sleep_for(std::chrono::milliseconds(110));
      f.transport.read_source(); check(f.transport.last_error() && !f.transport.intercepting(),
          "device identity loss releases even without a new physical packet"); }
    { Fixture f; f.arm();
      f.driver->source({4, 0, 0, 0, 0, 0});
      f.driver->source({8, 0, 0, 7, 0, 0});
      f.transport.read_source();
      check(f.transport.submit_frame({}, false), "first button window commits");
      std::this_thread::sleep_for(std::chrono::milliseconds(70));
      f.transport.read_source();
      std::this_thread::sleep_for(std::chrono::milliseconds(40));
      check(!f.transport.submit_frame({7, 0}, false) && !f.transport.intercepting(),
          "freezing a queued packet must not renew its original age deadline"); }
}
#endif
}
int main(int argc, char** argv) {
    try {
#ifdef MOUSE_VIRTUAL_LEGACY_RED
        (void)argc;
        (void)argv;
        legacy_red();
#else
        if (argc > 1 && std::string(argv[1]) == "--slow-freshness-only") slow_split_freshness();
        else {
            exact_replacement(argc > 1); ordered_edges_and_wheels(); fire_ownership_and_calibration();
            split_and_partial_failure(); trust_boundary_failures(); slow_split_freshness();
        }
#endif
        std::cout << "PASS independent virtual final, window ownership, ordered buttons, calibration, splitting and release\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
