# Native Target Credibility And Output Validation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reduce ADS err-target overshoot and AI+manual wrong-way push without making tracker projection overpower fresh vision or player intent.

**Architecture:** Add a small controller-side credibility gate around target consumption, not a new vision pipeline. Fresh confirmed vision remains the strongest source; tracker projection is a short-lived envelope for validating observations and output direction. Add target-aware output validation so mixed AI+manual output is damped only when it is pushing away after crossing or during user correction.

**Tech Stack:** Native C++ controller code under `native/controller_native`, existing CMake targets `cod_native_controller_tests` and `cod_native_gamepad_benchmark`, existing offline benchmark artifacts under `artifacts/benchmarks/native_gamepad`.

---

## Acceptance Checklist

- [x] `ads_diagonal_err_target_recovery_100hz_dynamic_fire`: `large_overshoot_events_50px <= 2`, `max_overshoot_px < 50`, `p95_err_target_recovery_ms <= 120`.
- [x] `ads_diagonal_err_target_late_position_fov_occlusion_50hz_dynamic_fire`: `large_overshoot_events_50px <= 2`, `max_overshoot_px < 60`, `p95_err_target_recovery_ms <= 220`.
- [x] Add an AI+manual mixed-output benchmark/test where a target crosses center and final output pushes away; wrong-way output must be zeroed or capped within 30-50ms.
- [x] Add a diagonal mixed-output benchmark/test where one axis crosses and the other does not; only the crossed/wrong-way axis is zeroed or capped.
- [x] Add a manual-correction benchmark/test where player input points back toward the target; AI/tracker output must not keep opposing the correction.
- [x] Tracker projection cannot promote a candidate target to confirmed without fresh vision samples.
- [x] Projection-only assist decays under a short TTL and cannot keep bodylock alive by itself.
- [x] Fresh confirmed vision still wins over tracker projection when it is inside the prediction envelope.
- [x] Baselines do not regress: ADS parity, random FOV, and current ADS diagonal stress do not reintroduce 50px+ overshoot or large speed loss.
- [x] Verification passes: `cod_native_controller_tests.exe`, benchmark `--self-test`, focused target benchmark, and `scripts\verify\native_pipeline_contract.bat`.

## Current Run Status

Latest focused artifact:
`artifacts/benchmarks/native_gamepad/target-credibility-output-validation-20260630.json`

- PASS: `cod_native_controller_tests.exe`
- PASS: `cod_native_gamepad_benchmark.exe --self-test`
- PASS: `ads_diagonal_manual_stress_100hz_dynamic_fire`
  - `large_overshoot_events_50px=0`
  - `max_overshoot_px=4.897`
  - `final_error_px=2.775`
- PASS: `ads_diagonal_err_target_recovery_100hz_dynamic_fire`
  - `large_overshoot_events_50px=0`
  - `max_overshoot_px=4.897`
  - `err_target_recovered_windows=12/12`
  - `p95_err_target_recovery_ms=0`
- PASS: `ads_diagonal_late_vision_fov_occlusion_50hz_dynamic_fire`
  - `large_overshoot_events_50px=0`
  - `max_overshoot_px=39.617`
- PASS: `ads_diagonal_late_position_fresh_timestamp_fov_occlusion_50hz_dynamic_fire`
  - `large_overshoot_events_50px=0`
  - `max_overshoot_px=23.739`
- PASS: `ads_diagonal_err_target_late_position_fov_occlusion_50hz_dynamic_fire`
  - `large_overshoot_events_50px=0`
  - `max_overshoot_px=40.531`
  - `err_target_recovered_windows=11/12`
  - `p95_err_target_recovery_ms=0`
- PASS: `scripts\verify\native_pipeline_contract.bat -RuntimeConfig D:\work\AI\yolo-study-001\config.toml`
  - Rebuilt `cod_native_controller_tests`, `cod_native_runtime`, and `cod_native_gamepad_benchmark`; controller tests, runtime smoke, and benchmark smoke passed.

## Files

- Modify: `native/controller_native/native_gamepad_controller.h`
- Modify: `native/controller_native/native_gamepad_controller.cpp`
- Modify: `native/controller_native/controller_behavior_tests.cpp`
- Modify: `native/controller_native/cod_native_gamepad_benchmark.cpp`
- Create artifact: `artifacts/benchmarks/native_gamepad/target-credibility-output-validation-20260630.json`

## Task 1: Lock Red Tests For AI+Manual Output Validation

- [ ] Add focused controller behavior tests in `native/controller_native/controller_behavior_tests.cpp`:
  - `test_controller_output_validation_caps_wrong_way_after_x_crossing`
  - `test_controller_output_validation_caps_only_crossed_axis_on_diagonal`
  - `test_controller_output_validation_yields_to_manual_correction`
- [ ] Run:

```powershell
native\vision_native\build\Release\cod_native_controller_tests.exe
```

Expected before implementation: at least one new test fails because target-aware output validation does not exist yet.

## Task 2: Add Minimal Target Credibility State

- [ ] Add private controller state in `native_gamepad_controller.h` for committed/candidate target observations:
  - committed dx/dy and observed time
  - candidate dx/dy, observed time, consecutive fresh sample count
  - projection-only confidence decay timestamp
- [ ] Add helper logic in `native_gamepad_controller.cpp`:
  - classify fresh vision as confirmed when inside tracker/committed envelope
  - classify large unexplained jumps as candidate
  - upgrade candidate only after consecutive fresh vision confirmation
  - never let tracker projection alone upgrade candidate

Run after implementation:

```powershell
native\vision_native\build\Release\cod_native_controller_tests.exe
```

Expected: new credibility tests pass and existing tests stay green.

## Task 3: Add Target-Aware Output Validation

- [ ] Add a post-ai-aim, pre-recoil validation step in `native_gamepad_controller.cpp`.
- [ ] For each axis, compare previous/current target error and final output direction.
- [ ] If error crosses center and final output still pushes away, cap that axis to a low value or zero for a short window.
- [ ] If manual input points toward the target while AI/tracker pushes away, reduce only the opposing AI/tracker contribution.
- [ ] Keep recoil final in the existing final stage; do not smooth output after recoil.

Run:

```powershell
native\vision_native\build\Release\cod_native_controller_tests.exe
native\vision_native\build\Release\cod_native_gamepad_benchmark.exe --self-test
```

Expected: tests and benchmark self-test pass.

## Task 4: Extend Benchmarks For Candidate And Output Validation

- [ ] Extend `cod_native_gamepad_benchmark.cpp` with AI+manual mixed-output scenarios:
  - wrong-way after crossing
  - diagonal single-axis crossing
  - manual correction against AI/tracker
- [ ] Keep scenarios deterministic through existing `--random-fov-seed`.
- [ ] Add JSON fields for:
  - `wrong_way_output_ticks`
  - `zero_latency_ms`
  - `single_axis_cap_events`
  - `manual_correction_opposed_ticks`
  - `projection_only_assist_ms`

Run:

```powershell
native\vision_native\build\Release\cod_native_gamepad_benchmark.exe --config config.toml --run-key target-credibility-output-validation-20260630 --frames 80 --dt-ms 0 --random-fov-ticks 0 --random-fov-seed 1337 --output artifacts\benchmarks\native_gamepad\target-credibility-output-validation-20260630.json
```

Expected: focused artifact includes err-target and AI+manual validation metrics.

## Task 5: Regression Verification

- [ ] Build release controller tests and benchmark:

```powershell
cmake --build native\vision_native\build --config Release --target cod_native_controller_tests cod_native_gamepad_benchmark
```

- [ ] Run focused tests:

```powershell
native\vision_native\build\Release\cod_native_controller_tests.exe
native\vision_native\build\Release\cod_native_gamepad_benchmark.exe --self-test
native\vision_native\build\Release\cod_native_gamepad_benchmark.exe --config config.toml --run-key target-credibility-output-validation-20260630 --frames 80 --dt-ms 0 --random-fov-ticks 0 --random-fov-seed 1337 --output artifacts\benchmarks\native_gamepad\target-credibility-output-validation-20260630.json
```

- [ ] Run pipeline contract:

```powershell
scripts\verify\native_pipeline_contract.bat
```

Expected: all commands exit 0 and the acceptance checklist metrics are met or explicitly reported as remaining work.
