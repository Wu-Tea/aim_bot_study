# Production-Chain Strafe Dropout Benchmark Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend the deterministic left-stick benchmark with a selector/tracker/lifecycle scenario that reproduces the recorded drift-only output gap during player strafe without invoking vision or weapon data.

**Architecture:** Add a focused production-chain result beside the existing intent and closed-loop results. The fixture generates selector-owned `ControllerVisionSnapshot` frames with multiple candidates, drives the real FPS tracker and `NativeGamepadController`, keeps right-stick noise inside a `0.02` drift tolerance, withholds the selected observation for an evidence-matched gap, then reacquires under a new identity and scores requested versus final assist.

**Tech Stack:** C++17, existing `ControllerVisionSnapshot`, FPS reference tracker, native controller/lifecycle/dynamics, CMake/CTest, deterministic JSON.

---

### Task 1: Add a RED contract test for the production-chain result

**Files:**
- Create: `native/controller_native/left_stick_motion_defect_benchmark_tests.cpp`
- Modify: `native/vision_native/CMakeLists.txt`
- Modify: `native/controller_native/left_stick_motion_defect_benchmark.h`

- [ ] **Step 1: Write the failing test against the desired report contract**

Add a test executable whose first test calls the real benchmark and requires a populated production-chain result:

```cpp
const auto report = controller_native::left_stick_defect::run_benchmark();
const auto& chain = report.production_chain;
require_true(chain.behavior_populated, "production-chain behavior must run");
require_true(chain.drift_manual_correction_frames == 0,
             "deadzone-sized right-stick noise must not count as correction");
require_true(chain.detector_candidate_gap_frames > 0,
             "candidate-present selected-target gap must be exercised");
require_true(chain.production_target_missing_frames > 0,
             "production target must become unavailable during the gap");
require_true(chain.max_continuous_drift_only_ms >= 650.0,
             "evidence-matched drift-only output gap must be reproduced");
require_true(chain.reacquire_latency_ms >= 0.0,
             "reacquisition latency must be populated");
require_true(chain.body_lock_frames > 0 && chain.ads_snap_frames > 0 &&
                 chain.manual_frames > 0,
             "body-lock, ADS-snap, and manual phases must all occur");
```

The header must only declare the intended data contract at this point:

```cpp
struct ProductionChainEvent {
    std::string phase;
    int tick = 0;
    bool detector_candidates_present = false;
    bool production_target_present = false;
    std::uint64_t selected_track_id = 0;
    float manual_right_x = 0.0f;
    float requested_ai_x = 0.0f;
    float final_right_x = 0.0f;
    double target_error_px = 0.0;
    std::string aim_mode;
    std::string lifecycle;
    std::string limit_reason;
};

struct ProductionChainMetrics {
    std::string name = "production_chain_strafe_reacquire";
    int total_frames = 0;
    int body_lock_frames = 0;
    int ads_snap_frames = 0;
    int manual_frames = 0;
    int mode_transitions = 0;
    int detector_candidate_gap_frames = 0;
    int production_target_missing_frames = 0;
    int target_present_bodylock_unavailable_frames = 0;
    int drift_manual_correction_frames = 0;
    int drift_only_final_frames = 0;
    int requested_suppressed_frames = 0;
    int selected_track_changes = 0;
    double max_abs_manual_right = 0.0;
    double max_continuous_drift_only_ms = 0.0;
    double reacquire_latency_ms = -1.0;
    double pre_loss_error_px = 0.0;
    double post_reacquire_error_px = 0.0;
    bool behavior_populated = false;
    bool defect_reproduced = false;
    bool desired_gate_pass = true;
    std::vector<std::string> defect_reasons;
    std::vector<ProductionChainEvent> events;
};
```

Add `ProductionChainMetrics production_chain;` to `BenchmarkReport`.

- [ ] **Step 2: Add the test target and verify RED**

Create `cod_native_lstick_benchmark_tests` from the test file, benchmark module,
and the same controller/tracker sources used by `cod_native_lstick_benchmark`.
Register it as `NativeLeftStickMotionBenchmarkTests`.

Run:

```powershell
cmake --build --preset modern-release --target cod_native_lstick_benchmark_tests -- /m
native\vision_native\build-modern\Release\cod_native_lstick_benchmark_tests.exe
```

Expected: the executable fails with `production-chain behavior must run`
because the declared result has no implementation.

### Task 2: Drive the real selector/tracker/controller chain

**Files:**
- Modify: `native/controller_native/left_stick_motion_defect_benchmark.cpp`

- [ ] **Step 1: Add deterministic snapshot factories**

Create helpers that assign the same observation id to the selector candidate and
tracker detection:

```cpp
ControllerVisionSnapshot selector_snapshot(
    std::uint64_t frame_id,
    std::uint64_t selected_observation_id,
    double capture_time_seconds,
    double error_x_px,
    bool include_selected_target,
    bool bodylock_geometry_available);
```

Each updated frame must set `selector_identity_protocol=true`, populate two
valid candidates and two tracker detections, and use `selected_observation_id=0`
only during the selected-target gap. The non-selected candidate remains far
enough away that it cannot silently replace the selector-owned target.

- [ ] **Step 2: Implement the evidence-matched fixture timeline**

Use 10 ms controller ticks and 20 ms snapshot updates:

```cpp
constexpr int kChainStableStartTick = 80;
constexpr int kChainUnavailableStartTick = 200;
constexpr int kChainSelectedGapStartTick = 220;
constexpr int kChainReacquireTick = 290;
constexpr int kChainRecoveryTick = 330;
constexpr int kChainTicks = 430;
```

The phases are:

```text
0-79    acquire/settle
80-199  selected target + BodyLock + left strafe
200-219 selected target remains current but narrow/off-center body geometry
220-289 two detector candidates remain, selected observation id is zero
290-329 selected target reacquires under a new observation/track identity
330-429 recovered BodyLock while left strafe continues
```

Use `left_x=0.80`, deterministic `right_x=-0.00393677`, ADS held, recoil
disabled, and normal aim-assist dynamics enabled. Submit frames through
`NativeGamepadController::submit_vision_snapshot`, then call `build_output` and
read `last_output_components()` plus `last_frame_vision_state()`.

- [ ] **Step 3: Populate production-chain metrics from delivered behavior**

Classify right-stick input as intentional only when `abs(manual_right_x)>0.02`.
Classify a drift-only final frame when all of the following hold:

```cpp
const bool drift_only =
    std::fabs(manual_right_x) <= 0.02f &&
    std::fabs(final_right_x - manual_right_x) <= 0.005f;
```

Count requested suppression when `abs(requested_ai_x)>=0.05` but the delivered
assist above manual is at most `0.005`. Track the longest continuous drift-only
run while ADS, left strafe, and detector candidates remain active. Record events
at every phase boundary and whenever aim mode, selected track id, lifecycle, or
limit reason changes.

- [ ] **Step 4: Run the focused test and reach GREEN**

Run:

```powershell
cmake --build --preset modern-release --target cod_native_lstick_benchmark_tests -- /m
native\vision_native\build-modern\Release\cod_native_lstick_benchmark_tests.exe
```

Expected: the production-chain contract test passes and reports all five phases.

### Task 3: Add harness validation and desired RED classification

**Files:**
- Modify: `native/controller_native/left_stick_motion_defect_benchmark.cpp`
- Modify: `native/controller_native/left_stick_motion_defect_benchmark_tests.cpp`

- [ ] **Step 1: Write failing validation tests**

Copy a valid report, clear one required property at a time, and require
`validate_report` to reject it:

```cpp
auto missing_gap = report;
missing_gap.production_chain.detector_candidate_gap_frames = 0;
require_invalid(missing_gap, "candidate-present gap");

auto misclassified_drift = report;
misclassified_drift.production_chain.drift_manual_correction_frames = 1;
require_invalid(misclassified_drift, "drift classification");

auto missing_phase = report;
missing_phase.production_chain.ads_snap_frames = 0;
require_invalid(missing_phase, "production-chain mode coverage");
```

Run the focused test and verify it fails because `validate_report` does not yet
check the new result.

- [ ] **Step 2: Implement minimal report validation**

Require finite timing/error values, populated events, all three aim modes,
candidate-present gap frames, production-target-missing frames, zero drift
manual corrections, and coherent report summary fields.

- [ ] **Step 3: Classify the evidence as an intentional desired-behavior RED**

Set `defect_reproduced` when either condition is present:

```cpp
chain.max_continuous_drift_only_ms >= 100.0 ||
chain.requested_suppressed_frames > 0
```

Add `production_chain_drift_only_gap` and
`requested_assist_suppressed_before_final` as applicable defect reasons. Fold
the chain result into `defect_count` and the report-level `desired_gate_pass`.

- [ ] **Step 4: Re-run focused tests and verify GREEN**

Run the focused executable and CTest entry. Expected: both pass; the normal
harness is green while the desired behavior remains recorded as RED.

### Task 4: Emit evidence JSON and update benchmark documentation

**Files:**
- Modify: `native/controller_native/left_stick_motion_defect_benchmark.cpp`
- Modify: `docs/project/NATIVE_CONTROLLER_BENCHMARKS.md`

- [ ] **Step 1: Write a failing JSON contract test**

Serialize the report and require these keys:

```cpp
require_contains(json, "\"production_chain\"");
require_contains(json, "\"max_continuous_drift_only_ms\"");
require_contains(json, "\"requested_suppressed_frames\"");
require_contains(json, "\"selected_track_changes\"");
require_contains(json, "\"detector_candidates_present\"");
require_contains(json, "\"limit_reason\"");
```

Run and verify failure because the JSON writer has no production-chain object.

- [ ] **Step 2: Serialize the production-chain result and events**

Write the result as a top-level `production_chain` object before `scenarios`.
Preserve schema version 1 because the change is additive and the artifact is not
consumed as a stable external API.

- [ ] **Step 3: Document interpretation and commands**

Document that `0.0118` is treated as drift, that the new probe exercises
candidate-present selector loss and final-output suppression, and that it does
not replay pixels or identify a weapon.

- [ ] **Step 4: Generate two artifacts and prove determinism**

Run the benchmark twice to separate JSON files and compare SHA-256 hashes. Then
replace the canonical artifact with one verified copy.

### Task 5: Full regression and scope verification

**Files:**
- Verify only; no new production files.

- [ ] **Step 1: Run focused and existing regression suites**

```powershell
native\vision_native\build-modern\Release\cod_native_lstick_benchmark_tests.exe
native\vision_native\build-modern\Release\cod_native_left_stick_motion_benchmark.exe
native\vision_native\build-modern\Release\cod_native_controller_tests.exe
native\vision_native\build-modern\Release\cod_native_benchmark_metrics_tests.exe
native\vision_native\build-modern\Release\cod_native_gamepad_benchmark.exe --self-test
```

Expected: all normal commands exit zero.

- [ ] **Step 2: Verify the intentional fixed gate remains RED**

```powershell
native\vision_native\build-modern\Release\cod_native_left_stick_motion_benchmark.exe --require-fixed
```

Expected: exit code 2 with `RED desired behavior gate failed`.

- [ ] **Step 3: Verify CTest, JSON determinism, and diff scope**

Run `ctest -R NativeLeftStickMotionBenchmarkTests`, `git diff --check`, and
`git status --short`. Confirm that only benchmark source, benchmark test, CMake,
benchmark docs/plan, and the ignored benchmark artifact changed; no production
controller, tracker, runtime, or vision source changed.

