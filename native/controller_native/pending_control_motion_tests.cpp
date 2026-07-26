#include "pending_control_motion.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {

using controller_native::DeliveredPreRecoilSample;
using controller_native::PendingControlMotion;

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

}  // namespace

int main() {
    try {
        test_integrates_delivered_pre_recoil_since_observation_capture();
        test_target_change_and_failed_delivery_invalidate_pending_motion();
        test_incomplete_time_coverage_is_not_guessed();
        std::cout << "cod_native_pending_control_motion_tests PASS\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "cod_native_pending_control_motion_tests FAIL: "
                  << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
