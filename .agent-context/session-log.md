# Agent Session Log

Last compacted: 2026-07-20
Primary scope: native C++ FPS gamepad runtime, target selection, tracker/controller, ADS, BodyLock, intent fusion, AutoFire, recoil boundary, benchmarks and runtime operations.

## How To Use This File

- This is the compact startup index, not the complete history.
- Pre-compaction handoff and session-log content is preserved verbatim in [context-through-2026-07-20-pre-compaction.md](archive/context-through-2026-07-20-pre-compaction.md).
- The July 16-20 causal history is in [AIM_CONTROL_OPTIMIZATION_HISTORY_20260716_20260720.md](../docs/project/AIM_CONTROL_OPTIMIZATION_HISTORY_20260716_20260720.md).
- The reusable workflow is in [EVIDENCE_DRIVEN_REALTIME_CONTROL_OPTIMIZATION.md](../docs/project/EVIDENCE_DRIVEN_REALTIME_CONTROL_OPTIMIZATION.md).
- Older long history remains in `session-log-full.md` and dated archive files.

## Evidence Labels

- **User-confirmed:** live feel, video or explicit user statement.
- **Repository evidence:** source, tests, commit, config-proven artifact or acceptance report.
- **Inferred:** causal explanation not yet independently proven by matched A/B.

## 2026-07-20 - Knowledge Capture and Context Compaction

- Recorded the complete July 16-20 optimization sequence as a causal case study rather than a commit list.
- Extracted the reusable loop: runtime identity -> evidence chain -> defect fixture -> metric contract -> counterfactual A/B -> minimal policy -> fixed-seed gate -> live smoke -> hand-feel validation -> durable decision.
- Preserved every line of the old 277-line handoff and 247-line session log in the archive before compacting either file; only line endings were normalized to LF.
- Kept the global learning roadmap explicit: G0 decision journal, G1 sequence oracle, G2 shadow tail value, G3 bounded coordinator adjustment, G4 memory lifetime/persistence decision.
- Inferred lesson: useful old feel may come from accidental stacking; removing duplication can improve naturalness while exposing a missing legitimate control function.

## 2026-07-19 - Counterfactual, Vector Fusion and Runtime Contracts

- Repository evidence: the sustained benchmark gained deterministic conflict episodes, finite candidate replays, 40/80/160 ms local regret, 500 ms future burden, causal-vs-hindsight separation and determinism checks.
- Repository evidence: `VectorIntentFuser` replaced independent X/Y arbitration with one causal 2-D manual/AI decision and bounded weight transition. Runtime enable commit: `3d20ba6`.
- User-confirmed: BodyLock should allow bounded target inertia across reversal, jump apex and fall; every center crossing is not a brake failure.
- Repository evidence: radial closing/away state and target inertia were added in `cf030ba`; ADS Brake remains ADS-only and BodyLock does not manufacture a zero-output brake.
- Important limitation: the final legacy/vector A/B JSON was not retained as a checked-in acceptance artifact. Do not repeat conversation-only percentages as audited results.
- Repository evidence: selector prefers the current near target through short occlusion (`0309804`).
- Repository evidence: ADS snap is consumed once per physical LT epoch (`821e255`); new targets during the same held ADS do not restart snap.
- Repository evidence: green friendly cue is a hard reject; yellow enemy cue is auxiliary evidence only (`5403abe`).
- Repository evidence: unreliable opposing BodyLock fallback and benchmark ADS epoch semantics were aligned (`b0c5bec`).
- Repository evidence: native vision capture/engine contract restored to `480x416` (`0c7a2f9`).
- Repository evidence: zero-console VBS start/stop launchers added and smoke-tested (`f4bcd4b`). They hide the window, not the process.

## 2026-07-18 to 2026-07-19 - Sustained AimLab, Response Model and Brake Metrics

- User required one-minute random-target tests, additive scoring, about 1000 ms tracking and a 250-330 ms acquisition deadline.
- Slowdown is modeled from the target perimeter inward; baseline edge/center is `0.50/0.40`.
- The benchmark separates ADS/BodyLock, pure/mixed human input and ordinary/small targets; BodyLock scoring begins only after valid ADS capture.
- `fb125e9` added the 60-second closed-loop sustained benchmark.
- `55bbb10` unified ADS and BodyLock around an in-memory response model; no weapon database or extra vision pass.
- Fixed three-seed acceptance recorded substantial ADS acquisition/tracking gains and smaller BodyLock gains. Mixed small-target interruption remained a diagnostic regression; no new gate was added solely for it.
- Old severe overshoot stayed zero and was non-discriminating. Brake episodes now measure center crossing, post-cross area/amplitude, circle exits, 10-20 px stalls, reversals, settle and handoff residual.
- Full response/slowdown matrix selected a synthetic `120 ms + BodyLock 0.52/0.58` candidate over the then-current `160 ms + 0.45/0.50`, but did not automatically replace the user's live config.
- `100 ms` ADS was faster but failed the brake-first worst-excursion rule.

## 2026-07-17 - Realistic Human Error, Axis Arbitration and AutoFire

- User-confirmed: left-stick movement is part of aiming; right-stick mistakes include delay, wrong direction, stale direction, crossing inertia and reverse correction.
- Added half-body occlusion, simultaneous X/Y motion, vision jitter/dropout, left-strafe onset/reversal/release and deterministic human-error cohorts.
- Per-axis intervention improved wrong-X/practical stress but changed normal combat little and could feel sticky near the target.
- User-confirmed: X/Y separation is insufficient because control is radial/tangential/diagonal; an error should be corrected as a full direction, not only cancelled per axis.
- Rejected and reverted early axis experiments when runtime behavior or confidence semantics were wrong (`da4194c`, `01bca7c`, `990946c`).
- The accepted per-axis line remained an intermediate step and was later replaced by vector fusion.
- AutoFire was restored to 10 pulse starts per second, 100 ms start period and at least 30 ms hold; no fresh vision tick is not a miss; physical RB/RT always pass through.
- High-rate perf logging was disabled by default after runtime stickiness concerns; debug logging remains explicit.

## 2026-07-16 - Refactor B and Feel Recovery

- Frozen baseline revision `e6c1f2f`; moving production-style cases had zero BodyLock frames while a focused handoff fixture entered BodyLock.
- Refactor B established one production path: observations + intent -> TargetCoordinator -> immutable TargetPlan -> ADS/BodyLock -> AimDynamicsShaper -> AutoFire -> Recoil.
- Removed production linkage to duplicate completion/carry brake, legacy tracker/AI, short-plan, authority, BodyLock lifecycle, old dynamics and output-validation policies.
- Result: BodyLock lifecycle, overshoot, user-fight and jerk improved, but moving mean/P95 error and live strength/damping regressed.
- User-confirmed: the first refactor made aim more natural and less destructive, but ADS and BodyLock felt much weaker.
- `ac7ff12` restored stronger smooth response with bounded stopping lookahead, tolerance-derived near-target feedback and filtered left-strafe response, without restoring duplicate gates.
- The strongest July 14-style profile was rejected because it recovered speed at the cost of large user-fight.
- Config pipeline became profile-faithful; artifact configuration provenance became mandatory. Old artifacts without configuration are configuration-unproven.
- Tracker aim geometry was canonicalized at `aim_height_ratio = 0.365`; deprecated aliases no longer silently override it.
- Fresh debug sessions gained manifest-based discovery and whole-session dry-run cleanup.

## Earlier Durable Milestones

- 2026-07-14: fixed SDL stale-handle input freeze and tracker-only ADS strong continuity; added checked ViGEm delivery/reconnect and live-failure benchmark.
- 2026-07-07: accepted authority-before-strength direction; rejected a crude body-box authority gate that improved ADS diagnostics but damaged BodyLock continuity.
- 2026-07-06: wired physical right-stick intent into native selector and introduced native AimLab/manual-input models.
- 2026-06-25: performance-first fusion canvas; target-dot-only overlay and capture exclusion boundary.
- 2026-06-15: tracker/controller/recoil boundary; recoil remains final feed-forward and cannot consume target state.
- 2026-05-29: weak association and fire-authority gating baseline.

## Current Verified Architecture

```text
Vision evidence
  -> intent-aware selector
  -> TargetCoordinator (single identity/lifecycle/ADS-epoch owner)
  -> immutable TargetPlan
  -> ADS response-model acquisition OR BodyLock trajectory follow
  -> one AimDynamicsShaper
  -> one VectorIntentFuser
  -> ADS Brake only in ADS
  -> recoil final feed-forward
  -> virtual gamepad

TargetPlan.fire_authority -> AutoFireGate
```

## Open Work

- Recreate and retain a current revision legacy/vector full acceptance artifact with config fingerprint and fixed seeds.
- Keep the global learning G0-G4 roadmap pending; no production tail-value policy exists yet.
- Build a small/far-target authority matrix using existing size/reliability before considering more vision work.
- Map any remaining 10-20 px BodyLock stickiness to radial/tangential conflict, slowdown and delivered-output evidence before changing policy.
- Record runtime identity for every new live log/video: executable, revision, config fingerprint, crop/engine and `--perf-log` state.

## Archive Map

- [Pre-compaction context through 2026-07-20](archive/context-through-2026-07-20-pre-compaction.md)
- `session-log-full.md`: older project history.
- `archive/2026-06-25-fusion-canvas.md`: fusion canvas detail.
- `archive/2026-06-24-audio-visual-fusion-plan.md`: prior audio/visual direction.

## Maintenance Rules

- Keep this file under 160 lines and use it as the primary chronological index.
- Move detail into dated project docs or archive files; do not silently discard rejected experiments.
- Do not store secrets, credentials, personal video paths or unnecessary local directories.
- Mark user-confirmed, repository evidence and inferred conclusions distinctly.
