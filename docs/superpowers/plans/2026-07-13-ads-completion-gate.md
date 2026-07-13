# ADS Completion Gate Implementation Plan

> **For Codex:** Execute this plan task-by-task with test-driven development and verify every claimed result from fresh command output.

**Goal:** Keep ADS acquisition authoritative until the selected target is centered for a configurable number of distinct fresh vision frames, or until a bounded timeout releases control to bodylock.

**Architecture:** Add a small stateful `AdsCompletionGate` owned by `NativeGamepadController`. Propagate vision freshness and frame identity through `NativeControllerVisionState`, use the gate output as ADS authority, and make `NativeAiAim` prefer that authority over bodylock. Expose the three tuning parameters under `[gamepad.ads]` and publish gate state in controller telemetry.

**Tech Stack:** C++17, CMake/MSBuild, native controller unit tests, runtime config tests, Python/pytest integration tests.

---

## Task 1: Configuration contract

**Files:**
- Modify: `native/controller_native/runtime_config.h`
- Modify: `native/controller_native/runtime_config.cpp`
- Modify: `native/controller_native/runtime_config_tests.cpp`
- Modify: `config.toml`

1. Add failing tests for defaults, explicit parsing, and invalid ranges for `completion_radius_px`, `completion_fresh_frames`, and `max_acquisition_ms`.
2. Run the runtime config tests and confirm the new assertions fail.
3. Add the typed config fields, parser keys, validation, and concise defaults in `config.toml`.
4. Rebuild and confirm runtime config tests pass.

## Task 2: ADS completion gate component

**Files:**
- Create: `native/controller_native/ads_completion_gate.h`
- Create: `native/controller_native/ads_completion_gate.cpp`
- Create: `native/controller_native/ads_completion_gate_tests.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

1. Add focused failing tests for activation, distinct-fresh-frame counting, projected/repeated-frame rejection, target loss/reset, target replacement reset, and timeout.
2. Run the new test target and confirm it fails before production implementation is linked.
3. Implement the smallest deterministic gate state machine and completion reason enum.
4. Rebuild and confirm the focused tests pass.

## Task 3: Vision metadata and controller integration

**Files:**
- Modify: `native/controller_native/controller_tick_context.h`
- Modify: `native/controller_native/target_snapshot_provider.cpp`
- Modify: `native/runtime_app/vision_controller_adapter.cpp`
- Modify: `native/controller_native/native_gamepad_controller.h`
- Modify: `native/controller_native/native_gamepad_controller.cpp`
- Modify: `native/controller_native/ai_aim.cpp`
- Modify: `native/controller_native/controller_behavior_tests.cpp`

1. Add failing controller tests proving bodylock cannot preempt active ADS acquisition, three distinct fresh centered frames complete acquisition, repeated/projected frames do not complete it, timeout releases it, and release/target loss resets it.
2. Run controller behavior tests and capture the expected failures.
3. Propagate source frame identity/freshness and stable target identity where available.
4. Integrate the gate into the controller and make active ADS authority precede bodylock.
5. Rebuild and confirm controller behavior tests pass.

## Task 4: Telemetry and diagnostics

**Files:**
- Modify: controller output/telemetry schema files identified by repository inspection
- Modify: runtime telemetry tests

1. Add failing serialization assertions for active state, stable fresh-frame count, completion reason, and configured thresholds.
2. Extend controller output and runtime telemetry without changing the default disabled-log behavior.
3. Rebuild and confirm telemetry tests pass.

## Task 5: Benchmark and full verification

**Files:**
- Modify or create: focused benchmark fixtures only if an existing benchmark hook is available

1. Run focused native config, gate, controller behavior, runtime telemetry, and benchmark targets.
2. Run the repository's relevant pytest/native integration suite.
3. Inspect the final diff for unintended changes and confirm the main `dev` worktree remains untouched.
4. Commit the implementation on `codex/ads-completion-gate`, then integrate it into `dev` only after all checks pass.

