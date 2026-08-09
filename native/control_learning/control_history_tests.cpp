#include "control_learning/control_history.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <type_traits>

namespace {

void require(bool value, int line) {
    if (!value) {
        std::cerr << "require failed at line " << line << '\n';
        std::abort();
    }
}

void require_near(float actual, float expected, float tolerance, int line) {
    if (std::fabs(actual - expected) > tolerance) {
        std::cerr << "require near failed at line " << line
                  << " actual=" << actual << " expected=" << expected << '\n';
        std::abort();
    }
}

#define REQUIRE(value) require((value), __LINE__)
#define REQUIRE_NEAR(actual, expected, tolerance) \
    require_near((actual), (expected), (tolerance), __LINE__)

control_learning::DeliveredControlSample sample(
    std::uint64_t seq,
    std::uint64_t time_ns,
    pipeline_contract::Vec2f right = {},
    pipeline_contract::Vec2f left = {}) {
    control_learning::DeliveredControlSample value;
    value.sample_seq = seq;
    value.applied_at_ns = time_ns;
    value.physical_right = right;
    value.physical_left = left;
    value.manual_component = right;
    value.pre_recoil = right;
    value.final_right = right;
    value.final_left = left;
    value.output_delivered = true;
    return value;
}

void test_history_integrates_piecewise_constant_delivery() {
    control_learning::ControlHistory<8> history;
    REQUIRE(history.push(sample(1, 10'000'000, {0.5f, 0.0f}, {0.2f, 0.0f})));
    REQUIRE(history.push(sample(2, 30'000'000, {1.0f, 0.0f}, {0.4f, 0.0f})));
    // The queried end must be covered by a retained delivery timestamp; an
    // endpoint sample at 40 ms makes the [20,40) interval explicit without
    // changing the held state inside it.
    REQUIRE(history.push(sample(3, 40'000'000, {1.0f, 0.0f}, {0.4f, 0.0f})));
    const auto interval = history.integrate(20'000'000, 40'000'000);
    REQUIRE_NEAR(interval.final_right_stick_seconds.x, 0.015f, 1.0e-6f);
    REQUIRE_NEAR(interval.final_left_stick_seconds.x, 0.006f, 1.0e-6f);
    REQUIRE(interval.complete);
    REQUIRE(interval.first_seq == 1);
    REQUIRE(interval.last_seq == 2);
}

void test_history_rejects_non_monotonic_sample() {
    control_learning::ControlHistory<8> history;
    REQUIRE(history.push(sample(1, 20'000'000)));
    REQUIRE(!history.push(sample(2, 19'000'000)));
    REQUIRE(!history.push(sample(1, 21'000'000)));
}

void test_history_overwrites_oldest_in_order() {
    control_learning::ControlHistory<3> history;
    REQUIRE(history.push(sample(1, 10, {0.1f, 0.0f})));
    REQUIRE(history.push(sample(2, 20, {0.2f, 0.0f})));
    REQUIRE(history.push(sample(3, 30, {0.3f, 0.0f})));
    REQUIRE(history.push(sample(4, 40, {0.4f, 0.0f})));
    REQUIRE(history.size() == 3);
    REQUIRE(history.oldest().sample_seq == 2);
    REQUIRE(history.newest().sample_seq == 4);
}

void test_failed_delivery_and_sequence_gap_make_interval_ineligible() {
    control_learning::ControlHistory<8> history;
    REQUIRE(history.push(sample(4, 10'000'000, {0.2f, 0.0f})));
    auto failed = sample(6, 20'000'000, {0.4f, 0.0f});
    failed.output_delivered = false;
    REQUIRE(history.push(failed));
    REQUIRE(history.push(sample(7, 30'000'000, {0.6f, 0.0f})));
    REQUIRE(history.push(sample(8, 40'000'000, {0.6f, 0.0f})));
    const auto interval = history.integrate(10'000'000, 40'000'000);
    REQUIRE(interval.failed_delivery);
    REQUIRE(interval.expected == 4);
    REQUIRE(interval.written == 3);
    REQUIRE(!interval.complete);
}

void test_history_retains_components_and_flags() {
    control_learning::ControlHistory<8> history;
    auto value = sample(1, 10'000'000, {0.7f, -0.2f}, {-0.4f, 0.3f});
    value.ai_component = {0.2f, -0.1f};
    value.recoil_component = {0.0f, 0.05f};
    value.ads_epoch = 12;
    value.firing = true;
    value.recoil_active = true;
    value.saturated = true;
    value.output_disabled = true;
    REQUIRE(history.push(value));
    REQUIRE(history.newest().final_left.x == -0.4f);
    REQUIRE(history.newest().ai_component.x == 0.2f);
    REQUIRE(history.newest().ads_epoch == 12);
    REQUIRE(history.newest().firing);
    REQUIRE(history.newest().recoil_active);
    REQUIRE(history.newest().saturated);
    REQUIRE(history.newest().output_disabled);
}

void test_history_owns_only_inline_fixed_capacity_storage() {
    static_assert(std::is_trivially_copyable_v<
        control_learning::ControlHistory<8>>);
    REQUIRE(sizeof(control_learning::ControlHistory<8>) < 8192);
}

void test_recent_interval_precision_survives_long_process_uptime() {
    control_learning::ControlHistory<8> history;
    for (std::uint64_t index = 1; index <= 10'000; ++index) {
        REQUIRE(history.push(sample(
            index, index * 1'000'000'000ull, {0.5f, 0.0f})));
    }
    const auto interval = history.integrate(
        9'999'980'000'000ull, 10'000'000'000'000ull);
    REQUIRE_NEAR(interval.final_right_stick_seconds.x, 0.01f, 1.0e-6f);
}

}  // namespace

int main() {
    test_history_integrates_piecewise_constant_delivery();
    test_history_rejects_non_monotonic_sample();
    test_history_overwrites_oldest_in_order();
    test_failed_delivery_and_sequence_gap_make_interval_ineligible();
    test_history_retains_components_and_flags();
    test_history_owns_only_inline_fixed_capacity_storage();
    test_recent_interval_precision_survives_long_process_uptime();
    return 0;
}
