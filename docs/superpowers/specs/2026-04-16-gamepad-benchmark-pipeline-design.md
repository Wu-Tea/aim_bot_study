# Gamepad Benchmark Pipeline Design

**Goal:** Add a reproducible gamepad benchmark pipeline that runs closed-loop tracking scenarios with random direction changes, turn events, and deceleration events; records the current controller as the baseline; and stores every benchmark run in a way that can be replayed later by key.

**Scope:**
- Cover the `gamepad` controller only.
- Simulate target motion and controller response in a deterministic closed loop.
- Record benchmark configuration, scenario logic, per-run metrics, and baseline comparisons.
- Store a lightweight event-level scenario manifest for replay.
- Write a human-readable scoreboard to Markdown and a machine-readable artifact to JSON.

**Non-goals:**
- No mouse benchmark in this phase.
- No real-time desktop capture or live game integration.
- No frame-by-frame full trace export in the default path.
- No single aggregate score in phase 1.

## Problem

The current project can test individual controller plugins and a few simplified tracking simulations, but it does not have a benchmark pipeline that can answer these questions consistently:

- Does the controller still track well when the target changes direction unexpectedly?
- Does the controller overshoot during turns or deceleration?
- Is a new version better or worse than the current baseline?
- Can a suspicious run be replayed later without relying on memory or ad-hoc notes?

The missing pieces are:

- a standard scenario generator
- a standard metric set
- a stored baseline
- a reproducible replay mechanism
- a durable human-readable benchmark history

## Approaches Considered

### 1. Add more randomised `unittest` coverage only

Run random simulations inside the existing test suite and inspect printed output.

Pros:
- smallest code change
- quick to start

Cons:
- poor historical tracking
- no durable baseline scoreboard
- awkward replay workflow
- too easy to lose important context from a run

### 2. Add a dedicated benchmark pipeline with Markdown and JSON outputs

Keep the existing simulation style, but standardize scenario generation, metric calculation, baseline comparison, artifact storage, and replay by key.

Pros:
- reproducible
- benchmark history is durable
- baseline comparison becomes part of the workflow
- easy to extend later to mouse

Cons:
- more structure than a plain test
- requires a small reporting layer

### 3. Build a heavy benchmark system with per-frame trace storage

Store detailed traces for every frame of every scenario.

Pros:
- maximum debugging detail

Cons:
- too heavy for the current need
- large artifacts
- more implementation complexity than the user requested

## Chosen Design

Choose **Approach 2**.

The pipeline will:

1. generate a deterministic set of event-driven tracking scenarios
2. run the current gamepad controller logic in a closed-loop simulation
3. calculate tracking, overshoot, turn recovery, and deceleration-settling metrics
4. compare each run against a stored baseline
5. persist:
   - one Markdown scoreboard entry
   - one JSON artifact with replay data
6. support replay by key without re-randomizing the scenario

## Architecture

The pipeline is split into four bounded pieces.

### 1. Benchmark runner

`tools/run_gamepad_benchmark.py`

Responsibilities:
- parse CLI arguments
- build the gamepad AI plugin under test
- generate or load scenario manifests
- execute the benchmark suite
- write JSON artifacts
- update the Markdown scoreboard

### 2. Scenario generator

`tests/gamepad/benchmark_scenarios.py`

Responsibilities:
- generate deterministic event-level scenario manifests
- support multiple scenario families
- derive stable scenario keys
- load scenarios back from stored manifests for replay

### 3. Metric evaluator

`tests/gamepad/benchmark_metrics.py`

Responsibilities:
- simulate reticle response from gamepad output
- detect overshoot and recovery events
- summarize per-scenario and per-run metrics
- compute relative deltas versus baseline

### 4. Benchmark scoreboard

`docs/project/GAMEPAD_BENCHMARKS.md`

Responsibilities:
- define the baseline
- document benchmark parameters and scenario logic
- show latest run
- show historical comparisons versus baseline

## Closed-Loop Simulation Model

The benchmark will reuse the existing gamepad-style simulation approach already present in the repository:

- target position evolves over time
- the controller receives `target_dx` and `target_dy`
- controller output is mapped to simulated reticle motion
- new error is computed from `target - reticle`

The simulation uses a fixed frame step. It must not depend on wall-clock time.

Initial phase constants are fixed in code and recorded in the artifact:

- `frame_dt`
- `sim_frames`
- `measure_from_frame`
- `max_reticle_speed_pps`
- `stick_max`

Initial default values:

- `frame_dt = 1.0 / 60.0`
- `sim_frames = 180`
- `measure_from_frame = 60`
- `max_reticle_speed_pps = 1500.0`
- `stick_max = 32767`

These values are configuration, not hidden assumptions. Every run records them.

## Scenario Model

The pipeline uses lightweight **event-level manifests**, not per-frame random traces.

Each scenario is defined by:

- `scenario_key`
- `kind`
- `initial_state`
- `turn_events`
- `decel_events`
- `resume_events`

### Initial state

Each scenario records:

- `initial_dx`
- `initial_dy`
- `initial_speed_px_per_sec`
- `initial_heading_deg`

### Turn events

Each turn event records:

- `frame`
- `delta_heading_deg`
- `speed_scale`

Meaning:
- at the given frame, the target heading changes by the specified angle
- `speed_scale` defaults to `1.0` when omitted from authoring input and is always materialized in the stored manifest

### Deceleration events

Each deceleration event records:

- `frame`
- `duration_frames`
- `target_speed_scale`
- `hard_stop`

Meaning:
- starting at the given frame, target speed is reduced over the given duration
- if `hard_stop=true`, the target reaches zero speed during the event

### Resume events

Each resume event records:

- `frame`
- `duration_frames`
- `target_speed_scale`

Meaning:
- after a deceleration or stop, the target may resume movement toward a new speed

### Scenario count

Phase 1 run composition is fixed:

- `steady_turns = 8`
- `turn_then_decel = 8`
- `decel_resume = 8`

Total:

- `scenario_count = 24`

## Randomisation Strategy

Randomness is used only to create the event manifests.

The runner does **not** use random values while executing the scenario itself.

Phase 1 random parameters include:

- initial heading
- initial speed
- turn timing
- turn angle
- whether turn also changes speed
- deceleration timing
- deceleration duration
- deceleration target speed scale
- whether the deceleration is a hard stop
- whether motion resumes after deceleration

The generator uses local RNG instances only. It must not depend on global random state.

## Scenario Families

Phase 1 includes a small, explicit set of families:

### 1. `steady_turns`

Purpose:
- stress turn recovery without strong deceleration effects

Shape:
- moving target
- one or more heading changes
- no hard stop

### 2. `turn_then_decel`

Purpose:
- stress overshoot after direction change followed by slowing motion

Shape:
- moving target
- one turn
- later deceleration or stop

### 3. `decel_resume`

Purpose:
- stress braking and re-acquisition after speed reduction

Shape:
- moving target
- slow-down
- zero or one resume event

Each family contributes a fixed number of scenarios per run so benchmark composition is stable.

## Metrics

Phase 1 stores individual metrics, not one combined score.

### Tracking error

- `mean_error_px`
- `p95_error_px`
- `p99_error_px`

These answer:
- overall tracking quality
- tail-risk quality

### Overshoot

- `overshoot_events`
- `max_overshoot_px`

Overshoot is counted when:
- the error on an axis crosses through zero
- and the crossed magnitude exceeds `2.0 px`

The threshold is recorded in the artifact and documented in the scoreboard.

### Turn recovery

- `mean_recovery_frames_after_turn`

For each turn event:
- measure frames until radial error returns below `6.0 px`

### Deceleration settling

- `mean_settle_frames_after_decel`

For each deceleration event:
- measure frames until radial error stays inside `5.0 px` for `4` consecutive frames

## Baseline Model

The user-defined baseline is the **current gamepad controller version** at the moment this pipeline is introduced.

The first successful benchmark run created with baseline intent becomes the stored baseline entry.

The baseline record includes:

- `baseline_key`
- run timestamp
- git commit if available
- dirty worktree flag
- benchmark configuration snapshot
- controller tuning snapshot
- benchmark metrics

If the worktree is dirty, the artifact must store `dirty=true`. The pipeline does not pretend the baseline came from a clean revision.

## Result Storage

Every benchmark run writes two outputs.

### 1. JSON artifact

Path:

`artifacts/benchmarks/gamepad/<run_key>.json`

Contents:

- `run_key`
- `baseline_key` set to `null` when no baseline exists yet
- timestamp
- git metadata
- benchmark configuration
- controller configuration snapshot
- aggregate run metrics
- relative deltas versus baseline
- per-scenario manifests
- per-scenario metrics

### 2. Markdown scoreboard

Path:

`docs/project/GAMEPAD_BENCHMARKS.md`

Sections:

- `Baseline Definition`
- `Benchmark Parameters`
- `Scenario Logic`
- `Latest Run`
- `History vs Baseline`

The Markdown file stores summary information only. It references keys and artifact paths instead of duplicating full manifests.

## Replay by Key

Replay is based on stored manifests, not on re-running the random generator.

This keeps replay stable even if scenario generation logic changes later.

### Run replay

Supported by:

`python tools/run_gamepad_benchmark.py --replay-run-key <run_key>`

Behavior:
- load the JSON artifact for the given run
- replay every stored scenario manifest
- produce a fresh result summary

### Scenario replay

Supported by:

`python tools/run_gamepad_benchmark.py --replay-scenario-key <scenario_key>`

Behavior:
- locate the scenario manifest inside stored artifacts
- replay only that scenario
- print or store the scenario metrics

## CLI Workflow

The benchmark runner supports these flows.

### 1. Create baseline

`python tools/run_gamepad_benchmark.py --set-baseline`

Behavior:
- run the full suite
- write JSON artifact
- mark the run as the baseline in the scoreboard

### 2. Normal benchmark run

`python tools/run_gamepad_benchmark.py`

Behavior:
- run the full suite
- compare against stored baseline
- append a new history entry

### 3. Replay by key

- `--replay-run-key`
- `--replay-scenario-key`

Behavior:
- load stored manifest data
- replay deterministically

## Files

### New files

- `tools/run_gamepad_benchmark.py`
- `tests/gamepad/benchmark_scenarios.py`
- `tests/gamepad/benchmark_metrics.py`
- `tests/gamepad/benchmark_scoreboard.py`
- `tests/gamepad/test_gamepad_benchmark_scenarios.py`
- `tests/gamepad/test_gamepad_benchmark_metrics.py`
- `tests/gamepad/test_gamepad_benchmark_scoreboard.py`
- `docs/project/GAMEPAD_BENCHMARKS.md`

## Testing Strategy

Add tests for these behaviors:

1. event-level manifests expand into deterministic target motion
2. the same stored manifest replays the same scenario result
3. run keys and scenario keys are stable and unique
4. metric calculations correctly detect:
   - error summaries
   - overshoot events
   - turn recovery
   - deceleration settling
5. scoreboard updates do not corrupt existing baseline/history sections

The benchmark pipeline itself is not the same as unit testing. The unit tests protect the pipeline logic; the runner is the user-facing benchmark entrypoint.

## Error Handling

- If no baseline exists during a normal run:
  - still run the benchmark
  - write the artifact
  - report that no baseline comparison is available yet
- If a replay key does not exist:
  - fail with a clear error
- If the scoreboard file does not exist:
  - create it
- If the artifact directory does not exist:
  - create it

## Rollout

Phase 1:
- implement runner, scenario generator, metrics, and scoreboard
- define the baseline from the current gamepad controller
- verify replay by key

Phase 2:
- tune scenario counts and thresholds using real controller iteration history
- add a lightweight regression gate that rejects severe degradation versus baseline if benchmark usage shows that protection is needed

Future extension:
- reuse scenario and metric modules for a mouse benchmark pipeline
