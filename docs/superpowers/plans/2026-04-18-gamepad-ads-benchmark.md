# Gamepad ADS Benchmark Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a dedicated `ads` benchmark suite for the gamepad controller with synthetic ADS-start scenarios, ADS-specific manual input profiles, safety-first metrics, replay support, and an isolated scoreboard/artifact path.

**Architecture:** Add a third benchmark family alongside `phase1` and `manual-mix`. The new suite will use its own scenario schema, manual-input generator, and metrics evaluator, while reusing the existing benchmark runner entrypoint, scoreboard renderer, and controller-under-test (`AIAimPlugin`). Runner/replay/artifact behavior stays uniform across suites, but the ADS scenario model and metrics remain isolated so they do not pollute existing benchmark history.

**Tech Stack:** Python 3, `unittest`, dataclasses, existing gamepad benchmark runner and scoreboard modules.

---

### Task 1: ADS Scenario Schema And Generator

**Files:**
- Create: `tests/gamepad/ads_benchmark_scenarios.py`
- Test: `tests/gamepad/test_gamepad_ads_benchmark_scenarios.py`

- [ ] **Step 1: Write failing scenario-schema and generation tests**
- [ ] **Step 2: Run the new scenario test file and verify it fails**
- [ ] **Step 3: Implement ADS scenario dataclasses, validation, expansion, and deterministic manifest generation**
- [ ] **Step 4: Re-run the scenario test file and verify it passes**

### Task 2: ADS Manual Input Profiles

**Files:**
- Create: `tests/gamepad/ads_manual_inputs.py`
- Test: `tests/gamepad/test_gamepad_ads_manual_inputs.py`

- [ ] **Step 1: Write failing tests for `none`, `aligned_follow`, `opposing_burst`, and `overshoot_recover`**
- [ ] **Step 2: Run the new manual-input test file and verify it fails**
- [ ] **Step 3: Implement deterministic ADS manual-input generation and per-frame annotations**
- [ ] **Step 4: Re-run the manual-input test file and verify it passes**

### Task 3: ADS Metrics Evaluator

**Files:**
- Create: `tests/gamepad/ads_benchmark_metrics.py`
- Test: `tests/gamepad/test_gamepad_ads_benchmark_metrics.py`

- [ ] **Step 1: Write failing tests for ADS config defaults, closed-loop simulation, per-scenario metrics, aggregate metrics, and subset-only metric aggregation**
- [ ] **Step 2: Run the new metrics test file and verify it fails**
- [ ] **Step 3: Implement ADS simulation records, scenario evaluation, run evaluation, and metric helpers**
- [ ] **Step 4: Re-run the metrics test file and verify it passes**

### Task 4: Runner, Replay, And Scoreboard Wiring

**Files:**
- Modify: `tools/run_gamepad_benchmark.py`
- Modify: `tests/gamepad/benchmark_scoreboard.py`
- Modify: `tests/gamepad/test_gamepad_benchmark_runner.py`
- Modify: `tests/gamepad/test_gamepad_benchmark_scoreboard.py`
- Create: `docs/project/GAMEPAD_ADS_BENCHMARKS.md`

- [ ] **Step 1: Write failing runner and scoreboard tests for the `ads` suite**
- [ ] **Step 2: Run the affected runner/scoreboard test subset and verify it fails**
- [ ] **Step 3: Implement `ads` suite routing, artifact serialization, replay support, parameter snapshots, and scoreboard title/path support**
- [ ] **Step 4: Re-run the runner/scoreboard test subset and verify it passes**

### Task 5: End-To-End Verification

**Files:**
- Verify only: `tests/gamepad/test_gamepad_ads_benchmark_scenarios.py`
- Verify only: `tests/gamepad/test_gamepad_ads_manual_inputs.py`
- Verify only: `tests/gamepad/test_gamepad_ads_benchmark_metrics.py`
- Verify only: `tests/gamepad/test_gamepad_benchmark_runner.py`
- Verify only: `tests/gamepad/test_gamepad_benchmark_scoreboard.py`

- [ ] **Step 1: Run the full ADS-focused test subset**
- [ ] **Step 2: Run a real `ads` benchmark command into a temporary artifact directory**
- [ ] **Step 3: Confirm replay by run key and scenario key works for the generated ADS artifact**
- [ ] **Step 4: Record any implementation gaps before claiming completion**
