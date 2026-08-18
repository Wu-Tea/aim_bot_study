# Agent Session Log

Last compacted: 2026-08-03
Scope: native C++ FPS gamepad runtime, selector/tracker/controller, ADS, BodyLock, final-output fusion, causal response telemetry, benchmarks and runtime operations.

## How To Use This File

- Read the newest checkpoint, then the linked current-state and audit documents.
- August 1-3 pre-compaction detail: [archived session log](archive/session-log-2026-08-01-to-2026-08-03-pre-20260803-compaction.md).
- July 23-31 detail: [archived July session log](archive/session-log-2026-07-23-to-2026-07-31-pre-20260801-compaction.md).
- Earlier history: [pre-July-20 context](archive/context-through-2026-07-20-pre-compaction.md) and `session-log-full.md`.
- Labels: **User-confirmed** = live/explicit statement; **Repository evidence** = source/test/artifact; **Inferred/open** = explanation not isolated by controlled A/B.

## 2026-08-11 - Native I/R/D/T Single-Owner V1 Implemented

- **User-confirmed semantics:** D is always the desired hittable point on the
  current person (upper chest/head or a posture-valid visible point), R is the
  current recognisable hittable region, and directional input while I is owned
  is first a same-person D correction rather than an automatic detach/switch.
- Selector now owns I without score-only automatic replacement and publishes a
  source point plus R. Input purpose is explicit (`AcquireTarget`,
  `CorrectCurrentTarget`, `HandoverTarget`) so the same stick sample cannot vote
  for a challenger while it is correcting D.
- TargetCoordinator owns D separately from the observed source point, preserves
  normalized D inside a moving R, carries it through same-generation cue, and
  requests handover only after continued outward pressure at R's boundary.
- AssistControlStateMachine remains the sole final T owner. A correction axis
  gets immediate physical output instead of manual plus AI competition. The
  second D-specific deadzone was removed after review; IntentFilter is the one
  noise/deadzone owner, and a 0.05 micro-correction regression is green.
- Cue translates the prior R for no more than 180 ms of fresh same-generation
  evidence, remains aim-only, and cannot update its own source geometry.
- Telemetry schema 14 exposes source point, R, D, their sources, correction
  axes, boundary contact and exit intent. The current R is a lightweight
  detector-derived pose heuristic, not proven Body/Pose or occlusion truth.
- Repository evidence: known-bad incident `0.546220 / 0.142001 / 0.734222`
  (direct ack / cue ack / release step, RED) became
  `1.116667 / 0.876222 / 0.000000` on unchanged oracles (GREEN). Full runtime
  build, 43/43 CTest, 4/4 skill contracts and the real native pipeline one-shot
  passed. Broad benchmark runtime/plant/scoring code was intentionally not
  changed; one stale unit oracle was aligned to the wrong-identity hard gate.
- **Open:** matched Black Ops 7 video/input/schema-14 telemetry is still needed
  to accept R geometry, cue placement, provisional 180/250 ms calibration and
  gameplay feel.
- **Deferred only:** on first person recognition in either aim or idle-swarm,
  emit one D-pad Up press after edge/debounce semantics are specified. It was
  not implemented in this change.
- Decision: [native I/R/D/T single-owner V1](decisions/DEC-2026-08-11-002-native-irdt-single-owner-v1.md).

## 2026-08-10 - Cue Geometry, Manual Correction and Runtime Acceptance

- Three incident classes were isolated: cue geometry crossing selector identity,
  cue target-first control suppressing valid manual correction, and detailed
  telemetry contaminating active Vision throughput measurements.
- Cue offset state now resets at confirmed selector generation changes and is
  refreshed only by direct person-plus-cue observations. Cue-held output cannot
  train itself. A reconstruction moving more than 36 px from the prior
  same-generation point is rejected before aim authority.
- Cue continuation retains a bounded 45% physical correction only from reliable
  single-target evidence; it works at `T=0` and against a wrong cue direction.
  Cue remains aim-only and multi-target handover remains selector-owned.
- Repository evidence: Release full build, focused selector/controller/Vision/
  timing tests and 40/40 CTest pass. Installed runtime SHA-256 begins
  `C99E237C`; a local executable/config backup is under the build runtime-backups
  directory.
- Runtime smoke without physical LT held controller/output near 1000 Hz and
  result-to-ViGEm P99 at 0.875-1.125 ms with no summary writer drops. Active
  Vision remained at its expected idle ~20 Hz, so active throughput and whole
  source-present P99 require live ADS validation.
- Detailed event telemetry is disabled for that validation. The low-overhead
  five-second performance summary remains enabled with stdout disabled.

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

## 2026-08-10 - Low-Rate Legacy Control Stack Retired

- **User-confirmed scope:** audit the current native Vision-to-controller chain,
  record how the old approximately 80 Hz compensation path accumulated, delete
  unused or harmful layers, synchronize project context and commit.
- **Current chain:** fresh unique native Vision -> selector ->
  `VisionDeliveryGate` -> `TargetCoordinator`/one `TargetPlan` -> ADS or
  BodyLock -> `AimDynamicsShaper` -> `AssistControlStateMachine` -> AutoFire ->
  recoil feed-forward -> ViGEm. A no-new-source controller tick retains only
  the immutable plan; a fresh no-target result releases generic authority.
- Removed generic projection/coast/hold and player-motion bridges; legacy AI,
  ADS carry/brake and BodyLock short-plan/lifecycle code; axis/vector/causal
  mix and pending-output layers; old trackers/FPS tracker package; W3/W5
  observer/pending/rollout/online-learning surfaces; duplicate replay cadence,
  AimPerf/config/telemetry contracts and tests/benchmarks that only kept those
  branches alive.
- A late source audit found and removed native `AimEnhancementPipeline`: it ran
  after selection with its own prior-target/velocity state, applied
  lead/catch-up/near-target damping, then fed controller layers that modeled
  motion again. Its pybind API, `enhance_ms` contract and native parity tests
  were retired.
- The same audit removed `ControlResponseEstimator` and its response-hint
  fields after proving that no production adapter or runtime caller supplied
  observations, so the compiled estimator stayed zero-valued. It also removed
  coordinator fallback election for frames without selector identity; those
  frames now fail closed.
- Root cause: local gap compensators were added without removing the prior
  owner. Selector, tracker, AI, fusers, lifecycle, carry/brake and research code
  used different identities, freshness clocks, previous outputs and reset
  boundaries while modifying the same stick. This explains sticky handover,
  elastic same-direction stacking, delayed release, fresh/non-fresh rebound and
  manual suppression better than any single timeout.
- Repository verification: clean CMake + full Release build PASS; `43/43`
  CTest PASS; focused Python native-boundary/performance tests `32/32` PASS;
  retired config keys are unknown/inert; flick-handover integration remains
  green.
- Sustained smoke: 12/12 production-only combinations PASS (three seeds,
  ADS/BodyLock, strafe off/full reversal, requested Vision 180 Hz, 36 ms short
  occlusion). Pre/post reports are not matched because the pre report used the
  legacy harness/schema. Shared evidence is mixed: output delta/jerk and stale
  stop carry improved, while synthetic error and oscillation counters worsened.
  Matched live validation remains open.
- Final provenance audit replayed the flick-handover fixture on the completed
  C++ build, refreshed candidate runtime/executable/source hashes and passed
  the complete regression contract with zero issues. The release contract
  script was also moved from the deleted gamepad benchmark/runtime label to the
  production-only sustained smoke and current source-age gate.
- **Inferred/open:** no exact percentage is assigned to an individual retired
  layer; source ownership conflicts are verified, but per-layer live causal
  contribution was not isolated.
- SyncSet: `sync-20260810-001`; sensitive raw telemetry, binaries, personal
  paths and unrelated untracked artifacts excluded. Reviewer disposition:
  `accept_draft`.
- Accepted decision:
  [retire the low-rate legacy control stack](decisions/DEC-2026-08-10-001-retire-low-rate-control-stack.md).

## 2026-08-11 - Benchmark Review and Incident-First Validation Accepted

- **User-confirmed symptom:** the system was not vertically settled on the
  intended body point; sustained downward correction felt blocked, and a later
  larger input or authority release caused an abrupt disengagement. Incorrect
  cue geometry is plausible but remains unproven without synchronized runtime
  evidence.
- Workspace audit retained the new product/acceptance documents. Five apparent
  native modifications were byte-identical to their index blobs and were
  cleared as file-status noise; untracked evidence was preserved. The latest
  controller commit was not blindly reverted because it fixes a separately
  proven zero-manual centered-motion discontinuity.
- Source review found that the existing Sustained AimLab is already a useful
  deterministic micro-plant, but its PASS is smoke-level and aggregate metrics
  can hide the confirmed manual-suppression/release-cliff failure.
- **Accepted sequence:** Phase 0 creates one production-controller RED fixture
  with trigger assertions, hard symptom oracles and a counterfactual; Phase 1
  reuses the existing micro-plant; measured COD profile calibration comes only
  after those gates prove useful. No Body/Pose model or complete game simulator
  is a Phase 0 prerequisite.
- Production algorithm changes are prohibited until the RED owner checkpoint
  is reviewed. Current known-bad behavior is expected to exit nonzero.
- SyncSet: `sync-20260811-001`; personal media paths, raw telemetry, binaries,
  secrets and exact unproven video internals excluded. Reviewer disposition:
  `accept_draft`.
- Accepted decision:
  [incident-first gameplay validation](decisions/DEC-2026-08-11-001-incident-first-gameplay-validation.md).
- **Phase 0 RED completed:** a dedicated executable runs the production
  `NativeGamepadController` through direct observation, twelve fresh
  same-generation cue frames and one fresh no-target release. It is not
  registered as a normal CTest and no production algorithm was changed.
- Trigger and neutral counterfactual both executed. Direct downward
  acknowledgement measured `0.546220` and passed; cue acknowledgement measured
  `0.142001` against the fixed `>= 0.25` oracle; held-input cue release stepped
  `0.734222` against the fixed `<= 0.25` oracle, while neutral release stepped
  only `0.076222`.
- The current executable returned 1 (expected RED), a repeated run produced an
  identical report hash, the regression contract validated `PASS` with zero
  issues, and the existing Release CTest suite remained `43/43` green.
- RED bundle:
  `artifacts/regressions/vertical-correction-cue-release-20260811/`.
- Owner checkpoint: review desired-point, cue and per-axis authority contracts
  before authoring a candidate. Do not change the declared fixture thresholds
  based on candidate output.

## 2026-08-12 - Controller V1 and Final-Plan Auto-Mark Completed

- **User-confirmed scope:** fix the Controller and auto-mark defects in one
  bounded production refactor, without restarting the running application and
  without adding another patch mechanism or Body/Pose model.
- Controller now uses per-axis `T - M`: compatible manual work is not stacked,
  missing work remains assisted, opposing manual may be damped but never
  reversed, and ordinary/down/firing-down damping ceilings are 35%/10%/0%.
- ADS authority is full after selector admission with or without cue. Cue,
  visibility, reliability and distance cannot reduce ADS; BodyLock remains
  evidence-scaled. A selector-confirmed replacement starts a new ADS acquisition
  while LT remains held.
- Firing-down moves D within R, never arms downward handover, and short physical
  fire gaps use the recent-fire contract. A state-publication audit also fixed
  completed ADS being exposed as both BodyLock and active acquisition.
- Selector accepts only current model class 0, rejects green-friendly candidates,
  preserves identity without publishing stale coordinates on short dropout, and
  retains marker-loss memory to suppress upright/drifting corpse reacquisition.
- Auto-mark is now a final-plan transaction: L3/LT creates one 250 ms request;
  two fresh same-generation direct-person/current-cue plans with crosshair inside
  valid R emit one 50 ms D-pad Up. L3 can wake Vision but grants no aim authority;
  physical D-pad Up passes through.
- Telemetry schema 17 records mark pending/fire/cancel, confirmation,
  selector-generation/scope and block reason plus visual/evidence authority.
- The Aimlab release score now fails closed to zero on any wrong-person strong
  ADS event; an obsolete scenario assertion no longer requires a fixed path to
  reproduce a wrong lock.
- Verification: candidate full Release build PASS; official CTest `49/49` PASS;
  direct candidate replay `49/49` PASS; product-contract fixture RED -> GREEN;
  telemetry record `1848 < 2576` bytes; `git diff --check` PASS.
- Candidate runtime:
  `native/vision_native/build/candidate-20260812/cod_native_runtime.exe`, SHA-256
  `64FCDF14DB6B4D04FCDC884BE3D863DD58B609ED946DE1EC6033E9227F8AC4A5`.
  Codex did not launch it or stop the user's running application.
- Regression package:
  `artifacts/regressions/controller-v1-product-contract-20260812/`.
- Current local `config.toml` keeps auto-mark disabled; example config enables
  it. Matched live gameplay acceptance remains open.
- L3 mark requests now use configurable `gamepad.enemy_mark.l3_cooldown_ms`
  (default/current 1000 ms); LT requests are unaffected. Focused gesture and
  config parsing tests pass. Standard Release SHA-256 is
  `51F1A3ACDD204D1BA064DFA873A64CAB9DBA2C92E76BB849252BDDB5B1071861`.

## 2026-08-16 - Benchmark and Redundancy Audit Recorded

- Goal: review whether the current benchmark inventory contains duplicate or
  obsolete evidence, and identify unused code or repeated infrastructure. This
  was a read-only audit; no production source, benchmark or build output changed.
- Verified: `cod_native_aimlab_benchmark` mostly scores preconstructed outputs
  instead of executing the production controller and prints PASS without a
  quality threshold. Two named selector-intent scenarios map to the same path.
  It should not be treated as current production-controller evidence.
- Verified: Sustained AimLab does execute `NativeGamepadController` through the
  production adapter, but its PASS is harness/smoke-level rather than a gameplay
  quality oracle. Incident regressions remain the appropriate hard gates.
- Verified: the CTest left-strafe `off` cohort is repeated inside the `both`
  cohort. Prefer separate `off` and `full-reversal` tests for equal coverage and
  clearer attribution.
- Verified: maintained benchmark documentation and several PowerShell runners
  still reference removed targets, old build directories, retired CLI options
  or old report fields. The native production contract runner and current pulse
  smoothing comparison remain compatible with the production schema.
- Verified: the native replay writer/reader/runner are compiled but have no
  production caller; the reader does not parse frames, the writer omits fields
  required by metrics, and existing tests bypass the broken serialization path.
- Verified: recoil visual/debug plumbing is a no-op layer; several structs and
  an unbuilt telemetry benchmark have no consumers. Repeated offline JSON
  parsers/escaping, ad-hoc native test assertions and inconsistent percentile
  helpers are consolidation candidates.
- Verified: no broad case was found where an already-linked mature C++ library
  is fully reimplemented. The strongest library opportunity is dev-only JSON
  Schema instance validation; custom graph, ownership and hot-path control
  checks remain project-specific.
- Proposed, not user-approved: clean evidence integrity first, then remove dead
  runtime/replay and no-op layers, then consolidate offline infrastructure.
- Follow-up requested by the user: audit the current Vision frame publication
  chain and measure end-to-end application latency because live feel appears
  worse after the refactor. Do not assume control arbitration is the cause until
  frame freshness, publication loss and stage latency are measured.
- SyncSet: `sync-20260816-001`; reviewer disposition: `accept_draft`. Secrets,
  personal media paths, raw telemetry and unrelated untracked files excluded.

## 2026-08-16 - Current Vision Chain Latency Self-Test

- Re-ran the current runtime with the existing lightweight performance-summary
  path after the user stopped the normal process. Production source and
  `config.toml` were not changed; the bounded test used an analysis-only config
  under `artifacts/telemetry-audits/20260816-vision-delivery-latency-audit/`.
- The 30.865 s full-chain self-test completed normally with no residual runtime
  process: controller/output 971.965 Hz, 891 controller-accepted Vision results
  (28.867 Hz), and zero performance-writer drops. The desktop source was not a
  sustained active gameplay stream (`aiming_ratio=0`), so this is a chain
  latency self-test rather than a live gameplay A/B.
- Current mean result-to-controller latency was 0.422 ms;
  Vision-publish-to-ViGEm was 0.437 ms; controller-consume-to-ViGEm was
  0.017 ms; controller-submit-to-final-output was 0.006 ms. These measurements
  show no added frame or material queue inside the refactored controller path.
- Source-present-to-ViGEm was 3.329 ms mean in this low-change desktop run, with
  a median window P95 of 10.875 ms. It must not be compared as an equivalent
  gameplay result to the historical active run (9.54 ms mean / 13.375 ms
  median-window P95), because scene cadence and aiming state differ.
- Vision publication remains a latest-only mailbox. DXGI may coalesce display
  presents (`AccumulatedFrames > 1` was 18.631% here), and lightweight summaries
  count accepted results but do not expose worker-published sequence gaps.
  Therefore the current evidence proves fast same-tick consumption of accepted
  results, but does not prove lossless delivery of every produced inference.

## 2026-08-17 - BodyLock Axis-Local Position/Motion Conflict Accepted For Repair

- **User-confirmed priority:** fix Controller first and record the defect;
  Vision remains a separate concern because its misses and geometry depend on
  actual imagery. The user authorized implementation after the record.
- Six detailed sessions and four runtime identities passed audit preflight.
  There were 53 durable manual/AI conflict pulses, including 15 strong
  near-center motion-dominated pulses across all six sessions and three cases
  where manual moved toward the published source point while AI opposed it.
- Vision is not globally perfect: 4,097/15,072 authoritative BodyLock samples
  used cue continuation. That does not explain the clean controller incidents:
  13/15 strong pulses have complete committed-observation joins with fresh,
  strong, stable geometry and no frozen box-jump anomaly.
- Verified source defect: `ResponseModelAimSolver` applies a 2-D radial
  position/motion constraint while final arbitration is per-axis. Orthogonal
  same-direction work can mask an axis that reverses current position. Track
  508 exhibits this exact vector relation; the fully joined track 140 case
  independently shows final reversal against current geometry and manual.
- **Inferred implementation direction, not yet verified:** preserve motion
  feed-forward but enforce the existing current-position sign contract on each
  axis. Do not create another owner or fixed manual/AI blend.
- Required RED contract: reproduce X and Y axis leakage through production
  BodyLock solver plus final state machine; require no reversal and at least
  25% same-direction manual acknowledgement. Counterfactuals must preserve
  target-first handling of genuinely wrong-direction coupled manual, explicit
  exit passthrough and aligned motion.
- Audit evidence:
  `artifacts/telemetry-audits/20260817-bodylock-sticky-manual-ai/`.
- SyncSet: `sync-20260817-001`; reviewer disposition: `accept_draft`, with
  deferred archive-backed compaction because the primary session log is above
  its soft size threshold. Raw telemetry, media, binaries, personal paths,
  secrets and unrelated working-tree changes are excluded.

## 2026-08-17 - BodyLock Axis-Local Position/Motion Repair Completed

- Replaced the response solver's whole-vector radial constraint with a single
  axis-local primitive. X/Y motion feed-forward remains active when compatible;
  when it opposes current source-owned position, it is smoothly bounded so the
  combined request cannot cross that axis's current sign.
- No Vision, ADS snap, recoil, explicit exit, authority, response-curve or final
  arbitration weights were changed. The existing public constraint reason was
  retained for telemetry/schema compatibility.
- The frozen production-path incident changed from RED to GREEN: X/Y reversal
  count `2 -> 0`; minimum manual acknowledgement `-0.956947 -> 1.021007`; aligned
  motion, wrong coupled manual and explicit exit counterfactuals remained `2/2`.
- The fixture trigger predicate was corrected after the first candidate run:
  trigger now describes the input conflict and no longer requires the bad output
  to survive. Scenario vectors and all five oracle thresholds were unchanged;
  this correction is recorded in both regression manifests.
- Added solver and BodyLock unit coverage for the cross-axis masking relation and
  registered the incident executable as a permanent CTest gate.
- Release build passed; focused controller/incident tests `13/13`; full Release
  CTest `63/63`; scoped diff review and `git diff --check` passed.
- Candidate runtime SHA-256:
  `7bdd917e0e676e48847e3c969c1e0de9f4d87be5d9c0db564e1fa0629e4d6546`.
- Regression package:
  `artifacts/regressions/bodylock-position-motion-axis-conflict-20260817/`;
  complete contract artifact SHA-256
  `3c0e995f916b07a64e11f98ac9301e5b2966fe95d8874c1862898fb4dc96e848`,
  status PASS, issues `0`.
- SyncSet: `sync-20260817-002`; reviewer disposition: `accept_draft`. The primary
  session log remains above its soft size threshold; archive-backed compaction
  is still deferred because it was not part of the user's request.

## 2026-08-17 - Direct Experiment Retired; Production Response Lessons Retained

- **User-confirmed result:** the simple Direct/PID-style ADS plus BodyLock path
  was materially faster and more accurate on immediate correction than the
  layered production behavior, but pervasive high-frequency oscillation made it
  unacceptable. Direct and the subsequent hybrid are abandoned for production.
- Direct implementation, runtime selection, configuration and tests were removed
  rather than kept as a dormant second production architecture.
- Durable benefit: Direct remains a diagnostic counterfactual for whether fresh,
  authoritative target error produces material output in the correct direction
  on the current tick. Its lower latency helped expose production no-output and
  stale-arbitration defects.
- Durable limitation: raw frame-error response amplified measurement/geometry
  changes and lacked mature completion, target-generation, cue, explicit-exit,
  manual-correction and one-LT/one-snap ownership policies. Adding those back
  incrementally began recreating the original layer stack.
- Production absorbed only incident-proven behavior: close ADS response changes
  continuously from nominal 135 ms toward 60 ms by target visual size; BodyLock
  fresh-position ownership removes stale opposing manual work outside the
  existing settle envelope. One LT still owns at most one ADS snap.
- Repository evidence: full Release build and `68/68` CTest pass. The new
  BodyLock latency fixture moved from six wrong-direction ticks at `+0.0975` to
  target direction `-0.25` on tick zero with four counterfactuals preserved;
  complete regression contract status is PASS with zero issues.
- **New user-confirmed observation:** ADS now has no apparent problem, while
  sustained horizontal BodyLock tracking can still fall behind. This is the
  next telemetry audit question; target velocity, identity/freshness and final
  arbitration remain separate hypotheses until joined evidence distinguishes
  them.
- Decision: [retire Direct from production](decisions/DEC-2026-08-17-001-retire-direct-controller-experiment.md).
- SyncSet: `sync-20260817-003`; reviewer disposition: `accept_draft`. Raw logs,
  binaries, personal paths and unrelated worktree changes are excluded. The
  oversized primary session log was not compacted in this task.

## 2026-08-18 - Sustained Horizontal BodyLock Demand Defect Isolated

- **User-confirmed symptom:** on continuously laterally moving targets,
  BodyLock still falls behind and can feel as though it sticks to an old screen
  position instead of sustaining the target's motion. ADS remains outside this
  repair scope because the user currently considers ADS behavior acceptable.
- The newest session's whole-session audit is strictly blocked by one truncated
  final JSONL row in an otherwise stopped-but-still-active manifest. The first
  complete telemetry shard independently passed the unchanged preflight with
  zero issues; the malformed row was neither deleted nor repaired.
- A strict same-direction follow cohort required direct observed BodyLock,
  authority, stable identity, no gap above 20 ms, no manual correction/exit,
  material X error, and manual/error/raw-AI request pointing in the same
  direction. It produced 27 runs across 22 target IDs and 340 samples.
- Final arbitration preserved at least 95% of `max(manual, BodyLock request)`
  on 328/340 samples (96.47%). Nevertheless, error continued growing in the
  same direction at at least 35 px/s on 90/340 samples (26.47%); all 90 were
  already fully delivered. Only one sample approached the configured 0.60
  BodyLock request cap. A separate stable-rate cohort found material Dynamics
  Shaper attenuation on only 11/557 samples (1.97%).
- Representative tracks 445, 460 and 626 had stable identity, strong authority
  and 90--100% exact committed-observation joins. Track 445 ran about 190 ms:
  error expanded from roughly 7 to 61 px while median manual/final output was
  about 0.55/0.57 and median raw BodyLock request only 0.34. Tracks 460 and 626
  reproduced growing error while their requested output was passed through.
- **Evidence-supported inference:** `TargetCoordinator` publishes observed
  screen-relative point velocity as target velocity/error-rate, and
  `BodylockFollowController` turns it into position plus relative-motion
  correction. That is not a true desired total `T`. The physical relation is
  approximately `screen_error_rate = target_motion - response * delivered`, so
  steady target following requires reconstructing the sustaining motion from
  both terms. As relative error-rate shrinks, the current producer also shrinks
  even though the camera still needs nonzero motion; downstream arbitration
  cannot invent the omitted demand.
- Restoring additive manual-plus-AI is explicitly excluded. The bounded repair
  direction is a BodyLock-only target-motion/total-demand field derived from
  causally capture-aligned delivered output plus a position correction, with
  stable-identity/reset/reversal safeguards. Blindly adding the previous output
  is unsafe because it may be temporally unaligned or belong to a prior target,
  manual transition, firing state or recoil context.
- The response estimator inferred roughly 1090--1160 px/(stick*s) in the
  representative incidents versus its 500 fallback. This can halve correction
  magnitude and is a credible secondary contributor, but current evidence does
  not prove the higher estimate is wrong for the game.
- Required next checkpoint: freeze the track-445/626 mechanism as a
  production-path RED before modifying the demand producer. Counterfactuals
  must cover a stationary world target under manual camera motion, target
  replacement/reset, target reversal and non-additive final-output ownership.
- Audit: `artifacts/telemetry-audits/20260817-latest-bodylock-horizontal/`.
  SyncSet: `sync-20260818-001`; reviewer disposition: `accept_draft`. Raw logs,
  video, binaries, personal paths, secrets and unrelated worktree changes were
  excluded. Archive-backed compaction of the oversized primary session log
  remains deferred to a separate task.

## 2026-08-18 - Capture-Aligned BodyLock Total Demand RED to GREEN

- **Goal:** repair the isolated sustained-horizontal BodyLock producer defect
  without changing accepted ADS behavior, one-LT/one-snap ownership, final
  manual arbitration, Vision, or independent recoil composition.
- A frozen 1 kHz controller/180 Hz Vision production-path fixture established
  known-bad RED: a same-direction target speed step from required stick `0.40`
  to `0.60` never held the required output within 900 ms, grew error by
  `21.040884 px`, and finished with `16.869894 px` residual. Trigger, no-step,
  slowdown and target-generation replacement controls were fixed before the
  implementation.
- Added one focused `BodylockTargetMotionObserver`. It reconstructs target
  motion from raw inter-capture screen rate plus the pre-recoil output averaged
  over the same capture interval and 9 ms effect alignment. It uses no start
  magnitude gate: two samples establish the estimate, median-of-three rejects a
  one-frame spike, rise is bounded, reversal first discharges old work, and
  slowdown releases faster than rise.
- `TargetPlan` now carries the BodyLock-only target-motion total. A valid direct
  `Observed` estimate uses full motion weight because it is already total
  sustaining demand; until valid, BodyLock retains the old conservative
  screen-relative fallback. Cue continuation cannot train or consume this
  authority, preserving close-target full cue hold and far-target attenuation.
- **Verified result:** response latency `23 ms`, step error growth `2.698075 px`,
  recovery overshoot `0 px`, final residual `0.735421 px`; all four oracles and
  three counterfactuals pass. Full Release CTest is `70/70`; complete regression
  contract status PASS, issues `0`, artifact SHA-256
  `adf7e720d24dea7f641620a00518c222494138ca279bcdcb9ea9ab0cb3addc97`.
- Candidate runtime:
  `native/vision_native/build/Release/cod_native_runtime.exe`, SHA-256
  `bbdb1bc5344b094d7553623e7a735c8a37e2f53461d65c0cfff87e9563dfe8a8`.
  Architecture contract is version 4 and records the new state owner.
- **AI-inferred/open:** the offline incident proves the original upstream demand
  omission is repaired under the declared plant, but live firing/FOV/geometry
  noise remains unverified. The current response estimate and any need to bound
  stronger same-direction manual require separate evidence; they were not
  changed in this candidate.
- Verification-tool gap: `native_pipeline_contract.bat -SkipBuild
  -SkipBenchmark` rejects the pre-existing identifier `pre_recoil_stick`; the
  same matches exist at HEAD. This was not "fixed" by changing recoil ownership.
- SyncSet: `sync-20260818-002`; reviewer disposition: `accept_draft`. No decision
  or subagent brief was created. Raw logs/video, binaries, personal paths,
  secrets, stash state, `acquisition_gesture_purpose_incident.json` and unrelated
  changes were excluded. Session-log compaction remains deferred to an explicit
  future task.
