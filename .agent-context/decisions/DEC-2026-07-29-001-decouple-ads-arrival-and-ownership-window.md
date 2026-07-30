# Decouple ADS arrival planning and ownership window

Status: proposed
Date: 2026-07-29
Confirmed by: not yet confirmed; direction proposed by the user
Related sessions: 2026-07-29 player POV motion benchmark follow-up
Related files:

- `native/controller_native/ads_acquisition_controller.cpp`
- `native/controller_native/native_gamepad_controller.cpp`
- `native/controller_native/target_coordinator.cpp`
- `native/controller_native/runtime_config.cpp`
- `artifacts/benchmarks/player-motion-20260729/SUMMARY.md` on
  `codex/player-motion-benchmark-20260729`

Supersedes: none
Superseded by: none

## Context

COD sprint-to-fire latency can leave roughly 35-200 ms between movement and
fire readiness. The user proposed a small ADS positioning delay (starting near
30 ms), while allowing the ADS acquisition phase to remain active longer
(example 220 ms) even if the position controller is planned around a faster
arrival (example 120 ms).

The current implementation conflates two meanings:

- `snap_duration_ms` becomes the response-model arrival horizon.
- The same value becomes `TargetCoordinatorConfig::ads_snap_window_ms` and
  ends ADS ownership.

`max_acquisition_ms` already exists in config, defaults to 220 ms, is parsed
and passed to the coordinator, but is not used by the mode-transition logic.

## Decision

Evaluate a three-parameter ADS timing model before changing production:

1. configurable initial response delay or authority ramp, starting near 30 ms;
2. independent position-arrival horizon, with 120 ms as the first candidate;
3. independent ADS ownership ceiling, with 220 ms as the first candidate.

The ownership ceiling must remain an upper bound. A stable capture set may
still hand off to BodyLock early. This record does not authorize production
actuation yet.

## Reasons

- Separates physical timing, control effort and lifecycle ownership.
- Reuses the existing single-owner architecture and the currently unused
  `max_acquisition_ms` instead of adding another output gate.
- Can use the run-to-fire interval for target confirmation and smoother
  positioning without forcing the solver itself to become slow.
- Gives the new POV-motion matrix a direct ADS timing dimension.

## Rejected alternatives

- Keep one duration for arrival and ownership: preserves the current
  conflation and cannot express 120 ms arrival with 220 ms ownership.
- Add a universal hard 30 ms zero-output gate immediately: may create a delayed
  impulse and penalize non-sprint or low-latency weapons.
- Maintain a weapon-name delay table: conflicts with the established
  weapon-independent runtime boundary.
- Extend ADS ownership by preventing early handoff: risks restoring the old
  ADS-to-BodyLock overshoot problem.

## Evidence

- The player POV matrix shows combined player/enemy motion cuts tracking about
  43-50% and that mixed input remains helpful.
- Code inspection shows the arrival horizon and ownership window both derive
  from `ads_snap_window_ms`.
- `ads_max_acquisition_ms` is present but unused in TargetCoordinator mode
  transition.
- AutoFire already has a separate minimum ADS-ready time, so firing readiness
  should not be silently encoded by slowing the aim solver.

## Consequences

- Benchmark must report first-output latency, time-to-first-entry, time to
  stable capture, handoff residual, overshoot, continued push, misses and
  AutoFire-before-ready events.
- Initial experiments should compare hard delay, smooth authority ramp and
  movement-intent-conditioned delay.
- No weapon database or additional Vision inference is required.

## Review triggers

- A paired benchmark shows one timing model improves acquisition/handoff
  burden across static, moving, strafe, slide, jump and combined cohorts.
- Live logs identify a reliable sprint/slide intent signal suitable for
  conditioning.
- A universal delay measurably harms close non-sprint acquisition or the
  fastest weapon class.
- Existing ADS/BodyLock lifecycle semantics change.
