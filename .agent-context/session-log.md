# Agent Session Log

Last compacted: 2026-08-03
Scope: native C++ FPS gamepad runtime, selector/tracker/controller, ADS, BodyLock, final-output fusion, causal response telemetry, benchmarks and runtime operations.

## How To Use This File

- Read the newest checkpoint, then the linked current-state and audit documents.
- August 1-3 pre-compaction detail: [archived session log](archive/session-log-2026-08-01-to-2026-08-03-pre-20260803-compaction.md).
- July 23-31 detail: [archived July session log](archive/session-log-2026-07-23-to-2026-07-31-pre-20260801-compaction.md).
- Earlier history: [pre-July-20 context](archive/context-through-2026-07-20-pre-compaction.md) and `session-log-full.md`.
- Labels: **User-confirmed** = live/explicit statement; **Repository evidence** = source/test/artifact; **Inferred/open** = explanation not isolated by controlled A/B.

## 2026-08-03 - Five-Case Audit and Schema-13 Control-Continuity Diagnosis

- Current installed diagnostic runtime SHA-256 is `25CF27F9946FF58404C4AE57795870E97512337EE1A90FC47776AB2826A10155`; capture `20260803T135819Z_61220_1` is schema 13 with ADS trace v3 and ego-motion v2.
- **User-confirmed:** the new run felt slightly more delayed. The user asked whether Luna reading logs in the background may have contributed.
- Repository evidence: common-field comparison against old five-case session `20260803T131652Z_65236_1` shows result-ready to ViGEm at `0.58/1.05 ms` versus `0.60/1.08 ms` P50/P95, so the Vision-result-to-controller/output transport did not materially regress.
- Repository evidence: within active acquisition, source cadence P50 stayed `7.85 -> 7.86 ms`, but P95 widened `15.71 -> 21.03 ms`; controller-consume cadence P50 improved `8.93 -> 8.04 ms`, while P95 widened `18.99 -> 21.00 ms`.
- **Inferred/open:** the latency feeling is compatible with increased tail jitter. Shared CPU/disk/memory/video-decoding contention is plausible, but the two runs are not a controlled background-load A/B and do not prove Luna caused it.
- Repository evidence: Vision publish to controller consume is `0.50/0.98 ms` P50/P95 in schema 13; source present to ViGEm is `11.31/17.28 ms`; 21,370 source frames are unique/increasing and observer counters show no duplicate/out-of-order consumption. A queued-vector or repeated-frame diagnosis is rejected.
- Repository evidence: the current fuser has a command-continuity defect. A fresh post-slew envelope clamp updates `previous_output_`; an intervening non-fresh tick then slews toward the larger legacy proposal. Schema 13 contains 44 large fresh clamps, with 31 opposite rebounds within 25 ms.
- Reviewer evidence: the old five-case session independently reproduces 109 fresh-clamp transitions and 79 rebounds. Case 2 and Case 5 strongly contain the mechanism; Case 1 is multi-target-confounded; Case 3 is weak; Case 4 has no qualifying event and remains unexplained.
- W3 is shadow-only and blocked: ADS valid `64.18%`, BodyLock valid `73.40%`, compute P95 `2.08 ms`. W4 has a calibrated clock and provisional `5-10 ms` peaks, but broad peak bands and W3-valid selection bias prevent promotion.
- ADS continuation obeys the intended contract: 135 ms is nominal, extension occurs only with a visible unacquired target, and the observed approximately 221 ms maximum is controller-tick reporting around the 220 ms ceiling, not runaway operation.
- Telemetry still needs per-target assist segmentation and a true first-material-AI field; current `first_fused_output` can capture manual output and trace identity can survive a target change.
- Full record: [Five-case and schema-13 control audit](../docs/project/FIVE_CASE_SCHEMA13_CONTROL_AUDIT_20260803.md).

## 2026-08-03 - Live-Accepted Baseline Protected

- Source baseline commit is `5d2f3be`; protected rollback executable/config identity is `872F6FFF...50558C38` under `artifacts/runtime-backups/`.
- **User-confirmed:** two games, using an approximately 400 ms ADS LMG, felt substantially better than the previous runtime; both sticks and all other controller inputs worked after SDL event ownership was repaired.
- The accepted improvement covers one final-output manual/AI envelope, fresh-position/feed-forward separation, target replacement reset and admission-relative ADS lifecycle.
- **Correction retained:** W5 CausalMotionLedger/Causal Remaining v2 is not implemented. W3, PendingMotion diagnostics and rollout telemetry are shadow-only and provide no 150-200 ms memory actuation.
- Decision: [protect live baseline and defer W5](decisions/DEC-2026-08-03-001-protect-live-baseline-defer-w5.md).

## 2026-08-02 - Final-Output and Causal-Chain Work

- Eight-clip audit found ADS over/under, selector/anchor jumps and BodyLock wrong-way swing; active present-to-ViGEm was normally 9-12 ms with unique delivery.
- W0-W2 established acquisition/provenance telemetry. W3 latest-only ego-motion remained shadow-only. OCR/profile lookup left the hot path and recoil returned to fixed downward feed-forward.
- P1 changed manual and AI from additive budgets into fallible proposals for one target-relative final output, separated BodyLock position from feed-forward, restored escape ownership and exposed the envelope telemetry now used to find the continuity defect.
- User-confirmed target policy: one credible target may be AI-primary; multiple credible targets may permit an intent-aligned handover through selector/coordinator ownership. It is planned, not implemented.
- Decisions: [predictive final-output envelope](decisions/DEC-2026-08-02-002-predictive-manual-ai-control-envelope.md) and [target-count-aware exit authority](decisions/DEC-2026-08-03-002-target-count-aware-manual-exit-authority.md).

## 2026-08-01 - Earlier Repair and Long-Session Audit

- Tasks 1-4 removed duplicate reliability gating, made shaper state target/mode-aware, enforced selector-owned no-selection and added a bounded replacement admission transition.
- **User-confirmed:** later acquisitions were nearly direct, BodyLock held during view movement, moving follow jitter fell sharply, and severe jumps stopped being the dominant usability failure.
- Later long-session ADS over/under did not correlate with elapsed time; movement plus acquisition/handoff timing was better supported than cumulative learner drift.
- The later dual-proposal policy was superseded by the single final-output envelope; do not restore additive manual-plus-AI force or fixed manual preservation floors.

## Earlier Durable Milestones

- 2026-07-31: accepted a bounded firing-disturbance observer; fresh position remains authoritative and only consistent residuals may inform target velocity.
- 2026-07-29 to 2026-07-30: Remaining work defined as fresh Vision error plus exogenous motion minus delivered camera work; one state is shared by ADS and BodyLock.
- 2026-07-28: response delay generalized in benchmarks, but mixed-motion confidence stayed insufficient; PendingMotion remained shadow-only.
- 2026-07-23 to 2026-07-24: live strafe increased overshoot and continued push; causal ego-motion was explored but not given production authority.
- 2026-07-16 to 2026-07-22: one production chain/owner baseline established; selector retained same-target short occlusion and physical LT owned one ADS epoch.
- Earlier: SDL stale-handle recovery, intent-aware target selection, green-cue hard rejection, performance-first fusion canvas and capture-exclusion boundaries.

## Current Architecture and Maintenance

```text
Vision -> selector -> TargetCoordinator -> TargetPlan
       -> ADS acquisition OR BodyLock follow
       -> AimDynamicsShaper -> VectorIntentFuser
       -> ADS-only brake -> fixed recoil -> virtual gamepad
```

One owner per lifecycle, mode transition, shaping state and final fusion decision. Fresh Vision corrects position; shadow ego-motion and future realized-work accounting may bridge gaps only after evidence gates. Keep this index under 100 lines; do not store secrets, personal video paths or raw telemetry dumps.
