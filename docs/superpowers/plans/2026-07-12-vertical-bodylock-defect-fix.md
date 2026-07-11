# Vertical Bodylock Defect Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix vertical air-lock, manual-fight, and occlusion overshoot defects proven by the vertical bodylock benchmark.

**Architecture:** Keep target-point ownership in the selector, consume that point in bodylock, and yield only the conflicting Y-axis assist to deliberate manual correction. Validate every retained change against focused and full native benchmarks.

**Tech Stack:** C++17, CMake/MSVC, native selector/controller benchmark executables.

---

### Task 1: Turn defect reproduction into failing acceptance tests

**Files:**
- Modify: `native/controller_native/vertical_bodylock_defect_benchmark_tests.cpp`
- Modify: `native/controller_native/vertical_bodylock_defect_benchmark.cpp`

- [ ] Assert selector targets are within the visible-body oracle, manual escape completes, opposition is transient, and reverse-input recovery begins immediately.
- [ ] Build and run `cod_native_vlock_defect_tests`; verify it fails against current production behavior.

### Task 2: Correct wide/low selector target Y

**Files:**
- Modify: `native/vision_native/src/target_selector.cpp`
- Modify: `native/vision_native/tests/target_selector_tests.cpp`

- [ ] Add a focused selector assertion for a wide/low detection.
- [ ] Change only the wide/low target ratio enough to put the aim point in the body oracle.
- [ ] Run selector tests and the vertical defect test.

### Task 3: Preserve selector Y through bodylock

**Files:**
- Modify: `native/controller_native/ai_aim.cpp`
- Modify: `native/controller_native/native_controller_tests.cpp`

- [ ] Add a test proving bodylock uses the provided target Y instead of recomputing it from body-box height.
- [ ] Use input target Y for vertical lock delta while preserving bounded motion lead.
- [ ] Run controller and vertical defect tests.

### Task 4: Yield Y during deliberate opposing correction

**Files:**
- Modify: `native/controller_native/ai_aim.cpp`
- Modify: `native/controller_native/native_controller_tests.cpp`

- [ ] Add a test proving meaningful opposite manual Y input cannot receive opposite AI Y assistance.
- [ ] Suppress only conflicting bodylock Y assist for that frame.
- [ ] Run focused tests and confirm the overshoot/recovery score passes.

### Task 5: Full regression benchmark

**Files:**
- Update generated report only: `runs/native_benchmark/vertical_defect_fixed.json`

- [ ] Run vertical benchmark twice and compare deterministic output.
- [ ] Run controller tests, selector tests, benchmark metrics tests, gamepad self-test, and native pipeline contract.
- [ ] Run the full native gamepad benchmark and compare established bodylock scenario metrics with the pre-fix artifact.
- [ ] Keep the fix only if defect acceptance passes without material established-scenario regression.
