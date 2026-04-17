# Gamepad Manual-Mix Benchmark Design

**Goal:** Add an independent gamepad benchmark suite that reuses the existing target-motion manifests but injects deterministic simulated player stick input, including both helpful and harmful corrections, so we can measure how well `ADS Snap` and `Body Lock` mix with manual control.

**Primary outcome:** the repository should gain a reproducible "manual-mix" benchmark path that can answer whether the controller still tracks well when the simulated player jitters, over-corrects, or briefly pulls in the wrong direction.

**Scope:**
- Cover the `gamepad` controller only.
- Reuse the existing phase-1 target motion manifests as the target-side input.
- Add a deterministic simulated-manual-input layer that can emit aligned and opposed right-stick input.
- Record both classic tracking metrics and manual/AI mixing metrics.
- Keep artifacts, scoreboard, and replay behavior separate from the existing default benchmark suite.

**Non-goals:**
- No live-game capture or recording of real human controller traces in this phase.
- No controller tuning changes in the same task; this phase adds measurement, not behavior changes.
- No replacement of the existing benchmark scoreboard or baseline history.
- No machine-learned user model.
- No per-frame full-trace artifact by default.

## Problem

The current benchmark pipeline measures a clean closed loop where manual right-stick input always pushes in the direction that reduces the current tracking error. That is useful for measuring pure controller response, but it leaves out the most important real-world question for the new state machine:

- what happens when the player and the AI are not perfectly aligned?

Today the benchmark cannot answer:

- whether `Body Lock` still feels cooperative when the player briefly counter-steers
- whether the controller recovers gracefully from wrong-direction manual bursts
- whether small near-target manual wobble causes the AI to amplify noise or absorb it
- whether a parameter change makes mixed-input handling better or worse, even if pure tracking metrics improve

The missing pieces are:

- a deterministic simulated-manual-input model
- explicit conflict-aware metrics
- a separate artifact and scoreboard path so the existing baseline stays comparable

## Approaches Considered

### 1. Replace the current benchmark with a randomized manual-input benchmark

Pros:
- one benchmark command covers everything
- every run includes mixed-input behavior

Cons:
- destroys comparability with the existing baseline history
- makes debugging harder because the old "clean tracking" signal disappears
- too disruptive for the current tuning workflow

### 2. Add an independent manual-mix suite that reuses the existing target manifests

Pros:
- keeps the current benchmark stable
- lets us compare clean tracking and mixed-input behavior side-by-side
- easy to iterate on manual/AI coexistence without rewriting the existing benchmark
- can still share scenario generation and replay patterns with the main suite

Cons:
- adds another suite and another scoreboard
- needs a small amount of runner plumbing

### 3. Hand-author manual input events directly into every benchmark manifest

Pros:
- highly explicit scenarios
- very easy to debug one exact edge case

Cons:
- high authoring cost
- scales poorly once we want more than a handful of manual-input situations
- too rigid for searching across a broader mix of player mistakes

## Chosen Design

Choose **Approach 2**.

We will add an independent **manual-mix benchmark suite** that:

1. reuses the existing target motion manifests
2. injects deterministic seeded manual right-stick input using a small stateful input model
3. runs the existing controller logic unchanged
4. reports both tracking quality and mixing-specific metrics
5. writes to a separate artifact directory and separate Markdown scoreboard

This preserves the current benchmark as the "clean closed-loop controller" signal and adds a second signal for "controller under mixed human input."

## Architecture

The design is split into four bounded pieces.

### 1. Manual input model

New module:

- `tests/gamepad/manual_mix_inputs.py`

Responsibilities:

- generate deterministic manual right-stick input for each simulated frame
- use stable seeds so the same target manifest and user seed always reproduce the same sequence
- emit lightweight annotations for notable manual-input events such as opposing bursts

This module owns all "fake player" behavior. It must not know anything about AI tuning internals beyond the current frame error and recent target-motion context.

### 2. Manual-mix metric evaluator

New module:

- `tests/gamepad/manual_mix_metrics.py`

Responsibilities:

- run a closed-loop simulation using:
  - target motion from an existing manifest
  - manual input from the manual input model
  - controller output from the current `AIAimPlugin`
- preserve the core tracking metrics already used by the main suite
- compute new conflict-aware metrics
- support replay by stored run key and scenario key

This module is parallel to `tests/gamepad/benchmark_metrics.py`, not a replacement for it.

### 3. Suite-aware runner

Modified module:

- `tools/run_gamepad_benchmark.py`

Responsibilities:

- accept a suite selector such as `phase1` or `manual-mix`
- keep `phase1` as the default for backward compatibility
- route manual-mix runs to their own artifact and scoreboard destinations
- keep replay behavior deterministic for each suite

The user-facing workflow stays familiar, but the outputs remain isolated.

### 4. Manual-mix scoreboard

New document:

- `docs/project/GAMEPAD_MANUAL_MIX_BENCHMARKS.md`

Responsibilities:

- define the current manual-mix baseline
- show benchmark parameters and manual-input model assumptions
- show latest run and historical runs for the manual-mix suite

## Manual Input Model

The manual input model must be **seeded, deterministic, and stateful**. Pure white-noise input is not acceptable because it is too unrealistic and too hard to debug.

The model will use the current frame context:

- `error_x`
- `error_y`
- current scenario kind
- whether a turn or deceleration event has recently occurred
- recent manual-input state

The model emits `manual_right_x` and `manual_right_y` with a bounded set of behavioral modes.

### Behavioral modes

The first version should include these modes:

- `aligned_follow`
  - manual input generally moves in the error-reducing direction
  - strongest when the error magnitude is large

- `corrective_wobble`
  - near the target, manual input makes smaller alternating corrections
  - used to simulate a player making tiny adjustments rather than holding one perfect value

- `opposing_burst`
  - a short wrong-direction pull, usually triggered around turns, deceleration, or near-target corrections
  - lasts for a small deterministic number of frames

- `overshoot_recover`
  - after an over-strong aligned input or a burst, manual input briefly reverses to recover

- `vertical_jitter`
  - small Y-axis wobble around the upper-body lock point
  - used to test whether `Body Lock` amplifies or calms vertical instability

### Determinism rules

The generator must be reproducible from:

- suite run key
- target manifest scenario key
- manual-input seed

The same triple must always produce the same manual input sequence.

### Event annotations

The generator should annotate at least these events:

- start and end frame of each `opposing_burst`
- start and end frame of each `overshoot_recover` window

These annotations are needed for conflict-aware metrics and for replay inspection.

## Manual-Mix Scenario Model

The manual-mix suite reuses the existing target motion manifests generated by `tests/gamepad/benchmark_scenarios.py`.

It adds a second dimension:

- `manual_seed`

Each evaluated scenario in the manual-mix suite is therefore:

- one target-motion manifest
- one manual-input seed

The first version should keep the seed count intentionally small so runtime stays practical. A good initial default is:

- `3` manual-input seeds per target-motion manifest

This keeps the suite large enough to expose mixing problems without becoming expensive to replay.

## Closed-Loop Simulation Rules

The simulation should remain structurally similar to the existing benchmark:

1. expand the target-motion manifest into per-frame target positions
2. compute current reticle error
3. ask the manual input model for manual right-stick input
4. build `GamepadFrame` using that manual input plus compact controller target metadata
5. run the plugin under test
6. update reticle position from the combined output
7. store frame-level records needed for metrics

The simulation must additionally record:

- manual stick components
- final output stick components
- derived AI stick components when available by subtraction (`output - manual`)
- active manual-input mode for the frame
- whether the frame is inside an annotated opposing-burst window

## Metrics

The manual-mix suite should preserve the current core metrics:

- `mean_error_px`
- `p95_error_px`
- `p99_error_px`
- `overshoot_events`
- `max_overshoot_px`
- `mean_recovery_frames_after_turn`
- `mean_settle_frames_after_decel`

It should add these mixing-specific metrics.

### 1. `conflict_frames_ratio`

Definition:

- the fraction of measured frames where:
  - manual X input and inferred AI X contribution have opposite signs
  - both magnitudes exceed a small threshold

Purpose:

- estimate how often the player and AI are actively fighting each other instead of reinforcing the same movement

Lower is not automatically better, but large unexplained spikes are suspicious.

### 2. `wrong_input_recovery_frames`

Definition:

- for each annotated `opposing_burst`, count frames from burst start until radial error returns below a recovery threshold or resumes shrinking consistently for a short consecutive window

Purpose:

- measure how quickly the controller stabilizes after a simulated player mistake

Lower is better.

### 3. `manual_yield_score`

Definition:

- computed only during annotated `opposing_burst` frames
- compare inferred AI X magnitude against manual X magnitude
- score should trend toward `1.0` when AI yields more to opposing player input and toward `0.0` when AI keeps overpowering the player

A simple first version is:

- `mean(clamp(1 - abs(ai_x) / max(abs(manual_x), 1), 0, 1))`

Purpose:

- quantify whether the controller gives the player room to break out of sticky behavior during explicit counter-steer moments

Higher is more yielding.

## Artifact and Scoreboard Layout

The manual-mix suite must be stored separately from the current benchmark suite.

New defaults:

- artifact directory:
  - `artifacts/benchmarks/gamepad_manual_mix/`
- scoreboard:
  - `docs/project/GAMEPAD_MANUAL_MIX_BENCHMARKS.md`

Artifacts should include:

- run key
- suite key
- benchmark config
- manual-input config
- manual seeds used
- aggregate metrics
- relative deltas versus the manual-mix baseline
- scenario payloads containing:
  - original target manifest
  - manual seed
  - per-scenario metrics

## Testing

The implementation should add focused tests for:

- manual-input determinism
  - same manifest plus same seed yields identical manual input frames

- event generation sanity
  - opposing bursts and recovery windows can actually occur under deterministic seeds

- conflict metric correctness
  - synthetic frame records produce the expected `conflict_frames_ratio`

- yield metric correctness
  - synthetic frame records produce the expected `manual_yield_score`

- recovery metric correctness
  - synthetic burst windows produce the expected `wrong_input_recovery_frames`

- runner routing
  - manual-mix runs write to the separate artifact directory and separate scoreboard

## Migration Notes

- The existing benchmark remains the default suite and its command-line behavior should continue to work unchanged unless the user opts into the manual-mix suite.
- The manual-mix suite is intended first as a tuning and investigation tool, not as a replacement for the clean benchmark baseline.
- Controller behavior changes such as `Body Lock` yield logic should be implemented only after this suite exists and can measure their effect.
