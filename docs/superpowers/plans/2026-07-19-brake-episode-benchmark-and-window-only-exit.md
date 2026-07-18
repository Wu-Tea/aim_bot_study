# Brake Episode Benchmark and Window-only Exit Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the native runtime keyboard termination hook and add continuous braking diagnostics that can rank ADS/BodyLock parameter combinations without rewarding premature stopping.

**Architecture:** Native termination uses a Windows console close-event atomic flag observed by the existing runtime loop; finite test limits remain unchanged. The sustained scorer owns a target-local brake episode, receives plan-level prediction telemetry through the benchmark adapter, exports per-target and aggregate diagnostics, and feeds a staged deterministic PowerShell sweep.

**Tech Stack:** C++20, Win32 console control API, CMake/CTest, Python unittest for launcher contracts, PowerShell benchmark orchestration, JSON artifacts.

---

### Task 1: Remove native keyboard termination

**Files:**
- Modify: `native/runtime_app/runtime_loop.h`
- Modify: `native/runtime_app/runtime_loop.cpp`
- Modify: `native/runtime_app/main.cpp`
- Modify: `native/controller_native/runtime_config.h`
- Test: `native/runtime_app/runtime_loop_tests.cpp`
- Test: `tests/test_startup_scripts.py`

- [ ] **Step 1: Add failing termination-policy tests**

Add a small public/testable termination policy that returns false for digit and
letter key state and true only for a requested console close or finite tick
limit. Extend startup tests to require that the native launcher contains no
`VISION_QUIT_KEY`, `quit_key`, or termination-hotkey message.

- [ ] **Step 2: Run the focused tests and confirm failure**

Run:

```powershell
python -m unittest tests.test_startup_scripts -v
cmake --build b --config Release --target cod_native_runtime_loop_tests
b\Release\cod_native_runtime_loop_tests.exe
```

Expected: the new native termination assertions fail while existing launcher
assertions remain green.

- [ ] **Step 3: Implement window-close-only stop**

Set the native default `quit_key` to an empty compatibility value, remove
`GetAsyncKeyState` from `RuntimeLoop::should_quit`, and register a
`SetConsoleCtrlHandler` callback in `main.cpp`. The callback handles
`CTRL_CLOSE_EVENT` by setting an atomic stop request and does no logging or
allocation. `--once` and `--max-ticks` continue through their current path.

- [ ] **Step 4: Rebuild and verify focused tests**

Expected: launcher and runtime-loop tests pass, and source search finds no
native runtime keyboard polling.

- [ ] **Step 5: Commit termination behavior**

```powershell
git add native/runtime_app native/controller_native/runtime_config.h tests/test_startup_scripts.py
git commit -m "runtime: remove native keyboard quit shortcut"
```

### Task 2: Define brake diagnostics contracts

**Files:**
- Modify: `native/controller_native/sustained_aimlab_types.h`
- Modify: `native/controller_native/sustained_aimlab_score.h`
- Test: `native/controller_native/sustained_aimlab_score_tests.cpp`

- [ ] **Step 1: Add failing scorer tests for every required trajectory**

Create deterministic traces for: no-cross fast settle, 2 px crossing, 18 px
sustained crossing, circle exit, 10--20 px stall, two AI reversals, unreliable
pause, manual-caused crossing, moving target, handoff, and never-settled target.
Assertions distinguish geometric crossing from AI continued-push attribution.

- [ ] **Step 2: Run score tests and confirm compile/test failure**

```powershell
cmake --build b --config Release --target cod_native_sustained_aimlab_score_tests
```

Expected: compilation fails on the new diagnostic fields before implementation.

- [ ] **Step 3: Add ScoreFrame and result fields**

`ScoreFrame` gains `tracker_reliable`, `ads_mode`, `mode_transition`,
`predicted_terminal_error_px`, and `radial_closing_velocity_px_per_sec` where
not already present. `TargetResult` and `BenchmarkResult` gain the fields named
in the approved spec, using `-1` for unavailable settle/brake times.

- [ ] **Step 4: Compile contracts without implementing scoring**

Expected: fields compile and trajectory assertions fail at runtime with zero or
sentinel values.

- [ ] **Step 5: Commit the contract extension**

```powershell
git add native/controller_native/sustained_aimlab_types.h native/controller_native/sustained_aimlab_score.h native/controller_native/sustained_aimlab_score_tests.cpp
git commit -m "bench: define brake episode diagnostics"
```

### Task 3: Implement target-relative brake episodes

**Files:**
- Modify: `native/controller_native/sustained_aimlab_score.cpp`
- Modify: `native/controller_native/sustained_aimlab_score.h`
- Test: `native/controller_native/sustained_aimlab_score_tests.cpp`

- [ ] **Step 1: Implement the minimal episode state machine**

Episode state stores the frozen approach unit vector, start/first-entry/cross/
settle ticks, previous signed longitudinal error, previous AI radial projection,
maximum opposite excursion, overshoot area, continued-push duration, stable
ticks, and reversal latch. Use the approved radius-normalized noise and settle
bands.

- [ ] **Step 2: Separate geometry from blame**

Always count a noise-qualified crossing. Accumulate continued AI push only for
observed, reliable, non-escape frames whose shaped-assist projection continues
the pre-cross direction. Pause attribution on unreliable/coasting evidence.

- [ ] **Step 3: Export existing hidden diagnostics**

Aggregate `circle_exit_events`, `stall_ring_ms`, `max_error_px`, and
`direction_discontinuities`; do not duplicate their frame-level computation.

- [ ] **Step 4: Run score tests**

Expected: every new trajectory test and all existing score tests pass.

- [ ] **Step 5: Commit scorer implementation**

```powershell
git add native/controller_native/sustained_aimlab_score.cpp native/controller_native/sustained_aimlab_score.h native/controller_native/sustained_aimlab_score_tests.cpp
git commit -m "bench: score continuous brake episodes"
```

### Task 4: Connect live controller plan diagnostics

**Files:**
- Modify: `native/controller_native/sustained_aimlab_simulator.h`
- Modify: `native/controller_native/sustained_aimlab_simulator.cpp`
- Modify: `native/controller_native/native_gamepad_controller.h`
- Modify: `native/controller_native/native_gamepad_controller.cpp`
- Modify: `native/controller_native/cod_native_sustained_aimlab_benchmark.cpp`
- Test: `native/controller_native/sustained_aimlab_simulator_tests.cpp`

- [ ] **Step 1: Add a failing adapter propagation test**

Assert that predicted terminal error, radial closing speed, ADS mode, and the
single ADS-to-BodyLock transition reach `ScoreFrame` unchanged.

- [ ] **Step 2: Expose the last immutable TargetPlan**

Add a const accessor to `NativeGamepadController`; do not copy ownership or add
logging. Extend `ControllerStepResult` with only the four scorer inputs.

- [ ] **Step 3: Populate ScoreFrame and transition edges**

The simulator derives `mode_transition` from consecutive controller-step modes.
No scorer reads private controller internals.

- [ ] **Step 4: Run simulator and integration tests**

Expected: propagation tests, target pipeline integration, and existing
simulator tests pass.

- [ ] **Step 5: Commit adapter wiring**

```powershell
git add native/controller_native/sustained_aimlab_simulator* native/controller_native/native_gamepad_controller* native/controller_native/cod_native_sustained_aimlab_benchmark.cpp
git commit -m "bench: propagate target plan brake telemetry"
```

### Task 5: Export JSON and add staged sweep

**Files:**
- Modify: `native/controller_native/cod_native_sustained_aimlab_benchmark.cpp`
- Modify: `scripts/verify/compare_sustained_aimlab.ps1`
- Create: `scripts/verify/run_brake_episode_sweep.ps1`
- Test: `native/controller_native/sustained_aimlab_simulator_tests.cpp`

- [ ] **Step 1: Add failing JSON field checks**

Require aggregate and per-target JSON keys for every new diagnostic and explicit
`-1` unavailable times.

- [ ] **Step 2: Serialize diagnostics**

Keep schema `sustained-aimlab-v1` for compatible additive fields and write the
new keys next to existing tracking diagnostics. Reject non-finite numeric
values before rename of the partial artifact.

- [ ] **Step 3: Implement the staged sweep script**

Stage 1 screens the approved horizon/strength/slowdown/response matrix with
short deterministic runs. Stage 2 runs three seeds for surviving configurations
on ordinary/small and pure/mixed cohorts. Stage 3 re-runs finalists at 400 and
650 response plus `0.40/0.30` slowdown. The script creates isolated configs
under its output directory and never edits root `config.toml`.

- [ ] **Step 4: Verify a smoke sweep**

Run with a smoke flag that limits duration and candidate count. Expected: all
artifacts have matching script hashes per environment and the summary lists
rejection reasons rather than only a scalar rank.

- [ ] **Step 5: Commit export and orchestration**

```powershell
git add native/controller_native/cod_native_sustained_aimlab_benchmark.cpp scripts/verify
git commit -m "bench: export brake metrics and stage parameter sweep"
```

### Task 6: Full verification and evidence

**Files:**
- Create: `docs/project/BRAKE_EPISODE_BENCHMARK_ACCEPTANCE_20260719.md`
- Create artifacts: `artifacts/benchmarks/sustained_aimlab/brake_episode_20260719/`

- [ ] **Step 1: Run all native unit and contract tests**

```powershell
cmake --build b --config Release
ctest --test-dir b -C Release --output-on-failure
b\Release\cod_native_controller_tests.exe
scripts\verify\native_pipeline_contract.bat
python -m unittest tests.test_startup_scripts -v
```

Expected: zero failures and native pipeline contract PASS.

- [ ] **Step 2: Run the full staged sweep**

Use seeds `1337`, `20260718`, and `424242`; retain raw finalist artifacts and a
summary with all safety gates and metrics.

- [ ] **Step 3: Document results and recommendation**

Record the frozen baseline, rejected combinations and reasons, finalists,
limitations, and recommended config. Do not change root `config.toml`.

- [ ] **Step 4: Verify repository hygiene**

```powershell
git diff --check
git status --short
```

Expected: only intended source, test, script, documentation, and selected
artifact changes are committed; build and temporary sweep files remain ignored.
