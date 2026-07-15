# Left-Stick Relative-Motion Defect Benchmark Design

## Goal

Add a deterministic native benchmark that reproduces and measures the current
controller/tracker behavior when the player strafes with the left stick while a
target is stationary, moves in the same direction, or moves in the opposite
direction.

The benchmark is evidence-only. It must not change production tracker,
controller, authority, recoil, or vision behavior.

## Current State

The physical left stick is passed through to the virtual controller, but it is
not part of the tracker control sample. The FPS tracker receives the final right
stick only. The existing native benchmark helper initializes right-stick input
but leaves the left stick at zero.

Consequently, the current system sees left-stick-induced target-box motion as
ordinary target motion. It cannot react to a left-stick start, release, or
reversal until that change appears in a later vision observation.

## Live-Evidence Supplement

The recording `Content 2026.07.15 - 16.17.02.01.mp4` and the aligned runtime
telemetry session add a second defect class that the original closed-loop
fixture does not exercise.

The evidence is interpreted as follows:

- the observed right-stick magnitude of at most `0.0118` is deadzone/drift
  noise, not an intentional manual correction;
- while ADS is held, `body_lock` owns about 66% of controller samples,
  `ads_snap` owns about 19.5%, and `manual` owns about 14.5%;
- one continuous `manual` interval lasts about 0.91 seconds and has no AI
  contribution to the final right-stick output;
- within that interval the production target is unavailable for about 0.70
  seconds even though detector candidates remain present on almost every
  sample;
- the selected track identity is rebound repeatedly during the ADS interval;
- after reacquisition, requested ADS-snap force can be removed by lifecycle or
  dynamics yield before the final output is sent.

This evidence separates two problems:

1. **Relative-motion under-compensation.** BodyLock owns the target and applies
   force in the correct direction, but the force is too weak or too late to
   cancel player strafe. The original closed-loop fixture covers this problem.
2. **Production-chain assist dropout.** Detector candidates exist, but selector
   ownership, tracker binding, lifecycle state, or final-output dynamics remove
   BodyLock ownership and sometimes remove all assist. The original fixture
   does not cover this problem because it directly submits a permanently strong
   target.

## Approaches Considered

### Direct telemetry replay

Replay recorded `has_target`, authority, mode, and output fields directly at the
controller boundary. This is deterministic, but it reproduces recorded answers
instead of exercising the selector/tracker behavior that produced them.

### Synthetic selector-snapshot replay (selected)

Generate deterministic `ControllerVisionSnapshot` frames with multiple tracker
detections, selector observation ids, a selected-target gap, identity rebinding,
and reacquisition. Submit them through the real target snapshot provider,
tracker, controller, lifecycle, and output dynamics. This keeps the benchmark
independent of a weapon database and extra vision processing while covering the
missing production state chain.

### Full video/vision replay

Run the recorded video through capture, detector, selector, tracker, and
controller. This is the closest end-to-end replay, but it is hardware/model
dependent, slower, and would make a controller regression gate depend on the
vision workload. It is intentionally out of scope.

## Scope

The benchmark will:

- reuse the existing target box and controller interfaces;
- run at a deterministic 100 Hz controller rate and 50 Hz vision rate;
- model delayed vision delivery separately from capture time;
- model slow and fast ADS strafe response without naming or recording weapons;
- model left-stick onset, hold, reversal, and release;
- model stationary, same-direction, and opposite-direction target motion;
- include a simultaneous bounded right-stick correction scenario;
- report right-stick AI/final output, target error, event response, and
  left-intent sensitivity;
- emit a JSON artifact and a non-zero `--require-fixed` gate while defects are
  present.
- add a production-chain dropout probe that uses selector-owned snapshots and
  real tracker binding rather than direct stable-target submission;
- treat right-stick input within `0.02` magnitude as drift/deadzone noise in
  the production-chain probe;
- keep detector candidates populated while deliberately withholding a selected
  production observation during the evidence-matched gap;
- enable normal aim-assist dynamics for the production-chain probe and score
  the final output after lifecycle/dynamics, not only requested AI force.

The benchmark will not:

- add optical flow, background scanning, or another vision model;
- add weapon or attachment mobility data;
- implement left-stick compensation;
- tune current AI/controller constants;
- change small-target authority behavior.
- replay pixels or invoke a detector;
- require weapon, attachment, ADS-mobility, or map data;
- classify deadzone-sized right-stick noise as user correction.

## Architecture

Create a focused module under `native/controller_native` rather than extending
the already large `cod_native_gamepad_benchmark.cpp`.

The module has two probes:

1. **Open-loop intent-invariance probe**
   Feed two controllers the same target observations and the same right-stick
   input. Give only one controller a left-stick onset/reversal/release profile.
   Compare their AI and final right-stick traces. An effectively zero trace
   delta proves that current right-stick behavior does not consume left intent.

2. **Closed-loop relative-motion simulator**
   Simulate player lateral velocity, target lateral velocity, camera movement
   from the real controller output, target-box observations, and delayed vision.
   Run the real `NativeGamepadController` without replacing its target or output.

3. **Production-chain dropout probe**
   Drive the real `ControllerVisionSnapshot` selector-identity path with two or
   more detector candidates. Hold the right stick inside drift tolerance while
   the left stick produces lateral motion. Transition through stable BodyLock,
   target-present/bodylock-unavailable, selected-target gap, reacquisition ADS
   Snap, lifecycle/dynamics yield, and recovered BodyLock. Record both requested
   AI force and final delivered right-stick assist.

The simulation uses body-height-normalized lateral velocity. For a fixed body
box height `h`, relative screen velocity is:

```text
screen_velocity_px_s =
    (target_velocity_body_s - player_velocity_body_s) * h
```

Player velocity follows a first-order response driven by the physical left
stick. Slow and fast mobility fixtures differ only in top speed and response
time. They are benchmark fixtures, not weapon profiles.

## Deterministic Timeline

Each closed-loop run uses 360 controller ticks at 10 ms per tick:

- ticks 0-79: acquire and settle;
- ticks 80-179: strafe right;
- ticks 180-279: reverse to strafe left;
- ticks 280-359: release and coast to zero.

Vision is captured every 20 ms and delivered after a fixed 30 ms delay. The
observation retains its capture timestamp so timing behavior is exercised rather
than hidden.

The production-chain probe uses a separate evidence-matched sequence:

- stable selector-owned BodyLock while the player strafes;
- about 200 ms with a current selected target but BodyLock unavailable;
- about 700 ms with detector candidates present but no selected production
  observation;
- reacquisition under a new selected observation identity;
- ADS Snap and any lifecycle/dynamics suppression caused by the transition;
- recovered BodyLock while strafe continues.

The exact tick boundaries are constants in the fixture and are emitted in the
JSON artifact. No wall-clock timing or video decoding is used by the benchmark.

## Scenarios

- `intent_invariance_same_vision`: identical vision/right input, different left
  intent.
- `stationary_target_slow_ads`: slow player strafe, stationary target.
- `stationary_target_fast_ads`: fast player strafe, stationary target.
- `same_direction_target_fast_ads`: target follows the player's strafe
  direction at a lower normalized speed.
- `opposite_direction_target_fast_ads`: target moves against the player's
  strafe direction.
- `stationary_target_fast_ads_manual_correction`: fast player strafe while the
  right stick performs a bounded proportional manual correction.
- `production_chain_strafe_reacquire`: stationary target plus player strafe,
  multiple detector candidates, selector-owned tracking, evidence-matched
  selected-target loss, identity rebinding, reacquisition, and final-output
  dynamics.

## Metrics

Every closed-loop scenario reports:

- mean, P95, and maximum absolute target error;
- maximum AI and final right-stick magnitude;
- maximum single-tick final-output delta;
- high-AI-output and output-spike frame counts;
- frames where AI opposes committed manual correction;
- frames where final output opposes the oracle correction direction;
- response latency after left-stick onset, reversal, and release;
- peak absolute error in a fixed window after each event;
- settled-frame count and final error;
- a compact event trace around phase boundaries.

The open-loop probe reports:

- maximum AI trace delta;
- maximum final-right trace delta;
- maximum left-output passthrough error;
- number of sampled phase-event frames;
- `left_intent_ignored`.

The production-chain probe reports:

- BodyLock, ADS Snap, and manual dwell frames and mode-transition count;
- time with a current target but BodyLock unavailable;
- time with detector candidates present but no production target;
- continuous duration of drift-only final output while ADS and strafe remain
  active;
- selected-track change count and reacquisition latency;
- requested AI magnitude versus delivered final-assist magnitude;
- frames where requested force is materially suppressed before final output;
- target error immediately before loss and immediately after reacquisition;
- whether the expected evidence phases were actually exercised.

## Defect Classification

The benchmark distinguishes harness correctness from desired behavior:

- normal execution exits zero when fixtures are valid, runs are deterministic,
  and all required metrics are populated;
- JSON records `defect_reproduced` and `desired_gate_pass` for each scenario;
- `--require-fixed` exits non-zero if the left-intent invariance defect or any
  closed-loop desired-behavior gate is still present.
- the production-chain probe is valid only when detector candidates remain
  populated through the selected-target gap and all required lifecycle phases
  occur;
- the production-chain defect is reproduced when a continuous zero-assist gap
  occurs during ADS strafe or when requested force is suppressed to drift-only
  final output during reacquisition;
- the future fixed gate requires bounded reacquisition latency and no prolonged
  drift-only output while candidates and ADS intent remain present.

The first accepted artifact is expected to reproduce defects. That is the RED
baseline for later optimization, not a failing benchmark harness.

## Acceptance

This benchmark-only change is complete when:

- the original native controller, metrics, and gamepad self-tests remain green;
- the new benchmark is deterministic across two runs;
- left-stick output passthrough is preserved in the fixture;
- the open-loop probe demonstrates whether left intent affects AI output;
- the original intent-invariance result and five closed-loop scenarios remain
  present in the JSON report;
- the production-chain scenario appears in the JSON report with populated
  selector/tracker/lifecycle/final-output metrics;
- drift-sized right-stick input is not counted as manual correction;
- a normal benchmark run demonstrates the production-chain defect against the
  current code, while `--require-fixed` remains intentionally RED;
- `--require-fixed` gives an intentional RED result against the current code;
- no production source file is changed.
