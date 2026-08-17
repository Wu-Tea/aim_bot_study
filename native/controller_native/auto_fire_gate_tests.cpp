#include "auto_fire_gate.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void require_true(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_false(bool condition, const char* message) {
    if (condition) {
        throw std::runtime_error(message);
    }
}

void require_eq_u64(
    std::uint64_t actual,
    std::uint64_t expected,
    const char* message) {
    if (actual != expected) {
        throw std::runtime_error(message);
    }
}

controller_native::NativeControllerVisionState strong_target(double observed_at_seconds) {
    controller_native::NativeControllerVisionState state;
    state.has_target = true;
    state.auto_fire_requested = true;
    state.aim_authority = true;
    state.fire_authority = true;
    state.current_observed_target_present = true;
    state.target_tier = "strong";
    state.dx = 1.0f;
    state.dy = 1.0f;
    state.observed_at_seconds = observed_at_seconds;
    return state;
}

controller_native::AutoFireGateInput ready_input(
    double now_seconds,
    std::uint64_t vision_sequence = 0) {
    controller_native::AutoFireGateInput input;
    input.vision_state = strong_target(now_seconds);
    input.vision_state.vision_sequence = vision_sequence;
    input.aiming = true;
    input.ads_min_elapsed = true;
    input.manual_fire_pressed = false;
    input.now_seconds = now_seconds;
    input.settle_dx = 1.0f;
    input.settle_dy = 1.0f;
    input.manual_right_x = 0.0f;
    input.manual_right_y = 0.0f;
    input.output_right_x = 0.0f;
    input.output_right_y = 0.0f;
    return input;
}

void test_ready_frames_gate_before_firing() {
    controller_native::GamepadAutoFireConfig auto_fire;
    auto_fire.require_aim_ready = true;
    auto_fire.max_source_age_ms = 50.0f;

    controller_native::GamepadAiAimConfig ai;
    ai.auto_fire_ready_error_px = 4.0f;
    ai.auto_fire_ready_frames = 2;
    ai.auto_fire_ready_max_ai_stick = 6000.0f;
    ai.ai_delta_gain = 1.0f;

    controller_native::AutoFireGate gate(auto_fire, ai);

    controller_native::AutoFireGateDecision decision =
        gate.evaluate(ready_input(10.000, 1));
    require_false(decision.aim_ready, "first settled frame should not be aim-ready");
    require_false(decision.should_fire, "first settled frame should not fire");
    require_true(
        decision.block_reason == controller_native::AutoFireBlockReason::AimNotReady,
        "readiness block should be observable");

    decision = gate.evaluate(ready_input(10.010, 2));
    require_true(decision.aim_ready, "second settled frame should be aim-ready");
    require_true(decision.should_fire, "ready target should fire");
    require_true(
        decision.block_reason == controller_native::AutoFireBlockReason::None,
        "allowed fire should have no block reason");

    const controller_native::NativeAutoFireCounters counters = gate.counters();
    require_eq_u64(counters.requested, 2, "ready-frame gate should count both requests");
    require_eq_u64(counters.allowed, 1, "ready-frame gate should count one allowed request");
    require_eq_u64(counters.blocked, 1, "ready-frame gate should count one blocked request");
}

void test_ready_frames_count_unique_vision_sequences() {
    controller_native::GamepadAutoFireConfig auto_fire;
    auto_fire.require_aim_ready = true;

    controller_native::GamepadAiAimConfig ai;
    ai.auto_fire_ready_error_px = 4.0f;
    ai.auto_fire_ready_frames = 2;
    ai.ai_delta_gain = 1.0f;

    controller_native::AutoFireGate gate(auto_fire, ai);
    auto decision = gate.evaluate(ready_input(11.000, 100));
    require_false(decision.aim_ready, "first vision sequence should start readiness");

    decision = gate.evaluate(ready_input(11.001, 100));
    require_false(
        decision.aim_ready,
        "repeated controller ticks from one vision sequence must not satisfy readiness");
    require_false(decision.should_fire, "one vision sequence must never trigger auto-fire");

    decision = gate.evaluate(ready_input(11.012, 101));
    require_true(decision.aim_ready, "second unique vision sequence should satisfy readiness");
    require_true(decision.should_fire, "stable target should fire on the second vision sequence");
}

void test_stale_source_blocks_fire_and_resets_readiness() {
    controller_native::GamepadAutoFireConfig auto_fire;
    auto_fire.require_aim_ready = true;
    auto_fire.max_source_age_ms = 20.0f;

    controller_native::GamepadAiAimConfig ai;
    ai.auto_fire_ready_error_px = 4.0f;
    ai.auto_fire_ready_frames = 2;
    ai.auto_fire_ready_max_ai_stick = 6000.0f;

    controller_native::AutoFireGate gate(auto_fire, ai);

    controller_native::AutoFireGateInput stale = ready_input(10.100);
    stale.vision_state.observed_at_seconds = 10.000;
    controller_native::AutoFireGateDecision decision = gate.evaluate(stale);
    require_false(decision.aim_ready, "stale target should not be aim-ready");
    require_false(decision.should_fire, "stale target should not fire");

    decision = gate.evaluate(ready_input(10.110));
    require_false(decision.should_fire, "fresh target needs a new first ready frame after stale reset");
}

void test_manual_takeover_preserves_physical_output_then_guards_resume() {
    controller_native::GamepadAutoFireConfig auto_fire;
    auto_fire.require_aim_ready = false;
    auto_fire.manual_takeover_release_seconds = 0.050f;
    auto_fire.manual_takeover_resume_delay_seconds = 0.100f;

    controller_native::GamepadAiAimConfig ai;
    controller_native::AutoFireGate gate(auto_fire, ai);

    controller_native::AutoFireGateDecision decision = gate.evaluate(ready_input(20.000));
    require_true(decision.should_fire, "auto-fire should start before manual takeover");

    controller_native::AutoFireGateInput manual = ready_input(20.010);
    manual.manual_fire_pressed = true;
    decision = gate.evaluate(manual);
    require_false(decision.should_fire, "manual fire should block auto-fire takeover frame");
    require_true(
        decision.block_reason == controller_native::AutoFireBlockReason::ManualFire,
        "manual fire takeover should be observable");

    controller_native::AutoFireGateInput guarded = ready_input(20.080);
    guarded.manual_fire_pressed = false;
    decision = gate.evaluate(guarded);
    require_false(decision.should_fire, "takeover guard should delay auto-fire resume");
    require_true(
        decision.block_reason == controller_native::AutoFireBlockReason::ManualTakeoverGuard,
        "manual takeover guard should be observable");
    decision = gate.evaluate(ready_input(20.170));
    require_true(decision.should_fire, "auto-fire should resume after takeover guard expires");
}

void test_pulse_scheduler_emits_ten_thirty_ms_presses_per_second() {
    controller_native::GamepadAutoFireConfig auto_fire;
    auto_fire.require_aim_ready = false;
    auto_fire.pulse_width_ms = 30.0f;
    auto_fire.pulse_period_ms = 100.0f;
    controller_native::AutoFireGate gate(auto_fire, {});

    std::uint64_t starts = 0;
    bool previous = false;
    int current_width_ticks = 0;
    std::vector<int> widths;
    for (int tick = 0; tick < 1000; ++tick) {
        const auto decision = gate.evaluate(
            ready_input(30.0 + tick * 0.001, tick + 1));
        if (decision.should_fire && !previous) ++starts;
        if (decision.should_fire) ++current_width_ticks;
        if (!decision.should_fire && previous) {
            widths.push_back(current_width_ticks);
            current_width_ticks = 0;
        }
        previous = decision.should_fire;
    }
    require_eq_u64(starts, 10, "one second must start exactly ten pulses");
    require_true(widths.size() == 10, "all ten pulses must finish inside the sample");
    require_true(
        std::all_of(widths.begin(), widths.end(),
                    [](int width) { return width >= 30; }),
        "every pulse must last at least 30ms");
    require_eq_u64(gate.counters().pulse_starts, 10,
                   "pulse-start counter must not count held ticks");
}

void test_pulse_boundaries_and_wait_state_are_explicit() {
    controller_native::GamepadAutoFireConfig auto_fire;
    auto_fire.require_aim_ready = false;
    auto_fire.pulse_width_ms = 30.0f;
    auto_fire.pulse_period_ms = 100.0f;
    controller_native::AutoFireGate gate(auto_fire, {});

    auto decision = gate.evaluate(ready_input(40.000, 1));
    require_true(decision.should_fire, "first authorized tick must fire immediately");
    decision = gate.evaluate(ready_input(40.029, 2));
    require_true(decision.should_fire, "pulse must remain pressed through 29ms");
    decision = gate.evaluate(ready_input(40.031, 3));
    require_false(decision.should_fire, "pulse must release after 30ms");
    require_true(decision.pulse_waiting, "cadence wait must be observable");
    decision = gate.evaluate(ready_input(40.099, 4));
    require_false(decision.should_fire, "cadence must stay released before 100ms");
    decision = gate.evaluate(ready_input(40.100, 5));
    require_true(decision.should_fire, "next pulse must start at 100ms");
}

void test_pulse_scheduler_never_catches_up_with_a_burst() {
    controller_native::GamepadAutoFireConfig auto_fire;
    auto_fire.require_aim_ready = false;
    controller_native::AutoFireGate gate(auto_fire, {});

    require_true(gate.evaluate(ready_input(50.000, 1)).should_fire,
                 "first pulse must start immediately");
    require_true(gate.evaluate(ready_input(50.250, 2)).should_fire,
                 "late evaluation may start one current pulse");
    require_eq_u64(gate.counters().pulse_starts, 2,
                   "clock jump must not replay missed starts");
    require_true(gate.evaluate(ready_input(50.251, 3)).should_fire,
                 "late pulse must remain one continuous press");
    require_eq_u64(gate.counters().pulse_starts, 2,
                   "next tick must not add a catch-up start");
}

void test_authority_revocation_interrupts_and_resets_pulse() {
    controller_native::GamepadAutoFireConfig auto_fire;
    auto_fire.require_aim_ready = false;
    controller_native::AutoFireGate gate(auto_fire, {});

    require_true(gate.evaluate(ready_input(60.000, 1)).should_fire,
                 "precondition: pulse must be active");
    auto revoked = ready_input(60.010, 2);
    revoked.vision_state.auto_fire_requested = false;
    require_false(gate.evaluate(revoked).should_fire,
                  "safety revocation must interrupt a pulse immediately");
    require_true(gate.evaluate(ready_input(60.011, 3)).should_fire,
                 "fresh authorization after revocation starts immediately");
    require_eq_u64(gate.counters().pulse_starts, 2,
                   "reauthorization must create one new pulse");
}

}  // namespace

int main() {
    try {
        test_ready_frames_gate_before_firing();
        test_ready_frames_count_unique_vision_sequences();
        test_stale_source_blocks_fire_and_resets_readiness();
        test_manual_takeover_preserves_physical_output_then_guards_resume();
        test_pulse_scheduler_emits_ten_thirty_ms_presses_per_second();
        test_pulse_boundaries_and_wait_state_are_explicit();
        test_pulse_scheduler_never_catches_up_with_a_burst();
        test_authority_revocation_interrupts_and_resets_pulse();
        std::cout << "[AutoFireGateTests] PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[AutoFireGateTests][FAIL] " << error.what() << '\n';
        return 1;
    }
}
