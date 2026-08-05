# DEC-2026-08-02-002: Use One Predictive Envelope for Manual and AI Proposals

Status: accepted
Date: 2026-08-02
Confirmed by: user explicitly authorized limiting wrong-direction or excessive
manual input and prioritized eliminating manual-plus-AI force stacking
Related sessions: August 2 sensitivity 2.4 manual/AI authority discussion and
eight-clip live control-chain audit
Related files:

- `native/controller_native/vector_intent_fuser.cpp`
- `native/controller_native/vector_intent_fuser.h`
- `native/controller_native/native_gamepad_controller.cpp`
- `native/controller_native/response_model_aim_solver.cpp`
- `native/pipeline_contract/target_plan.h`

Supersedes: `DEC-2026-08-01-002-contextual-manual-ai-dual-proposal-arbitration.md`
for static manual preservation, fixed effective-sensitivity normalization and
parallel-headroom semantics; retains its single-fuser dual-proposal architecture
Superseded by: none

## Context

The contextual dual-proposal policy reduced close-range elastic-rope stacking,
but it still granted manual input special preservation and escape rights. At game
sensitivity 2.4, the user is not consistently adapted to the camera response.
Wrong or excessive physical input can therefore combine with AI and produce too
much final force even though Vision and AI already detect the required correction.

The original intent of putting manual and AI in one calculation was not to
guarantee manual preservation. Both are fallible proposals. The user wants to
push the physical stick confidently while the application limits whichever
proposal would create wrong-direction motion or excessive predicted travel.

The separate far-target/head-peek micro-input report is real but explicitly
deferred. It must not expand the current repair mission.

## Decision

`VectorIntentFuser` remains the only manual/AI arbitration owner and applies one
shared predictive control envelope:

- Manual and shaped AI enter as evidence/proposals, not additive forces and not
  separately owned output contributions.
- The actuator receives one final target-relative vector. The fuser determines
  its sign and magnitude from current error, stopping demand, the active-mode
  force envelope and the strongest valid individual proposal; it does not
  calculate `manual output + AI output` or a manual/AI allocation budget.
- Direction and magnitude are evaluated against the latest authoritative target
  error, the response/stopping model and, when validated later, short-horizon
  causal memory.
- A manual component that deepens error or predicts excess travel may be reduced
  substantially or removed. There is no guaranteed manual preservation floor.
- An AI component that reverses fresh position or predicts excess travel is
  limited by the same principle.
- Large physical stick input remains allowed at the input boundary; the final
  target-relative output is bounded so the user can operate decisively without
  manual-plus-AI force stacking.
- Deliberate target exit remains possible through a continuous, observable
  intent/lifecycle transition. A single near-full tick is not unconditional
  exact manual ownership.
- Ambiguous identity or weak evidence lowers AI authority rather than guessing.
- Far-target 3% micro adjustment and target-relative aim trim remain recorded
  future work. Current P1 must avoid making them worse but does not implement
  them.

Exact horizons, curves and exit persistence are implementation parameters to be
chosen by deterministic regressions and matched benchmarks, not fixed by this
decision.

## Reasons

- It implements the user's original meaning of calculating both inputs.
- It removes dependence on the user's adaptation to sensitivity 2.4 while
  preserving the high actuator range useful to AI.
- It fixes excessive combined force at the only arbitration owner instead of
  adding another post-output brake.
- It treats bad AI and bad manual symmetrically while still responding to
  evidence quality and target identity.

## Rejected Alternatives

### Preserve a fixed fraction of every manual input

Rejected because it preserves user error and caused observed ADS overtravel.

### Continue fixed 1.9/2.0 effective manual normalization

Rejected as the main solution because a static sensitivity ratio cannot know
current stopping distance, direction correctness or AI proposal magnitude.

### Add raw manual and AI, then clamp the final stick magnitude

Rejected because a scalar clamp does not fix wrong direction and hides which
proposal caused the excessive demand.

### Globally lower game sensitivity or AI gain

Rejected because it makes valid AI tracking lazy and does not establish a
correct ownership calculation.

### Implement far-target micro trim in the same patch

Rejected for current scope by the user. First remove excessive large-input force;
micro precision can be revisited if AI cannot directly lock the desired point.

## Evidence

- User statement: manual should be processed like AI when its direction or force
  is wrong; the application may limit it so large physical movements feel safe.
- User statement: manual mixed with AI frequently produces excessive force at
  sensitivity 2.4.
- Live ADS crossing showed AI reverse correctly while preserved old-direction
  manual kept final output moving past the target.
- Existing fuser configuration used manual preservation, static normalization
  and parallel headroom rather than a shared predicted-result bound.
- The isolated P1 implementation passed full CTest `36/36` and focused cases at
  manual magnitudes `0.10/0.30/0.45/0.70/1.00`, same/opposing directions,
  near/far ADS and BodyLock, center crossing, rotated force ellipses and held
  escape. Candidate SHA-256 is `1AA216FB...4947D744`; live acceptance is pending.

## Consequences

- P1 tests must cover strong same-direction and opposing manual input, center
  crossing, near/far targets, full deflection and continuous exit.
- Acceptance measures final predicted travel, overshoot and continued push, not
  whether a fixed amount of manual survives.
- Telemetry exposes raw and validated manual/AI proposals plus stopping demand,
  permitted, pre-slew and actual final radial output and reason. Proposal fields
  are diagnostic evidence, not a claimed decomposition of the final output.
- Short-term memory can later extend the evidence horizon but must not create a
  second control owner or treat scheduled commands as realized motion.

## Review Triggers

Review this decision if correct target changes become sticky, large manual input
cannot exit a mistaken target, ordinary moving tracking becomes lazy, bounds
oscillate at thresholds, or live telemetry shows excessive combined travel
despite the shared envelope.
