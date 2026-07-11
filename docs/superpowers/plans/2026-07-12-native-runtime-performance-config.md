# Native Runtime Performance and Configuration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement compact but complete per-module native runtime configuration, cadence/wakeup, asynchronous telemetry, precision scheduler, pinned color readback, and Pascal build boundaries defined by the July 12 design while preserving controller, authority, and recoil behavior.

**Architecture:** Keep control behavior and safety constants compiled/profile-controlled, and layer user configuration through defaults, profile, file, then CLI/environment. Move timing and telemetry mechanics behind independently testable runtime services, retain explicit fallbacks, and expose machine-readable evidence for every performance gate. Modern and Pascal builds remain distinct artifacts.

**Tech Stack:** C++17, Windows waitable timers/events, CUDA Runtime, TensorRT, CMake/Visual Studio, TOML-style native configuration, JSONL telemetry, native deterministic test executables.

---

### Task 1: Configuration schema, profiles, compatibility, and reporting

**Files:**
- Modify: `native/controller_native/runtime_config.h`
- Modify: `native/controller_native/runtime_config.cpp`
- Modify: `native/controller_native/runtime_config_tests.cpp`
- Modify: `native/runtime_app/main.cpp`
- Create: `native/controller_native/runtime_config_schema.h`
- Create: `native/controller_native/runtime_config_schema.cpp`
- Create: `config.native.example.toml`
- Create: `native/controller_native/testdata/legacy_full_config.toml`

- [ ] Add failing tests for balanced defaults, profile selection, precedence, legacy key inheritance, invalid profiles/ranges, unknown/deprecated diagnostics, and effective value source layers.
- [ ] Run `cod_native_runtime_config_tests.exe` and verify the new assertions fail because profiles/source metadata do not exist.
- [ ] Add profile/config/source types and a table-driven schema without changing controller or recoil defaults.
- [ ] Implement precedence `defaults -> profile -> user config -> environment/CLI`, strict validation, legacy aliases, concise startup summary, and `--dump-effective-config`.
- [ ] Expose at most 8 coherent controls for each major module and at most 60 non-recoil keys in the normal template, resolving composite ADS/bodylock/tracker controls deterministically into existing detailed constants.
- [ ] Build and run the focused config tests; count template assignments and verify the effective dump covers every overridable schema key.

### Task 2: Canonical vision cadence

**Files:**
- Modify: `native/controller_native/runtime_config.h`
- Modify: `native/controller_native/runtime_config.cpp`
- Modify: `native/runtime_app/runtime_loop.cpp`
- Modify: `native/runtime_app/vision_service.h`
- Modify: `native/runtime_app/vision_service.cpp`
- Modify: `native/runtime_app/vision_service_tests.cpp`

- [ ] Add failing tests proving `capture_fps = 160` drives service and direct paths, legacy nonzero service rates override with a reported source, and latest-only scheduling never replays missed polls.
- [ ] Run the config and vision-service tests and verify expected failures.
- [ ] Make canonical active/idle cadence resolution shared by both paths; remove the hidden independent 120 Hz default and retain legacy aliases.
- [ ] Add requested/achieved rate and backlog counters.
- [ ] Run focused tests including a bounded cadence test that proves achieved rate does not exceed requested rate by more than 1%.

### Task 3: Interruptible aim wakeup and post-transition authority barrier

**Files:**
- Modify: `native/runtime_app/vision_service.h`
- Modify: `native/runtime_app/vision_service.cpp`
- Modify: `native/runtime_app/vision_service_tests.cpp`
- Modify: `native/runtime_app/runtime_loop.cpp`
- Modify: `native/vision_native/include/vision_native/types.h`

- [ ] Add failing deterministic tests for false-to-true wake interruption, immediate fresh poll, transition sequence tagging, and rejection of pre-aim authority.
- [ ] Verify failures occur against the current sleep-based worker.
- [ ] Add a condition-variable wake path, transition epoch/sequence barrier, and wake-to-dispatch/capture/result metrics without moving inference onto the controller thread.
- [ ] Add a 100-transition machine-readable wake benchmark and percentile report.
- [ ] Run service tests and confirm authority-bearing pre-aim results remain zero.

### Task 4: Telemetry disabled fast path and bounded queue

**Files:**
- Create: `native/runtime_app/runtime_telemetry.h`
- Create: `native/runtime_app/runtime_telemetry.cpp`
- Create: `native/runtime_app/runtime_telemetry_tests.cpp`
- Modify: `native/controller_native/runtime_config.h`
- Modify: `native/controller_native/runtime_config.cpp`
- Modify: `native/runtime_app/runtime_loop.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] Add failing tests that disabled telemetry creates no file/thread/serialization and that queue overflow rejects without blocking while incrementing exact counters.
- [ ] Verify the tests fail because the telemetry boundary is absent.
- [ ] Implement fixed-size record envelopes and a bounded non-blocking queue with a zero-work disabled branch.
- [ ] Map legacy aim-perf keys to telemetry settings with migration diagnostics.
- [ ] Build and run telemetry/config tests.

### Task 5: Background telemetry writer, schemas, sampling, and rotation

**Files:**
- Modify: `native/runtime_app/runtime_telemetry.h`
- Modify: `native/runtime_app/runtime_telemetry.cpp`
- Modify: `native/runtime_app/runtime_telemetry_tests.cpp`
- Modify: `native/runtime_app/aim_perf_file_logger.h`
- Modify: `native/runtime_app/aim_perf_file_logger.cpp`
- Modify: `native/runtime_app/runtime_loop.cpp`

- [ ] Add failing tests for ManualControllerTick/VisionFrame/RuntimeEvent joins, per-frame candidate deduplication, debug/profile sampling, event windows, rotation limits, writer failure, bounded drain, and malformed final-line tolerance.
- [ ] Verify the failures against the initial queue-only implementation.
- [ ] Move all JSON encoding and file I/O to the writer thread, with critical-event accounting and rotation.
- [ ] Adapt the legacy logger API to enqueue compatibility records instead of writing synchronously.
- [ ] Run unit/stress tests and emit enqueue latency/high-water/drop artifacts.

### Task 6: Precision one-millisecond scheduler

**Files:**
- Modify: `native/runtime_app/runtime_timing.h`
- Modify: `native/runtime_app/runtime_timing.cpp`
- Modify: `native/runtime_app/runtime_timing_tests.cpp`
- Modify: `native/runtime_app/runtime_loop.cpp`
- Modify: `native/runtime_app/perf_logger.h`
- Modify: `native/runtime_app/perf_logger.cpp`

- [ ] Add failing tests for absolute deadline advancement, late-tick realignment, no replay, missed/consecutive counters, percentile aggregation, and fallback selection.
- [ ] Verify failures against `sleep_until_precise`.
- [ ] Implement a Windows high-resolution waitable-timer scheduler with a short configurable spin/yield tail and existing-path fallback.
- [ ] Expose interval/lateness/pipeline/ViGEm metrics through perf telemetry.
- [ ] Run deterministic tests plus isolated baseline/candidate scheduler measurements, retaining raw machine-readable artifacts; keep the old path default unless the design's CPU/context-switch gate passes.

### Task 7: Behavior-equivalent pinned color readback

**Files:**
- Modify: `native/vision_native/include/vision_native/vision_engine.h`
- Modify: `native/vision_native/include/vision_native/types.h`
- Modify: `native/vision_native/src/vision_engine.cpp`
- Create: `native/vision_native/src/color_readback.h`
- Create: `native/vision_native/src/color_readback.cpp`
- Create: `native/vision_native/src/color_readback_tests.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] Add failing deterministic BGRA fixture tests comparing pageable and pinned classification, friendly/cue/bonus/selection/authority outputs and allocation/transfer fallbacks.
- [ ] Verify failures because the pinned strategy does not exist.
- [ ] Add a reusable high-watermark pinned host buffer, selector-region copy, async D2H on the existing stream, synchronization-before-CPU-consumption, and per-frame pageable fallback.
- [ ] Add color copy/classification/region/mode metrics without extending CUDA/D3D mapping lifetime.
- [ ] Run fixture parity and A/B tests; leave pageable default unless the p95 improvement gate passes.

### Task 8: Modern/Pascal artifact boundary

**Files:**
- Modify: `native/vision_native/CMakeLists.txt`
- Create: `native/vision_native/CMakePresets.json`
- Create: `native/vision_native/include/vision_native/build_family.h`
- Modify: `native/vision_native/src/tensorrt_engine.cpp`
- Create: `native/vision_native/src/build_family_tests.cpp`
- Modify: `docs/project/NATIVE_CPP_RUNTIME.md`

- [ ] Add failing tests for modern/Pascal runtime-engine signature mismatch before inference.
- [ ] Verify the mismatch test fails against the current unrestricted loader.
- [ ] Add separate modern SM75+ and Pascal SM61 presets/dependency roots, TensorRT API compatibility macros, build signatures, and startup validation.
- [ ] Document separate engine generation and required CUDA/TensorRT stacks.
- [ ] Build/test the modern preset; mark Pascal target validation `unverified` unless real SM 6.1 hardware and dependencies are available.

### Task 9: Automated acceptance harness and artifacts

**Files:**
- Create: `tools/native_runtime_acceptance.py`
- Create: `tools/tests/test_native_runtime_acceptance.py`
- Create: `scripts/verify/native_runtime_performance_acceptance.ps1`
- Create: `docs/project/NATIVE_RUNTIME_PERFORMANCE_ACCEPTANCE.md`

- [ ] Add failing tests for percentile calculation, three-run median selection, hashes/signatures, invalid/dropped samples, hard-gate behavior, and explicit `pass/fail/unverified` statuses.
- [ ] Verify the Python tests fail before the harness exists.
- [ ] Implement artifact collection and sections A-G scorecard generation without allowing performance gains to offset hard safety failures.
- [ ] Add a PowerShell entry point that records commands and invokes focused native tests/contracts.
- [ ] Run harness unit tests and a short local smoke acceptance to validate artifact structure.

### Task 10: Full non-regression and modern-machine acceptance

**Files:**
- Modify only files required by failures discovered through a new failing regression test.
- Create: `runs/native_perf/runtime_acceptance/<timestamp>/...` (generated, ignored)

- [ ] Build all touched Release targets.
- [ ] Run focused config, vision service, telemetry, timing, color, build-family, controller, ADS, bodylock, auto-fire, protocol, tracker, output-validation, benchmark, AimLab, and recoil tests.
- [ ] Run `scripts/verify/native_pipeline_contract.bat`.
- [ ] Produce deterministic benchmark comparisons and verify all authority/non-regression counters against the retained baseline.
- [ ] Run every locally feasible A-F measurement for its specified duration and three-run protocol; mark game-load or hardware-dependent gates unverified when the required workload cannot be reproduced.

### Task 11: Pascal target gate and release decision

**Files:**
- Update: `docs/project/NATIVE_RUNTIME_PERFORMANCE_ACCEPTANCE.md`
- Create: `runs/native_perf/runtime_acceptance/<timestamp>/pascal/...` (generated, ignored)

- [ ] On a real GTX 1060 6 GB system, build the Pascal binary/engine and run warm inference, 30-minute soak, memory, vision-age, game-frame-time, and FP32/FP16/INT8 comparisons exactly as specified.
- [ ] Verify incompatible artifact combinations fail before inference.
- [ ] Mark G passed only from retained real-SM61 artifacts; otherwise record `unverified` and do not advertise GTX 1060 support.
- [ ] Make the final release decision: A-F must pass on the modern reference machine; any unavailable long-running live/game measurement remains explicitly unverified rather than inferred.
