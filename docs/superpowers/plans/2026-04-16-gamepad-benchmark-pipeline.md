# Gamepad Benchmark Pipeline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the phase-1 gamepad benchmark pipeline so the repository can generate deterministic scenario manifests, simulate closed-loop tracking, compute replayable benchmark metrics, persist JSON artifacts, and maintain a Markdown scoreboard with a stored baseline.

**Architecture:** Keep the existing deterministic scenario generator in `tests/gamepad/benchmark_scenarios.py`, add a focused `tests/gamepad/benchmark_metrics.py` module for closed-loop simulation plus metric aggregation, add a focused `tests/gamepad/benchmark_scoreboard.py` module for Markdown persistence, and wire everything together through `tools/run_gamepad_benchmark.py`. The runner should be thin: compose configuration, scenario loading/replay, metric execution, JSON persistence, and scoreboard updates without embedding scoring logic.

**Tech Stack:** Python 3, `unittest`, dataclasses, JSON, Markdown text generation, existing gamepad plugin pipeline

---

### Task 1: Add benchmark metrics simulation and tests

**Files:**
- Create: `D:\work\AI\yolo-study-001\.worktrees\codex-gamepad-benchmark-pipeline\tests\gamepad\benchmark_metrics.py`
- Create: `D:\work\AI\yolo-study-001\.worktrees\codex-gamepad-benchmark-pipeline\tests\gamepad\test_gamepad_benchmark_metrics.py`
- Verify against: `D:\work\AI\yolo-study-001\.worktrees\codex-gamepad-benchmark-pipeline\controllers\gamepad\ai_aim.py`
- Verify against: `D:\work\AI\yolo-study-001\.worktrees\codex-gamepad-benchmark-pipeline\controllers\gamepad\state.py`

- [ ] **Step 1: Write failing metric tests**

Add tests that prove:
- a stored manifest produces deterministic per-frame simulation output
- tracking summaries expose `mean_error_px`, `p95_error_px`, and `p99_error_px`
- overshoot detection counts zero-cross events only when the crossed magnitude exceeds `2.0 px`
- turn recovery measures frames until radial error returns below `6.0 px`
- deceleration settling measures frames until radial error stays within `5.0 px` for `4` consecutive frames
- aggregate run summaries preserve per-scenario metrics and compute relative deltas versus a baseline

- [ ] **Step 2: Run the new test module and verify it fails**

Run: `python -m unittest tests.gamepad.test_gamepad_benchmark_metrics -v`
Expected: FAIL because `benchmark_metrics.py` does not exist yet.

- [ ] **Step 3: Implement the minimal metrics module**

Implement:
- benchmark constants/config dataclass for `frame_dt`, `sim_frames`, `measure_from_frame`, `max_reticle_speed_pps`, `stick_max`, `overshoot_threshold_px`, `turn_recovery_threshold_px`, `settle_threshold_px`, and `settle_consecutive_frames`
- dataclasses for frame snapshots, per-scenario metrics, aggregate run metrics, and relative deltas
- a closed-loop simulator that:
  - expands target motion from a stored manifest
  - feeds `target_dx` / `target_dy` into the current gamepad AI plugin in fixed-step time
  - maps stick output back into reticle motion using `max_reticle_speed_pps` and `stick_max`
  - records frame-level error information needed for metric calculations
- helpers to calculate tracking error, overshoot, turn recovery, deceleration settling, run aggregation, and deltas versus a baseline

- [ ] **Step 4: Run the metric tests until they pass**

Run: `python -m unittest tests.gamepad.test_gamepad_benchmark_metrics -v`
Expected: PASS.

### Task 2: Add scoreboard persistence and tests

**Files:**
- Create: `D:\work\AI\yolo-study-001\.worktrees\codex-gamepad-benchmark-pipeline\tests\gamepad\benchmark_scoreboard.py`
- Create: `D:\work\AI\yolo-study-001\.worktrees\codex-gamepad-benchmark-pipeline\tests\gamepad\test_gamepad_benchmark_scoreboard.py`
- Create: `D:\work\AI\yolo-study-001\.worktrees\codex-gamepad-benchmark-pipeline\docs\project\GAMEPAD_BENCHMARKS.md`

- [ ] **Step 1: Write failing scoreboard tests**

Add tests that prove:
- a missing scoreboard file is created with the required sections:
  - `Baseline Definition`
  - `Benchmark Parameters`
  - `Scenario Logic`
  - `Latest Run`
  - `History vs Baseline`
- a baseline update records the chosen run key and benchmark parameter snapshot
- later non-baseline runs replace the latest-run section and append to history without corrupting the baseline section
- the Markdown only stores summary information and references artifact paths instead of embedding full scenario manifests

- [ ] **Step 2: Run the new scoreboard test module and verify it fails**

Run: `python -m unittest tests.gamepad.test_gamepad_benchmark_scoreboard -v`
Expected: FAIL because `benchmark_scoreboard.py` does not exist yet.

- [ ] **Step 3: Implement the minimal scoreboard module**

Implement:
- helpers to create/load/save the scoreboard Markdown
- a small data model for scoreboard entries derived from run artifacts
- a formatter that keeps the baseline definition stable, rewrites the latest-run section, and appends history rows keyed by run key
- helpers that document benchmark parameters, metric thresholds, and scenario-family composition from the benchmark config

- [ ] **Step 4: Run the scoreboard tests until they pass**

Run: `python -m unittest tests.gamepad.test_gamepad_benchmark_scoreboard -v`
Expected: PASS.

### Task 3: Add runner CLI, replay support, and integration tests

**Files:**
- Create: `D:\work\AI\yolo-study-001\.worktrees\codex-gamepad-benchmark-pipeline\tools\run_gamepad_benchmark.py`
- Modify: `D:\work\AI\yolo-study-001\.worktrees\codex-gamepad-benchmark-pipeline\tests\gamepad\benchmark_scenarios.py`
- Modify: `D:\work\AI\yolo-study-001\.worktrees\codex-gamepad-benchmark-pipeline\tests\gamepad\benchmark_metrics.py`
- Modify: `D:\work\AI\yolo-study-001\.worktrees\codex-gamepad-benchmark-pipeline\tests\gamepad\benchmark_scoreboard.py`
- Create: `D:\work\AI\yolo-study-001\.worktrees\codex-gamepad-benchmark-pipeline\tests\gamepad\test_gamepad_benchmark_runner.py`

- [ ] **Step 1: Write failing runner/integration tests**

Add tests that prove:
- a normal run generates 24 manifests, writes `artifacts/benchmarks/gamepad/<run_key>.json`, and reports when no baseline exists
- `--set-baseline` stores the created run as the baseline and updates the scoreboard
- `--replay-run-key <run_key>` replays stored manifests instead of regenerating randomness
- `--replay-scenario-key <scenario_key>` finds a stored manifest and replays just that scenario
- missing replay keys fail with clear errors

- [ ] **Step 2: Run the runner test module and verify it fails**

Run: `python -m unittest tests.gamepad.test_gamepad_benchmark_runner -v`
Expected: FAIL because `run_gamepad_benchmark.py` does not exist yet.

- [ ] **Step 3: Implement the runner and persistence flow**

Implement:
- CLI parsing for normal runs, `--set-baseline`, `--replay-run-key`, and `--replay-scenario-key`
- run-key generation, git metadata capture, dirty-worktree detection, benchmark-config snapshots, and controller-config snapshots
- JSON artifact persistence containing:
  - run metadata
  - baseline linkage
  - benchmark configuration
  - controller configuration snapshot
  - aggregate metrics and baseline deltas
  - per-scenario manifests and per-scenario metrics
- replay helpers that load stored manifests directly from artifacts
- scoreboard updates routed through `benchmark_scoreboard.py`

- [ ] **Step 4: Run the runner tests until they pass**

Run: `python -m unittest tests.gamepad.test_gamepad_benchmark_runner -v`
Expected: PASS.

### Task 4: Verify the full phase-1 slice and seed the initial baseline

**Files:**
- Verify only: benchmark modules, runner, docs, and generated artifact output

- [ ] **Step 1: Run benchmark-focused tests**

Run: `python -m unittest tests.gamepad.test_gamepad_benchmark_scenarios tests.gamepad.test_gamepad_benchmark_metrics tests.gamepad.test_gamepad_benchmark_scoreboard tests.gamepad.test_gamepad_benchmark_runner -v`
Expected: PASS.

- [ ] **Step 2: Run the broader gamepad regression slice**

Run: `python -m unittest discover -s tests/gamepad -p "test_*.py" -v`
Expected: PASS.

- [ ] **Step 3: Run syntax verification**

Run: `python -m py_compile tests\gamepad\benchmark_scenarios.py tests\gamepad\benchmark_metrics.py tests\gamepad\benchmark_scoreboard.py tests\gamepad\test_gamepad_benchmark_metrics.py tests\gamepad\test_gamepad_benchmark_scoreboard.py tests\gamepad\test_gamepad_benchmark_runner.py tools\run_gamepad_benchmark.py`
Expected: no output, exit code 0.

- [ ] **Step 4: Seed the baseline and verify replay**

Run:
- `python tools\run_gamepad_benchmark.py --set-baseline`
- `python tools\run_gamepad_benchmark.py --replay-run-key <baseline_run_key>`

Expected:
- baseline artifact written under `artifacts/benchmarks/gamepad/`
- `docs/project/GAMEPAD_BENCHMARKS.md` updated with baseline and latest-run sections
- replay succeeds from stored manifests without regenerating random inputs
