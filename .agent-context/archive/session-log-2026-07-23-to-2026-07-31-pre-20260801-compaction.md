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

## 2026-07-23 to 2026-07-24 - Moving Baseline and Causal Ego Motion

- User-confirmed goal: treat sustained full-speed player strafe as a primary
  real-game benchmark rather than optimizing only static target scores.
- Repository evidence: worktree
  `.worktrees/aimlab-left-strafe-20260723`, branch
  `codex/aimlab-left-strafe-20260723`, retains a 24-run fixed-seed 60-second
  no-strafe/full-reversal matrix at `2a99a80`. Full strafe reduced aggregate
  tracking 12.17%, increased overshoot area 68.16%, circle exits 62.50%, and
  continued push after crossing 461.90%.
- Approved design `5eb7ebb` and implementation plan `23841a9` assign one
  fixed-size, memory-only ego-motion estimator to `TargetCoordinator`. No
  weapon identity, persistence, active calibration input, extra vision pass,
  output gate, heap allocation or new thread is allowed.
- Repository evidence: estimator RED contract and review fixes span
  `50838e9` through `4d0040d`; the minimal estimator and hardening span
  `a471b01` through `1892f63`. Focused Release tests pass for drift, onset,
  steady motion, full reversal, release inertia, pre-held movement, bounded
  gain/tau learning, coast/reacquire re-baselining, target switches, ambiguity
  rejection, rate-invariant confidence decay and determinism.
- Subagent result: Task 2 specification review passed at `1892f63`. The final
  code-quality re-review was interrupted when the user paused work.
- Inferred: the estimator cannot affect current gameplay yet because it has
  not been connected to `TargetCoordinator`, `TargetPlan`, ADS or BodyLock.
- Resume point: finish the interrupted Task 2 quality review, then use TDD for
  the single coordinator integration and matched moving A/B. Do not skip
  directly to tuning.
- Workspace note: two generated, untracked `.obj` files remain in the feature
  worktree and are not part of any commit.

## 2026-07-22 - Fresh-Vision BodyLock Counter-Correction

- User-directed boundary: tracker-only prediction must preserve the existing
  user/AI fusion; clear fresh Vision may constrain a demonstrably wrong manual
  direction more strongly.
- Repository evidence: `VectorIntentFuser` now receives a fresh single-target
  observation pulse and owns a 16 ms evidence envelope. Only reliable
  `BodyLockFollow` may select `FreshVisionCounterCorrected`; ADS keeps its
  existing brake, multi-target/coasting/reacquisition/escape do not gain the
  stronger authority.
- The new candidate reduces only the wrong radial manual component. Tangential
  manual and shaped AI remain at full weight. Default radial floor is `0.35`;
  `1.0` disables the path.
- Fixed 3-seed, 60-second evidence selected floor `0.35`: BodyLock tracking
  +1.83%, settled targets +7.61%, overshoot area -20.21%, false interruptions
  0.33 -> 0; BodyLock stall-ring time increased 1.46%. ADS cohort acquisition
  improved 0.73% and acquired targets 3.16%, while post-acquisition tracking
  decreased 1.47%; this remains a live hand-feel validation point.
- Rejected floor `0.20`, floor `0.50`, a hard 18 px gate and a distance-ramped
  floor. Full rationale and artifacts are linked from
  `docs/project/FRESH_VISION_MANUAL_COUNTER_CORRECTION_ACCEPTANCE_20260722.md`.
- Candidate telemetry schema advanced from v3 to v4. Release runtime built and
  all 23 registered CTest tests passed before merge to `dev` (`2a37b33`,
  evidence `926eeb5`).

## 2026-07-20 - Knowledge Capture and Context Compaction

- Recorded the complete July 16-20 optimization sequence as a causal case study rather than a commit list.
- Extracted the reusable loop: runtime identity -> evidence chain -> defect fixture -> metric contract -> counterfactual A/B -> minimal policy -> fixed-seed gate -> live smoke -> hand-feel validation -> durable decision.
- Added a project-neutral adoption entry at `docs/methods/REALTIME_CONTROL_OPTIMIZATION_START_HERE.md` with an intake template, copyable Codex prompt, minimum artifacts and skill-upgrade boundary.
- Added `docs/methods/prompts/ADOPT_EVIDENCE_DRIVEN_CONTROL_OPTIMIZATION.md` as the ready-to-paste onboarding and continuation prompt for other repositories.
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

- 2026-07-22: completed a static `480x416` TensorRT engine parameter matrix without replacing production. FP16 workspace 4 preserved F1 and produced the most repeatable native p50 improvement (about 8-9%); p95 remained noisy, workspace 10 was not worth its build cost, and FP32 was slower without an accuracy gain. See `docs/project/VISION_ENGINE_PARAMETER_MATRIX_20260722.md`; promotion still requires explicit A/B approval and rollback preservation.
- 2026-07-22: implemented the B0-B2 causal blind-window benchmark checkpoint on `codex/causal-response-integration-plan`. The 1 kHz plant separates capture, publication and response queues; K1 has no target surprise and exposes queued-motion debt across 675 fixed combinations. The sustained native adapter is now shared without output drift; retained production artifacts cover scale 1.00/0.90/0.80/0.70 and fingerprint `config.native.example.toml`. K2-K4 and acquisition guardrails remain required before changing production policy.
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

## 2026-07-24 - Live Strong-Assist Overshoot Audit

- Detailed evidence is preserved in [the July 24 overshoot audit](archive/2026-07-24-live-overshoot-audit.md).
- The 17:45-20:46 run had perf logging disabled. The usable 10:41-13:00 session shows the defect concentrates in BodyLock: strong AI was wrong-way on 6.56% of samples, and 37.56% of BodyLock center crossings retained old-direction final output.
- Human directional inertia was more common than stale AI; only 53 / 1,681 crossings had both AI and manual stacking in the old direction. Optimize reversal attribution/counter-correction rather than applying a blanket strength reduction.
- Reuse these results for causal POV/ego-motion work: decompose target motion, user/camera motion and anchor jumps; compare against the recorded wrong-way/crossing-debt baselines and reject gains that increase stickiness or user-fight.

## 2026-07-28 - Controller-Rate Event Posterior Stage 2

- Goal: determine whether the existing online response stack can learn
  controller-to-camera delay without weapon tables, injected calibration,
  persistence or more Vision inference.
- Repository evidence: `c87a7dd` on
  `codex/controller-rate-observer-20260728` adds a physical-right-stick event
  window, even/odd held-out delay posteriors, cross-event dwell and response
  prediction validation. AI-only output may train response diagnostics but
  cannot authorize delay evidence.
- Fixed 90-round result across 25/45/70 ms, ADS/BodyLock, three seeds and five
  retained 60-second rounds: delay MAE 1.889 ms, 85/90 within +/-5 ms, zero
  boundary selections, zero rollout decisions and exactly unchanged aggregate
  controller score versus the true learning-disabled baseline.
- Corrected benchmark interpretation: the earlier “direct interval baseline”
  still allowed sparse rollout and was not the pure controller baseline.
- Repository evidence: `PendingMotionModel` previously ignored `decision_ns`;
  scheduled debt now includes post-capture controller signals through the
  actual decision time, with a focused regression test.
- Rejected execution experiments: naive posterior publication, continuous
  pending attenuation, direct stale-error correction, settle-only rollout and
  bounded settle scaling. They reproduced overconfidence, undertracking,
  duplicate tracker prediction or inherited manual-escape arbitration.
- User-confirmed: do not overfit one metric or scenario; mysterious runtime
  failures are likely when a large local score gain bypasses broad behavioral
  guardrails.
- AI-inferred next step: separate right response, left ego-motion, target
  acceleration and Vision disturbance in a multi-scenario residual benchmark,
  then feed pending motion into tracker/TargetCoordinator shadow planning
  rather than adding another final-output gate.
- Context files updated: `handoff.md`, `session-log.md`.

## 2026-07-28 - Controller-Rate Generalization Audit

- Repository evidence: `54aa56c` on
  `codex/controller-rate-observer-20260728`.
- Repaired a benchmark contract defect: sustained learning silently ignored
  scenario, short occlusion, left-strafe and slowdown CLI settings. These now
  reach the simulator, appear in JSON provenance and have focused coverage.
- Expanded learning reports from total score only to a balanced controller
  scorecard: mean/P95 error, over/under-track, false interruption/stop,
  overshoot area, continued push, stall ring, residual discontinuity/kicks
  and post-occlusion recovery.
- Broad matrix: 48 baseline/observer pairs and 240 retained 60-second rounds
  across baseline/compound motion, strafe off/full reversal, occlusion 0/36
  ms, ADS/BodyLock and three initial seeds. Script mismatches, score drift,
  metric drift and rollout decisions were all zero. Delay MAE was 0.188 ms,
  239/240 were within +/-5 ms, and all 48 final rounds selected 45 ms.
- Response-prediction confidence stayed zero for 240/240 rounds. The delay
  observer generalizes; the response model is not control-ready.
- Full-reversal strafe was the dominant current-controller stress. Versus
  strafe off, ADS points -8.1%, mean error +7.0%, overshoot area +58.6% and
  continued push +128.4%; BodyLock points -4.0%, mean error +13.8%,
  overshoot +42.6% and continued push +69.5%. The 36 ms occlusion effect was
  much smaller.
- Plant matrix: camera response 350/500/700, slowdown 0.5->0.4 and 0.8->0.7,
  strafe off/full reversal, ADS/BodyLock, compound motion and 36 ms occlusion.
  Higher camera response generally improved ADS mean error, matching the
  user's live sensitivity observation, but did not monotonically eliminate
  overshoot/continued-push debt.
- Rejected assumption: one global 2x2 RLS response matrix cannot represent
  local response across ADS/BodyLock, slowdown strength and strafe state.
  Closed-loop error/input correlation contaminates the estimate.
- Retained next direction: one event-locked local response observer using
  natural manual onset/reversal/release, robust aggregation and uncertainty.
  Feed valid pending debt into tracker/coordinator shadow planning; do not add
  another final-output brake or strength gate.
- Verification: Release runtime build passed; focused causal learner,
  pending-motion, short-rollout and sustained-learning executables passed;
  `git diff --check` passed.

## 2026-07-29 - Player POV Motion Matrix and ADS Timing Hypothesis

- Repository evidence on `codex/player-motion-benchmark-20260729`: `afdeaf5`
  adds seeded full-reversal strafe, slide with instant/linear recovery, jump,
  combined player motion and paired stationary/moving enemy modes. Scenario,
  simulator and counterfactual tests pass.
- Evidence commit `58984cd` retains 120 one-minute runs using seeds
  `2026072901..03`, pure/mixed input and ADS/BodyLock. Combined player plus
  enemy motion reduces tracking by about 43-50%; pure AI also fails, so the
  defect is not only wrong manual right-stick input.
- Mixed input remains useful: in the simultaneous-motion cohort it improves
  ADS tracking 39.6% and BodyLock tracking 18.0% versus pure AI, although ADS
  retains more obsolete tail push. Do not solve this with blanket manual
  suppression.
- User-proposed hypothesis: exploit common COD sprint-to-fire latency by
  starting ADS positioning after a small configurable delay (about 30 ms) and
  keeping ADS acquisition ownership longer (example 220 ms), while preserving
  a faster position-arrival plan (example 120 ms).
- Repository finding: `snap_duration_ms` currently drives both solver arrival
  horizon and ADS ownership window. `max_acquisition_ms` is parsed and passed
  into `TargetCoordinatorConfig` but is not consumed by its mode transition.
  This supports testing a minimal parameter separation before adding policy.
- AI-inferred guardrail: a universal dead delay may harm non-sprint and
  low-latency cases; benchmark dead-delay, authority-ramp and sprint-intent
  conditioned variants before production rollout. Decision remains proposed.
- Context files updated: `handoff.md`, `session-log.md`, and
  `DEC-2026-07-29-001-decouple-ads-arrival-and-ownership-window.md`.

## 2026-07-30 - Gun-Kick Reproduction, Conservative Tracker Response and ADS Range

- User-confirmed current Vision baseline: dynamic ROI with a `480x384`
  inference tensor; `480x416` is not the live baseline.
- Repository evidence: sustained AimLab gained deterministic
  `--vision-disturbance gun-kick`, modeling 100 ms firing cadence, up to 8 px
  vertical kick, alternating 4 px horizontal kick and recovery. It composes
  with short occlusion and existing target/player motion without altering the
  physical target script.
- Three fixed 60-second pure-BodyLock seeds with compound target motion,
  100 Hz Vision and 36 ms occlusion reproduced the defect. Gun kick reduced
  tracking points from 38,220 to 26,695 (-30.2%), raised undertrack from 91 to
  205 events, and raised continued push from 86 to 237 ms (+175.6%).
- Rejected experiment: a near-center radial no-reversal guard passed focused
  tests but produced negligible broad improvement, so it was removed.
- Rejected experiment: clipping one-frame velocity innovation to 4 px slightly
  recovered tracking but increased overshoot/continued-push and broke the
  persistent vertical-motion classification contract, so it was removed.
- Retained production change: tracker motion velocity alpha `0.15` rather than
  `0.20`. In pure gun-kick A/B it improved tracking 12.5%, mean error 8.1%,
  continued push 7.6% and smooth bonus 18.4%. In ordinary pure BodyLock it
  improved tracking 7.0% and mean error 9.0%. Complex mixed POV gains were
  smaller; this is a conservative baseline, not a complete disturbance model.
- Repository finding: `[gamepad.ads].range_px` was not a true initial ADS
  selection radius in the TargetCoordinator path. It now maps to
  `ads_activation_radius_px`; initial acquisition uses
  `base_radius * (1 + 0.75 * normalized_target_height)`.
- ADS range expansion is limited to the unconsumed ADS epoch before a target
  is committed. Candidate distance from the reticle now participates in
  initial selection. Existing one-LT-epoch snap ownership remains unchanged.
- BodyLock's size-aware continuation remains fresh-Vision-only; stale coast
  cannot reuse old large-target geometry to widen authority.
- Verification: Release runtime built; TargetCoordinator, controller
  integration, runtime config, sustained scenario/simulator/score and BodyLock
  focused tests passed; `git diff --check` passed.

## 2026-07-31 - Live Firing Disturbance Evidence and Failed Local Fixes

- Aligned the three newest stationary firing-range videos with the 203 MB
  telemetry session beginning at 13:09:01. This is a useful isolation cohort:
  the player and far target are stationary while repeated firing supplies
  camera/gun-kick disturbance.
- Requested assist reversed direction many times inside single firing bursts.
  Across the three video windows, horizontal/vertical requested reversals were
  30/39, 15/20 and 18/18; individual 0.2-0.5 second bursts contained five to
  nine reversals.
- The appearance/body anchor was present for roughly 61-81% of samples and
  reduced raw box delta, but it still includes real camera recoil. Anchor
  stability alone cannot identify target motion.
- No unsupported ADS epoch was found in these windows: every controller ADS
  epoch had an LT activation. Akimbo LT-as-fire versus ADS semantics remains a
  separate input-mode issue.
- Added a deterministic `horizontal-aim-bias` benchmark fixture: the true
  target remains fixed, the observation shifts 22 px for 150 ms and recovers
  over 90 ms. Baseline BodyLock retained 132 ms of near-center wrong-way work
  and 516 ms of stall-ring time after this error.
- Rejected three local controller experiments: fresh-anchor cap bypass,
  firing-time velocity ramp/zero and two-frame reversal confirmation. They
  moved individual ADS metrics but introduced BodyLock overshoot, lazy
  tracking, extra reversal or correction-tail regressions.
- All experimental behavior was rolled back. Focused coordinator, simulator
  and geometry tests passed, and the production double-click runtime was not
  replaced.
- User-confirmed completion standard: the next deliverable must be an
  acceptance-ready build, not a report that one metric improved. The retained
  direction is the dual-state observer recorded in
  `DEC-2026-07-31-001-separate-target-motion-and-firing-disturbance.md`.

## 2026-07-31 - Firing Disturbance Observer Acceptance and Deployment

- Found and fixed a tracker time-semantics defect: a carried Vision candidate
  could reach the measurement branch on every controller tick. A unique
  `(frame_id, capture_time)` sample now updates position/velocity only once;
  later controller ticks only propagate state and delivered work.
- Rejected during implementation:
  - a leaky fast disturbance state, because it changed jitter into a slower
    BodyLock oscillation;
  - a global three-sample velocity medoid/mean, because it delayed true target
    motion and raised BodyLock overshoot;
  - immediate velocity zeroing on every low-anchor frame, because isolated
    anchor dropout erased valid tracking speed;
  - adding final recoil output to the Remaining delivery ledger, because
    recoil compensation is not equivalent to net camera displacement and
    BodyLock tracking/overshoot regressed sharply.
- Accepted one bounded velocity-admission observer. Position strength is
  unchanged. During firing, directionally inconsistent 2-D residuals cannot
  become target velocity until supported by the next observation. BodyLock
  uses it only for low/absent appearance anchor and established speed no
  greater than 80 px/s.
- Strength, recoil, selector and vision inference settings were not changed.
  LT press remains immediate; release debounce increased from about 8 ms to
  about 24 ms to prevent held-trigger dropout from rearming ADS snap.
- Three-seed acceptance covered stationary firing, moving target plus 36 ms
  occlusion, horizontal wrong-aim recovery and mixed full-reversal left
  strafe. Wrong-aim requested/shaped reversals fell 86-98%; all cohorts kept
  false stop/interruption at zero. Moving/mixed guardrails remained within
  small bounded changes and retained or improved tracking.
- Focused coordinator, geometry, aim dynamics, BodyLock, runtime config,
  telemetry and sustained simulator tests passed. `git diff --check` passed.
- Previous runtime backup:
  `artifacts/runtime-backups/20260731-150805`.
- Deployed and background-restarted runtime:
  `native/vision_native/build/Release/cod_native_runtime.exe`,
  PID `60328`, SHA-256
  `80831B5197B8AC58C20844A349BD673FCC21B2768A055AB4446911257BA70F50`.
