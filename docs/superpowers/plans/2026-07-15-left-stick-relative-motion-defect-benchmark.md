# Left-Stick Relative-Motion Defect Benchmark Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a deterministic native benchmark that proves and quantifies left-stick intent blindness and the resulting strafe transition defects without changing production behavior.

**Architecture:** Add a focused benchmark module with an open-loop intent-invariance probe and a closed-loop body-height-normalized motion simulator. Drive the real `NativeGamepadController`, emit JSON evidence, and expose a `--require-fixed` command that intentionally fails against the current behavior.

**Tech Stack:** C++17, existing native controller/tracker code, CMake/MSBuild, deterministic JSON artifact output.

---

### Task 1: Benchmark Contracts and Fixture Validation

**Files:**
- Create: `native/controller_native/left_stick_motion_defect_benchmark.h`
- Create: `native/controller_native/left_stick_motion_defect_benchmark.cpp`
- Create: `native/controller_native/cod_native_left_stick_motion_benchmark.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Write the failing benchmark entry point**

Declare and call the desired API before its implementation exists:

```cpp
namespace controller_native::left_stick_defect {
struct BenchmarkReport;
BenchmarkReport run_benchmark();
bool validate_report(const BenchmarkReport&, std::string* reason);
void write_json(std::ostream&, const BenchmarkReport&);
}
```

The entry point must validate the report, optionally write `--output <path>`,
and return `2` for `--require-fixed` while any desired gate fails.

- [ ] **Step 2: Configure/build and verify RED**

Run:

```powershell
& $cmake --build --preset modern-release --target cod_native_lstick_benchmark -- /m
```

Expected: build fails because the benchmark API has no implementation.

- [ ] **Step 3: Implement minimal contracts and fixture validation**

Define:

```cpp
struct PhaseEvent {
    int tick;
    double time_seconds;
    float left_x;
    float manual_right_x;
    float ai_right_x;
    float final_right_x;
    double error_px;
    double player_velocity_body_s;
    double target_velocity_body_s;
};

struct ScenarioMetrics {
    std::string name;
    double mean_abs_error_px;
    double p95_abs_error_px;
    double max_abs_error_px;
    double onset_response_latency_ms;
    double reversal_response_latency_ms;
    double release_response_latency_ms;
    int ai_opposes_manual_frames;
    int final_opposes_oracle_frames;
    int high_ai_output_frames;
    int output_spike_frames;
    bool defect_reproduced;
    bool desired_gate_pass;
    std::vector<PhaseEvent> events;
};
```

Add report-level scenario count, deterministic seed/timing metadata, and fixture
validation. Do not run production controller behavior yet.

- [ ] **Step 4: Build and run the fixture contract**

Expected: executable exits zero, reports six named fixtures, and says the
behavior probes are not yet populated.

### Task 2: Open-Loop Left-Intent Invariance Probe

**Files:**
- Modify: `native/controller_native/left_stick_motion_defect_benchmark.cpp`
- Modify: `native/controller_native/cod_native_left_stick_motion_benchmark.cpp`

- [ ] **Step 1: Add the desired-behavior RED assertion**

Run two real controllers with identical vision and right-stick traces but
different left-stick phase traces. Assert that at least one AI-right sample
changes by more than `1e-4` around onset, reversal, or release.

- [ ] **Step 2: Build/run and verify the expected RED reason**

Expected: the desired assertion fails because maximum AI trace delta is zero or
effectively zero, while left output passthrough remains exact.

- [ ] **Step 3: Convert the assertion into persistent defect evidence**

Populate:

```cpp
struct IntentInvarianceMetrics {
    double max_ai_trace_delta;
    double max_final_right_trace_delta;
    double max_left_passthrough_error;
    int phase_event_samples;
    bool left_intent_ignored;
    bool desired_gate_pass;
};
```

Normal benchmark execution must remain green when `left_intent_ignored` is
true; `--require-fixed` must remain RED.

- [ ] **Step 4: Run twice and require exact deterministic metrics**

Expected: both runs return identical invariance metrics and phase-event traces.

### Task 3: Closed-Loop Strafe Transition Simulator

**Files:**
- Modify: `native/controller_native/left_stick_motion_defect_benchmark.cpp`

- [ ] **Step 1: Add a failing closed-loop desired-behavior gate**

Implement the fixture timeline and initially require bounded event response:

```cpp
desired_gate_pass =
    reversal_response_latency_ms <= 20.0 &&
    release_response_latency_ms <= 20.0 &&
    ai_opposes_manual_frames == 0 &&
    output_spike_frames == 0;
```

Run the current controller and verify at least one scenario fails for a measured
transition/output reason, not a fixture error.

- [ ] **Step 2: Implement deterministic physics and delayed vision**

At every 10 ms controller tick:

```cpp
player_velocity += response_alpha * (desired_player_velocity - player_velocity);
target_error_px +=
    (target_velocity - player_velocity) * body_height_px * dt;
target_error_px -= final_right_x * reticle_speed_px_s * dt;
```

Capture vision every 20 ms, deliver it 30 ms later with the original capture
timestamp, and feed the real controller output back into camera motion.

- [ ] **Step 3: Add all five closed-loop fixtures**

Run slow/fast stationary target, fast same/opposite target motion, and fast
stationary target with bounded manual right correction. Record all metrics and
phase-boundary traces from the design spec.

- [ ] **Step 4: Preserve current failures as evidence**

Normal execution must assert determinism, finite values, valid timestamps,
left-axis passthrough, and populated metrics. Desired thresholds remain report
fields and the `--require-fixed` gate.

### Task 4: JSON Report, Documentation, and Regression Isolation

**Files:**
- Modify: `native/controller_native/cod_native_left_stick_motion_benchmark.cpp`
- Modify: `docs/project/NATIVE_CONTROLLER_BENCHMARKS.md`

- [ ] **Step 1: Emit the complete JSON artifact**

Write schema version, timing, intent-invariance metrics, all scenario metrics,
phase-event traces, defect count, and overall desired-gate result. Reject an
unopenable output path with a clear non-zero exit.

- [ ] **Step 2: Build and run the report benchmark**

Run:

```powershell
native\vision_native\build-modern\Release\cod_native_left_stick_motion_benchmark.exe `
  --output artifacts\benchmarks\native_gamepad\left-stick-motion-defect-20260715.json
```

Then run the same executable with `--require-fixed` and verify its intentional
non-zero RED result.

- [ ] **Step 3: Run regression isolation checks**

Run:

```powershell
native\vision_native\build-modern\Release\cod_native_controller_tests.exe
native\vision_native\build-modern\Release\cod_native_benchmark_metrics_tests.exe
native\vision_native\build-modern\Release\cod_native_gamepad_benchmark.exe --self-test
git diff --check
```

Expected: all existing checks pass; only `--require-fixed` fails intentionally.

- [ ] **Step 4: Review scope and findings**

Confirm that git diff contains benchmark sources, CMake target, spec/plan, and
benchmark documentation only. Summarize which scenarios reproduced the defect
and which metrics should become optimization acceptance gates.
