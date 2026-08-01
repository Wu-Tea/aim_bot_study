# Agent Handoff

Last updated: 2026-08-01
Active scope: native C++ FPS gamepad runtime, target identity, ADS, BodyLock, intent fusion and response estimation.
Staleness trigger: refresh after runtime/config/engine or a control owner changes, or live evidence reproduces ADS pull, BodyLock jump, moving-follow jitter, sticky escape or lazy close tracking.

## Current Objective

Preserve the live-accepted Task 1–4 runtime as the stable baseline. Do not tune strength or reopen old experiments without a fingerprinted, reproducible live defect.

## Current State

- Chain: Vision/selector -> TargetCoordinator -> TargetPlan -> ADS or BodyLock -> AimDynamicsShaper -> VectorIntentFuser -> ADS-only brake -> recoil/output.
- Task 1 removed the fuser's duplicate reliability hard gate while retaining exact strong manual escape and bounded 2-D re-entry.
- Task 2 made shaper state target/mode-aware; ADS saturation no longer enters the first BodyLock tick and new targets do not inherit old AI state.
- Task 3 enforced selector-owned no-selection admission; Release `34/34`, focused tests, matched A/B and `git diff --check` pass.
- Task 4 gives a different replacement target in the same LT epoch one exact manual/zero-AI tick, then the existing `0.08` slew. Fresh Vision and valid same-track movement remain authoritative.
- User live-accepted the result: severe ADS pulls are gone, BodyLock follows a stationary target during main-view movement, moving follow no longer jitters frequently, and later acquisitions were nearly direct.
- Stable runtime: `native/vision_native/build/Release/cod_native_runtime.exe`, SHA-256 `B7F9F28B6A39E1AE58DB75C5F3A3A18DDF3D9692886245ABF2C5493FDC84140C`.
- Candidate archive: `artifacts/runtime-candidates/20260801-task4-ownership-admission/cod_native_runtime-task4-B7F9F28B.exe`; previous runtime backup: `artifacts/runtime-backups/20260801-task4-before-overwrite/cod_native_runtime-pre-task4-3D9ED74C.exe`.
- `rollout_shadow` remains observation-only. The live memory-only `AimResponseEstimator` influences coordinator feedback; its contribution to early-session warm-up is inferred, not independently proven.

## Next Action

Leave control and parameters unchanged. For a new defect, fingerprint runtime/config/engine/source, isolate one short live window, and compare with the August 1 baseline before editing code.

## Blockers

- None for the accepted runtime.
- The intentionally dirty worktree means commit identity alone is insufficient; preserve unrelated user changes.
- Session manifests omit executable SHA-256, so retain installation-chain evidence for future live tests.

## Active Questions

- Only if requested: estimator confidence versus tracker/body-geometry state in the first few acquisitions.
- Whether any new weapon/mode reproduces a jump on the accepted identity.

## Relevant Decisions

- [Accept Task 1–4 runtime](decisions/DEC-2026-08-01-001-accept-control-chain-jump-stability-runtime.md)
- [Separate target motion from firing disturbance](decisions/DEC-2026-07-31-001-separate-target-motion-and-firing-disturbance.md)
- [Tracker publishes remaining work](decisions/DEC-2026-07-29-002-tracker-remaining-work-contract.md)
- [ADS timing remains proposed](decisions/DEC-2026-07-29-001-decouple-ads-arrival-and-ownership-window.md)

## Files To Read First

1. [Live acceptance](../docs/project/CONTROL_CHAIN_JUMP_STABILITY_ACCEPTANCE_20260801.md)
2. [Compact session log](session-log.md)
3. [Task 4 verification](../artifacts/benchmarks/control-chain-jump-fix-20260801/task4-ownership-admission/VERIFICATION.md)
4. [Task 1–3 verification](../artifacts/benchmarks/control-chain-jump-fix-20260801/codex-final-ab/VERIFICATION.md)
5. [Implementation plan](../docs/superpowers/plans/2026-08-01-ads-bodylock-jump-stability-repair.md)

## Do Not Reopen Unless Needed

- Do not lower ADS/BodyLock strength, restore duplicate gates/brakes or rearm ADS to hide discontinuity.
- Do not clamp all fresh Vision innovation or add a second target-position state.
- Do not give `rollout_shadow` actuation or add persistent learning without evidence and approval.
- Do not reset, checkout over or bulk-clean the dirty worktree.

## Notes

- Archives: [pre-compaction handoff](archive/handoff-2026-07-31-pre-20260801-compaction.md) and [detailed July 23–31 session log](archive/session-log-2026-07-23-to-2026-07-31-pre-20260801-compaction.md).
- Keep user-confirmed behavior separate from AI-inferred learning attribution.
