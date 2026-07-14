#include "io_recovery_policy.h"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void require_true(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void test_reconnect_selects_only_the_original_physical_shape() {
    std::vector<controller_native::SdlJoystickDevice> devices;
    devices.push_back({0, "Xbox 360 Controller", 6, 15, 1, true});
    devices.push_back({1, "DualSense Wireless Controller", 6, 15, 0, true});

    require_true(
        controller_native::select_sdl_reconnect_device(
            devices, "DualSense Wireless Controller", 6, 15) == 1,
        "reconnect must select the original physical device instead of ViGEm output");
    require_true(
        controller_native::select_sdl_reconnect_device(
            devices, "Missing Physical Controller", 6, 15) == -1,
        "reconnect must not fall back to an arbitrary virtual controller");
}

void test_reconnect_rejects_unopened_or_incompatible_matches() {
    std::vector<controller_native::SdlJoystickDevice> devices;
    devices.push_back({0, "DualSense Wireless Controller", 2, 15, 0, true});
    devices.push_back({1, "DualSense Wireless Controller", 6, 15, 0, false});
    require_true(
        controller_native::select_sdl_reconnect_device(
            devices, "DualSense Wireless Controller", 6, 15) == -1,
        "reconnect must require an opened device with the original control shape");
}

void test_retry_throttle_is_immediate_then_bounded() {
    using namespace std::chrono_literals;
    const auto start = std::chrono::steady_clock::time_point{} + 1s;
    controller_native::IoReconnectThrottle throttle(500ms);
    require_true(throttle.should_attempt(start), "first reconnect attempt should be immediate");
    throttle.record_failure(start);
    require_true(!throttle.should_attempt(start + 499ms), "failed reconnect must be throttled");
    require_true(throttle.should_attempt(start + 500ms), "retry should open at configured interval");
    throttle.record_success();
    require_true(throttle.failure_count() == 0, "successful reconnect should reset failure count");
    require_true(throttle.should_attempt(start + 501ms), "success should clear retry delay");
}

}  // namespace

int main() {
    try {
        test_reconnect_selects_only_the_original_physical_shape();
        test_reconnect_rejects_unopened_or_incompatible_matches();
        test_retry_throttle_is_immediate_then_bounded();
        std::cout << "[IoRecoveryPolicyTests] PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[IoRecoveryPolicyTests][FAIL] " << error.what() << '\n';
        return 1;
    }
}
