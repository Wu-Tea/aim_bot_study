# Native AimLab-Style User Intent Benchmark Design

Date: 2026-07-06
Status: proposed
Owner: Codex/user discussion

## Summary

Build a pure data benchmark for the native C++ gamepad pipeline that scores how well user input, vision selection, tracker memory, and controller output cooperate in multi-target situations.

The next runtime direction is:

1. pass user right-stick intent into native vision/selector;
2. use an AimLab-style synthetic benchmark to score target selection and controller cooperation;
3. defer any user-input-based vision image cropping until intent-aware selection is measurable.

This benchmark has no UI and does not render images. It runs deterministic scenarios, emits JSON/summary metrics, and is meant to compare commits and controller/selector changes.

## Problem

Current tests can measure isolated behavior such as ADS acquire speed, overshoot, smoothing, or bodylock tracking. They do not fully answer the live question:

> Is the system helping the user aim at the target the user intended to shoot?

Live failures can happen when multiple candidates exist. For example, the user may pull toward a close lower-left side-running enemy, while selector/ADS chooses a farther upper-right front-facing target. A benchmark must know the intended target so it can penalize wrong strong locks, not just aim error.

## Goals

- Create a deterministic synthetic multi-target benchmark with seed-based reproducibility.
- Feed generated detections into the real native selector/tracker/controller path as much as practical.
- Score target selection, controller behavior, vision/controller cooperation, smoothness, and authority safety.
- Make wrong-target ADS snap, corpse lock, stale strong control, and controller/user fighting visible as numbers.
- Provide a stable A/B comparison tool for the next userInput-to-vision changes.

## Non-Goals

- No graphical UI.
- No AimLab-like rendered game surface.
- No real model inference in the first version.
- No hard vision image crop in the first version.
- No broad config explosion; defaults should be fixed and reproducible.

## Architecture

```text
ScenarioGenerator
  -> SyntheticWorldFrame
  -> VisionStub
  -> VisionTargetSelector
  -> TargetTracker
  -> NativeGamepadController
  -> ReticleSimulator
  -> ScoreAggregator
  -> JSON/Summary Report
```

### ScenarioGenerator

Generates ground-truth target state, user input, target intent, and scripted events.

Each frame should include:

- target id
- target position and velocity
- size and body pose
- alive/dead/friendly/unknown state
- cue score and cue visibility
- occlusion state
- intended target id
- right-stick user input
- ADS/bodylock mode

### VisionStub

Converts ground truth into synthetic vision detections. It should support:

- position noise
- delayed updates
- dropped detections
- low-confidence detections
- wrong target injection
- corpse-like lingering detections
- cue loss

The first version uses synthetic detections only. A future replay path can feed real logs into the same scorer.

### Pipeline Under Test

The benchmark should use the real native selector/tracker/controller interfaces where practical. The benchmark is intended to expose pipeline cooperation, not a separate toy controller.

### ReticleSimulator

Applies controller output to a simulated reticle position. The simulator only needs enough fidelity to compare A/B behavior:

- apply right-stick output to reticle velocity;
- track target-relative error;
- measure acquire time, overshoot, and time-on-target.

## Scoring

The benchmark produces component scores plus a final score.

```text
final_score =
  0.30 * selection_score
+ 0.30 * control_score
+ 0.20 * cooperation_score
+ 0.10 * smoothness_score
+ 0.10 * authority_safety_score
- hard_penalties
```

### Selection Score

Measures whether vision/selector chose the user-intended target.

Metrics:

- intended target selected ratio
- time to select intended target
- wrong strong lock frames
- wrong target switch count
- opposite-intent lock frames
- corpse/friendly/unknown strong lock frames

### Control Score

Measures whether controller moves the reticle onto the intended target.

Metrics:

- time to acquire intended target
- average aim error
- time-on-intended-target ratio
- overshoot count
- overshoot over 50px count
- max overshoot px
- settle time after reaching target

### Cooperation Score

Measures whether controller output helps reduce error to the intended target.

Metrics:

- helpful output ratio
- frames where AI output increases intended target error
- user fight frames where AI output opposes clear user correction
- stale-vision strong-control frames
- recovery time after err target injection

### Smoothness Score

Measures whether output is mechanically plausible and stable.

Metrics:

- output direction reversal count
- high-frequency jitter score
- sudden force spike count
- abrupt zeroing count
- near-target tremor frames

### Authority Safety Score

Measures whether strong control is granted only to valid targets.

Metrics:

- weak target strong snap frames
- cue-hold strong snap frames
- predicted-only strong snap frames
- corpse strong lock frames
- friendly/unknown strong lock frames
- stale target strong lock frames

## First Scenario Set

1. `multi_target_flick`
   - Several targets spawn around center.
   - User input points toward one intended target.
   - Expected: selector prefers the intended target or avoids strong snap to others.

2. `near_side_vs_far_front`
   - Close lower-left side-running target competes with a farther upper-right front-facing target.
   - User input points lower-left.
   - Expected: no ADS strong snap to the far target.

3. `ads_diagonal_pull`
   - Fast diagonal ADS acquire.
   - Expected: low large-overshoot count and quick settle.

4. `moving_track`
   - Target moves continuously with acceleration and direction changes.
   - Expected: bodylock/ADS follow without sticky stalls or excessive brake.

5. `slide_occlusion_delay`
   - Target slides downward, briefly occludes, and vision runs with delayed updates.
   - Expected: tracker memory helps briefly without wrong strong lock.

6. `corpse_cue_loss`
   - Target dies; box/color may linger but cue disappears.
   - Expected: no corpse strong lock or fire authority.

7. `err_target_recovery`
   - Vision injects a random wrong candidate for a short interval.
   - Expected: selector/tracker/controller recover without a long wrong pull.

## Output

The benchmark writes a machine-readable report and a concise console summary.

Suggested report fields:

```json
{
  "seed": 12345,
  "duration_sec": 60,
  "final_score": 87.2,
  "selection_score": 91.5,
  "control_score": 84.0,
  "cooperation_score": 86.8,
  "smoothness_score": 82.1,
  "authority_safety_score": 93.0,
  "wrong_target_ads_snap_count": 1,
  "corpse_lock_frames": 0,
  "overshoot_over_50px_count": 3,
  "time_to_acquire_ms_p50": 118,
  "time_to_acquire_ms_p95": 210,
  "time_on_intended_target_ratio": 0.78,
  "helpful_output_ratio": 0.86,
  "user_fight_frames": 14
}
```

Default output location should follow existing benchmark conventions, preferably under `runs/native_perf/` unless the repository already standardizes this benchmark under another artifact directory.

## Acceptance Criteria

- Benchmark is deterministic with a fixed seed.
- Benchmark runs without UI or image rendering.
- Benchmark reports per-scenario and aggregate scores.
- Benchmark can detect a wrong strong lock in `near_side_vs_far_front`.
- Benchmark can detect overshoot over 50px in `ads_diagonal_pull`.
- Benchmark can detect corpse strong lock in `corpse_cue_loss`.
- Benchmark can detect controller/user fighting through `helpful_output_ratio` and `user_fight_frames`.
- First implementation includes focused native tests for scorer math and at least one regression scenario.

## Later Work

- Add real vision log replay using the same score aggregator.
- Add soft ROI planning after user intent selection behavior is stable.
- Add comparison reports across two commits or two benchmark JSON files.
- Add richer trace output for frame-level debugging only when needed.
