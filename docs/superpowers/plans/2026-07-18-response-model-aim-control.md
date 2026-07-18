# Response-Model Aim Control Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace fixed-range ADS and underpowered BodyLock control with one response-normalized vector solver, correct the ADS/BodyLock handoff, and prove ordinary plus small-target gains with isolated deterministic benchmarks.

**Architecture:** Keep `TargetCoordinator` as the sole semantic mode owner and `AimDynamicsShaper` as the sole delivery owner. Add a small in-memory right-stick response estimator and a stateless vector solver shared by ADS and BodyLock wrappers. Split ADS acquisition scoring from a warm-start BodyLock cohort so controller changes can be attributed correctly.

**Tech Stack:** C++17, CMake/MSBuild, existing native controller test executables, deterministic JSON benchmark artifacts, PowerShell verification.

---

## File map

- `sustained_aimlab_*`: scenario generation, ADS cohort, isolated BodyLock cohort, scoring, and JSON.
- `aim_response_estimator.*`: robust in-memory right-stick response identification only.
- `response_model_aim_solver.*`: unit-correct vector request; no state, modes, or output smoothing.
- `ads_acquisition_controller.*` and `bodylock_follow_controller.*`: thin policy wrappers selecting horizon, motion weight, and force envelope.
- `target_coordinator.*`: semantic capture-set and symmetric transition hysteresis.
- `native_gamepad_controller.*`: command-interval accumulation, estimator wiring, and one final mix/shaper path.
- `pipeline_contract/target_plan.h`: explicit left-motion and right-aim response fields; no overloaded `response_scale` meaning.

### Task 1: Correct benchmark phase ownership

**Files:**
- Modify: `native/controller_native/sustained_aimlab_types.h`
- Modify: `native/controller_native/sustained_aimlab_score.h`
- Modify: `native/controller_native/sustained_aimlab_score.cpp`
- Modify: `native/controller_native/sustained_aimlab_simulator.h`
- Modify: `native/controller_native/sustained_aimlab_simulator.cpp`
- Modify: `native/controller_native/sustained_aimlab_score_tests.cpp`
- Modify: `native/controller_native/sustained_aimlab_simulator_tests.cpp`

- [ ] **Step 1: Write failing scorer tests for BodyLock occupancy**

Add tests that feed 1000 tracking frames without `bodylock_mode` and require
`bodylock_entry_failed=true`, then feed a confirmed BodyLock entry followed by ADS
fallback and require exact active/fallback milliseconds.

```cpp
require_true(result.bodylock_entry_failed,
             "never entering BodyLock must not score as zero interruptions");
require_equal(result.bodylock_active_ms, 800);
require_equal(result.unexpected_mode_ms, 200);
```

- [ ] **Step 2: Run RED**

Run `cmake --build b --config Release --target cod_native_sustained_aimlab_score_tests cod_native_sustained_aimlab_simulator_tests` and both executables. Expected: compile failure because the occupancy fields and BodyLock cohort do not exist.

- [ ] **Step 3: Add explicit cohort and occupancy fields**

Add `BenchmarkCohort::{AdsAcquire,BodyLockFollow}` and these result fields:

```cpp
bool bodylock_entry_failed = false;
int bodylock_entry_ms = -1;
int bodylock_active_ms = 0;
int unexpected_mode_ms = 0;
int wrong_direction_output_ms = 0;
int longest_outside_ms = 0;
double mean_projected_lag_px = 0.0;
double p95_projected_lag_px = 0.0;
```

ADS scoring ends at first circle entry or deadline. BodyLock scoring begins only
after confirmed `bodylock_mode`; entry timeout becomes a failed BodyLock result.

- [ ] **Step 4: Run GREEN and regressions**

Run the two new test executables. Expected: PASS. Then run
`cod_native_aimlab_benchmark_tests`; expected PASS.

### Task 2: Add small-target and plant-sweep cohorts

**Files:**
- Modify: `native/controller_native/sustained_aimlab_types.h`
- Modify: `native/controller_native/sustained_aimlab_scenario.cpp`
- Modify: `native/controller_native/sustained_aimlab_scenario_tests.cpp`
- Modify: `native/controller_native/cod_native_sustained_aimlab_benchmark.cpp`
- Modify: `scripts/verify/run_sustained_aimlab_baseline.ps1`
- Create: `scripts/verify/compare_sustained_aimlab.ps1`

- [ ] **Step 1: Write failing deterministic scenario tests**

Require identical target motion across ordinary radius 24 and small radii 8, 11,
14, plus response scales 300, 500, 750 and slowdown pairs `(0.35,0.25)`,
`(0.50,0.40)`, `(0.65,0.55)`.

```cpp
require_equal(small.targets[i].initial_error_px, ordinary.targets[i].initial_error_px);
require_true(small.targets[i].visible_radius_px >= 8.0);
require_true(small.targets[i].visible_radius_px <= 14.0);
```

- [ ] **Step 2: Run RED**

Run `cod_native_sustained_aimlab_scenario_tests`. Expected: compile failure for
`visible_radius_px` and plant sweep options.

- [ ] **Step 3: Implement scenario variants without changing motion RNG**

Give `TargetScript` a radius after all motion RNG draws, include it in script hash,
and add repeated CLI flags `--target-profile ordinary|small` and
`--camera-response N`. Keep slowdown values in report metadata.

- [ ] **Step 4: Add comparison script**

The script reads baseline and candidate JSON, joins on
`seed/profile/cohort/target_profile/plant`, and emits acquired %, normalized tracking
%, P95 lag %, over, false stop, mode exit, and jerk deltas. It exits non-zero when
any design acceptance gate fails.

- [ ] **Step 5: Run GREEN and record pre-control BodyLock baseline**

Run scenario tests and a 2-second JSON smoke for every cohort. Then write a new
immutable pre-control artifact under
`artifacts/benchmarks/sustained_aimlab/bodylock-pre-response-model-20260718.json`.

### Task 3: Add right-stick response identification

**Files:**
- Create: `native/controller_native/aim_response_estimator.h`
- Create: `native/controller_native/aim_response_estimator.cpp`
- Create: `native/controller_native/aim_response_estimator_tests.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Write failing estimator tests**

Define interval samples using average delivered stick and observed error rate. Tests
must prove fallback=500, convergence to 300 and 750, persistence across target IDs,
adaptation from 500 to 250 slowdown response, and rejection of target swaps,
coasting, manual ambiguity, high acceleration, tiny command delta, invalid dt, and
sign-inconsistent estimates.

```cpp
AimResponseInterval interval{
    .average_final_stick = {0.40f, 0.0f},
    .observed_error_rate = {-120.0f, 0.0f},
    .target_id = 7,
    .dt_seconds = 0.010f,
    .reliability = 0.95f,
    .observed = true,
};
require_true(estimator.update(interval));
```

- [ ] **Step 2: Run RED**

Build the new target. Expected: compile failure because estimator files are absent.

- [ ] **Step 3: Implement bounded delta regression**

For consecutive eligible intervals, cancel smooth target velocity using:

```cpp
delta_rate = current.observed_error_rate - previous.observed_error_rate;
delta_stick = current.average_final_stick - previous.average_final_stick;
sample_scale = -dot(control_coordinates(delta_rate), delta_stick) /
               max(dot(delta_stick, delta_stick), epsilon);
```

Reject samples outside 80–1200 px/stick/second and update a clipped EWMA. Blend the
estimate with the 500 fallback by confidence. `begin_target()` clears interval
history but preserves learned scale and confidence; `reset()` clears both.

- [ ] **Step 4: Run GREEN**

Run estimator tests. Expected: PASS with deterministic final scale tolerances.

### Task 4: Add the shared response-model vector solver

**Files:**
- Create: `native/controller_native/response_model_aim_solver.h`
- Create: `native/controller_native/response_model_aim_solver.cpp`
- Create: `native/controller_native/response_model_aim_solver_tests.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Write failing unit tests**

Cover radial direction invariance, Y sign, shorter horizon producing more demand,
correct velocity feed-forward units, predictive lead opposite residual error,
response 250 requiring twice response 500, and elliptical force clamp.

```cpp
const auto out = solve({.error_px={3,4}, .relative_velocity_px_per_sec={0,0},
                        .response_px_per_stick_second=500,
                        .arrival_horizon_seconds=0.05f,
                        .max_force={0.5f,0.4f}});
require_near(out.unclamped_stick.x / -out.unclamped_stick.y, 3.0f/4.0f, 1e-3f);
```

- [ ] **Step 2: Run RED**

Build and run the solver tests. Expected: compile failure because `solve` is absent.

- [ ] **Step 3: Implement the stateless solver**

Convert error and velocity once into stick coordinates, compute position and motion
terms separately, sum them, then apply one elliptical vector clamp. Do not multiply
the motion term by max force and do not clamp it to current error sign.

- [ ] **Step 4: Run GREEN**

Run solver tests. Expected: PASS.

### Task 5: Wire real ADS completion settings and capture-set transition

**Files:**
- Modify: `native/pipeline_contract/target_plan.h`
- Modify: `native/controller_native/target_coordinator.h`
- Modify: `native/controller_native/target_coordinator.cpp`
- Modify: `native/controller_native/target_coordinator_tests.cpp`
- Modify: `native/controller_native/native_gamepad_controller.cpp`
- Modify: `native/controller_native/controller_behavior_tests.cpp`

- [ ] **Step 1: Write failing config mapping and transition tests**

Require completion radius 8, stable evidence `frames*10ms`, max acquisition 220ms,
rejection of a high-closing-speed radius crossing, acceptance of a stable predicted
capture, 80/100Hz equivalence, symmetric exit hysteresis, and coasting authority
decay without bypass.

- [ ] **Step 2: Run RED**

Run `cod_native_controller_tests`. Expected failures must show that BodyLock
tolerance currently controls ADS completion and high-speed crossings hand off.

- [ ] **Step 3: Extend plan/control feedback explicitly**

Add to `TargetPlan`:

```cpp
float left_motion_response_scale = 0.0f;
float left_motion_response_confidence = 0.0f;
float aim_response_scale = 500.0f;
float aim_response_confidence = 0.0f;
float acquisition_elapsed_ms = 0.0f;
float predicted_terminal_error_px = 0.0f;
float radial_closing_velocity_px_per_sec = 0.0f;
```

Pass previous delivered stick and aim response to coordinator through a focused
`TargetControlFeedback`. Use ADS completion config, predicted BodyLock-horizon
error, and delivered-command closure in the capture-set test. Use a wider predicted
exit radius for reverse hysteresis.

- [ ] **Step 4: Run GREEN**

Run coordinator and controller tests. Expected: PASS, including existing bumpless
handoff and autofire readiness tests.

### Task 6: Replace ADS fixed range with shared solver

**Files:**
- Modify: `native/controller_native/ads_acquisition_controller.h`
- Modify: `native/controller_native/ads_acquisition_controller.cpp`
- Modify: `native/controller_native/controller_behavior_tests.cpp`
- Modify: `native/controller_native/native_gamepad_controller.cpp`

- [ ] **Step 1: Write failing ADS behavior tests**

Require output to ignore `range_px`, increase when remaining arrival time shrinks,
adapt inversely to right-stick response, reduce smoothly under measured closing
velocity, and retain the existing X/Y force envelope and manual authority rules.

- [ ] **Step 2: Run RED**

Run `cod_native_controller_tests`. Expected: fixed-range implementation fails the
response and time-to-arrival assertions.

- [ ] **Step 3: Make ADS a thin solver wrapper**

Use acquisition elapsed/budget to choose a bounded horizon and call the shared
solver. Keep `range_px` parsed but do not pass it into the solver. Preserve authority
and one `AimDynamicsShaper` call in `NativeGamepadController`.

- [ ] **Step 4: Run unit tests and official ADS matrix**

Run all controller tests and the six official ordinary ADS runs across the plant
sweep. Keep the change only if pure acquired improves >=20%, mixed >=15%, both
acquisition point totals rise, false events do not regress, and P95 jerk <=105%.

### Task 7: Replace BodyLock unit mismatch with shared solver

**Files:**
- Modify: `native/controller_native/bodylock_follow_controller.h`
- Modify: `native/controller_native/bodylock_follow_controller.cpp`
- Modify: `native/controller_native/controller_behavior_tests.cpp`

- [ ] **Step 1: Write failing BodyLock behavior tests**

Require full unit-correct velocity compensation, lead opposite a small residual,
equal feedback gain with and without left-stick magnitude for equal residual, lower
right-stick response producing more command, and bounded output on reversal.

- [ ] **Step 2: Run RED**

Run `cod_native_controller_tests`. Expected failures must reproduce the current
extra `max_force` multiplication, 75px strafe range weakening, and sign clamp.

- [ ] **Step 3: Make BodyLock a thin solver wrapper**

Use a longer horizon and reliability-weighted motion term. Feed left-stick effects
through relative velocity, remove strafe-dependent feedback range and error-sign
clamp, then apply one force ellipse and the existing dynamics shaper.

- [ ] **Step 4: Run unit tests and BodyLock matrix**

Run ordinary and 8/11/14px pure/mixed cohorts across all plant responses and
slowdowns. Require >=15% normalized tracking gain, >=15% P95 lag reduction, no
entry/false-stop/mode-exit regression, reduced wrong-direction ms, and jerk <=105%.

### Task 8: Integrate estimator, telemetry, artifacts, and regressions

**Files:**
- Modify: `native/controller_native/native_gamepad_controller.h`
- Modify: `native/controller_native/native_gamepad_controller.cpp`
- Modify: `native/controller_native/native_gamepad_types.h`
- Modify: `native/controller_native/cod_native_sustained_aimlab_benchmark.cpp`
- Modify: `docs/benchmarks/sustained-aimlab.md`
- Create: `artifacts/benchmarks/sustained_aimlab/response-model-candidate-20260718.json`

- [ ] **Step 1: Write failing integration tests**

At 1000Hz, accumulate delivered stick over each fresh-vision interval, update the
estimator once per interval, preserve learned response across target changes, reject
manual escape intervals, and reset on controller reset. Assert telemetry exposes
estimate, confidence, horizon, radial demand, capture reason, and limit reason.

- [ ] **Step 2: Run RED**

Run `cod_native_controller_tests`. Expected: telemetry and persistence assertions fail.

- [ ] **Step 3: Wire estimator without adding an output stage**

Update interval accumulation after final output, consume it on the next fresh
observation, attach the estimate to the next plan, and keep output ownership as
solver -> one shaper -> mixer -> recoil.

- [ ] **Step 4: Run full verification**

Build and run scenario, score, simulator, estimator, solver, controller, old AimLab,
autofire, recoil, and runtime smoke targets. Run the full deterministic matrix twice
and require identical joined JSON results.

- [ ] **Step 5: Review complexity and commit cohesive slices**

Run `git diff --check`, inspect controller line counts and config keys, and verify no
new distance bands, brake stages, or mode owners were added. Commit benchmark
correction, response-model control, and final artifacts as no more than three
independently reviewable commits, respecting repository commit discipline.
