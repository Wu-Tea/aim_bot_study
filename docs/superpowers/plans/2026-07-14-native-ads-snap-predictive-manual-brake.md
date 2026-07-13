# Native ADS Snap Predictive Manual Brake Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restore ADS snap overshoot close to the A0 baseline by allowing a short, evidence-gated target-axis brake without weakening global user control or bodylock.

**Architecture:** Pass explicit ADS and observation context into the existing assist dynamics boundary, keep independent per-axis crossing history there, and extend the existing near-target output budget to cover manual-dominated carry. Separately repair selector identity propagation at the VisionEngine/runtime boundary so the live runtime uses the tested authority path.

**Tech Stack:** C++17, CMake/MSVC Release builds, native controller tests, native gamepad benchmark JSON scorecards, PowerShell verification scripts.

---

### Task 1: Lock the ADS crossing defect with focused tests

**Files:**
- Modify: `native/controller_native/controller_behavior_tests.cpp`

- [ ] **Step 1: Add a failing controller test**

Add a test that keeps ADS snap active with a fresh strong target, feeds a large
manual carry, crosses the X error from positive to negative, and asserts that
the final X output becomes materially smaller than the raw manual input.

- [ ] **Step 2: Add non-regression fixtures**

Add separate assertions that a large opposing manual input without a confirmed
crossing still yields AI authority, that only a crossed axis is braked, and
that bodylock retains the existing user-yield result.

- [ ] **Step 3: Run the focused executable and verify RED**

Run:

```powershell
native\vision_native\build\Release\cod_native_controller_tests.exe
```

Expected: the new ADS crossing assertion fails because
`NativeAimAssistDynamics::strong_opposing_manual` returns zero assist.

### Task 2: Add ADS-specific crossing arbitration

**Files:**
- Modify: `native/controller_native/aim_assist_dynamics.h`
- Modify: `native/controller_native/aim_assist_dynamics.cpp`
- Modify: `native/controller_native/native_gamepad_controller.cpp`

- [ ] **Step 1: Extend the dynamics input contract**

Pass `ads_snap_active`, fresh-observation state, vision sequence, selected target
identity, and target error into the dynamics layer. Store independent X/Y
crossing state and a bounded brake deadline.

- [ ] **Step 2: Implement minimum ADS arbitration**

Reset ADS state on authority loss, target change, stale evidence, or ADS exit.
Detect an error sign crossing on a new observation while manual input continues
in the previous target direction. During the bounded window, preserve the ADS
counter-steering request; outside it, retain the current strong-manual yield.

- [ ] **Step 3: Run focused tests and verify GREEN**

Run the controller test executable and require all existing and new tests to
pass.

### Task 3: Extend the predictive near-target output budget

**Files:**
- Modify: `native/controller_native/native_gamepad_controller.cpp`
- Modify: `native/controller_native/controller_behavior_tests.cpp`

- [ ] **Step 1: Add a failing manual-dominated carry test**

Create a fresh strong ADS snap near the target where manual input is larger than
the planned AI correction. Assert that total output is smoothly capped below
manual magnitude while preserving its direction.

- [ ] **Step 2: Allow the near-target budget to cap total carry**

Use remaining error, configured reticle speed, and the existing short horizon
to compute the maximum total target-axis output. Do not require a nonzero raw AI
assist before applying this cap. Preserve orthogonal output and never run this
path outside fresh ADS snap.

- [ ] **Step 3: Run controller tests and inspect stage components**

Require the final target-axis output to respect the bound without recoil or
bodylock component changes.

### Task 4: Repair live selector identity propagation

**Files:**
- Modify: `native/vision_native/src/vision_engine.cpp`
- Modify: `native/controller_native/controller_protocol_tests.cpp`

- [ ] **Step 1: Extend the protocol regression test**

Construct an updated `VisionResult` with selector identity enabled and a valid
selected detection. Assert that the adapted snapshot keeps the protocol and
derives the expected nonzero selected observation ID.

- [ ] **Step 2: Verify the test exposes the boundary expectation**

Run `cod_native_controller_protocol_tests.exe`. The adapter fixture should pass;
then use the pipeline contract and source check to demonstrate that VisionEngine
does not yet populate those fields.

- [ ] **Step 3: Copy selector identity fields in VisionEngine**

Copy the protocol flag, selected-detection flag, and detection index alongside
the other selector result fields before moving detections.

- [ ] **Step 4: Build and run selector, protocol, and target snapshot tests**

Require all three executables to pass.

### Task 5: Tune against focused ADS benchmarks

**Files:**
- Modify only if evidence requires: `native/controller_native/aim_assist_dynamics.cpp`
- Modify only if evidence requires: `native/controller_native/native_gamepad_controller.cpp`

- [ ] **Step 1: Run focused ADS scenarios**

Run the ADS manual-stress, dynamic-fire, parity, late-vision, err-target, carry,
and adversarial suites into a new JSON artifact.

- [ ] **Step 2: Compare with current and A0 artifacts**

Compare error, maximum overshoot, user-fight, wrong-target, no-fresh high output,
manual preservation, turn smoothness, and final error. Change one constant or
one state rule at a time only when a measured gate fails.

- [ ] **Step 3: Repeat focused tests after each adjustment**

Keep the smallest version that reaches the A0 neighborhood without damaging
user escape or parity.

### Task 6: Full acceptance and integration

**Files:**
- Update if acceptance changes: `docs/project/NATIVE_CONTROLLER_BENCHMARKS.md`

- [ ] **Step 1: Build all native Release targets**

Build the full configured CMake project and require exit code zero.

- [ ] **Step 2: Run every native test executable**

Run all test executables in `native/vision_native/build/Release`, including
controller, selector, tracker, telemetry, benchmark metrics, AimLab, recoil,
and vision tests. Record failures exactly and fix before continuing.

- [ ] **Step 3: Run the native pipeline contract**

Run `scripts\verify\native_pipeline_contract.bat` and require PASS.

- [ ] **Step 4: Run the complete benchmark suite**

Run `cod_native_gamepad_benchmark.exe --suite all` with the fixed random seed and
write a final artifact under `runs/native_perf`.

- [ ] **Step 5: Verify the scorecard against A0 and pre-change current**

Require ADS dynamic metrics near A0, no material regression in bodylock,
adversarial, parity, late-vision, recoil, or vision metrics, and a clean
`git diff --check`.

- [ ] **Step 6: Commit and merge to dev**

Commit the focused implementation, merge the feature branch into local `dev`,
repeat the critical build/tests/benchmark checks on the merged tree, and leave
`dev` clean and rollbackable.

