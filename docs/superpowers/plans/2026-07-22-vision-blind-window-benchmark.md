# Vision Blind-Window Benchmark Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a deterministic phase-locked benchmark that reproduces stale-observation control debt, scores it with uncapped causal metrics, and proves the current controller baseline is discriminating before any learner is implemented.

**Architecture:** Add a focused benchmark module beside the sustained AimLab simulator rather than expanding its score owner. A 1 kHz delayed plant owns ground truth, capture/result scheduling and response queues; a pure metrics accumulator consumes trace frames; fixture factories encode K1-K4 knowledge classes. The first implementation batch is B0-B2: contracts, scheduler, and the K1 pending-crossing baseline.

**Tech Stack:** C++17, CMake/MSVC Release, existing `sustained_aimlab` vector/controller contracts, fixed-seed native tests, JSON artifact output.

---

## File map

| File | Responsibility |
|---|---|
| `native/controller_native/blind_window_benchmark.h/.cpp` | Public fixture, timing, plant, trace and result API. |
| `native/controller_native/blind_window_metrics.h/.cpp` | Pure primary/guardrail metric calculations and acceptance helpers. |
| `native/controller_native/blind_window_fixtures.h/.cpp` | K1-K4 fixture factories only. |
| `native/controller_native/blind_window_benchmark_tests.cpp` | Contract, plant, causality and metric tests. |
| `native/controller_native/cod_native_blind_window_benchmark.cpp` | Fixed-seed CLI and JSON report. |
| `native/vision_native/CMakeLists.txt` | Test and executable targets. |
| `docs/benchmarks/vision-blind-window.md` | Commands, schema and interpretation. |

The benchmark may use `Vec2d` and `ControllerStepResult`, but it does not modify `run_simulation`, `TargetScorer`, production controller state, or runtime configuration.

### Task 1: B0 metric and report contracts

**Files:**
- Create: `native/controller_native/blind_window_benchmark.h`
- Create: `native/controller_native/blind_window_metrics.h`
- Create: `native/controller_native/blind_window_metrics.cpp`
- Create: `native/controller_native/blind_window_benchmark_tests.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Write failing metric-contract tests**

```cpp
void test_harmful_pending_counts_opposing_and_excess_motion() {
    const Vec2d error{8.0, 0.0};
    require_near(harmful_pending_at_reveal(error, {-3.0, 0.0}), 3.0);
    require_near(harmful_pending_at_reveal(error, {12.0, 0.0}), 4.0);
    require_near(harmful_pending_at_reveal(error, {5.0, 0.0}), 0.0);
}

void test_future_burden_keeps_px_ms_units() {
    BlindWindowTrace trace = constant_error_trace(10.0, 80);
    require_near(future_error_burden(trace, 0, 80), 800.0);
}

void test_user_fight_ignores_neutral_drift() {
    BlindWindowTrace trace = one_ms_trace(
        {.manual_stick={0.02,0.0}, .ai_stick={-1.0,0.0}});
    require_near(user_fight_area(trace, 0.03), 0.0);
}
```

- [ ] **Step 2: Build and prove RED**

Run:

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_blind_window_benchmark_tests
```

Expected: target and headers do not exist.

- [ ] **Step 3: Define versioned contracts**

```cpp
enum class BlindKnowledgeClass : std::uint8_t {
    SelfPredictable,
    InputObservable,
    ExternallyUnobservable,
    ObservationInvalid,
};

struct BlindTimingProfile {
    int vision_period_us = 10'000;
    int event_phase_per_mille = 50;
    int result_latency_us = 16'000;
    int response_delay_us = 45'000;
    std::uint32_t jitter_seed = 0;
};

struct BlindWindowMetrics {
    double blind_duration_ms = 0.0;
    double stale_ai_impulse_stick_ms = 0.0;
    double harmful_ai_motion_px = 0.0;
    double harmful_pending_at_reveal_px = 0.0;
    double future_burden_40_px_ms = 0.0;
    double future_burden_80_px_ms = 0.0;
    double future_burden_160_px_ms = 0.0;
    double reverse_correction_80_stick_ms = 0.0;
    int reveal_to_reacquire_ms = -1;
    double post_cross_area_px_ms = 0.0;
    double user_fight_stick_ms = 0.0;
    double far_error_closing_speed_px_per_sec = 0.0;
    double output_total_variation = 0.0;
    double p95_output_delta = 0.0;
    int incorrect_interruption_count = 0;
    int identity_authority_violations = 0;
    int future_dependency_violations = 0;
};
```

Add `kBlindWindowFixtureSemanticsVersion = 1` and `finite(const BlindWindowMetrics&)`.

- [ ] **Step 4: Implement pure metric helpers**

Implement the exact formulas from the approved design. `future_error_burden` integrates one-millisecond ground-truth frames. `harmful_pending_at_reveal` is:

```cpp
const Vec2d axis = normalized(error);
const double closing = dot(axis, pending_reticle_motion);
return std::max(0.0, -closing) +
       std::max(0.0, closing - length(error));
```

Do not calculate a total score.

- [ ] **Step 5: Run focused tests**

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_blind_window_benchmark_tests
ctest --test-dir native/vision_native/build -C Release -R NativeBlindWindowBenchmarkTests --output-on-failure
```

Expected: all metric-contract tests pass.

- [ ] **Step 6: Commit**

```powershell
git add native/controller_native/blind_window_benchmark.h native/controller_native/blind_window_metrics.* native/controller_native/blind_window_benchmark_tests.cpp native/vision_native/CMakeLists.txt
git commit -m "bench: define blind-window metric contracts"
```

### Task 2: B1 deterministic capture/result/response scheduler

**Files:**
- Modify: `native/controller_native/blind_window_benchmark.h`
- Create: `native/controller_native/blind_window_benchmark.cpp`
- Modify: `native/controller_native/blind_window_benchmark_tests.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Write failing scheduler tests**

```cpp
void test_event_occurs_at_requested_capture_phase() {
    BlindTimingProfile timing{10'000, 250, 16'000, 45'000, 0};
    const auto schedule = build_blind_schedule(timing, 0, 50'000);
    require(schedule.event_at_us == 2'500);
    require(schedule.next_capture_at_us == 10'000);
    require(schedule.next_result_at_us == 26'000);
}

void test_controller_never_receives_future_capture() {
    auto trace = run_blind_fixture(minimal_fixture(), zero_controller());
    for (const auto& frame : trace.frames) {
        require(frame.max_controller_source_time_us <= frame.now_us);
    }
}

void test_delivered_input_changes_plant_only_after_response_delay() {
    auto fixture = minimal_fixture();
    fixture.timing.response_delay_us = 25'000;
    const auto trace = run_blind_fixture(fixture, constant_x_controller(1.0));
    require_near(trace.at_us(24'000).true_error_px.x,
                 trace.at_us(0).true_error_px.x);
    require(trace.at_us(26'000).true_error_px.x <
            trace.at_us(24'000).true_error_px.x);
}
```

- [ ] **Step 2: Run and prove RED**

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_blind_window_benchmark_tests
```

Expected: scheduler and runner symbols are missing.

- [ ] **Step 3: Implement separate clocks**

At each 1 ms plant tick:

```text
apply target motion/events to ground truth
apply control samples whose response-delay deadline has arrived
capture ground truth if capture is due
publish only captured frames whose result deadline has arrived
call controller with latest published observation and current manual/left input
enqueue final delivered stick for future plant response
append immutable trace frame
```

The controller receives capture timestamp and result timestamp for audit, but only data already published at `now`.

- [ ] **Step 4: Implement fixed and jittered result latency**

Use an integer xorshift32 seeded by `jitter_seed`; jitter selects a deterministic latency in `[8,32] ms`. Re-running identical fixture/seed must produce byte-identical non-performance fields.

- [ ] **Step 5: Run scheduler tests twice**

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_blind_window_benchmark_tests
& native/vision_native/build/Release/cod_native_blind_window_benchmark_tests.exe
& native/vision_native/build/Release/cod_native_blind_window_benchmark_tests.exe
```

Expected: both runs pass.

- [ ] **Step 6: Commit**

```powershell
git add native/controller_native/blind_window_benchmark.* native/controller_native/blind_window_benchmark_tests.cpp native/vision_native/CMakeLists.txt
git commit -m "bench: schedule phase-locked blind windows"
```

### Task 3: B2 K1 pending-crossing fixture and current baseline

**Files:**
- Create: `native/controller_native/blind_window_fixtures.h`
- Create: `native/controller_native/blind_window_fixtures.cpp`
- Modify: `native/controller_native/blind_window_benchmark_tests.cpp`
- Create: `native/controller_native/cod_native_blind_window_benchmark.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Write failing K1 discrimination tests**

```cpp
void test_k1_fixture_crosses_during_blind_window_with_naive_controller() {
    const auto result = run_blind_fixture(
        bodylock_pending_crossing_fixture(1337, timing_100hz_phase_5()),
        stale_proportional_controller());
    require(result.metrics.harmful_pending_at_reveal_px > 0.5);
    require(result.metrics.reverse_correction_80_stick_ms > 0.0);
    require(result.metrics.future_burden_80_px_ms > 0.0);
}

void test_k1_fixture_has_no_target_surprise() {
    const auto fixture = bodylock_pending_crossing_fixture(
        1337, timing_100hz_phase_5());
    require(fixture.knowledge_class == BlindKnowledgeClass::SelfPredictable);
    require(fixture.target_acceleration_px_per_sec2.x == 0.0);
    require(fixture.target_acceleration_px_per_sec2.y == 0.0);
}
```

- [ ] **Step 2: Run and prove RED**

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_blind_window_benchmark_tests
```

- [ ] **Step 3: Implement the cleaned track-6671 fixture**

Use a constant-velocity target, initial error near 16 px, response delay 45 ms, and an initial delivered-control history that creates same-direction scheduled motion. No reversal, target switch, observation noise, manual input, or left-stick input is allowed in K1.

- [ ] **Step 4: Add the current-controller adapter**

Reuse the existing benchmark controller construction used by `cod_native_sustained_aimlab_benchmark`. Feed only published observations. Record base AI, shaped AI, manual, final delivered output, mode and diagnostics separately. Do not introduce learner inputs.

- [ ] **Step 5: Emit the B2 JSON baseline**

Run every combination:

```text
seed: 1337, 7331, 20260722
Vision: 80, 100, 120 Hz
phase: 5, 25, 50, 75, 95 percent
result latency: 8, 16, 28 ms
response delay: 10, 25, 45, 70, 100 ms
```

The CLI writes provenance, fixture semantics, raw per-episode metrics, phase summaries, worst episodes and `baseline_discriminating`.

- [ ] **Step 6: Run tests and baseline**

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_blind_window_benchmark_tests cod_native_blind_window_benchmark
ctest --test-dir native/vision_native/build -C Release -R NativeBlindWindowBenchmarkTests --output-on-failure
& native/vision_native/build/Release/cod_native_blind_window_benchmark.exe --fixture k1 --output artifacts/benchmarks/blind-window/k1-baseline.json
```

Expected: tests pass, JSON is finite, at least the 5% and 25% phases show nonzero harmful pending under one or more 25-70 ms response delays, and no future dependency violation exists.

- [ ] **Step 7: Commit**

```powershell
git add native/controller_native/blind_window_fixtures.* native/controller_native/blind_window_benchmark* native/controller_native/cod_native_blind_window_benchmark.cpp native/vision_native/CMakeLists.txt artifacts/benchmarks/blind-window/k1-baseline.json
git commit -m "bench: reproduce blind-window pending crossing"
```

## Execution checkpoint

Implement Tasks 1-3 first. Report B2 metric distributions and any non-discriminating combinations before proceeding to K2-K4. No production source or runtime config may change in this batch.

K2-K4 fixtures, mutations, and the retained B7 full baseline remain specified by the approved design but intentionally require a second implementation plan after the B2 discrimination checkpoint. This prevents later fixture assumptions from being built on a non-discriminating K1 plant.
