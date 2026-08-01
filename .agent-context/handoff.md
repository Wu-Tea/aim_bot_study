# Agent Handoff

Last updated: 2026-08-01
Active scope: native C++ FPS gamepad runtime, target identity, ADS, BodyLock, intent fusion and response estimation.
Staleness trigger: refresh after runtime/config/engine or a control owner changes, or live evidence reproduces ADS pull, BodyLock jump, moving-follow jitter, sticky escape or lazy close tracking.

## Current Objective

Preserve `DE31...` as the current validation baseline while isolating slight ADS
over/under. Separate handoff timing from movement before tuning or blaming learning.

## Current State

- Chain: Vision/selector -> TargetCoordinator -> TargetPlan -> ADS/BodyLock -> AimDynamicsShaper -> VectorIntentFuser -> ADS brake -> recoil/output.
- Task 1–4 removed duplicate gating/state carry, enforced selector no-selection and bounded held-LT replacement admission. User live-accepted removal of severe jumps and frequent moving-follow jitter.
- Follow-up keeps fresh firing position authoritative, separates Coasting actuation from identity hold, and removes same-target `Reacquiring` manual-only cuts.
- Manual and shaped AI are absolute fuser proposals. Strong same-direction ADS/near BodyLock uses complete AI, contextual manual normalization and 20% headroom; tangent/opposing input, full escape and far BodyLock retain ownership.
- Runtime: `native/vision_native/build/Release/cod_native_runtime.exe`, SHA-256 `DE31FF53B4C0CFBAB091F589CB194296A01DC9C0C8B5E74513F90AB94ED30590`.
- Candidate: `artifacts/runtime-candidates/20260801-contextual-dual-proposal-headroom20-DE31FF53/cod_native_runtime.exe`; rollback: `artifacts/runtime-backups/20260801-pre-contextual-dual-proposal-0E5E9A3B/cod_native_runtime.exe`.
- Latest session `20260801T131000Z_6544_1`: 25.51 min, 355 ADS runs, 246 normal handoffs >=20 ms.
- **User-confirmed:** longer play still sometimes ends slightly past or short.
- **Repository evidence:** end error/time `rho=0.070` and response proxy/time `rho=-0.054`; late incidents cluster at the physical-epoch `220 ms` ceiling under movement or late acquisition.
- `rollout_shadow` is observation-only. Production `AimResponseEstimator` can change magnitude, but unlogged scale/confidence and direction-flip evidence do not support it as primary cause.

## Next Action

No new fix is accepted. If requested, first add epoch/segment timing, handoff
reason, contribution and response telemetry plus late-target/center-cross/control
fixtures; then evaluate a bounded guard without held-LT ADS rearm.

## Blockers

- No blocker for the current documentation/commit.
- The intentionally dirty worktree means commit identity alone is insufficient; preserve unrelated user changes.
- Session manifests omit executable SHA-256, so retain installation-chain evidence for future live tests.
- The latest manifest remains `state=active` and carries stale `git_commit`
  provenance; production response estimator scale/confidence is also absent.

## Active Questions

- Should bounded continuation depend on target-segment age, closing error or both?
- Should old-direction feed-forward be suppressed after fresh error crosses center?
- How large is any secondary estimator-magnitude effect once telemetry exists?

## Relevant Decisions

- [Contextual dual-proposal arbitration](decisions/DEC-2026-08-01-002-contextual-manual-ai-dual-proposal-arbitration.md)
- [Accept Task 1–4 runtime](decisions/DEC-2026-08-01-001-accept-control-chain-jump-stability-runtime.md)
- [Separate target motion from firing disturbance](decisions/DEC-2026-07-31-001-separate-target-motion-and-firing-disturbance.md)
- [Tracker publishes remaining work](decisions/DEC-2026-07-29-002-tracker-remaining-work-contract.md)
- [ADS timing remains proposed](decisions/DEC-2026-07-29-001-decouple-ads-arrival-and-ownership-window.md)

## Files To Read First

1. [Latest ADS long-session diagnosis](../docs/project/ADS_LONG_SESSION_DIAGNOSIS_20260801.md)
2. [Current acceptance and runtime identity](../docs/project/CONTROL_CHAIN_JUMP_STABILITY_ACCEPTANCE_20260801.md)
3. [Dual-proposal verification](../artifacts/benchmarks/sensitivity-manual-mix-20260801/CONTEXTUAL_DUAL_PROPOSAL_VERIFICATION.md)
4. [Compact session log](session-log.md)
5. [Four-situation plan and execution record](../docs/superpowers/plans/2026-08-01-four-situation-control-chain-reproduction-and-repair.md)

## Do Not Reopen Unless Needed

- Do not lower ADS/BodyLock strength, restore duplicate gates/brakes or rearm ADS to hide discontinuity.
- Do not globally scale all manual input; contextual normalization belongs only
  to the strong same-direction arbitration path.
- Do not clamp all fresh Vision innovation or add a second target-position state.
- Do not give `rollout_shadow` actuation or add persistent learning without evidence and approval.
- Do not reset, checkout over or bulk-clean the dirty worktree.

## Notes

- Archives: [pre-compaction handoff](archive/handoff-2026-07-31-pre-20260801-compaction.md) and [detailed July 23–31 session log](archive/session-log-2026-07-23-to-2026-07-31-pre-20260801-compaction.md).
- Keep user-confirmed behavior separate from AI-inferred learning attribution.
