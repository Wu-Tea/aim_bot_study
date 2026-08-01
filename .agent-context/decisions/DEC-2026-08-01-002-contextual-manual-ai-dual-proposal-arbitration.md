# DEC-2026-08-01-002: Use Contextual Manual/AI Dual-Proposal Arbitration

Status: accepted
Date: 2026-08-01
Confirmed by: user direction that both manual and AI must enter calculation,
followed by explicit implementation, build, documentation-sync and commit
requests
Related sessions: 2026-08-01 high-sensitivity manual-mix benchmark,
implementation, runtime installation and bot log validation
Related files:

- `native/controller_native/vector_intent_fuser.cpp`
- `native/controller_native/vector_intent_fuser.h`
- `native/controller_native/vector_intent_fuser_tests.cpp`
- `native/controller_native/target_pipeline_integration_tests.cpp`
- `artifacts/benchmarks/sensitivity-manual-mix-20260801/CONTEXTUAL_DUAL_PROPOSAL_VERIFICATION.md`
- `docs/project/CONTROL_CHAIN_JUMP_STABILITY_ACCEPTANCE_20260801.md`
- `docs/project/ADS_LONG_SESSION_DIAGNOSIS_20260801.md`

Supersedes: `DEC-2026-08-01-001` only for the current runtime identity and
manual/AI arbitration semantics; its Task 1–4 lifecycle, identity and continuity
contracts remain accepted
Superseded by: none

## Context

The Task 1–4 runtime removed severe jumps and high-frequency moving-follow
jitter, but game sensitivity had increased from 1.65 to 2.4. Under strong
same-direction player input, the old cooperative path preserved almost all
manual demand while independently attenuating AI. Treating both values as
additive forces created a close-range “elastic rope” effect: player and AI
accelerated in the same direction, then the controller had to recover from the
extra camera motion.

The user explicitly rejected a policy that either blindly preserves manual or
blindly suppresses it. Both sources must participate in one calculation.

## Decision

Treat manual and shaped AI as two absolute stick proposals inside the single
`VectorIntentFuser` owner.

- Raw physical manual input continues to classify magnitude, direction,
  deliberate escape and the `0.45..0.70` transition.
- Strong same-direction ADS input uses AI-priority arbitration at every target
  distance.
- Strong same-direction near-BodyLock input uses the same policy; far
  BodyLock retains its previous behavior.
- AI remains a complete proposal. The parallel manual proposal is normalized
  to an effective sensitivity of 1.9 for ADS and 2.0 for near BodyLock at the
  current 2.4 game sensitivity, then contributes 20% parallel headroom.
- Tangential manual work, opposing correction and non-cooperative near-full
  manual escape remain preserved by their existing boundaries.
- Pure-AI behavior, ADS/BodyLock gains and configured game sensitivity are not
  reduced.

## Reasons

- It fixes the ownership error instead of hiding it with lower global gain.
- Same-seed pure controls are exactly unchanged.
- The final matched matrix materially improves the strict old additive
  baseline while the contextual layer itself stays mostly within about one
  percent of the first-stage AI-priority candidate.
- A 20% headroom sweep had lower aggregate overshoot and better error/tail
  metrics than 10% or 30%, while still preserving visible manual participation.
- Full manual escape and orthogonal tracking remain explicit safety contracts.

## Rejected Alternatives

### Preserve nearly all manual and decay AI

Rejected because this is the ownership behavior that produced high-sensitivity
same-direction stacking.

### Globally scale the entire manual vector

Rejected because it improves a synthetic score by also shrinking useful
opposing and tangential input.

### Make the runtime pure AI during ADS or BodyLock

Rejected because it would remove deliberate player correction and escape,
especially across target ambiguity and close movement.

### Lower game sensitivity or ADS/BodyLock gains

Rejected because pure AI benefits from the 2.4 response and the defect is in
manual/AI arbitration, not insufficient global damping.

## Evidence

- Release build passed.
- CTest passed `34/34`.
- Focused fuser and target-pipeline integration tests passed.
- Left-stick field harness passed `5` scenarios with `0` defects.
- Authoritative matrix:
  `artifacts/benchmarks/sensitivity-manual-mix-20260801/contextual-headroom-0.20-final-exact/`.
- Installed runtime SHA-256:
  `DE31FF53B4C0CFBAB091F589CB194296A01DC9C0C8B5E74513F90AB94ED30590`.

## Consequences

- `VectorIntentFuser` remains the only manual/AI arbitration owner.
- Benchmarks must keep raw manual input at scale 1.0 and perform contextual
  normalization in production logic; global benchmark scaling is not evidence
  for a shippable policy.
- New ADS over/under reports must first separate handoff timing and target
  movement from response-scale learning.
- The latest long-session log shows a residual ADS timing/motion issue. It does
  not reverse this decision and is documented separately rather than being
  silently folded into the arbitration policy.

## Review Triggers

Review this decision if:

- full manual escape becomes delayed or imprecise;
- orthogonal player tracking is measurably attenuated;
- far BodyLock changes despite being outside the contextual path;
- production logs show the dual-proposal path itself creating wrong-direction
  output after controlling for ADS handoff timing and target motion;
- a matched sensitivity matrix supports different normalization or headroom
  without weakening the above safety boundaries.
