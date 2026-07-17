# Left-Strafe Prediction and Manual Retention Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Improve deterministic tracking and wrong-axis recovery metrics while preserving normal aim feel, zero overshoot, smooth output, and explicit user escape.

**Architecture:** Extend the existing per-axis arbiter with a bounded manual-retention output and add an X-only, response-estimator-backed inter-frame intent projection to `TargetPlan`. Keep one ADS/BodyLock/dynamics path and one final manual-plus-AI mix; reject any candidate that improves synthetic error cases by changing normal combat output or creating pulses.

**Tech Stack:** C++20, CMake/MSBuild Release targets, native controller pipeline, deterministic JSON benchmarks, TOML runtime configuration.

---

## File Map

- `native/controller_native/runtime_config.h/.cpp`: canonical `[gamepad.intent]` configuration.
- `native/controller_native/runtime_config_tests.cpp`: parser/default/clamp contract.
- `native/controller_native/axis_intent_arbiter.h/.cpp`: per-axis retention decision and bounded attack/release.
- `native/controller_native/axis_intent_arbiter_tests.cpp`: wrong axis, unaffected axis, escape, ambiguity, and inter-frame hold.
- `native/pipeline_contract/target_plan.h`: X-only intent projection field.
- `native/controller_native/target_coordinator.h/.cpp`: response-estimator-backed unobserved left-input projection.
- `native/controller_native/target_coordinator_tests.cpp`: fresh-frame reset, direction, confidence, reversal, and long-gap behavior.
- `native/controller_native/native_gamepad_controller.cpp`: pass config, consume projected error, and scale physical axes before the existing mix.
- `native/controller_native/output_mixer.h`: retention/projection diagnostics.
- `native/controller_native/target_pipeline_integration_tests.cpp`: real pipeline axis isolation and smooth recovery.
- `native/controller_native/partial_occlusion_benchmark.h/.cpp`: A/B metadata and retention/projection metrics.
- `native/controller_native/cod_native_partial_occlusion_benchmark.cpp`: paired baseline/candidate scenarios.
- `native/controller_native/partial_occlusion_benchmark_tests.cpp`: scenario and JSON contract.
- `docs/project/NATIVE_CONTROLLER_BENCHMARKS.md`: accepted/rejected evidence.

### Task 1: Freeze strict acceptance baselines

- [ ] **Step 1: Verify the committed intervention-only baseline**

Run:

```powershell
& D:\codex-build\pob\Release\cod_native_partial_occlusion_benchmark.exe `
  --config D:\work\AI\yolo-study-001\config.toml `
  --output D:\work\AI\yolo-study-001\runs\benchmarks\dual_axis_prechange_seed1337.json `
  --seed 1337
```

Expected normal aggregate `71.293475`, human-error aggregate `69.253609`, zero
X/Y overshoot, and normal intervention `0/0`.

- [ ] **Step 2: Record rejection thresholds**

Reject a candidate if normal mean error changes by more than `0.10 px`, normal
overall drops by more than `0.10`, any new overshoot appears, P95 output delta
increases by more than `0.005`, strong escape is attenuated, or the unaffected
axis differs beyond `1e-6` in focused tests.

### Task 2: Add the canonical retention-floor config

- [ ] **Step 1: Write failing parser tests**

Add tests that parse:

```toml
[gamepad.intent]
wrong_way_manual_preservation_floor = 0.65
```

Assert default `0.65`, explicit `0.72`, low clamp `0.50`, high clamp `1.00`, and
unknown-key diagnostics within the new section.

- [ ] **Step 2: Run RED**

Build and run `cod_native_runtime_config_tests`; expect compilation or assertion
failure because `GamepadIntentConfig` and the section do not exist.

- [ ] **Step 3: Implement the minimal config contract**

Add:

```cpp
struct GamepadIntentConfig {
    float wrong_way_manual_preservation_floor = 0.65f;
};
```

Parse only the canonical key under `[gamepad.intent]` and clamp to `[0.50, 1.00]`.
Do not add aliases.

- [ ] **Step 4: Run GREEN and commit**

Run runtime-config tests and commit `feat: configure per-axis manual retention`.

### Task 3: Extend the pure axis arbiter

- [ ] **Step 1: Write failing behavior tests**

Add tests requiring:

```cpp
require_near(normal.manual_retention, 1.0f, 1e-6f);
require_near(first_wrong.manual_retention, 0.85f, 1e-3f);
require_true(sustained_wrong.manual_retention >= 0.65f);
require_true(sustained_wrong.manual_retention < first_wrong.manual_retention);
require_near(escape.manual_retention, 1.0f, 1e-6f);
require_near(other_axis.manual_retention, 1.0f, 1e-6f);
```

Also require smooth release toward `1.0`, target-change reset, geometry reset,
and expiry after the 12ms bridge.

- [ ] **Step 2: Run RED**

Build/run `cod_native_axis_intent_arbiter_tests`; expect missing
`manual_retention` and floor input/configuration.

- [ ] **Step 3: Implement minimal retention state**

Add one `manual_retention` float to each existing `AxisState`. On confirmed
wrong-way evidence attack from `1.0` with the first decision capped at `0.85`,
then approach the configured floor. On release approach `1.0`; on escape or
hard reset set `1.0` immediately. Reuse the existing confirmation and 12ms hold;
do not add another mode enum or brake.

- [ ] **Step 4: Run GREEN and commit**

Run arbiter tests and commit `feat: retain confirmed wrong-way manual axes`.

### Task 4: Add left-strafe inter-frame projection to TargetPlan

- [ ] **Step 1: Write failing coordinator tests**

Use real `TargetCoordinator` and `observe_control_response` samples. Require:

- fresh Observed tick projection equals zero;
- after left X changes between observations, projection has the learned sign;
- same input already present at the last observation is not double-counted;
- reversal flips the projection without a jump beyond the bounded horizon;
- confidence below the threshold yields zero;
- Reacquiring/None and age beyond the horizon yield zero;
- Y projection is always zero.

Expected API:

```cpp
plan.intent_projection_px.x
plan.intent_projection_px.y
```

- [ ] **Step 2: Run RED**

Build/run `cod_native_target_coordinator_tests`; expect failure because
`TargetPlan.intent_projection_px` does not exist.

- [ ] **Step 3: Implement projection without double-counting**

Store left X seen on the last Observed sample. Compute only the unobserved delta:

```cpp
delta_left = intent.filtered_left.x - last_observed_left_x_;
horizon = min(observation_age_seconds, 0.012f);
projection_x = response.scale_px_per_stick_second * delta_left * horizon *
               response.confidence;
```

Clamp projection to a small pixel envelope derived from the 12ms horizon. Reset
on target/ADS epoch reset and publish zero outside the safety conditions.

- [ ] **Step 4: Run GREEN and commit**

Run coordinator and pipeline-contract tests; commit
`feat: project unobserved left strafe motion`.

### Task 5: Integrate one runtime mix

- [ ] **Step 1: Write failing integration tests**

Add a paired-controller test. With identical target and AI trace, confirmed
wrong X must scale only physical `right_x`; `right_y`, AutoFire flags, and recoil
components must remain identical. Add a left-onset test requiring earlier X
response and bounded per-tick delta.

- [ ] **Step 2: Run RED**

Run `cod_native_controller_tests`; expect unchanged physical mix and missing
projection/retention diagnostics.

- [ ] **Step 3: Implement the single mix point**

Use projected X error for ADS/BodyLock demand while leaving the original plan
immutable for AutoFire settle checks. Replace only:

```cpp
physical.right_x + shaped.x
physical.right_y + shaped.y
```

with:

```cpp
physical.right_x * retention_x + shaped.x
physical.right_y * retention_y + shaped.y
```

Record retention and projection diagnostics. Recoil remains downstream.

- [ ] **Step 4: Run GREEN and commit**

Run ADS, BodyLock, dynamics, arbiter, controller, AutoFire, and recoil-focused
tests; commit `feat: integrate intent-aware axis mixing`.

### Task 6: Build strict paired benchmarks

- [ ] **Step 1: Write failing benchmark-contract tests**

Require JSON fields for retention exposure, minimum retention, projection-active
frames, projection peak, and paired baseline/candidate case names. Require
wrong-X/Y to stay below escape while stale/crossing remain above it.

- [ ] **Step 2: Run RED**

Run `cod_native_partial_occlusion_benchmark_tests`; expect missing metrics and
paired cases.

- [ ] **Step 3: Implement identical-input A/B cases**

Baseline uses retention floor `1.0` and response confidence `0`; candidate uses
configured floor and clean response hints. Feed identical target motion, vision
frames, left/right inputs, and seed to both. Add same-direction, opposite-direction,
left reversal, low-confidence, and long-gap cases.

- [ ] **Step 4: Run GREEN and benchmark seed 1337 twice**

Require identical SHA-256 hashes between repeated candidate outputs.

### Task 7: Optimize against metrics, not feature count

- [ ] **Step 1: Compare raw metrics**

Inspect mean/P95 error, acquisition/recovery latency, overshoot X/Y, P95 output
delta, spikes, mode changes, wrong-axis retention, and unaffected-axis trace.

- [ ] **Step 2: Tune only existing constants if necessary**

Adjust retention attack/release or projection horizon only when a failing case
identifies the cause. Re-run all paired cases after every change. Do not add a
new gate, state machine, or public config key to rescue one fixture.

- [ ] **Step 3: Accept or reject**

Accept only if wrong-X/Y and both left-strafe directions improve while every
normal/safety threshold from Task 1 holds. Otherwise revert the ineffective
production portion and retain only trustworthy benchmark fixes.

### Task 8: Full verification and handoff

- [ ] **Step 1: Build Release runtime and all affected tests**

Build runtime-config, coordinator, ADS, BodyLock, dynamics, arbiter, controller,
partial-occlusion benchmark/tests, AutoFire, recoil, and `cod_native_runtime`.

- [ ] **Step 2: Run tests and final benchmark**

Run every focused executable plus final seed `1337` twice. Confirm hashes match.

- [ ] **Step 3: Check repository state**

Run `git diff --check`, update benchmark docs and `.agent-context/handoff.md`,
commit intentionally, and require both the feature worktree and main `dev` to
remain clean.
