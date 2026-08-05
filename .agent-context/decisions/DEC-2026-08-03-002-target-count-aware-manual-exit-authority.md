# DEC-2026-08-03-002: Use Target-Count-Aware Manual Exit Authority

Status: accepted
Date: 2026-08-03
Confirmed by: user explicitly asked to record the policy that AI may dominate
when Vision has one target, while multiple targets may allow the user to pull
away toward another target
Related sessions:

- 2026-08-03 review of live session `20260802T181639Z_53916_1` and the remaining manual/AI conflict

Related files:

- `docs/project/CURRENT_STATE.md`
- `native/vision_native/src/target_selector.cpp`
- `native/controller_native/target_coordinator.cpp`
- `native/controller_native/vector_intent_fuser.cpp`
- `native/pipeline_contract/target_plan.h`

Supersedes: none
Superseded by: none
Refines: `DEC-2026-08-02-002-predictive-manual-ai-control-envelope.md`

## Context

The accepted predictive envelope treats manual and AI as fallible proposals to
one final target-relative output. Live telemetry shows that this prevents simple
force stacking, but BodyLock can still spend hundreds of milliseconds opposing
sustained right-stick input after the current target has crossed the crosshair.
This can be a desirable correction when the user merely overshoots, or unwanted
interference when the user intends to select another person. The controller
cannot distinguish those cases from the current target-relative vector alone.

The user clarified the intended product rule: if Vision has only one valid
target, the application may trust AI completely; if Vision has multiple targets,
the user should have a bounded way to pull away from the current target.

## Decision

Target ownership is conditioned on the number and geometry of credible target
candidates, not only on manual magnitude:

- With exactly one credible hostile candidate carrying full assist authority,
  AI is primary for the target-relative solution. Manual remains an observed
  proposal and may contribute when it helps, but persistent opposition does not
  automatically obtain within-target ownership or release the only target.
- With two or more credible candidates, stable manual intent may open a bounded
  handover only when its direction is consistent with an eligible alternative
  candidate. Multiple candidates without aligned intent do not globally weaken
  AI on the current target.
- A fresh, evidence-gated candidate set is authoritative. Raw detector-box count,
  stale tracks, friendly/corpse candidates, cue-only candidates and rejected
  identities do not turn a single-target scene into a multi-target scene.
- The intent-aware selector and `TargetCoordinator` own candidate choice,
  ownership transition and identity replacement. They publish one immutable
  target plan and an explicit ownership reason. `VectorIntentFuser` remains the
  sole within-target manual/AI arbitration owner and does not independently
  infer or switch among targets.
- Physical ADS release, target invalidation and existing weak/ambiguous evidence
  gates remain safety exits. This decision does not grant a false detection
  unconditional AI authority.

Exact candidate thresholds, intent persistence, hysteresis and handover curves
are implementation parameters. They require deterministic and live evidence and
are not fixed by this decision.

## Reasons

- Single-target opposing input is usually overshoot or sensitivity mismatch;
  allowing it to fight a reliable AI correction recreates the elastic-rope and
  prolonged-brake behavior.
- In a multi-target scene, the same input may carry real target-selection intent
  that a one-target controller cannot interpret correctly.
- Candidate geometry is available before the controller collapses the scene to
  one target, so selector/coordinator is the correct architectural owner.
- Conditioning authority on an aligned alternative is narrower and safer than
  globally "respecting manual more" whenever more than one box exists.

## Rejected Alternatives

### Always Let Sustained Manual Opposition Release the Target

Rejected because it makes a reliable single-target lock sensitive to user
overshoot and reintroduces prolonged manual/AI fighting.

### Always Keep AI Primary in Multi-Target Scenes

Rejected because the application can trap the user on the wrong person even
when right-stick intent clearly points toward another credible candidate.

### Put Candidate Count and Switching Inside VectorIntentFuser

Rejected because the fuser sees one target-relative plan and lacks the complete
candidate evidence needed to choose another identity. This would create a
second selector and split target-lifecycle ownership.

### Use Raw Detector Box Count or Switch on One Manual Tick

Rejected because invalid boxes, stale observations and transient stick noise
would cause authority flicker, false releases and target churn.

## Evidence

- User-confirmed policy: multiple detected targets may permit some user pull-away;
  with one target, AI may be authoritative.
- The reviewed live session contains BodyLock manual/AI opposition episodes up
  to about 499 ms, showing that an explicit ownership decision is preferable to
  prolonged output-level compromise.
- Existing accepted architecture already assigns target choice and lifecycle to
  the selector/`TargetCoordinator`, and within-target output to one fuser.
- The current telemetry does not yet expose a clean eligible-candidate cohort,
  so the exact one-target versus multi-target thresholds remain unvalidated.

## Consequences

- Preserve eligible candidate identities and geometry until target ownership is
  decided; do not reduce the scene to raw `detector_box_count` at the controller.
- Add shadow telemetry for eligible candidate count, selected/alternative IDs,
  manual-intent direction and persistence, ownership mode, transition reason and
  time to confirmed handover.
- W5 short-term memory may retain manual intent and delivered/realized motion,
  but it must not become a second target owner.
- W6 acceptance must cover: one target plus opposing/overshoot input; two targets
  plus input toward the alternative; two targets plus input toward no candidate;
  candidate appearance/disappearance, brief occlusion and identity churn.
- Score false release, false switch, time to intended switch, conflict duration,
  final-output discontinuity and current-target error separately.

## Review Triggers

- A reliable single target becomes impossible to leave after target evidence or
  physical ADS state says it should release.
- Multi-target handover becomes slow, sticky or biased toward transient boxes.
- Candidate-count churn causes authority oscillation or output discontinuities.
- W4/W5 evidence shows that apparent manual intent was mostly delayed camera
  response rather than an intended target switch.
- Live or deterministic evidence supports different candidate eligibility,
  persistence or handover semantics.
