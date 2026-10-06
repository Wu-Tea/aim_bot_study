#include "io_recovery_policy.h"
#include "test_support/native_test_registry.h"

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
    devices.push_back({2, "PS4 Controller", 6, 15, 0, true});

    require_true(
        controller_native::select_sdl_reconnect_device(
            devices, "DualSense Wireless Controller", 6, 15) == 1,
        "reconnect must select the original physical device instead of ViGEm output");
    require_true(
        controller_native::select_sdl_reconnect_device(
            devices, "Missing Physical Controller", 6, 15) == -1,
        "reconnect must not fall back to an arbitrary virtual controller");
    devices.erase(devices.begin() + 1);
    require_true(
        controller_native::select_sdl_reconnect_device(
            devices, "DualSense Wireless Controller", 6, 15) == -1,
        "detached DSE must not reconnect to the runtime's DS4 output");
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

void test_explicit_identity_selection() {
    std::vector<controller_native::SdlJoystickDevice> devices{
        {3,"DualSense Wireless Controller",6,21,0,true,"path:aaa"},
        {0,"DualSense Wireless Controller",6,21,0,true,"path:bbb"}};
    const auto select=[&] { return controller_native::select_sdl_reconnect_device(devices,{},4,0,"path:bbb"); };
    require_true(select()==0,"selected identity must override enumeration order and equal names");
    devices[1].device_index=2;
    require_true(select()==2,"device selection must follow identity after slot reorder");
    devices.pop_back();
    require_true(select()==-1,"missing selected identity cannot switch to the other same-name controller");
    devices.push_back({1,"DualSense Wireless Controller",6,21,0,true,"path:bbb"});
    devices.push_back({2,"DualSense Wireless Controller",6,21,0,true,"path:bbb"});
    require_true(select()==-1,"ambiguous identities must never select the first match");
    devices.pop_back();devices.back().opened=false;
    require_true(select()==-1,"closed selected devices cannot be admitted");
}

}  // namespace

void register_io_recovery_policy_tests(native_test::Registry& registry) {
    registry.add_case("BaseRuntimeFreshness", "explicit_input_identity_selection", test_explicit_identity_selection);
    registry.add_case("BaseRuntimeFreshness", "reconnect_selects_original_physical_shape", test_reconnect_selects_only_the_original_physical_shape);
    registry.add_case("BaseRuntimeFreshness", "reconnect_rejects_incompatible_matches", test_reconnect_rejects_unopened_or_incompatible_matches);
    registry.add_case("BaseRuntimeFreshness", "retry_throttle_is_immediate_then_bounded", test_retry_throttle_is_immediate_then_bounded);
}
