# Agent Handoff

Last updated: 2026-09-26
Active scope: commit the tested gamepad controller checkpoint and preserve its limitations.

## Current State

- **User-confirmed:** after the new build, ordinary multiplayer with normal aim assist felt free of the previously noticed jitter/oscillation. User requested latest-log review, documentation, generated-file cleanup and a scoped commit.
- **Repository evidence:** retained fixes cover the 15–30% continuous per-axis intent curve, raw zero-deadzone passthrough, acknowledged DS4 camera-command coordinates/time, confidence units and high-rate source-window starvation. No rejected predictive/adaptive model experiment remains in production.
- Tested gameplay binary SHA-256: `14b5bdb4435f8d20c8fe3a766a724a82e26e3e4da335df813f8dd4d801fd58e8`. It was built from the pre-commit working tree, not solely the embedded old commit identity.
- **Offline evidence:** 410 product cases passed; 56 high-rate subcases prove observation/learning repair; 16 matrix packs (864 opportunities) have no new protected regressions against the preceding continuous-intent worktree; 1,944 development scan cases unchanged. This does not reverse prior rejected candidate results.
- **Log evidence:** two intact rotations cover about 658 seconds, 61,766 unique controller samples and 142,981 delivery records; no recorded delivery errors, nonfinite axes or output range violations. Final rotation has a truncated last line and remains excluded and untouched.
- **Open observations:** one short firing-related error/output reversal segment and one ADS-to-extension authority jump remain screening findings, not proven causes. No matched live A/B, independent camera truth or complete game/hardware covariates.
- Audit status is **INSUFFICIENT_EVIDENCE** for full acceptance. User feel confirmation is limited to normal multiplayer. Strong-AA Warzone/Zombie and the frozen synthetic response/delay mismatch RED remain open; do not label the entire system LIVE-ACCEPTED.

## Next Action

- Preserve this checkpoint. If a new symptom is reported, compare against its exact binary/config identity and convert a confirmed episode into a RED before changing behavior.
- Prioritize the two saved screening episodes if they recur; do not attribute all improvement to the high-rate fix, since stable 160 Hz did not trigger starvation.
- Raw logs and build backups stay local. Telemetry was enabled for this capture; its overhead precludes comparison with no-log timing.

## Relevant Records

- `docs/benchmarks/CONTROLLER_MULTIPLAYER_CHECKPOINT_20260926.md`
- `docs/benchmarks/CONTROLLER_OFFLINE_REPAIR_20260926.md`
- `docs/benchmarks/CONTROLLER_CHAIN_REFACTOR_20260926.md`
- `docs/benchmarks/oscillation-20260926/high-rate-regression-manifest.json`
- `docs/benchmarks/AIMLAB_OPTIMIZATION_CONTRACT_V1_20260827.md`
- `decisions/DEC-2026-08-11-001-incident-first-gameplay-validation.md`

## Boundaries

- Native passthrough has zero software deadzone; AI manual weight rises smoothly over 15–30%. Keep the full controller chain in lockstep.
- Do not exchange acquisition/identity/handover regressions for aggregate smoothness points.
- Unrelated GPU contention, training, Flash tools and research work remain outside this commit and must not be cleaned up.
- Previous Fusion handoff is preserved in `archive/handoff-2026-09-01-before-controller-checkpoint.md`; its live-display questions were not investigated in this task.
