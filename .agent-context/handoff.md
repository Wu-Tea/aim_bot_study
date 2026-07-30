# Agent Handoff

Last updated: 2026-07-30
Active scope: native C++ FPS gamepad runtime, selector/tracker/controller, ADS, BodyLock, intent fusion, AutoFire, recoil boundary, benchmark semantics and runtime operations.
Staleness trigger: refresh after a production pipeline owner changes, a benchmark/config schema changes, crop/model identity changes, global policy learning begins, or new live evidence contradicts this state.

## Current Objective

Preserve the single-owner control architecture while improving real gameplay speed, tracking and robustness. New optimization must reduce measurable defect burden and retain natural hand feel; benchmarks are instruments for improving the program, not product features by themselves.

## Current State

- Runtime architecture is observation/intent -> TargetCoordinator -> immutable TargetPlan -> ADS or BodyLock -> AimDynamicsShaper -> VectorIntentFuser -> ADS-only brake -> recoil/final output.
- Refactor B removed duplicated gates and made control more natural, but initially reduced ADS/BodyLock strength and damping. Response-model control, better near-target feedback and causal vector fusion recovered capability without restoring the duplicate stack.
- ADS snap is scoped to one physical LT epoch. A new target while LT remains held cannot restart strong snap.
- Selector keeps the near committed target through short occlusion, hard-rejects green friendly cue and treats yellow enemy cue as auxiliary evidence only.
- Production Vision uses the dynamic-ROI path with a `480x384` inference
  tensor. The older fixed `480x416` engine matrix is historical evidence only.
- AutoFire contract is 100 ms pulse period, at least 30 ms pressed, no synthetic fire for weak/cue-only targets, same-tick release on fresh miss, physical RB/RT passthrough.
- Background VBS start/stop launchers run without a console window and prevent duplicate instances; the process remains normally visible to Windows.
- High-rate telemetry/perf logging is opt-in. Fresh sessions use manifests and whole-session cleanup.
- Complete July 16-20 history: [AIM_CONTROL_OPTIMIZATION_HISTORY_20260716_20260720.md](../docs/project/AIM_CONTROL_OPTIMIZATION_HISTORY_20260716_20260720.md).
- Fresh single-target Vision can now strengthen BodyLock correction of only a
  wrong radial manual component for a 16 ms envelope. ADS and tracker-only
  behavior remain on their prior policies; tangent stays fully user-owned. The
  configurable floor defaults to `0.35` and `1.0` disables it. Acceptance:
  [FRESH_VISION_MANUAL_COUNTER_CORRECTION_ACCEPTANCE_20260722.md](../docs/project/FRESH_VISION_MANUAL_COUNTER_CORRECTION_ACCEPTANCE_20260722.md).
- A causal vision blind-window benchmark now separates capture, result-publication, controller and delayed-response clocks. B0-B2 retain 675-episode fixture and production-controller K1 artifacts plus 0.70/0.80/0.90 strength mutations; every production artifact fingerprints its config.
- Reusable method: [EVIDENCE_DRIVEN_REALTIME_CONTROL_OPTIMIZATION.md](../docs/project/EVIDENCE_DRIVEN_REALTIME_CONTROL_OPTIMIZATION.md).
- Cross-project adoption entry: [REALTIME_CONTROL_OPTIMIZATION_START_HERE.md](../docs/methods/REALTIME_CONTROL_OPTIMIZATION_START_HERE.md).
- Ready-to-paste onboarding prompt: [ADOPT_EVIDENCE_DRIVEN_CONTROL_OPTIMIZATION.md](../docs/methods/prompts/ADOPT_EVIDENCE_DRIVEN_CONTROL_OPTIMIZATION.md).
- A fixed-seed 60-second full-speed left-strafe baseline is retained on
  `codex/aimlab-left-strafe-20260723`; it showed tracking -12.17%, overshoot
  area +68.16%, circle exits +62.50%, and continued obsolete push +461.90%
  versus matched no-strafe runs.
- The same worktree now contains a fixed-size causal ego-motion estimator
  through `1892f63`. Focused estimator tests pass and Task 2 spec review passed.
  It is not connected to `TargetCoordinator`, so current `dev` and the live
  runtime do not consume it. The final Task 2 code-quality re-review was
  interrupted at the user's pause request.
- Controller-rate Stage 2 is retained on
  `codex/controller-rate-observer-20260728` through `54aa56c`. A windowed
  physical-stick event posterior reduced planted-delay MAE from 15.000 ms
  (uninformed controller baseline) to 1.889 ms, reached 85/90 rounds within
  +/-5 ms and made zero boundary selections. Mixed-motion response residual
  remains 0.9412, so executable confidence and rollout stay at zero.
- The broader 240-round scenario audit preserves exact controller output and
  gets 239/240 delay estimates within +/-5 ms, but response-prediction
  confidence remains zero. Full-reversal left strafe is the dominant stress:
  ADS overshoot area +58.6% / continued push +128.4%; BodyLock +42.6% /
  +69.5%. A 36 ms occlusion is much smaller in the current matrix.
- A 350/500/700 px/s and strong/weak slowdown matrix confirms higher
  sensitivity can improve ADS mean error, while one global 2x2 response matrix
  does not generalize across mode, slowdown and strafe state. Evidence:
  `artifacts/benchmarks/controller-rate-observer-20260728/GENERALIZATION_AUDIT_20260728.md`.
- `PendingMotionModel` now includes signals delivered after the latest Vision
  capture through the actual controller decision time. It remains shadow-only;
  no production TargetPlan or controller output consumes it.
- Player-POV stress benchmark `codex/player-motion-benchmark-20260729`
  (`afdeaf5`, evidence `58984cd`) retains 120 paired one-minute runs across
  strafe/slide/jump/combined, stationary/moving enemies, pure/mixed input and
  ADS/BodyLock. Combined motion cuts tracking about 43-50%; vertical POV
  disturbance is worse than strafe alone.
- Remaining-work feedback is now on `dev`: fresh Vision resets the work
  anchor, while delivered camera work after capture is subtracted before ADS
  and BodyLock consume the plan.
- BodyLock continuation and initial ADS acquisition now use observed target
  size to expand their configured pixel radius. ADS applies this only before
  the first snap target is committed; holding ADS cannot rearm wide snap.
- A deterministic gun-kick Vision disturbance can be combined with 36 ms
  occlusion in sustained AimLab. It reproduced a 30.2% pure-BodyLock tracking
  loss and 175.6% more continued push. Production velocity alpha `0.15` is the
  retained conservative response: pure gun-kick tracking +12.5%, mean error
  -8.1%, continued push -7.6%, smooth bonus +18.4% versus `0.20`.

## Next Action

1. Live-smoke the `0.15` tracker response and size-aware ADS selection on the
   dynamic-ROI `480x384` runtime, especially firing-time BodyLock jitter,
   initial multi-target ADS selection and close vertical targets.
2. Compare the next perf-log episodes against the retained gun-kick/occlusion
   fixtures before changing prediction or adding another gate.
3. Resume event-locked response learning only as shadow planning; do not add a
   post-controller brake, global response matrix or strength scale.

## Blockers

- No current checked-in final legacy/vector A/B artifact exists; conversation-only vector-fusion percentages are not independently auditable.
- Real gameplay `config.toml` is local/untracked and must be fingerprinted per session before it can support numeric comparison.
- Cross-target global optimum learning is planned but not implemented.
- K2-K4 fixtures and ADS/far-closing guardrails are not yet connected to the blind-window executable, so K1 alone cannot authorize a production policy change.
- Task 2's last code-quality re-review is incomplete because work was paused.
- Delay identification is no longer the main blocker. Mixed-motion response
  prediction is not control-ready: mean residual is 0.9412 and the final
  Stage-2 matrix correctly grants zero rollout decisions.

## Active Questions

- Does remaining near-target stickiness come from slowdown, vector ownership, BodyLock feedback, or delivered-output shaping?
- How much small/far target authority can be reduced continuously through existing reliability without hurting valid small-target tracking?
- Can a shadow tail-value estimator improve 500-1000 ms sequence burden without increasing switches, interruption or handoff residual?
- In strong assist, are large live overshoots caused primarily by stale
  relative-motion prediction, AI/manual same-direction stacking, or late
  reversal after center crossing?
- Can ADS arrival planning remain fast while its ownership window is aligned
  with sprint-to-fire latency? See proposed
  [DEC-2026-07-29-001](decisions/DEC-2026-07-29-001-decouple-ads-arrival-and-ownership-window.md).
- Does the conservative `0.15` velocity response remove live firing jitter
  without making abrupt close-target reversals feel late?

## Relevant Decisions

- Authority before strength: verify target, mode, ownership and delivery before tuning gain.
- One owner per target lifecycle, intent interpretation, fusion decision, stateful shaper and AutoFire decision.
- ADS is point acquisition; BodyLock is trajectory following and may retain bounded target inertia.
- ADS Brake is ADS-only and must never be smuggled into BodyLock through a shared gate.
- Raw physical stick remains available; deliberate escape overrides AI-owned candidates.
- Online response learning is memory-only, confidence-gated and weapon-name independent.
- Hindsight oracle measures headroom only; runtime policy must use causal information.
- Recoil remains final feed-forward and cannot consume target/tracker state.
- Benchmark artifacts without runtime/config/schema identity are historical evidence, not authoritative baselines.
- User-confirmed: do not specialize around one benchmark metric. Prefer a
  slightly smaller local gain that survives diverse scenarios over a large
  single-cohort result that introduces unexplained runtime behavior.

## Files To Read First

1. [Compact session log](session-log.md)
2. [July 16-20 causal history](../docs/project/AIM_CONTROL_OPTIMIZATION_HISTORY_20260716_20260720.md)
3. [Cross-project adoption entry](../docs/methods/REALTIME_CONTROL_OPTIMIZATION_START_HERE.md)
4. [Optimization methodology](../docs/project/EVIDENCE_DRIVEN_REALTIME_CONTROL_OPTIMIZATION.md)
5. [Response-model acceptance](../docs/project/RESPONSE_MODEL_AIM_CONTROL_ACCEPTANCE_20260718.md)
6. [Brake episode acceptance](../docs/project/BRAKE_EPISODE_BENCHMARK_ACCEPTANCE_20260719.md)
7. [Counterfactual acceptance](../docs/project/COUNTERFACTUAL_CONFLICT_BENCHMARK_ACCEPTANCE_20260719.md)
8. [Causal vector fusion design](../docs/superpowers/specs/2026-07-19-causal-vector-intent-fusion-design.md)
9. [Global policy learning plan](../docs/superpowers/plans/2026-07-19-global-aim-policy-learning.md)
10. [Vision blind-window benchmark](../docs/benchmarks/vision-blind-window.md)
11. [Causal ego-motion design](../docs/superpowers/specs/2026-07-23-causal-ego-motion-estimator-design.md)
12. [Causal ego-motion implementation plan](../docs/superpowers/plans/2026-07-23-causal-ego-motion-estimator.md)

## Do Not Reopen Unless Needed

- Do not restore duplicated legacy hold/brake/lifecycle/output-validation paths merely to regain strength.
- Do not return to fixed `left_x * constant` or a weapon database without new evidence.
- Do not reintroduce independent X/Y arbitration; use complete vector relationships.
- Do not use a crude body-box authority gate shared by ADS and BodyLock.
- Do not optimize a capped total score or an overshoot metric that never triggers.
- Do not compare artifacts across config fingerprint, scenario semantics or runtime identity.
- Do not add persistent learning, live exploration or additional vision inference without explicit evidence and approval.
- Do not let cue-only, weak-only or predicted-only targets gain fire authority.

## Notes

- 2026-07-24 live-log audit: the 17:45-20:46 run (including about 18:00) explicitly started with `perf_log=false`, so it has no per-tick evidence. The usable gameplay telemetry is the 10:41-13:00 session; the 13:55 session is manual/drift-only. In the usable session, BodyLock—not ADS snap—contained most center-crossing debt: 1,371 axis crossings, 10.43% with AI still pointing in the old direction and 37.56% with final output still pointing in the old direction. These are acceptance baselines for the in-progress causal POV/ego-motion estimator; it must separate target motion, camera motion and anchor discontinuity without becoming another control owner. See the latest session-log entry before tuning.
- Pre-compaction handoff and session history is preserved verbatim in [context-through-2026-07-20-pre-compaction.md](archive/context-through-2026-07-20-pre-compaction.md).
- User-confirmed: the first large refactor felt more natural but weaker; later benchmark-driven control recovered a better balance.
- Inferred: the transferable success was not any single formula, but the sequence of cleaning ownership, improving benchmark semantics, measuring future burden and changing only one control owner at a time.
- Keep this handoff below 120 lines and link detail instead of accumulating another long narrative.
