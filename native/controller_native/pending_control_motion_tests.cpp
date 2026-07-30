#include "pending_control_motion.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {

using controller_native::DeliveredPreRecoilSample;
using controller_native::PendingControlMotion;
using controller_native::delivered_camera_work_px;
using controller_native::remaining_work_after_delivery;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void require_near(float actual, float expected, float tolerance,
                  const char* message) {
    if (std::fabs(actual - expected) > tolerance) {
        throw std::runtime_error(message);
    }
}

void test_integrates_delivered_pre_recoil_since_observation_capture() {
    PendingControlMotion ledger;
    require(ledger.observe({0.001, {0.0f, 1.0f}, 7, true, true}),
            "first delivered sample must be accepted");
    require(ledger.observe({0.011, {0.0f, 1.0f}, 7, true, true}),
            "second delivered sample must be accepted");
    const auto pending = ledger.estimate(0.021, 20.0f, 500.0f, 7);
    require(pending.valid, "complete delivered interval must be valid");
    for (const auto sample : pending.camera_displacement_px) {
        require_near(sample.y, 10.0f, 0.001f,
                     "20ms at full stick and 500px/s must equal 10px");
    }
}

void test_target_change_and_failed_delivery_invalidate_pending_motion() {
    PendingControlMotion ledger;
    (void)ledger.observe({0.001, {0.0f, 1.0f}, 7, true, true});
    (void)ledger.observe({0.011, {0.0f, 1.0f}, 7, true, true});
    require(!ledger.estimate(0.021, 20.0f, 500.0f, 8).valid,
            "target mismatch must invalidate history");
    require(!ledger.observe({0.021, {0.0f, 1.0f}, 7, false, true}),
            "failed delivery must be rejected");
    require(!ledger.estimate(0.031, 20.0f, 500.0f, 7).valid,
            "failed delivery must clear pending coverage");
}

void test_incomplete_time_coverage_is_not_guessed() {
    PendingControlMotion ledger;
    (void)ledger.observe({0.011, {0.0f, 1.0f}, 7, true, true});
    const auto pending = ledger.estimate(0.021, 20.0f, 500.0f, 7);
    require(!pending.valid,
            "capture older than retained history must be invalid");
}

void test_sub_tick_zero_hold_before_first_delivery_is_allowed() {
    PendingControlMotion ledger;
    (void)ledger.observe({0.001001, {1.0f, 0.0f}, 7, true, true});
    (void)ledger.observe({0.002001, {1.0f, 0.0f}, 7, true, true});
    const auto pending = ledger.estimate(0.003, 2.0f, 500.0f, 7);
    require(pending.valid,
            "sub-tick gap before first delivered sample must be a zero hold");
    require_near(
        pending.camera_displacement_px.front().x,
        0.9995f,
        0.001f,
        "only delivered time after the sub-tick zero hold may be integrated");
}

void test_screen_space_remaining_work_uses_one_y_conversion() {
    const auto delivered = delivered_camera_work_px({10.0f, 6.0f});
    require_near(delivered.x, 10.0f, 0.0001f,
                 "right camera motion must complete positive X work");
    require_near(delivered.y, -6.0f, 0.0001f,
                 "up camera motion must complete negative screen-Y work");
    const auto remaining = remaining_work_after_delivery(
        {40.0f, -30.0f}, delivered);
    require_near(remaining.x, 30.0f, 0.0001f,
                 "delivered X work must be subtracted once");
    require_near(remaining.y, -24.0f, 0.0001f,
                 "delivered up work must reduce negative screen-Y error");
}

}  // namespace

int main() {
    try {
        test_integrates_delivered_pre_recoil_since_observation_capture();
        test_target_change_and_failed_delivery_invalidate_pending_motion();
        test_incomplete_time_coverage_is_not_guessed();
        test_sub_tick_zero_hold_before_first_delivery_is_allowed();
        test_screen_space_remaining_work_uses_one_y_conversion();
        std::cout << "cod_native_pending_control_motion_tests PASS\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "cod_native_pending_control_motion_tests FAIL: "
                  << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
