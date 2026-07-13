# ADS and Bodylock Brake Authority Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Keep ADS braking intact while removing all downstream brake authority from bodylock.

**Architecture:** Use `NativeAiAim::last_mode()` as the explicit policy boundary. ADS policies retain their current behavior; bodylock relies on assist limits, smoothing, takeover, authority gating, and final clamping.

**Tech Stack:** C++17, native controller tests, deterministic vertical/bodylock benchmark, CMake/MSBuild.

---

### Task 1: Lock the bodylock no-brake contract

**Files:**
- Modify: `native/controller_native/controller_behavior_tests.cpp`
- Modify: `native/controller_native/vertical_bodylock_defect_benchmark.cpp`
- Modify: `native/controller_native/vertical_bodylock_defect_benchmark.h`
- Modify: `native/controller_native/vertical_bodylock_defect_benchmark_tests.cpp`

- [ ] Add a failing controller test where bodylock error crosses while the user holds a direction; assert the post-takeover output keeps the user's sign and `ads_carry_brake_active` remains false.
- [ ] Add a failing moving-target benchmark with repeated error crossings; assert no long zero/reversal window and bounded overshoot followed by reacquisition.
- [ ] Run `cod_native_controller_tests.exe` and `cod_native_vlock_defect_tests.exe`; verify the new assertions fail because downstream bodylock brake is still active.

### Task 2: Gate brake authority by mode

**Files:**
- Modify: `native/controller_native/native_gamepad_controller.cpp`
- Modify: `native/controller_native/ads_carry_brake_policy.cpp`
- Modify: `native/controller_native/controller_behavior_tests.cpp`

- [ ] Skip short-plan and output-validation mutation when `last_mode() == "body_lock"`.
- [ ] Return input unchanged from ADS carry brake when `body_lock_active`.
- [ ] Update the old bodylock wrong-way test to the approved user-owned contract; do not change ADS expectations.
- [ ] Rebuild and verify both focused test executables pass.

### Task 3: Full regression and integration

**Files:**
- Modify: `docs/project/BODYLOCK_HANDOFF_ACCEPTANCE.md`

- [ ] Record the new authority boundary and benchmark results.
- [ ] Build `cod_native_runtime` in Release.
- [ ] Run every native `*tests.exe`, the Python native-controller/config/telemetry tests, and the native pipeline contract.
- [ ] Commit, fast-forward `dev`, rebuild and rerun focused tests in the main worktree.
