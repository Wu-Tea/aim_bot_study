#include "manual_fire_aim_activation.h"
#include "runtime_timing.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
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

void test_deadline_state_realigns_without_replaying_missed_ticks() {
    runtime_app::AbsoluteDeadlineState state(
        std::chrono::steady_clock::time_point{}, std::chrono::milliseconds(1));
    REQUIRE(state.next_deadline() ==
        std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(1));
    const auto skipped = state.advance_after_tick(
        std::chrono::steady_clock::time_point{} + std::chrono::microseconds(3400));
    REQUIRE(skipped == 2);
    REQUIRE(state.next_deadline() ==
        std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(4));
}

void test_deadline_state_has_no_accumulated_drift() {
    runtime_app::AbsoluteDeadlineState state(
        std::chrono::steady_clock::time_point{}, std::chrono::milliseconds(1));
    for (int tick = 1; tick <= 1000; ++tick) {
        const auto now = std::chrono::steady_clock::time_point{} +
            std::chrono::microseconds(tick * 1000 + 100);
        state.advance_after_tick(now);
    }
    REQUIRE(state.next_deadline() ==
        std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(1001));
}

struct ManualFireTargetPresentIncidentMeasurement {
    bool fire_edge_observed = false;
    bool target_present_scope_active = false;
    bool target_present_scope_held = false;
    bool target_present_scope_released = false;
    bool no_target_scope_active = false;
    bool disabled_scope_active = false;
};

ManualFireTargetPresentIncidentMeasurement measure_manual_fire_target_present_incident() {
    ManualFireTargetPresentIncidentMeasurement measured;

    runtime_app::ManualFireAimActivationTracker incident;
    (void)incident.update(true, false, true);
    const auto target_present_onset = incident.update(true, true, true);
    measured.fire_edge_observed = target_present_onset.fire_pressed_now;
    measured.target_present_scope_active = target_present_onset.scope_active;
    measured.target_present_scope_held = incident.update(true, true, true).scope_active;
    measured.target_present_scope_released = incident.update(true, false, true).scope_active;

    runtime_app::ManualFireAimActivationTracker no_target;
    measured.no_target_scope_active = no_target.update(true, true, false).scope_active;

    runtime_app::ManualFireAimActivationTracker disabled;
    measured.disabled_scope_active = disabled.update(false, true, true).scope_active;
    return measured;
}

void write_manual_fire_target_present_report(
    const std::string& path,
    const ManualFireTargetPresentIncidentMeasurement& measured) {
    if (path.empty()) return;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    REQUIRE(output.good());
    output
        << "{\n"
        << "  \"schema_version\": 1,\n"
        << "  \"incident_id\": \"manual-fire-target-present-activation\",\n"
        << "  \"target_present\": true,\n"
        << "  \"fire_edge_observed\": " << (measured.fire_edge_observed ? "true" : "false") << ",\n"
        << "  \"target_present_scope_active\": " << (measured.target_present_scope_active ? "true" : "false") << ",\n"
        << "  \"target_present_scope_held\": " << (measured.target_present_scope_held ? "true" : "false") << ",\n"
        << "  \"target_present_scope_released\": " << (measured.target_present_scope_released ? "true" : "false") << ",\n"
        << "  \"no_target_scope_active\": " << (measured.no_target_scope_active ? "true" : "false") << ",\n"
        << "  \"disabled_scope_active\": " << (measured.disabled_scope_active ? "true" : "false") << "\n"
        << "}\n";
}

std::string report_path_from_args(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--output") return argv[i + 1];
    }
    return {};
}

} // namespace

int main(int argc, char** argv) {
    test_short_deadline_uses_yield_margin_instead_of_one_ms_sleep();
    test_long_deadline_sleeps_only_until_precision_margin();
    test_precise_sleep_accepts_custom_precision_margin();
    test_vision_service_wait_margin_is_larger_than_controller_tick();
    test_runtime_thread_priority_names_are_stable();
    test_can_restore_current_thread_to_normal_priority();
    test_timer_period_scope_records_requested_period();
    test_deadline_state_realigns_without_replaying_missed_ticks();
    test_deadline_state_has_no_accumulated_drift();

    const auto measured = measure_manual_fire_target_present_incident();
    write_manual_fire_target_present_report(report_path_from_args(argc, argv), measured);
    const bool passed =
        measured.fire_edge_observed &&
        measured.target_present_scope_active &&
        measured.target_present_scope_held &&
        !measured.target_present_scope_released &&
        measured.no_target_scope_active &&
        !measured.disabled_scope_active;
    if (!passed) {
        std::cerr << "manual-fire target-present activation regression failed" << std::endl;
        return 1;
    }
    return 0;
}
