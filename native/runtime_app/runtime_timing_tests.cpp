#include "runtime_timing.h"

#include <chrono>
#include <cstdlib>
#include <iostream>

namespace {

#define REQUIRE(condition) require((condition), __LINE__)

void require(bool condition, int line) {
    if (!condition) {
        std::cerr << "require failed at line " << line << std::endl;
        std::abort();
    }
}

void test_short_deadline_uses_yield_margin_instead_of_one_ms_sleep() {
    const auto now = std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(100);
    const auto due = now + std::chrono::microseconds(900);

    const auto sleep_duration = runtime_app::coarse_sleep_duration_until(due, now);

    REQUIRE(sleep_duration == std::chrono::steady_clock::duration::zero());
}

void test_long_deadline_sleeps_only_until_precision_margin() {
    const auto now = std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(100);
    const auto due = now + std::chrono::milliseconds(10);

    const auto sleep_duration = runtime_app::coarse_sleep_duration_until(due, now);

    REQUIRE(sleep_duration > std::chrono::milliseconds(8));
    REQUIRE(sleep_duration < std::chrono::milliseconds(10));
}

void test_timer_period_scope_records_requested_period() {
    runtime_app::HighResolutionTimerPeriod period(1u);

    REQUIRE(period.requested_period_ms() == 1u);
}

} // namespace

int main() {
    test_short_deadline_uses_yield_margin_instead_of_one_ms_sleep();
    test_long_deadline_sleeps_only_until_precision_margin();
    test_timer_period_scope_records_requested_period();
    return 0;
}
