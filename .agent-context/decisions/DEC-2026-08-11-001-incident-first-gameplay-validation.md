# DEC-2026-08-11-001: Incident-First Gameplay Validation

Status: accepted
Date: 2026-08-11
Confirmed by: user asked to review the current workspace, roll back changes only
when they are ineffective, record the result, and begin the agreed plan; the
user also explicitly challenged the cost and credibility of building a complete
gameplay simulator up front
Related sessions:

- 2026-08-11 product-definition questionnaire review
- 2026-08-11 workspace and benchmark-fidelity review

Related files:

- `docs/project/AIM_CONTROL_PRODUCT_CONTRACT_V1_20260811.md`
- `docs/benchmarks/CLOSED_LOOP_GAMEPLAY_ACCEPTANCE_V1_20260811.md`
- `native/controller_native/native_gamepad_controller.cpp`
- `native/controller_native/assist_control_state_machine.h`

Supersedes: none
Superseded by: none
Related:

- `DEC-2026-08-07-001-target-first-final-output.md`
- `DEC-2026-08-10-001-retire-low-rate-control-stack.md`

## Context

The current synthetic benchmark already drives the production native controller
through a deterministic 1 kHz loop with delay, response-curve, slowdown and
target-motion models. Its reported score is nevertheless not a release oracle:
the executable's PASS condition checks smoke properties, and aggregate tracking
metrics can improve while a hard product failure becomes worse.

The user-confirmed incident is narrower and more valuable than a speculative
full-game simulator: the selected vertical aim point remained above the desired
point, sustained downward manual correction did not produce sufficient response,
and a later large input or authority release caused an abrupt disengagement.
Cue geometry may be involved, but the reference video is not synchronized with
the native runtime trace, so the exact internal branch remains unproven.

## Decision

- Keep the product and closed-loop acceptance documents as the north-star
  contract, but do not implement the entire contract in one step.
- Phase 0 is one deterministic native RED fixture for the confirmed vertical
  correction/release incident. It must execute the real
  `NativeGamepadController`, record trigger assertions, symptom oracles and at
  least one counterfactual, and return nonzero on the current known-bad behavior.
- The first hard oracles are product-facing: downward-correction acknowledgement
  while the same target remains owned, and the output step at authority release.
  Aggregate smoothness or tracking scores cannot compensate for either failure.
- The exact cue misplacement cause is an explicit unknown. The fixture may test
  a cue continuation as a supported hypothesis, but may not label it the proven
  cause of the video incident.
- After the RED owner checkpoint, decide whether the production defect is in
  aim-point ownership, user-intent/desired-point handling, axis authority, cue
  geometry, or a combination. Do not patch production behavior before that
  review.
- Phase 1 reuses the existing Sustained AimLab micro plant and adds only the
  semantics proven necessary by Phase 0. A calibrated COD profile and wider
  scene coverage are later investments, not Phase 0 prerequisites.
- Do not introduce a Body/Pose model by default. A new model requires evidence
  that geometry unavailable from the current detector/cue contract is necessary
  and that the expected product gain justifies data and training cost.

## Reasons

- One real incident that reliably fails is stronger evidence than a broad score
  whose relationship to gameplay is unknown.
- Reusing the production controller prevents a test-only controller from
  becoming a second architecture.
- Separating RED reproduction from the eventual fix prevents thresholds and
  fixtures from being tuned to make a preferred implementation pass.
- Staged fidelity makes cost proportional to demonstrated gaps: command-level
  ownership first, micro-plant behavior second, measured game calibration last.

## Rejected Alternatives

### Build a complete game simulator before defining one regression gate

Rejected because it has high implementation and calibration cost while still
lacking a known-good release oracle. Visual completeness would not prove causal
fidelity.

### Treat the sustained benchmark's aggregate score as the release decision

Rejected because its PASS condition is smoke-level and averages can hide the
user-confirmed manual-suppression/release-cliff failure.

### Revert the latest controller commit immediately

Rejected at this checkpoint. The commit fixes a separately proven centered
motion continuity defect, while the new incident has not yet been captured by a
production-faithful fixture. Blind reversion would trade one known defect for an
unmeasured one.

### Add Body/Pose estimation before clarifying desired-point ownership

Rejected because model training would not by itself define whose desired point
wins, how manual correction changes it, or how authority releases.

## Evidence

- Workspace audit: the five modified native files matched their index blobs;
  they were file-status noise rather than algorithm changes and were cleared by
  refreshing the index metadata without staging content.
- Workspace audit: the product and closed-loop documents add valid missing
  product/acceptance semantics and are retained.
- Source audit: `AssistControlStateMachine` replaces an entire axis with AI
  whenever shaped AI work is materially nonzero, and returns the entire axis to
  manual when authority disappears.
- Source audit: the current centered-motion regression intentionally uses zero
  right-stick input, so it does not cover sustained manual correction against a
  nonzero AI proposal.
- Source audit: sustained AimLab has a micro plant but its executable PASS does
  not enforce the incident's hard product oracles.
- Historical regression evidence shows a manual-control delta threshold can
  distinguish a stronger correction path from the current weaker response, but
  the exact threshold must be bound to the new fixture and measured report.
- **Inferred/open:** cue position error is compatible with the incident and the
  user's observation, but no synchronized runtime trace proves it was the sole
  cause.

## Consequences

- The first new benchmark executable is intentionally a known-bad RED gate and
  is not registered as a normal passing CTest until production behavior is fixed.
- A nonzero exit from the current runtime is success for reproduction, not a
  claim that the product fix is complete.
- Full-game rendering, new model training and per-title COD simulators remain out
  of scope until a measured fidelity gap requires them.
- Production code remains unchanged at the RED owner checkpoint.

## Review Triggers

- The Phase 0 fixture cannot trigger the same-target manual correction and
  release sequence through the production controller.
- A counterfactual fails to distinguish the claimed cause from generic output
  movement.
- Matched live telemetry disproves the chosen command-response threshold or
  establishes a different internal cause.
- Phase 0 passes after a production change and the team is ready to promote its
  hard oracles into Phase 1 closed-loop scenarios.
