# DEC-2026-08-02-001: Fresh Observation Bounds Radial Control Authority

Status: proposed
Date: 2026-08-02
Confirmed by: user confirmed that wrong manual direction/strength may be limited;
the exact Observed BodyLock radial implementation remains pending validation
Related sessions: August 2 eight-clip video and telemetry audit; luna-max W1/W3 completion review
Related files:

- `native/controller_native/vector_intent_fuser.cpp`
- `native/controller_native/vector_intent_fuser.h`
- `native/controller_native/bodylock_follow_controller.cpp`
- `native/controller_native/response_model_aim_solver.cpp`
- `native/controller_native/native_gamepad_controller.cpp`
- `native/controller_native/target_coordinator.cpp`
- `native/pipeline_contract/target_plan.h`

Supersedes: none
Superseded by: none

## Context

The accepted dual-proposal fuser fixed high-sensitivity same-direction force
stacking, but new live evidence exposed two missing fresh-observation boundaries.

During ADS center crossing, the AI correctly reversed while retained physical
manual input kept the final output moving in the old direction. The configured
`fresh_vision_wrong_way_manual_floor` is parsed but is not wired into the fuser;
the fuser receives the broader BodyLock manual-escape preservation value instead.

During BodyLock, fresh error crossed center while the requested AI continued in
the old direction for tens of milliseconds. Remaining/Pending shadow state was
invalid or zero in those intervals. The BodyLock solver adds retained target
velocity to current position, while its wrong-direction guard applies only to
non-Observed lifecycle. Fresh positional evidence therefore has less sign
authority than stale motion prediction.

## Proposed Decision

For a fresh, reliable, single-target observation, current target position owns
the sign of the radial correction:

- `VectorIntentFuser` remains the only manual/AI arbitration owner. Wire the
  dedicated fresh wrong-way manual policy into its config and use it only when
  fresh positional evidence and AI correction oppose the old manual direction.
- Preserve an explicit, continuous deliberate-exit channel, but near-full input
  is not automatically exact or immediate ownership. Tangential and ambiguous
  input remain eligible proposals and may be limited only by evidence that the
  predicted result worsens; do not apply an unconditioned global manual scale.
- BodyLock must keep position and motion proposals separately observable. Motion
  feed-forward may assist closing and tangential following but may not reverse a
  nontrivial fresh radial position correction.
- Near center, use a continuous stopping envelope or equivalent bounded rule;
  do not add a hard threshold impulse. A confirmed center crossing may clear or
  cap only stale radial motion state, not valid tangential motion.
- Telemetry must expose error rate, position-stick, motion-stick, the applied
  radial bound and its reason so live acceptance can distinguish planner error
  from fuser behavior.

This proposal does not authorize production deployment. It becomes accepted only
after deterministic live-derived regressions, broad controller tests, and a user
live trial pass.

## Reasons

- It corrects the two owners that emitted wrong-direction demand instead of
  hiding the incidents with lower global strength.
- It keeps fresh Vision authoritative while retaining useful target velocity.
- It preserves the accepted manual/AI dual-proposal and one-owner architecture.
- It is consistent with the Remaining contract: shadow delivered-work state is
  not blamed when it is invalid and the requested assist itself is wrong.

## Rejected Alternatives

### Globally reduce BodyLock/ADS gain or sensitivity

Rejected because the fault is wrong direction, not excessive correct-direction
strength, and lower gain recreates lazy tracking.

### Disable all velocity feed-forward

Rejected because moving-target and player-motion tracking need predictive lead.

### Clamp only final output or add another brake

Rejected because it creates another owner after the bad request rather than
fixing the planner/fuser source.

### Attribute the incidents to Remaining, learning, or an output queue

Rejected by the aligned live fields: Remaining and Pending shadow were inactive,
dynamic adjustment was zero, and delivered samples/source frames were unique.

## Evidence

- ADS crossing sample: error changed from `+4.4` to `-3.1 px`; AI changed to
  `-0.32`, manual remained `+0.14`, and final output remained `+0.07`.
- Later in the same crossing, AI was `-0.54`, manual `+0.32`, and final output
  still `+0.12`.
- Strong BodyLock sample: error changed from `+7.4` to `-123.8 px` while
  requested AI remained positive and final output reached about `+0.80`.
- Active chain timing was normally 9-12 ms present-to-ViGEm; no delivered frame
  duplication was found.
- The reviewed isolated P1 candidate implements the proposed fresh-position and
  feed-forward boundary and passed `36/36` tests. It also covers rotated/tangent,
  near-center, center-crossing and non-fresh-equivalence controls. Candidate
  SHA-256 is `1AA216FB...4947D744`; this record remains proposed until a user live
  trial confirms the behavior.

## Consequences

- ADS/manual wrong-way and BodyLock/motion wrong-way require separate RED tests.
- Planner contribution telemetry becomes part of live acceptance.
- Selector geometry and ADS lifecycle remain separate P2 work; they must not be
  folded into this patch.

## Review Triggers

Review or reject this proposal if the bound causes lazy reversal tracking,
attenuates tangential manual input, delays deliberate escape, changes far
BodyLock unexpectedly, or merely moves wrong-way demand to another stage.
