#include "mouse_native/mouse_interception_transport.h"
#include <chrono>
#include <deque>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

using namespace mouse_native;
namespace {
bool leak_fixture = false;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct ReceiverDriver final : MousePacketDriver {
    std::mutex mutex;
    bool available = true, captured = false, fail_send = false;
    std::deque<MouseDevicePacket> pending;
    std::vector<MouseDevicePacket> received;
    bool open() override { return available; }
    void close() noexcept override { capture(11, false); }
    std::vector<MouseDeviceInfo> devices() override { return {{11, L"fixture mouse"}}; }
    bool capture(int device, bool enabled) noexcept override {
        std::lock_guard<std::mutex> lock(mutex);
        if (device != 11) return false;
        captured = enabled; return true;
    }
    int receive(int& device, MouseDevicePacket& p) noexcept override {
        std::lock_guard<std::mutex> lock(mutex);
        if (pending.empty()) return 0;
        p = pending.front(); pending.pop_front(); device = 11; return 1;
    }
    bool send(int device, const MouseDevicePacket& p) noexcept override {
        std::lock_guard<std::mutex> lock(mutex);
        if (device != 11 || fail_send) return false;
        received.push_back(p); return true;
    }
    void physical(MouseDevicePacket p) {
        std::lock_guard<std::mutex> lock(mutex);
        if (captured) pending.push_back(p); else received.push_back(p);
        if (captured && leak_fixture) received.push_back(p);
    }
    std::vector<MouseDevicePacket> output() {
        std::lock_guard<std::mutex> lock(mutex); return received;
    }
};
template<class F> void until(F predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!predicate()) {
        check(std::chrono::steady_clock::now() < deadline, "worker deadline");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
void replacement_contract() {
    auto driver = std::make_unique<ReceiverDriver>(); auto* receiver = driver.get();
    MouseInterceptionTransport transport(std::move(driver));
    check(transport.start(11) && transport.enable_interception(), "start");
    // Physical mixed packet must not leak its movement along with the button.
    receiver->physical({0x004 | 0x400, 0, 120, 20, -7, 1234});
    until([&] { return transport.stats().source_packets == 1; });
    const auto source = transport.read_source();
    check(source.counts.dx == 20 && source.counts.dy == -7 && source.right_button_down, "source counts/buttons");
    check(transport.submit_final({0, 0}), "block");
    auto output = receiver->output();
    check(output.size() == 1 && output[0].x == 0 && output[0].y == 0, "RED: physical movement escaped before replacement");
    check(output[0].state == (0x004 | 0x400) && output[0].rolling == 120 && output[0].information == 1234, "button/wheel metadata preserved");
    check(transport.submit_final({-20, 7}), "invert");
    output = receiver->output();
    int x = 0, y = 0;
    for (const auto& p : output) { x += p.x; y += p.y; }
    check(x == -20 && y == 7, "receiver must see final only, never physical plus final");
    check(transport.read_source().counts.dx == 0, "output must not echo into source");
    receiver->physical({0, 0, 0, 9, -3, 0});
    until([&] { return transport.stats().source_packets == 2; });
    const auto manual = transport.read_source();
    check(transport.submit_final(manual.counts), "transparent manual forwarding");
    output = receiver->output();
    check(output.back().x == 9 && output.back().y == -3, "manual counts preserved exactly once");
    check(transport.submit_auto_fire(true), "auto down");
    receiver->physical({1, 0, 0, 0, 0, 0});
    until([&] { return transport.stats().source_packets == 3; });
    check(transport.submit_auto_fire(false), "physical takes fire ownership");
    output = receiver->output();
    check(output.back().state == 1, "no synthetic Up while physical held");
    transport.release_interception();
    check(!transport.submit_final({100, 0}), "late movement rejected");
    receiver->physical({0, 0, 0, 3, 4, 0});
    output = receiver->output();
    check(output.back().x == 3 && output.back().y == 4, "disarm restores physical path");
}
void failures_and_watchdog() {
    auto driver = std::make_unique<ReceiverDriver>(); auto* receiver = driver.get();
    MouseInterceptionTransport transport(std::move(driver));
    check(!transport.start(12), "wrong device must not auto-select another");
    check(transport.start(11) && transport.enable_interception(), "restart");
    check(transport.submit_auto_fire(true), "fire ownership before timeout");
    until([&] { return !transport.intercepting(); });
    check(transport.last_error() != 0, "consumer timeout reported");
    check(receiver->output().back().state == 2, "timeout releases synthetic fire");
    check(!transport.submit_calibration({40, 0}), "timeout rejects late calibration");
    transport.stop();
    receiver->available = false;
    check(!transport.start(), "missing driver cannot fall back to Win32");
}
}
int main(int argc, char**) {
    leak_fixture = argc > 1;
    try { replacement_contract(); failures_and_watchdog(); std::cout << "PASS exclusive receiver, buttons, fire, release, timeout and unavailable driver\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
