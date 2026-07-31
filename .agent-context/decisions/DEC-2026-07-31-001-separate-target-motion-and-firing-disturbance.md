# DEC-2026-07-31-001: Separate Target Motion from Firing Disturbance

Status: accepted
Date: 2026-07-31
Confirmed by: user
Related sessions: 2026-07-31 live firing-range telemetry and benchmark work
Related files:

- `native/controller_native/target_coordinator.cpp`
- `native/controller_native/target_coordinator.h`
- `native/controller_native/sustained_aimlab_simulator.cpp`
- `native/controller_native/target_coordinator_tests.cpp`
- `.agent-context/session-log.md`

Supersedes: none
Superseded by: none

## Context

The remaining-work controller treats fresh target displacement and propagated
target velocity as control debt. During firing, camera/gun-kick motion moves
the detected person on screen even when the physical target and player are
otherwise stationary. The same transient can therefore enter both fresh
position error and velocity prediction.

Three July 31 firing-range videos and aligned telemetry isolated this failure.
Individual 0.2-0.5 second bursts caused five to nine requested AI direction
reversals. Appearance/body anchors reduced box noise but did not distinguish
true target motion from camera recoil. A deliberately wrong horizontal aim
point also showed that a short observation error can leave a BodyLock
correction tail.

## Decision

Target tracking will estimate two causal states:

1. persistent target-relative position and velocity;
2. short-lived firing/camera disturbance.

Fresh observations may correct immediate position, but disturbance evidence
must not be accumulated as persistent target velocity or long-term remaining
work. The implementation must remain one tracker/coordinator owner, use
constant-time per-axis state, require no weapon database, add no visual
inference and add no post-controller brake.

This is an algorithm replacement boundary, not authorization for another stack
of local reversal gates.

## Accepted Implementation

The deployed implementation keeps the current observed position authoritative
and separates only the velocity admission path:

- a new firing residual is compared as one complete two-dimensional vector
  with the preceding residual;
- the first or directionally inconsistent residual cannot enter target
  velocity;
- two consecutive directionally consistent observations restore the existing
  velocity gain without changing ADS or BodyLock strength;
- BodyLock applies this observer only when firing, the appearance anchor is
  absent or below `0.45`, and the established target speed is at most
  `80 px/s`;
- reliable-anchor BodyLock and ordinary moving-target paths retain the prior
  estimator;
- rejected residuals are discarded and never released as later Remaining
  debt.

The implementation also consumes each unique Vision observation only once at
the measurement-update boundary. Controller-rate ticks may propagate state and
delivered camera work, but cannot repeatedly train velocity from one frame.

## Acceptance

The implementation is not complete merely because one metric improves.
Matched benchmark and test evidence must jointly cover:

- stationary firing with gun-kick/body-box disturbance;
- horizontal wrong-aim-point onset and recovery;
- ordinary moving and reversing targets;
- ADS and BodyLock;
- short Vision occlusion;
- mixed/manual and player-motion smoke cohorts.

Firing-time oscillation must be removed at its state-estimation source.
Ordinary target tracking strength and true reversal response must remain
usable. BodyLock must not gain lazy tracking, obsolete continued push, or a
long wrong-way correction tail. Only a build passing all guardrails may be
copied to the user-facing runtime.

## Rejected Alternatives

### Globally lower prediction or Remaining strength

Rejected because it reduces firing jitter by making valid target tracking lazy.

### Let a fresh appearance anchor bypass the firing innovation cap

Rejected because position and velocity compensate the same movement, raising
BodyLock overshoot.

### Zero or ramp velocity feed-forward while firing

Rejected because it roughly halved some oscillation measures but materially
increased ordinary BodyLock error.

### Confirm every reversal for two frames

Rejected because variants traded local ADS gains for BodyLock continued-push,
wrong-way tail, or extra reversals after a biased observation.

## Evidence

- Live session:
  `runs/native_perf/sessions/20260731T050901Z_59948_1/native_runtime_telemetry_0000f5e33be6129cfc4c6628ae1f4a8c_0.jsonl`
- Videos:
  `Content 2026.07.31 - 13.09.10.140.mp4`,
  `Content 2026.07.31 - 13.09.37.141.mp4`,
  `Content 2026.07.31 - 13.10.29.142.mp4`
- Per-video requested direction reversals reached 30/39, 15/20 and 18/18 on
  horizontal/vertical axes; raw target and remaining-work reversals were also
  frequent.
- The retained horizontal-aim-bias fixture keeps the true target stationary,
  biases the observation by 22 px for 150 ms, then recovers over 90 ms.
- All experimental controller behavior was rolled back. The production
  double-click runtime remained at SHA-256
  `2468DE28BAA0E4F043376F313A1204BCABCFAE948A54F2876701F3B2AC337D40`
  at the decision point.
- Final three-seed wrong-aim recovery A/B reduced ADS requested/shaped
  reversals from `94.3/44.0` to `13.0/4.7`, and BodyLock from `73.7/44.0` to
  `5.7/1.0`. False interruption and false stop remained zero.
- Stationary firing reduced ADS overshoot area by 2.3%, continued push by
  3.4%, and oscillation output area by 4.2%; BodyLock continued push improved
  4.8% with no tracking-strength change.
- Moving target plus 36 ms occlusion improved ADS tracking 1.6% and continued
  push 7.7%; ADS overshoot area rose 5.7% without additional shaped reversal,
  false stop or interruption. BodyLock aggregate score remained effectively
  unchanged.
- Mixed full-reversal left-strafe reduced ADS overshoot 2.3% and shaped
  wrong-way time 40.5%; BodyLock overshoot rose 1.1% and continued push by
  0.66 ms while requested/shaped reversal fell 18.2%/50%.
- Deployed runtime SHA-256:
  `80831B5197B8AC58C20844A349BD673FCC21B2768A055AB4446911257BA70F50`.

## Consequences

- Benchmark control needs an explicit observer off/on switch for causal A/B.
- Telemetry should expose disturbance state and the innovation admitted into
  target velocity.
- Existing appearance anchor and firing evidence may inform the observer, but
  they cannot become separate control owners.
- Existing strength settings remain unchanged during algorithm acceptance.
- The previous runtime is retained under
  `artifacts/runtime-backups/20260731-150805`.

## Review Triggers

Review or supersede this decision if:

- the observer requires weapon identity, persistent calibration, more Vision
  inference or a second output owner;
- real camera pose becomes available and directly identifies recoil motion;
- ordinary moving/reversing targets regress to recover stationary firing;
- firing disturbance still enters long-term Remaining debt.
