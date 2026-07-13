# Native ADS Snap Predictive Manual Brake Design

## Goal

Reduce ADS snap overshoot caused by sustained user input without globally
weakening manual control, while keeping bodylock, recoil, target selection, and
the current smooth assist envelope unchanged.

The acceptance target is observable behavior: ADS dynamic error and overshoot
must return close to the pre-refactor A0 baseline, user escape must remain
prompt, and the full native benchmark suite must not expose a new regression.

## What Exists Now

`NativeAiAim` already treats ADS manual input specially. Helpful manual input is
credited against the planned AI correction, while opposing and orthogonal
manual components are partially suppressed during acquisition. The result is
then passed to `NativeAimAssistDynamics`.

The refactored dynamics layer applies a global current-tick user-yield rule
before it distinguishes ADS from bodylock. When the reticle crosses a target,
the manual stick can still carry in the old direction while ADS requests a
counter-steering brake. The dynamics layer classifies that request as AI/user
opposition and deletes the complete ADS brake.

The current benchmark records the failure directly:

- manual X: `+0.92`;
- requested ADS assist X: `-1.0782`;
- dynamics adjustment X: `+1.0782`;
- final X: `+0.92`;
- maximum ADS overshoot: `139.02px`, compared with `60.42px` in A0.

The live selector-owned identity path also has an integration gap. The selector
sets its identity protocol and selected detection, but `VisionEngine` does not
copy those fields into the final `VisionResult`. The runtime adapter therefore
falls back to the legacy controller submission path. This must be corrected so
the behavior accepted by controller benchmarks is the behavior used live.

## Chosen Design

### ADS-only arbitration

Give `NativeAimAssistDynamics` explicit ADS snap context. Bodylock keeps the
existing immediate user-yield invariant. ADS uses a separate target-axis
arbitration rule:

1. Far from a confirmed crossing, strong opposing manual input still wins. This
   preserves deliberate target switching and wrong-target escape.
2. On a fresh strong target, observe target-error sign changes independently on
   X and Y.
3. If the error crosses zero while manual input continues in the old target
   direction, open a short crossing-brake window for that axis.
4. During that window, allow the bounded ADS counter-steering request produced
   by `NativeAiAim` to reduce the effective manual output instead of deleting
   it.
5. Expire the window quickly. Sustained opposing input after expiry is treated
   as deliberate user intent and receives full control.

Only the axis that crossed receives the temporary brake. The orthogonal axis is
unchanged. Losing authority, leaving ADS snap, changing target identity, or
losing fresh evidence resets the state.

### Predictive near-target budget

The existing near-target brake estimates a safe output from remaining pixel
error, reticle speed, and a short horizon. Extend it so a fresh, strong ADS snap
may cap total target-axis carry even when helpful manual input has already
consumed the full planned AI correction. This is a smooth output budget, not a
global manual multiplier.

The cap applies only while output is still moving toward the selected target.
After a confirmed crossing, the ADS-only arbitration above owns the short
counter-steering window. Stale, weak, projected-only, ambiguous, or rejected
targets never reduce manual output through this path.

### Identity propagation

Copy `selector_identity_protocol`, `has_selected_detection`, and
`selected_detection_index` from the selector result into the final
`VisionResult` before the runtime adapter consumes it. Add a protocol regression
test so selected observation identity is nonzero when the selector marks a
selected detection.

## Safety and Failure Behavior

- No new normal configuration keys are added.
- The ADS brake never runs in bodylock.
- Recoil remains the final independent feed-forward stage.
- Track-only/reject authority and stale evidence yield completely to manual.
- A target switch resets crossing history; brake state cannot leak between
  targets.
- The short brake window is bounded and expires into full user control.
- X and Y state are independent to avoid diagonal cross-axis suppression.

## Verification and Acceptance

Add focused tests for:

- ADS brake survives the global user-yield boundary after a confirmed crossing;
- strong opposing manual input without a crossing still wins;
- the brake expires into user control;
- only the crossed axis is affected;
- bodylock retains immediate user yield;
- near-target total output is capped without a discontinuous direction flip;
- selector identity fields reach `ControllerVisionSnapshot`.

Then run the full Release test set, native pipeline contract, and
`cod_native_gamepad_benchmark --suite all` against both the current pre-change
artifact and the clean A0 artifact.

Primary gates:

- `ads_diagonal_manual_stress_100hz_dynamic` max overshoot no worse than the A0
  neighborhood (`60.42px`) and P95 error near A0 (`74.43px`);
- the dynamic-fire variant remains near its A0 `66.61px` max overshoot and
  `80.81px` P95 error;
- ADS Python-parity, late-vision, wrong-target, adversarial user escape, and
  manual carry scenarios do not materially regress;
- all six positive bodylock scenarios, bodylock continuity/chatter, edge manual
  preservation, recoil contracts, and vision tests pass unchanged;
- complete benchmark and runtime build exit successfully.

