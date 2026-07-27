# Causal Response Ego-Motion Ownership Audit

Date: 2026-07-22  
Status: G3 shadow boundary frozen; G4 remains unauthorized.

## What this boundary solves

The production tracker/controller already estimates how delivered right-stick motion changes target error. The new learner can also reconstruct delivered and scheduled motion. Feeding both descriptions into the same prediction would compensate camera motion twice, producing exactly the reversal debt and over-correction that this work is intended to remove.

## Current production ownership

- `target_snapshot_provider.cpp:421-422` publishes `camera_attributed_velocity_x_px_per_sec` from the current response estimate.
- `native_gamepad_controller.cpp:188-190` converts that value into the coordinator's `has_control_response_hint` / `control_response_x_px_per_sec` observation.
- `target_coordinator.cpp:93` consumes that response hint and `target_coordinator.cpp:223-231` writes the resulting look-ahead into `TargetPlan.predicted_terminal_error_px` and radial closing state.
- `native_gamepad_controller.cpp:284-285` exposes the committed plan response again to the legacy BodyLock-facing state. This is a view of the same plan evidence, not a second owner.
- `bodylock_policy.cpp:141-143` uses camera-attributed velocity for bounded trajectory inertia. It does not own a separate delivered-control accumulator.
- Recoil stays outside this path and is appended at final output; the causal learner hard-rejects recoil/firing windows.

Therefore `TargetPlan.predicted_terminal_error_px` already contains the production estimate of realized camera-response look-ahead. It is a mixture of target motion and estimated realized control response. Variable names alone cannot separate those components after the plan has been committed.

## Frozen G3 boundary

The selected future boundary is **A**:

> A learner may eventually parameterize the tracker/coordinator ego-response projection; rollout may then add scheduled, not-yet-visible control debt only.

For the current G3 shadow evaluator:

- immutable state starts from the committed `TargetPlan`, including its existing `predicted_terminal_error_px`;
- the evaluator may read the learner's selected response/delay and `PendingMotionEstimate.scheduled_px`;
- it must not add `PendingMotionEstimate.realized_px` to the plan prediction;
- it must not mutate or call `TargetCoordinator`, `AimDynamicsShaper`, `VectorIntentFuser`, AutoFire, recoil, or controller state;
- it must not return an output to production control code.

This keeps one owner for realized response and one diagnostic owner for scheduled debt. A future G4 design must replace the current response hint at the same owner boundary; it may not stack the learned projection after `TargetPlan` construction.

## Mutation evidence

The retained G1 `M8_tracker_controller_double_compensation` mutation subtracts delayed pending response from the observation while the plant still realizes the same delayed input. It must increase cumulative error by at least 2% relative to the ordinary cohort. This deterministic failure is the guard against accidentally integrating both ownership paths.

## Review trigger

Re-audit before any live G4 adjustment, tracker response-source replacement, dynamic ROI coordinate change, or change to the semantics of `TargetPlan.predicted_terminal_error_px`.
