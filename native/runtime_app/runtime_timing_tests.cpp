#include "runtime_timing.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

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

void test_precise_sleep_accepts_custom_precision_margin() {
    runtime_app::sleep_until_precise(
        std::chrono::steady_clock::now(),
        std::chrono::milliseconds(3));
}

void test_vision_service_wait_margin_is_larger_than_controller_tick() {
    const auto margin = runtime_app::vision_service_wait_precision_margin();

    REQUIRE(margin >= std::chrono::milliseconds(3));
    REQUIRE(margin <= std::chrono::milliseconds(4));
}

void test_runtime_thread_priority_names_are_stable() {
    REQUIRE(runtime_app::runtime_thread_priority_name(
                runtime_app::RuntimeThreadPriority::Normal) == std::string("normal"));
    REQUIRE(runtime_app::runtime_thread_priority_name(
                runtime_app::RuntimeThreadPriority::AboveNormal) == std::string("above_normal"));
}

void test_can_restore_current_thread_to_normal_priority() {
    REQUIRE(runtime_app::set_current_thread_priority(
        runtime_app::RuntimeThreadPriority::Normal));
}

void test_timer_period_scope_records_requested_period() {
    runtime_app::HighResolutionTimerPeriod period(1u);

    REQUIRE(period.requested_period_ms() == 1u);
}

} // namespace

int main() {
    test_short_deadline_uses_yield_margin_instead_of_one_ms_sleep();
    test_long_deadline_sleeps_only_until_precision_margin();
    test_precise_sleep_accepts_custom_precision_margin();
    test_vision_service_wait_margin_is_larger_than_controller_tick();
    test_runtime_thread_priority_names_are_stable();
    test_can_restore_current_thread_to_normal_priority();
    test_timer_period_scope_records_requested_period();
    return 0;
}
