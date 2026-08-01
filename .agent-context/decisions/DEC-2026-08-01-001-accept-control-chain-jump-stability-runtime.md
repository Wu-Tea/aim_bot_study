# DEC-2026-08-01-001: Accept the Control-Chain Jump-Stability Runtime

Status: accepted
Date: 2026-08-01
Confirmed by: user live test and explicit context-sync request
Related sessions: 2026-08-01 Task 1–4 review, repair, build, installation and live test
Related files:

- `docs/project/CONTROL_CHAIN_JUMP_STABILITY_ACCEPTANCE_20260801.md`
- `docs/superpowers/plans/2026-08-01-ads-bodylock-jump-stability-repair.md`
- `artifacts/benchmarks/control-chain-jump-fix-20260801/codex-final-ab/VERIFICATION.md`
- `artifacts/benchmarks/control-chain-jump-fix-20260801/task4-ownership-admission/VERIFICATION.md`
- `native/controller_native/vector_intent_fuser.cpp`
- `native/controller_native/aim_dynamics_shaper.cpp`
- `native/controller_native/target_coordinator.cpp`

Supersedes: none
Superseded by: none

## Context

The prior experimental runtime produced severe ADS pulls, large BodyLock jumps
and frequent moving-follow jitter. Task 1–3 repaired duplicate reliability
gating, ADS-to-BodyLock state carry and selector-owned identity admission.
Conditional Task 4 then bounded replacement-target admission within a held LT
epoch without clamping fresh Vision or weakening normal same-track tracking.

The user authorized overwriting the current runtime, tested the resulting build
in gameplay and reported that the original failures no longer made the runtime
unusable.

## Decision

Accept runtime SHA-256
`B7F9F28B6A39E1AE58DB75C5F3A3A18DDF3D9692886245ABF2C5493FDC84140C`
as the current stable control-chain baseline.

Freeze its control parameters and ownership contracts until new reproducible
live evidence justifies a change. Treat Task 1–4 as complete and live accepted.
Preserve the archived candidate and pre-Task-4 runtime backup as rollback
evidence.

## Reasons

- The user confirmed that ADS no longer exhibits the severe sudden pull.
- BodyLock follows a stationary target while the player moves the main view.
- Moving follow no longer has the prior high-frequency jitter.
- The user observed that later acquisitions in the session were nearly direct
  to the person instead of repeatedly drifting or shaking.
- All 34 registered tests and focused controller tests pass.
- Matched A/B stays inside tracking, overshoot, continued-push, interruption,
  stop and output-delta guardrails without lowering assist strength.

## Rejected Alternatives

### Continue tuning strength immediately

Rejected because the accepted runtime already resolves the live defect, while
unnecessary gain changes could erase the verified speed/continuity balance.

### Revert to the pre-Task-4 runtime

Rejected because that runtime was explicitly described as unusable and lacks
the held-LT replacement-target admission contract.

### Enable shadow rollout as a production controller

Rejected because `rollout_shadow` is observation-only and its mixed-motion
decision confidence has not earned actuation authority.

### Clamp every fresh target-position innovation

Rejected because it would make legitimate close lateral movement lazy and
create a second position authority.

## Evidence

- User-confirmed live behavior recorded in
  `docs/project/CONTROL_CHAIN_JUMP_STABILITY_ACCEPTANCE_20260801.md`.
- Task 4 matched tracking `+0.0343%`, overshoot `-0.0712%`, continued push
  unchanged at `245 ms`, false interruptions unchanged at `9`, and false stops
  unchanged at `0`.
- Production-chain defect count remains `0`; reacquisition output delta remains
  bounded at `0.064`.
- Current config, effective config and TensorRT engine identities are retained
  in the acceptance record.

## Consequences

- New sessions should read the August 1 acceptance record before reopening old
  July overshoot or tracker experiments.
- The current runtime is the comparison baseline for future live evidence.
- The top-level causal learner remains shadow-only. The controller's in-memory
  `AimResponseEstimator` remains part of the accepted runtime.
- Runtime replacement requires the same identity, backup and live-feel boundary.

## Review Triggers

Review or supersede this decision if:

- ADS sudden pulls, BodyLock large jumps or high-frequency moving-follow jitter
  recur on the accepted runtime;
- a new build changes target identity, mode handoff, fuser continuity, response
  estimation, config, crop or engine identity;
- manual escape becomes sticky or legitimate close-target tracking becomes lazy;
- live evidence proves the initial-acquisition warm-up is caused by a distinct
  defect rather than normal session-state confidence establishment.
