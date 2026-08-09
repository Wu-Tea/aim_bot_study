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

## 2026-08-05 - Fixed-Shape Vision Throughput Optimization Accepted

- Goal: determine why the shared 4070 Super showed high compute utilization without full board power and improve YOLO throughput before deciding whether a dedicated RTX 4060 was necessary.
- Repository change: fixed TensorRT tensor addresses are bound once; the runtime uses a high-priority non-blocking CUDA stream; the fixed TensorRT inference segment is captured and replayed through CUDA Graph. Dynamic capture, ROI/preprocess and downstream result handling remain outside the graph.
- Benchmark change: C++ and Python dataset benchmarks expose binding, priority and Graph A/B controls plus `enqueue_cpu_ms` and expanded wall/GPU/output timing.
- Repository evidence: a 1,500-image A/B retained identical detections and exact 500-image float outputs. Wall P50/P95 improved `3.343/6.078 -> 1.207/2.432 ms`, enqueue CPU P50 `2.894 -> 0.083 ms`, and GPU-total P50/P95 `3.135/5.856 -> 1.015/2.213 ms`.
- Live evidence: same-config/same-engine 160 FPS sessions `20260805T120935Z_47940_1` and `20260805T125610Z_44588_1` used executable hashes beginning `69E8624C` and `57A78F84`. Capture-to-result P50/P95 improved `8.08/13.41 -> 5.42/9.34 ms`, active result rate `99.2 -> 127.8 Hz`, and source-present-to-ViGEm P50/P95 `13.05/20.63 -> 8.57/16.68 ms`.
- Hardware evidence: the matched stable HWiNFO window held 4070S core use and power essentially flat (`77.54%/84.94 W -> 77.26%/83.77 W`); power and thermal limiting stayed inactive. This supports an execution/launch-efficiency gain rather than a higher-power explanation.
- Verification: the candidate passed the relevant native tests, a one-shot runtime start, a 100-frame DXGI/model smoke test and exact-output comparison. One pre-existing CMake target remains unbuildable because its tracked source file is absent; it is unrelated to this change.
- User-confirmed: YOLO remained enabled during both game runs; logging conditions were intentionally kept the same; the user asked not to separately attribute logging-related stutter and explicitly requested recording and committing the validated optimization.
- Context updated: handoff plus accepted decision `DEC-2026-08-05-001`.
- Follow-up: preserve the validated executable hash and source commit; review the CUDA Graph contract after TensorRT engine, shape or driver changes.

## 2026-08-05 - Marker-Assisted Head-Glitch Tracking Discussed

- User-requested design direction: use the enemy yellow marker to help retain targets behind head-height cover or fences and learn, within a runtime session, a mapping from reliable target scale to marker-to-head/aim offset.
- **Proposed/inferred, not accepted implementation:** treat the marker as an auxiliary observation source for identity and ROI continuity, not as a second aim/control owner. Learn only from unambiguous same-frame pairs with strong direct body/head evidence; never train from marker-derived pseudo positions.
- **Proposed/inferred:** maintain a stable full-body-equivalent scale because an occlusion-truncated current box is not a valid distance label. Start with a robust proportional/monotonic model and explicit residual/coverage uncertainty before considering a short-lived low-authority pseudo-observation.
- User explicitly limited this discussion to design thinking; no marker code, telemetry, config or data was changed.
- Follow-up: first perform a read-only audit of the existing cue detector/association/continuation path, then propose shadow-only fields and promotion gates.

## 2026-08-07 - Pure-AI Smoothness and Target-First Output Clarified

- Goal and **user-confirmed boundary:** review a visibly non-smooth target-range clip in which no right-stick input was applied, so right-stick camera motion was pure AI (`M = 0`), and remove the ambiguity that AI merely supplements protected manual. The user considers the application better than most market alternatives as a subjective assessment while expecting substantial research headroom.
- Video-side evidence: all 1,198 decoded frames were unique, no frame interval exceeded 20 ms, and background-only motion around 10.3-11.4 seconds contained repeated same-direction speed losses of about 32-54% with one-to-two-frame recovery. Detailed controller logging was disabled.
- **Inferred/open:** the visible pulse shape is compatible with the proven fresh/non-fresh continuity defect and absent causal work accounting; video alone cannot assign a pulse to target demand, a controller branch or the game response model.
- **Accepted architecture clarification:** targetX/Y correctness is primary. Solve one final `T`; manual may be weakened, cancelled or ignored. `T = M + AI` is diagnostic accounting, not additive implementation or a promise to preserve `M`; W5 tracks delivered final `T` through `scheduled -> in-flight -> realized`. Decision: [target-first final output](decisions/DEC-2026-08-07-001-target-first-final-output.md).
- Post-W6 direction: survey maintained open-source mouse-to-gamepad projects. The old Python path sampled absolute cursor deltas into 5 ms windows, used a non-time-normalized curve plus EMA and additive AI, so empty/batched events could produce center/reverse chatter; reuse mature input/output plumbing, not that open-loop solver. Performance addendum: in user-split session `20260805T194515Z_35884_1`, ADS Vision measured about `135.5 Hz` while the game was 240 Hz and `160.5 Hz` after switching the game to 180 Hz; W3 CPU matching stayed latest-only at `1.98/2.21 ms` P50/P95 with no pending replacement, but the preceding grayscale CUDA/D2H stream synchronization remains serial and uninstrumented. Follow-up: repair continuity, add an event-triggered `D/P/R/M/T` trace, rerun the pure-AI regression, then instrument/remove the W3 staging wait before W3/W4/W5 promotion and later marker/M2G work.
