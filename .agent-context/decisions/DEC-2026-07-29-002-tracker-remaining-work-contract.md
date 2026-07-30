# DEC-2026-07-29-002: Tracker Publishes Remaining Control Work

Status: accepted
Date: 2026-07-29
Confirmed by: user
Related sessions: 2026-07-29 controller-rate, player-POV and mouse-position
comparison discussion
Related files:

- `docs/project/TRACKER_REMAINING_WORK_CONTROL_PLAN_20260729.md`
- `native/pipeline_contract/target_plan.h`
- `native/controller_native/target_coordinator.cpp`
- `native/controller_native/native_gamepad_controller.cpp`
- `native/controller_native/pending_control_motion.cpp`

Supersedes: none
Superseded by: none

## Context

The project has repeatedly added target prediction, ADS planning, BodyLock
following, causal player-motion hints, pending-motion experiments and handoff
metrics. These capabilities improved parts of the system but did not fully
express the user's original tracker goal.

The original product goal was a tracker that determines the net movement still
required to aim at and follow a target. The implemented tracker primarily
estimated target position and motion, leaving ADS and BodyLock to reconstruct
control demand locally. Delivered manual and AI camera motion was not
consistently subtracted from controller-rate target propagation.

This mismatch allowed relative screen motion caused by the camera to be treated
as continued target motion. It is a common cause candidate for lazy recovery,
obsolete directional push, player-POV errors and ADS-to-BodyLock debt.

## Decision

The tracker/`TargetCoordinator` contract will be control-oriented.

Its primary controller-facing displacement is the two-dimensional remaining
work required to complete the current aim task:

```text
remaining work
= latest authoritative Vision error
+ exogenous target and player-POV motion since capture
- camera motion actually delivered since capture
```

ADS and BodyLock will consume the same remaining-work state with different
mode-specific rates and motion lead. They will not independently recreate the
same displacement or introduce another shared brake owner.

Fresh Vision remains authoritative. Prediction bridges missing evidence but
cannot duplicate delivered work. Low-confidence remaining work falls back to
the existing controller behavior.

The detailed field names, confidence thresholds and rollout values remain
implementation details to be validated by the linked plan. Acceptance of this
decision does not claim that a production implementation already exists.

## Reasons

- It matches the user's intended tracker product rather than treating the
  tracker as only a target-state filter.
- It gives manual input, AI output, player motion and target motion one common
  accounting unit: remaining screen displacement.
- It uses the gamepad's ability to stop output immediately without pretending
  that a gamepad can teleport through its maximum angular-velocity limit.
- It provides a single place to prevent duplicate work and obsolete push.
- It preserves the existing one-owner architecture.
- Existing delivered-output history, response estimation, lifecycle and
  benchmarks can be reused.

## Rejected Alternatives

### Continue adding local ADS/BodyLock gates

Rejected because local gates do not establish how much work has already been
completed and recreate the duplicated ownership removed by Refactor B.

### Restore the legacy high-restriction mix stack

Rejected because prior exploration produced weak general gains and severe
ordinary-cohort overshoot or user-fight regressions.

### Use `left_x * constant` or weapon tables

Rejected because ADS movement response varies with weapon, sensitivity,
slowdown and state. The project retains memory-only online response estimation
and fresh-Vision correction.

### Treat X and Y as independent arbitration systems

Rejected because the required work, user input and target movement are vector
relationships. Axis-independent gates can suppress one component without
correcting the intended direction.

### Add a mouse-like teleport controller

Rejected for production because a gamepad remains bounded by angular velocity.
The useful part of the mouse comparison is direct displacement accounting, not
unbounded instantaneous movement.

### Make the shadow causal learner a second production planner

Rejected because it would create another control owner before its mixed-motion
response prediction is valid.

## Evidence

- User clarification: the intended tracker must calculate the required work
  distance; prior plan and causal terminology did not fully capture that goal.
- Repository fact: `TargetPlan` contains target state and terminal prediction
  but no authoritative remaining-work vector.
- Repository fact: `TargetCoordinator` propagates screen-relative velocity
  between observations and only uses previous delivered stick in a terminal
  estimate.
- Repository fact: `PendingControlMotion` can integrate delivered pre-recoil
  output but is currently restricted to benchmark fusion paths.
- Retained left-strafe evidence showed tracking -12.17%, overshoot area
  +68.16% and continued obsolete push +461.90% versus no strafe.
- Player-POV benchmarks showed combined player/enemy movement cuts tracking by
  roughly 43–50%.
- Live telemetry found BodyLock center-crossing debt with old-direction final
  output after crossing.
- A simplified matched simulation showed that shorter hand-controller horizons
  or full-stick pulses provide limited score gains and sharply increase
  directional switching, while ideal mouse positioning mainly benefits from
  unbounded displacement rather than braking.

## Consequences

- `TargetPlan` becomes a stronger control contract.
- Delivered-output accounting moves from benchmark-only support toward
  production shadow state.
- Tracker velocity semantics must distinguish exogenous motion from known
  delivered camera motion to avoid double subtraction.
- ADS and BodyLock implementations become consumers of a shared work vector.
- Existing terminal-error and handoff logic may become redundant, but cleanup
  occurs only after live acceptance.
- Telemetry and benchmarks must expose work anchor, delivered motion, remaining
  work, confidence and reset reason.
- Implementation requires a fallback path and broad matched regression matrix.

## Review Triggers

Review or supersede this decision if:

- delivered camera displacement cannot be estimated with sufficient
  confidence under strong/weak slowdown;
- remaining-work shadow error is worse than raw stale error on held-out
  scenarios;
- the contract requires a second planner or post-controller gate;
- ordinary ADS acquisition or correct manual adjustment regresses materially;
- future input APIs provide true camera pose or direct angular displacement,
  changing the response-estimation boundary.
