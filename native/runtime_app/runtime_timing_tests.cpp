#include "controller_native/aim_scope_reducer.h"
#include "runtime_timing.h"
#include "test_support/native_test_registry.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

std::string g_manual_fire_report_path;

#define REQUIRE(condition) require((condition), __LINE__)

void require(bool condition, int line) {
    if (!condition) throw std::runtime_error(
        "runtime timing assertion failed at line " + std::to_string(line));
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

struct ScopeHarness {
    controller_native::InputEdgeReducer input_edges;
    controller_native::AimScopeReducer scope;
    std::uint64_t sequence = 1;

    controller_native::AimScopeSnapshot update(
        const controller_native::PhysicalGamepadState& physical,
        bool rb_counts_as_aiming,
        bool manual_fire_activates_aim) {
        const auto edges = input_edges.sample(
            physical,
            rb_counts_as_aiming,
            &sequence);
        return scope.reduce(edges, manual_fire_activates_aim);
    }
};

ManualFireTargetPresentIncidentMeasurement measure_manual_fire_target_present_incident() {
    ManualFireTargetPresentIncidentMeasurement measured;

    controller_native::PhysicalGamepadState fire;
    fire.right_trigger = 1.0f;
    controller_native::PhysicalGamepadState released;

    ScopeHarness incident;
    (void)incident.update(released, false, true);
    const auto target_present_onset = incident.update(fire, false, true);
    measured.fire_edge_observed = target_present_onset.manual_fire_pressed;
    measured.target_present_scope_active = target_present_onset.assist_active;
    measured.target_present_scope_held =
        incident.update(fire, false, true).assist_active;
    measured.target_present_scope_released =
        incident.update(released, false, true).assist_active;

    ScopeHarness no_target;
    measured.no_target_scope_active =
        no_target.update(fire, false, true).assist_active;

    ScopeHarness disabled;
    measured.disabled_scope_active =
        disabled.update(fire, false, false).assist_active;
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

void register_runtime_timing_tests(native_test::Registry& registry) {
    registry.add_case("BaseRuntimeFreshness", "short_deadline_uses_yield_margin", test_short_deadline_uses_yield_margin_instead_of_one_ms_sleep);
    registry.add_case("BaseRuntimeFreshness", "long_deadline_sleeps_to_precision_margin", test_long_deadline_sleeps_only_until_precision_margin);
    registry.add_case("BaseRuntimeFreshness", "precise_sleep_accepts_custom_margin", test_precise_sleep_accepts_custom_precision_margin);
    registry.add_case("BaseRuntimeFreshness", "vision_wait_margin_exceeds_controller_tick", test_vision_service_wait_margin_is_larger_than_controller_tick);
    registry.add_case("BaseRuntimeFreshness", "runtime_thread_priority_names_are_stable", test_runtime_thread_priority_names_are_stable);
    registry.add_case("BaseRuntimeFreshness", "thread_priority_restores_to_normal", test_can_restore_current_thread_to_normal_priority);
    registry.add_case("BaseRuntimeFreshness", "timer_period_records_requested_period", test_timer_period_scope_records_requested_period);
    registry.add_case("BaseRuntimeFreshness", "deadline_realigns_without_replay", test_deadline_state_realigns_without_replaying_missed_ticks);
    registry.add_case("BaseRuntimeFreshness", "deadline_has_no_accumulated_drift", test_deadline_state_has_no_accumulated_drift);
    registry.add_context_case("BaseRuntimeFreshness", "manual_fire_target_present_activation", [](const native_test::TestContext& context) {
        const auto measured = measure_manual_fire_target_present_incident();
        const std::string path = g_manual_fire_report_path.empty()
            ? context.artifact_path("manual_fire_target_present_incident.json").string()
            : g_manual_fire_report_path;
        write_manual_fire_target_present_report(path, measured);
        REQUIRE(measured.fire_edge_observed);
        REQUIRE(measured.target_present_scope_active);
        REQUIRE(measured.target_present_scope_held);
        REQUIRE(!measured.target_present_scope_released);
        REQUIRE(measured.no_target_scope_active);
        REQUIRE(!measured.disabled_scope_active);
    });
}
