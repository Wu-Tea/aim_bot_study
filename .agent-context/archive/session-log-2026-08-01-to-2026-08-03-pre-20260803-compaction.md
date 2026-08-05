# Agent Session Log

Last compacted: 2026-08-01
Scope: native C++ FPS gamepad runtime, selector/tracker/controller, ADS, BodyLock, fusion, response estimation, benchmarks and runtime operations.

## How To Use This File

- Read the newest checkpoint, then its acceptance and decision links.
- Detailed July 23–31 history: [archived session log](session-log-2026-07-23-to-2026-07-31-pre-20260801-compaction.md).
- Earlier history: [pre-July-20 context](context-through-2026-07-20-pre-compaction.md) and `../session-log-full.md`.
- Labels: **User-confirmed** = live/explicit statement; **Repository evidence** = source/test/artifact; **Inferred** = explanation not isolated by A/B.

## 2026-08-01 - Dual-Proposal Runtime and Long-Session ADS Audit

- Goal: make manual and AI both enter one calculation at 2.4 sensitivity, then distinguish the remaining late-session ADS over/under from learning drift.
- Repository evidence: strong same-direction ADS/near BodyLock now uses complete AI plus context-normalized manual and 20% parallel headroom; tangent/opposing input, full escape, pure AI and far BodyLock retain their contracts.
- Verification: Release build, focused tests, left-stick 5/0 harness, CTest `34/34` and `git diff --check` pass; authoritative matrix is `sensitivity-manual-mix-20260801/contextual-headroom-0.20-final-exact/`.
- Installed runtime SHA-256: `DE31FF53B4C0CFBAB091F589CB194296A01DC9C0C8B5E74513F90AB94ED30590`; pre-overwrite `0E5E9A3B...` backup retained.
- **User-confirmed:** after longer bot play, ADS can occasionally pull slightly past or stop slightly short.
- Repository evidence: latest `20260801T131000Z_6544_1` session is 25.51 min; 355 ADS runs, 246 normal handoffs >=20 ms; ending error vs elapsed `rho=0.070` and response proxy vs elapsed `rho=-0.054`.
- Late examples end at the physical-LT `220 ms` acquisition ceiling: a target entering at 182 ms gets only 36 ms; a near-ceiling center crossing retains old-direction velocity/feed-forward before handoff.
- **Inferred:** movement plus physical-epoch handoff timing is primary; cumulative learning drift is not supported. Production response scale/confidence is unlogged, so a secondary magnitude effect remains open.
- No new control fix was made. Next gate is telemetry plus late-target/center-cross/stationary fixtures; held-LT strong ADS must not rearm.
- Decision: [DEC-2026-08-01-002](../decisions/DEC-2026-08-01-002-contextual-manual-ai-dual-proposal-arbitration.md). Diagnosis: [ADS long-session audit](../../docs/project/ADS_LONG_SESSION_DIAGNOSIS_20260801.md).

## 2026-08-01 - Task 1–4 Repair and Live Acceptance

- Goal: remove ADS pull-away, ADS→BodyLock force carry, held-LT replacement jumps and moving-follow jitter without reducing strength or adding another owner.
- Repository evidence: Task 1 removed duplicate fuser reliability gating; Task 2 made shaper state target/mode-aware; Task 3 enforced selector-owned no-selection; Task 4 added one manual/zero-AI replacement admission tick followed by the existing `0.08` slew.
- Fresh Vision and valid same-track motion remain authoritative; no tracker clamp, second position state, selector rewrite, gain reduction, ADS re-arm or final-output brake was retained.
- Release `34/34`, focused tests and `git diff --check` pass. Task 4 A/B: tracking `+0.0343%`, overshoot `-0.0712%`, continued push `245 ms`, discontinuities `59`, false interruptions `9`, false stops `0`.
- Installed after explicit authorization: runtime SHA-256 `B7F9F28B6A39E1AE58DB75C5F3A3A18DDF3D9692886245ABF2C5493FDC84140C`; candidate archive and prior `3D9ED74C...` backup retained.
- **User-confirmed:** later acquisitions were nearly direct; BodyLock held a stationary target during main-view movement; moving follow no longer jittered frequently; severe jumps are no longer the usability failure.
- **Inferred:** improvement is consistent with in-memory `AimResponseEstimator` plus tracker/body-geometry state. Top-level `rollout_shadow` is telemetry-only.
- Subagent result: earlier Luna Task 1–3 work was reviewed against focused tests/artifacts; final selector/Task 4 acceptance and installation were verified in the primary session.
- Context updated: handoff, session index, `DEC-2026-08-01-001`, acceptance doc, plan status and Task 4 verification.
- Follow-up: freeze the baseline; reopen only for a fingerprinted, reproducible live defect.

## 2026-07-31 - Firing-Disturbance Observer

- Live stationary firing showed five to nine requested reversals per 0.2–0.5 s burst; noise alone was insufficient.
- Accepted bounded velocity admission: fresh position stays authoritative; only consecutive consistent 2-D residuals enter target velocity during low-anchor firing at established speed <=80 px/s.
- Unique Vision samples update measurement once; controller ticks only propagate state and delivered work.
- Rejected leaky disturbance, global medoid/mean, immediate velocity zeroing and final recoil in Remaining due to oscillation, delay or BodyLock regression.
- Decision: [DEC-2026-07-31-001](../decisions/DEC-2026-07-31-001-separate-target-motion-and-firing-disturbance.md).

## 2026-07-29 to 2026-07-30 - Remaining Work and POV/Gun-Kick Evidence

- Accepted remaining work = fresh Vision error + exogenous motion - delivered camera work; ADS/BodyLock consume one state.
- Combined player/enemy motion cut tracking about 43–50%; mixed manual input remained useful, rejecting blanket suppression.
- Gun-kick plus occlusion reproduced BodyLock loss; retained conservative velocity alpha `0.15` and size-aware initial ADS range without held-LT rearm.
- ADS arrival versus ownership timing remains [proposed](../decisions/DEC-2026-07-29-001-decouple-ads-arrival-and-ownership-window.md).

## 2026-07-28 - Controller-Rate Learning Audit

- Delay posterior generalized (239/240 within +/-5 ms), but mixed-motion response confidence remained zero and no rollout decisions were authorized.
- `PendingMotionModel` learned decision-time debt but stayed shadow-only; one global 2x2 response matrix was rejected across mode/slowdown/strafe.
- Full-reversal strafe remained the main synthetic stress; delay accuracy alone cannot authorize a new control owner.

## 2026-07-23 to 2026-07-24 - Moving and Ego-Motion Evidence

- Full-speed left strafe reduced tracking 12.17%, raised overshoot 68.16% and continued push 461.90% versus no strafe.
- A causal ego-motion estimator was tested in a feature worktree but not connected to production.
- Live crossing debt concentrated in BodyLock; human inertia was more common than old-direction AI/manual stacking. See [audit](2026-07-24-live-overshoot-audit.md).

## 2026-07-16 to 2026-07-22 - Single-Owner Baseline

- Refactor B established one production path and removed duplicate lifecycle, brake, legacy tracker/AI, dynamics and output-validation owners.
- Response-model control, near-target feedback and vector fusion recovered speed; ADS snap became one physical-LT epoch.
- Selector retained committed targets through short occlusion; green cue is a hard reject, yellow cue auxiliary only.
- Fresh reliable BodyLock Vision may reduce only wrong radial manual work for 16 ms; tangent and deliberate escape remain user-owned.
- AutoFire stays 100 ms period / >=30 ms press; recoil remains final feed-forward.

## Earlier Durable Milestones

- 2026-07-14: SDL stale-handle recovery and tracker-only ADS continuity.
- 2026-07-07: authority-before-strength accepted; crude body-box gate rejected.
- 2026-07-06: physical right-stick intent entered native target selection.
- 2026-06-25: performance-first fusion canvas and capture exclusion boundary.
- 2026-05-29: weak-association and fire-authority gating baseline.

## Current Architecture and Maintenance

```text
Vision -> selector -> TargetCoordinator -> TargetPlan
       -> ADS acquisition OR BodyLock follow
       -> AimDynamicsShaper -> VectorIntentFuser
       -> ADS-only brake -> recoil -> virtual gamepad
```

One owner per lifecycle, mode transition, shaping state and manual/AI fusion decision. Fresh Vision corrects position; prediction/delivered-work accounting bridge gaps. Current acceptance: [August 1 live record](../../docs/project/CONTROL_CHAIN_JUMP_STABILITY_ACCEPTANCE_20260801.md).

Keep this index under 100 lines; archive detail before growth. Do not store secrets, personal video paths or raw telemetry dumps.

## 2026-08-02 - Eight-Clip Causal Audit and W1/W3 Acceptance
- User evidence: ADS over/under, one selector/anchor left-down jump, and repeated BodyLock wrong-way swing remained in eight live clips.
- Live chain evidence: active present-to-ViGEm was normally 9-12 ms with unique delivery; marked failures came from manual/fresh-AI authority, observed feed-forward, selector geometry, and brittle ADS extension rather than a queued vector or Remaining.
- luna-max W1 timing/provenance and W3 latest-only ego-motion shadow were independently rebuilt; isolated CTest passed 36/36 and candidate SHA is `B42C7B8427078ED44E61BF5E9B9B4084233F172513B03DCA22C846389BDDD3A6`.
- Follow-up: implement only the P1 fresh-manual wiring and Observed BodyLock radial position barrier, review, then separately address selector geometry and 135-to-220 ms ADS lifecycle.

## 2026-08-02 - P1 Single Final-Output Envelope Accepted in Isolation
- User-confirmed: manual and AI are fallible evidence/proposals; the controller should solve one final output instead of allocating and adding two forces.
- luna-max P1 separated fresh BodyLock position from bounded motion feed-forward, added one stopping/force envelope and post-slew guard, fixed persistent escape ownership, and exposed reconstructible envelope telemetry.
- Primary verification: focused coverage passed, full isolated CTest `36/36` passed, `git diff --check` passed; candidate SHA is `1AA216FB...4947D744` and user runtime `07434ADC...6663C7` remained untouched.
- Live acceptance is pending. Next: selector/coordinator replacement identity boundary, then ADS nominal 135 ms continuation to a hard 220 ms ceiling.
- Maintenance: this compact index is now just above the 100-line soft threshold; compact/archive it before the next substantial append.

## 2026-08-03 - Live-Accepted Baseline Protected and W5 Status Corrected
- Goal: preserve the first substantially improved live runtime before further tuning and correct the project record about short-term memory.
- **User-confirmed:** two complete games felt much better than the previous runtime; both used one LMG with about 400 ms weapon ADS time.
- Repository evidence: source was committed as `5d2f3be`; Release CTest passed `36/36`; installed and backup executables share SHA-256 `872F6FFF...50558C38` and the matching config snapshot is retained.
- Runtime changes include one final-output manual/AI envelope, fresh-position/feed-forward separation, replacement identity reset, target-admission-relative ADS lifecycle, latest-only telemetry, and restored SDL stick polling.
- **Correction:** W5 CausalMotionLedger/Causal Remaining v2 is not implemented. W3 ego-motion, PendingMotion timestamp repair and rollout diagnostics are shadow-only and do not create 150-200 ms memory actuation.
- **Inferred:** the live improvement is consistent with the final-output, identity, ADS and input fixes, but the single 400 ms LMG trial cannot establish cross-weapon ADS parameter fit.
- Context updated: handoff, accepted-baseline decision, current-state document, documentation index and August 3 acceptance record.
- Follow-up: keep runtime/config fixed and test an approximately 260 ms ADS weapon before deciding whether to tune acquisition or begin W4/W5 shadow work.
