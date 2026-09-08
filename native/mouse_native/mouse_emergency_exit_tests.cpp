#include "mouse_native/mouse_emergency_exit.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require_true(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void test_release_happens_before_stop_and_only_once() {
    std::vector<std::string> calls;
    mouse_native::MouseEmergencyExit exit(
        [&calls]() { calls.emplace_back("release"); },
        [&calls]() { calls.emplace_back("stop"); });

    require_true(
        exit.request() ==
            mouse_native::MouseEmergencyExitResult::ReleaseAndStopRequested,
        "first escape request must release and stop");
    require_true(exit.requested(), "escape request must remain observable");
    require_true(calls.size() == 2, "escape must invoke exactly two callbacks");
    require_true(
        calls[0] == "release" && calls[1] == "stop",
        "physical interception must release before application stop");

    require_true(
        exit.request() == mouse_native::MouseEmergencyExitResult::AlreadyRequested,
        "repeated escape must be idempotent");
    require_true(calls.size() == 2, "repeated escape must not repeat callbacks");
}

void test_release_exception_cannot_block_stop() {
    bool stop_requested = false;
    mouse_native::MouseEmergencyExit exit(
        []() { throw std::runtime_error("synthetic release failure"); },
        [&stop_requested]() { stop_requested = true; });

    require_true(
        exit.request() ==
            mouse_native::MouseEmergencyExitResult::ReleaseFailedButStopRequested,
        "release failure must be reported without escaping");
    require_true(stop_requested, "release failure must not suppress application stop");
}

}  // namespace

int main() {
    try {
        test_release_happens_before_stop_and_only_once();
        test_release_exception_cannot_block_stop();
        std::cout << "[MouseEmergencyExitTests] PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[MouseEmergencyExitTests][FAIL] " << error.what() << '\n';
        return 1;
    }
}
