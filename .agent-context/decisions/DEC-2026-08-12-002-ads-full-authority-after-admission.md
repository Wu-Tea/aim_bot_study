# DEC-2026-08-12-002: ADS Full Authority After Target Admission

Status: accepted
Date: 2026-08-12
Confirmed by: user
Related sessions:
- 2026-08-12 gameplay audit and product clarification
Related files:
- `DEC-2026-08-11-002-native-irdt-single-owner-v1.md`
- `artifacts/telemetry-audits/20260812-ads-bodylock-video-incidents/report.md`
- `native/controller_native/target_coordinator.cpp`
- `native/controller_native/assist_control_state_machine.h`
Supersedes: the current cue/visibility/reliability-based ADS authority-scaling policy
Superseded by: none

## Context

Live review found that cue-less persons were admitted by the selector, but ADS
visual authority was scaled low enough that the delivered AI increment was often
zero. The previous audit recommendation proposed a cue-less baseline followed by
a cue boost. The user rejected that product behavior and clarified that ADS has
two equally important uses:

- small, precise correction of an otherwise mostly correct manual placement;
- urgent, high-speed transfer to another target when the user may not yet see
  the person clearly enough to finish the movement unaided.

The second use fails if cue, visibility, clarity or similar evidence reduces ADS
authority after the target has already passed selection.

## Decision

- Target admission and ADS control authority are separate decisions. The
  selector may fail closed when no valid, fresh target exists; once it admits a
  valid person as the current ADS identity, ADS receives the full configured ADS
  authority.
- Cue presence, cue confidence, visibility, detection clarity, target distance,
  identity-confirmation class and BodyLock evidence policy must not multiply or
  otherwise reduce ADS authority after admission.
- Cue may help select, associate, stabilize or continue the same target. It does
  not strengthen ADS output and its arrival must not cause a gain step.
- Full authority does not mean a constant maximum stick command. The response is
  still determined by target residual: a small error produces a small precise
  correction, while a large error can request the configured high-speed transfer.
- A selector-confirmed handover to a different person starts that identity's own
  ADS acquisition even while LT remains held. It must not inherit the previous
  person's elapsed or already-consumed ADS window and fall straight into BodyLock.
- Correct manual contribution may reduce the remaining correction through the
  single `T - M` control objective; that is completion of the target motion, not
  evidence-based authority loss. Mere user activity must not make ADS yield.
- Invalid or stale identity ends ADS target ownership and therefore produces no
  target command. It must not be represented as a partially weakened valid ADS
  target.
- This decision is ADS-specific. It does not remove the separately accepted
  conservative cue/visibility policy for BodyLock, where false-person resistance
  remains a product requirement.

## Reasons

- Small correction and fast transfer are two scales of the same error-response
  problem. They do not require two authority modes.
- A cue-dependent gain cannot assist the user in the exact high-speed situation
  where the user has not yet obtained a clear view.
- Letting cue arrive after acquisition and increase gain creates delayed pull,
  perceived blocking followed by a yank, and avoidable overshoot.
- Keeping admission fail-closed preserves target safety without degrading a
  target that has already been accepted.

## Rejected Alternatives

- Low cue-less ADS baseline plus cue boost: rejected because it makes urgent
  transfer weakest before cue, when assistance is most necessary.
- Visibility-proportional ADS authority: rejected because poor visibility is an
  expected ADS use case, not evidence that the user wants less help.
- Separate light-correction and transfer modes: rejected because residual error
  already distinguishes the two behaviors and another state machine would add
  transitions without adding product meaning.
- Globally raising ADS or BodyLock gain: rejected because it does not remove the
  cue-driven gain step or selector identity churn.

## Evidence

- User-confirmed product statement on 2026-08-12: ADS must not be weakened for
  any reason; it must cover both small correction and urgent high-speed transfer
  when the player cannot clearly see the target.
- In the audited hallway incident, 27 cue-less ADS samples had a selected target
  but zero delivered AI increment over manual; output rose only after cue.
- Source audit located the scaling policy in
  `TargetCoordinator::produce_plan`, after selector admission.

## Consequences

- ADS authority scaling by visual evidence must be removed or bypassed in the
  existing authority owner, rather than offset with another gain or mode.
- Regression tests must compare identical accepted ADS observations with cue on
  and off and require equal authority and equal solver demand.
- Response-curve acceptance must separately cover small-error precision and
  large-error transfer speed.
- Selector validity, handover, per-axis manual protection and D correction remain
  separate correctness requirements; full ADS authority must not conceal them.

## Review Triggers

- Matched live evidence shows that full-authority ADS creates wrong-target motion
  even though selector admission and identity ownership are correct.
- Product scope introduces an explicit user-selectable ADS assistance strength.
- The target-admission contract changes so that accepted identity no longer means
  a valid person target.

## Implementation checkpoint — 2026-08-12

- Completed in the production C++ chain. ADS authority is 1.0 with or without
  cue after admission; BodyLock remains evidence-scaled.
- Per-axis `T - M` fusion preserves stronger compatible manual input, supplies
  missing compatible work, and permits bounded non-reversing resistance to
  likely wrong manual input. Downward/firing protection follows the accepted
  10%/0% ceilings.
- Firing-down now moves D inside R and cannot arm downward handover. A confirmed
  replacement receives a new identity-scoped ADS acquisition while LT is held.
- The product-contract fixture changed from RED to GREEN on unchanged hard
  oracles; the official Release CTest suite passes `49/49`.
- Candidate runtime SHA-256 and RED/GREEN metrics are recorded in
  `artifacts/regressions/controller-v1-product-contract-20260812/`. Matched live
  gameplay acceptance remains open.
