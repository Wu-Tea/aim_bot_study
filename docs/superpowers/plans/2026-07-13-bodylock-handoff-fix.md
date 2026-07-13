# Bodylock Handoff Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reproduce and fix the live single-target bodylock manual-takeover stall, while adding enough stage and identity evidence to validate future recordings.

**Architecture:** Extend the focused native bodylock defect benchmark with a deterministic horizontal handoff scenario and control cases. Add a small stateful manual-takeover policy inside `NativeAiAim`, then serialize already-available controller pipeline stages and explicit vision provenance into telemetry without changing the disabled-mode cost.

**Tech Stack:** C++17, native controller benchmark harness, JSONL telemetry, CMake/MSBuild, TensorRT-linked native test suite.

---

### Task 1: Reproduce the live handoff defect

**Files:**
- Modify: `native/controller_native/vertical_bodylock_defect_benchmark.h`
- Modify: `native/controller_native/vertical_bodylock_defect_benchmark.cpp`
- Modify: `native/controller_native/vertical_bodylock_defect_benchmark_tests.cpp`

- [ ] Add a deterministic `run_single_target_manual_takeover()` scenario using the observed `-0.14 -> -0.38` manual ramp against a stale `+X` bodylock correction, plus stable cooperative and short-noise controls.
- [ ] Add preservation ratio, reversal duration, stall duration, takeover latency, resistance integral, and mode-transition metrics.
- [ ] Build and run `cod_native_vlock_defect_tests`, verify RED because the current controller stalls or reverses manual takeover.
- [ ] Run the benchmark in report mode and retain the pre-fix JSON artifact under `runs/native_perf/`.

### Task 2: Implement bounded bodylock manual takeover

**Files:**
- Modify: `native/controller_native/ai_aim.h`
- Modify: `native/controller_native/ai_aim.cpp`
- Modify: `native/controller_native/runtime_config.h`
- Modify: `native/controller_native/runtime_config.cpp`
- Modify: `native/controller_native/controller_behavior_tests.cpp`

- [ ] Add focused failing tests proving sustained opposing manual intent releases opposing bodylock within 60 ms, short noise does not release bodylock, and cooperative tracking retains assist.
- [ ] Verify the focused controller tests fail for the expected takeover assertions.
- [ ] Add a time-based manual-takeover state using manual magnitude, opposition, persistence, and release hysteresis; during takeover remove only assist components that oppose the committed manual direction and bypass manual suppression.
- [ ] Add concise advanced config keys with safe defaults while keeping the simplified public config unchanged.
- [ ] Rebuild and verify the new controller tests and handoff benchmark pass without weakening control scenarios.
- [ ] Write the post-fix benchmark artifact and compare it to the retained baseline.

### Task 3: Add controller-stage and identity provenance telemetry

**Files:**
- Modify: `native/runtime_app/telemetry_schema.h`
- Modify: `native/runtime_app/telemetry_collectors.h`
- Modify: `native/runtime_app/telemetry_collectors.cpp`
- Modify: `native/runtime_app/runtime_loop.cpp`
- Modify: `native/runtime_app/runtime_telemetry.cpp`
- Modify: `native/runtime_app/runtime_telemetry_tests.cpp`
- Modify: `native/runtime_app/telemetry_collectors_tests.cpp`

- [ ] Add failing serializer and collector tests for post-AI, dynamics, near-brake, carry-brake, pre-recoil, final, production target source/tier, telemetry identity ID, box count, body box, and takeover state.
- [ ] Verify both telemetry tests fail because the new fields are absent.
- [ ] Populate fields from `NativeControllerOutputComponents`, `NativeControllerVisionState`, and `VisionResult`; keep telemetry identity explicitly named and separate from production evidence.
- [ ] Serialize only controller-relevant fields and preserve the bounded async writer behavior.
- [ ] Verify telemetry tests pass and a generated controller row contains every stage needed to attribute suppression.

### Task 4: Validate profile and ADS evidence readiness

**Files:**
- Create: `tools/analyze_native_user_profile.py`
- Create: `tools/analyze_ads_transition_model.py`
- Create: `tests/test_native_telemetry_analysis.py`
- Modify: `docs/project/NATIVE_CPP_RUNTIME.md`

- [ ] Add failing fixture tests for a basic shadow user profile and robust ADS transition filtering/reporting.
- [ ] Verify tests fail because analyzers do not exist.
- [ ] Implement streaming JSONL analyzers that tolerate a partial final row, emit sample counts/confidence/readiness, and never modify runtime configuration.
- [ ] For ADS, report clean/conditional counts, robust median/MAD estimates, outlier counts, and explicit readiness gates rather than publishing an unsafe calibration.
- [ ] Verify fixture tests and run both analyzers on the July 13 recording.

### Task 5: Full verification and integration

**Files:**
- Modify only if a verification failure reveals a regression in an in-scope file.

- [ ] Build the complete Release native project.
- [ ] Run all `cod_native_*tests.exe` executables and verify zero failures.
- [ ] Run `cod_native_gamepad_benchmark.exe --self-test` and benchmark metrics tests.
- [ ] Run `scripts/verify/native_pipeline_contract.bat`.
- [ ] Run `git diff --check` and review the final diff against the design specification.
- [ ] Commit the implementation, fast-forward `dev`, rebuild the main-workspace runtime, rerun focused tests, and preserve unrelated user files.
