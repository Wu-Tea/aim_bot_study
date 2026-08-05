#include "vision_native/present_interval_contract.h"
#include "vision_native/qpc_steady_clock.h"

#include <cstdlib>
#include <iostream>

namespace {

void require(bool value, const char* expression) {
    if (!value) {
        std::cerr << "require failed: " << expression << '\n';
        std::exit(19);
    }
}

#define REQUIRE(value) require((value), #value)

void test_known_offset_and_signed_drift() {
    const auto calibration = vision_native::QpcSteadyClockCalibration::from_sample(
        1'000, 1'000, 15'000'000, 25, 7);
    std::uint64_t mapped = 0;
    std::uint64_t uncertainty = 0;
    REQUIRE(calibration.map_qpc_to_steady(1'010, 1'000, &mapped, &uncertainty));
    REQUIRE(mapped == 25'000'000);
    REQUIRE(uncertainty == 25);
    REQUIRE(calibration.map_qpc_to_steady(990, 1'000, &mapped, nullptr));
    REQUIRE(mapped == 5'000'000);
    REQUIRE(!calibration.map_qpc_to_steady(1'010, 2'000, &mapped, nullptr));
}

void test_present_interval_never_uses_result_ready_time() {
    vision_native::PresentSteadyInterval interval;
    interval.valid = true;
    interval.previous_present_ns = 1'000;
    interval.current_present_ns = 2'000;
    interval.previous_calibration_id = 7;
    interval.current_calibration_id = 8;
    interval.previous_uncertainty_ns = 25;
    interval.current_uncertainty_ns = 27;
    // This output is after current present but before a hypothetical result
    // ready timestamp. It must not be admitted to the current interval.
    const auto after_current = vision_native::evaluate_present_output_join(
        interval, 2'050, 0, 100);
    REQUIRE(!after_current.eligible);
    REQUIRE(after_current.reason ==
        vision_native::PresentOutputJoinReason::OutputAfterCurrentPresent);

    // The same output can be admitted to a later interval only when the
    // declared response-delay window reaches that interval.
    vision_native::PresentSteadyInterval later;
    later.valid = true;
    later.previous_present_ns = 2'000;
    later.current_present_ns = 3'000;
    later.previous_calibration_id = 7;
    later.current_calibration_id = 8;
    later.previous_uncertainty_ns = 25;
    later.current_uncertainty_ns = 27;
    const auto later_join = vision_native::evaluate_present_output_join(
        later, 2'050, 100, 250);
    REQUIRE(later_join.eligible);
    REQUIRE(later_join.reason == vision_native::PresentOutputJoinReason::Eligible);

    vision_native::PresentSteadyInterval invalid;
    invalid.valid = false;
    invalid.previous_present_ns = 1'000;
    invalid.current_present_ns = 2'000;
    const auto invalid_join = vision_native::evaluate_present_output_join(
        invalid, 1'500, 0, 100);
    REQUIRE(!invalid_join.eligible);
    REQUIRE(invalid_join.reason ==
        vision_native::PresentOutputJoinReason::InvalidClock);

    auto missing_previous_provenance = interval;
    missing_previous_provenance.previous_calibration_id = 0;
    const auto missing_previous =
        vision_native::evaluate_present_output_join(
            missing_previous_provenance, 1'500, 0, 100);
    REQUIRE(!missing_previous.eligible);
    REQUIRE(missing_previous.reason ==
        vision_native::PresentOutputJoinReason::InvalidClock);

    auto missing_current_provenance = interval;
    missing_current_provenance.current_calibration_id = 0;
    const auto missing_current =
        vision_native::evaluate_present_output_join(
            missing_current_provenance, 1'500, 0, 100);
    REQUIRE(!missing_current.eligible);
    REQUIRE(missing_current.reason ==
        vision_native::PresentOutputJoinReason::InvalidClock);
}

}  // namespace

int main() {
    test_known_offset_and_signed_drift();
    test_present_interval_never_uses_result_ready_time();
    std::cout << "W3ClockContractTests passed\n";
    return 0;
}
