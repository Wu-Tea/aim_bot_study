# Agent Handoff

Last updated: 2026-07-20
Active scope: native C++ FPS gamepad runtime, selector/tracker/controller, ADS, BodyLock, intent fusion, AutoFire, recoil boundary, benchmark semantics and runtime operations.
Staleness trigger: refresh after a production pipeline owner changes, a benchmark/config schema changes, crop/model identity changes, global policy learning begins, or new live evidence contradicts this state.

## Current Objective

Preserve the single-owner control architecture while improving real gameplay speed, tracking and robustness. New optimization must reduce measurable defect burden and retain natural hand feel; benchmarks are instruments for improving the program, not product features by themselves.

## Current State

- Runtime architecture is observation/intent -> TargetCoordinator -> immutable TargetPlan -> ADS or BodyLock -> AimDynamicsShaper -> VectorIntentFuser -> ADS-only brake -> recoil/final output.
- Refactor B removed duplicated gates and made control more natural, but initially reduced ADS/BodyLock strength and damping. Response-model control, better near-target feedback and causal vector fusion recovered capability without restoring the duplicate stack.
- ADS snap is scoped to one physical LT epoch. A new target while LT remains held cannot restart strong snap.
- Selector keeps the near committed target through short occlusion, hard-rejects green friendly cue and treats yellow enemy cue as auxiliary evidence only.
- Production native vision contract is `480x416` with the matching 480x416 TensorRT engine fallback.
- AutoFire contract is 100 ms pulse period, at least 30 ms pressed, no synthetic fire for weak/cue-only targets, same-tick release on fresh miss, physical RB/RT passthrough.
- Background VBS start/stop launchers run without a console window and prevent duplicate instances; the process remains normally visible to Windows.
- High-rate telemetry/perf logging is opt-in. Fresh sessions use manifests and whole-session cleanup.
- Complete July 16-20 history: [AIM_CONTROL_OPTIMIZATION_HISTORY_20260716_20260720.md](../docs/project/AIM_CONTROL_OPTIMIZATION_HISTORY_20260716_20260720.md).
- Reusable method: [EVIDENCE_DRIVEN_REALTIME_CONTROL_OPTIMIZATION.md](../docs/project/EVIDENCE_DRIVEN_REALTIME_CONTROL_OPTIMIZATION.md).
- Cross-project adoption entry: [REALTIME_CONTROL_OPTIMIZATION_START_HERE.md](../docs/methods/REALTIME_CONTROL_OPTIMIZATION_START_HERE.md).

## Next Action

1. Before the next controller policy change, generate a current-revision legacy/vector full acceptance artifact with effective config fingerprint, fixed seeds and comparator identity.
2. For remaining BodyLock 10-20 px stickiness, capture proposal, selected vector weights, delivered output, slowdown state and target response; build a radial/tangential fixture before changing policy.
3. For small/far-target authority, use existing box size/reliability and mixed-input scenarios first; do not add another vision pass by default.
4. If global learning resumes, start at G0 journal and G1 sequence oracle. Do not jump directly to production learning or persistence.

## Blockers

- No current checked-in final legacy/vector A/B artifact exists; conversation-only vector-fusion percentages are not independently auditable.
- Real gameplay `config.toml` is local/untracked and must be fingerprinted per session before it can support numeric comparison.
- Cross-target global optimum learning is planned but not implemented.

## Active Questions

- Does remaining near-target stickiness come from slowdown, vector ownership, BodyLock feedback, or delivered-output shaping?
- How much small/far target authority can be reduced continuously through existing reliability without hurting valid small-target tracking?
- Can a shadow tail-value estimator improve 500-1000 ms sequence burden without increasing switches, interruption or handoff residual?

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

- Pre-compaction handoff and session history is preserved verbatim in [context-through-2026-07-20-pre-compaction.md](archive/context-through-2026-07-20-pre-compaction.md).
- User-confirmed: the first large refactor felt more natural but weaker; later benchmark-driven control recovered a better balance.
- Inferred: the transferable success was not any single formula, but the sequence of cleaning ownership, improving benchmark semantics, measuring future burden and changing only one control owner at a time.
- Keep this handoff below 120 lines and link detail instead of accumulating another long narrative.
