# Vertical Bodylock Defect Benchmark Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add deterministic native benchmarks that prove or disprove the reported vertical overshoot/occlusion and prone-or-stair air-lock defects without changing production behavior.

**Architecture:** Put the defect scenario simulator and metrics in a focused benchmark module, separate from the existing large gamepad benchmark. Feed real `VisionTargetSelector` results through `NativeGamepadController`, while retaining an oracle visible-body interval used only for scoring. Build a test executable for harness invariants and a report executable for current-behavior evidence.

**Tech Stack:** C++17, existing `vision_native::VisionTargetSelector`, `controller_native::NativeGamepadController`, CMake/MSBuild, JSON artifact output.

---

### Task 1: Geometry and Metric Contracts

**Files:**
- Create: `native/controller_native/vertical_bodylock_defect_benchmark.h`
- Create: `native/controller_native/vertical_bodylock_defect_benchmark_tests.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Write the failing contract test**

Define `VerticalDefectFrame`, `VerticalDefectMetrics`, and scenario entry points. Assert that the prone and stair fixtures have `visible_body_top > detection_top`, cue Y above the visible body, and manual recovery direction toward the body.

- [ ] **Step 2: Run the test target and verify RED**

Run `cmake --build native/vision_native/build --config Release --target cod_native_vertical_bodylock_defect_tests`.
Expected: build fails because the benchmark header/implementation does not exist.

- [ ] **Step 3: Add the minimal data contracts and fixture geometry**

The metrics must include selector/bodylock target Y, target-to-body distance, maximum overshoot, recovery-start frame, outside-body frames, AI-opposes-recovery frames, manual-escape frame, reacquire frame, and a `defect_reproduced` flag.

- [ ] **Step 4: Build and run the contract test**

Expected: fixture-contract tests pass without invoking production behavior.

### Task 2: Air-Lock Reproduction

**Files:**
- Create: `native/controller_native/vertical_bodylock_defect_benchmark.cpp`
- Modify: `native/controller_native/vertical_bodylock_defect_benchmark_tests.cpp`

- [ ] **Step 1: Add a failing current-behavior test**

Inject a detection box whose upper region contains cue/gap pixels and whose oracle body begins below the current 40% target point. Assert the computed target lies inside the oracle visible body and that sustained user input reaches the body within a bounded number of frames.

- [ ] **Step 2: Verify the test fails for the reported reason**

Expected: failure reports a target above `visible_body_top`, excessive AI opposition, or no bounded manual escape.

- [ ] **Step 3: Implement measurement-only simulation**

Run stationary prone and downward-moving stair variants through the real selector/controller. Do not clamp or replace target Y. Record the current behavior and set `defect_reproduced` from the measured violation.

- [ ] **Step 4: Replace the desired-behavior assertion with evidence assertions**

Assert that all required metrics are populated and that the current code reproduces or disproves the defect deterministically. Preserve the original failing desired-behavior values in the JSON report.

### Task 3: Cooperative Overshoot and Occlusion Reproduction

**Files:**
- Modify: `native/controller_native/vertical_bodylock_defect_benchmark.cpp`
- Modify: `native/controller_native/vertical_bodylock_defect_benchmark_tests.cpp`

- [ ] **Step 1: Add a failing desired-behavior test**

Start the reticle below the body, apply user and AI upward input, cross the visible target, drop detections for a bounded weapon-occlusion window, then apply user recovery input. Assert bounded overshoot and prompt reverse correction.

- [ ] **Step 2: Verify RED and inspect direction traces**

Expected: failure identifies overshoot depth, delayed reverse correction, continued wrong-direction output, or failed reacquisition.

- [ ] **Step 3: Implement the timeline and event capture**

Record pre-cross, crossing, dropout, recovery, and reacquisition frames, including manual Y, AI Y, final Y, vision authority, mode, target Y, reticle Y, and oracle body bounds.

- [ ] **Step 4: Run twice and require deterministic metrics**

Expected: identical counters and event frames for both runs.

### Task 4: Report Executable and Regression Isolation

**Files:**
- Create: `native/controller_native/cod_native_vertical_bodylock_defect_benchmark.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Add JSON/console report generation**

Emit all three scenarios (`cooperative_upward_overshoot_occlusion`, `prone_air_lock_manual_escape`, `stairs_low_target_manual_escape`) with geometry, counters, event traces, and `defect_reproduced`.

- [ ] **Step 2: Build and run the benchmark**

Run the executable with `--config config.toml --output runs/native_perf/runtime_acceptance/vertical-bodylock-defect.json`.

- [ ] **Step 3: Run existing isolation checks**

Run `cod_native_gamepad_benchmark --self-test`, `cod_native_controller_tests`, `cod_native_target_selector_tests`, and the new contract tests. Existing tests must remain green.

- [ ] **Step 4: Commit benchmark-only changes**

Stage only the new benchmark sources, tests, CMake target, spec, and plan. Do not stage user-deleted `config.native.example.toml` or root benchmark artifacts.
