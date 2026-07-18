# Response-Model Aim Control Design

## Goal

Replace ADS's fixed `error / range_px` gain with a closed-loop, full-direction
controller that estimates the live right-stick camera response in memory. Improve
ordinary ADS acquisition and small-visible-target tracking without weapon tables,
extra vision inference, additional mode gates, or loss of the current smooth feel.

## Current problem

`AdsAcquisitionController` currently divides each axis by a fixed pixel range and
then applies a force cap. One value therefore controls both far-target urgency and
near-target braking. With the game's slowdown reducing camera response from about
1.0 outside the target to 0.5 at the edge and 0.4 near the center, the same gain
becomes too weak close to the target. The sustained baseline confirms that the
dominant failure is missed acquisition rather than repeated crossing: across the
three pure seeds it acquired 52 targets and missed 335 with zero recorded over
events. Mixed manual mistakes increased mean and P95 error further.

The existing `ControlResponseEstimator` observes left-stick-induced relative
motion. It must not be treated as a calibrated right-stick/weapon ADS response.

## Selected approach

Use an estimated-time-to-arrival controller backed by one scalar, in-memory
right-stick response estimate.

For current error vector `e`, target-relative velocity `v`, desired arrival horizon
`T`, and estimated camera response `R` in pixels per stick per second, the desired
camera velocity is conceptually:

```
camera_velocity = v + e / T
requested_stick = camera_velocity / R
```

The implementation computes a single radial demand from the two-dimensional error
and projects it back onto X/Y. Existing horizontal and vertical force caps remain
the final directional envelope; they no longer define the distance curve. Closing
velocity and the remaining acquisition budget continuously shorten or lengthen the
horizon, so braking is continuous rather than a near/far mode switch.

`range_px` remains accepted by config parsing for compatibility and rollback, but
the new ADS law does not consume it. No replacement distance threshold is added.

## Right-stick response estimator

Add one focused `AimResponseEstimator`, separate from the existing left-stick
motion estimator. It keeps a scalar response and confidence in process memory.

A sample is eligible only when all of the following hold:

- the same target is observed in consecutive fresh frames;
- tracker reliability is stable and the target is not coasting or reacquiring;
- the final right-stick vector has enough excitation;
- target innovation, acceleration, and physical manual input do not make camera
  attribution ambiguous;
- elapsed sample time is finite and within the vision cadence envelope.

The estimator projects measured error closure onto the previous final-stick
direction, subtracts tracker-predicted target motion, rejects sign-inconsistent and
outlier samples, and updates a bounded robust EWMA. It starts from the existing
500 px/stick/second fallback, persists across ADS target changes, and adapts when a
weapon or slowdown response changes. It resets only with controller/application
reset. No weapon identity or saved weapon record is introduced.

Low-confidence estimates blend toward the fallback. The estimator may change the
force calculation but never bypass target authority, manual escape, output clamp,
or `AimDynamicsShaper`.

## Control and ownership boundaries

The production path remains:

```
vision/tracker -> TargetPlan -> ADS or BodyLock controller
               -> one AimDynamicsShaper -> manual + assist mix
```

The response estimator is data, not a new controller stage. ADS and BodyLock read
the same response estimate. It does not create a gate, own a mode transition, or
write final output.

Manual input remains in the final mixer. The new radial controller does not add
opposing/cooperative manual multipliers. Existing manual escape and wrong-way
arbitration remain unchanged in the first implementation so A/B results isolate
the new control law.

## Small-visible-target phase

Before changing BodyLock, extend the sustained benchmark with a deterministic
small-target profile:

- visible radius varies from 8 to 14 px;
- normal strafes, reversals, jump/fall, stops, and short occlusion are represented;
- the same 1.0 -> 0.5 -> 0.4 slowdown model applies from the small circle edge;
- pure and mixed-manual versions use identical target scripts per seed.

Metrics emphasize center-weighted tracking points, time inside the visible circle,
circle exits, false BodyLock interruption, false stops, undertracking, output delta,
and jerk. ADS is optimized and accepted first. BodyLock then adopts the shared
response estimate and time-based radial residual correction without adding a
second dynamics or braking stage.

## Benchmark and acceptance

All comparisons use seeds `1337`, `20260718`, and `424242`, both pure and mixed.
The old JSON remains immutable; each candidate writes a new artifact with config
fingerprint, revision, dirty state, script hashes, and per-target rows.

ADS acceptance requires all of the following:

- aggregate pure acquired targets improve by at least 20% over 52;
- aggregate mixed acquired targets improve by at least 15% over 44;
- acquisition points improve in both profiles, not only one seed;
- P95 output jerk does not regress by more than 5%;
- false interruption and false stop counts do not regress;
- any new over events are inspected per target and the improvement must survive a
  wider response sweep, not just the default 500 px/stick/second plant.

Small-target BodyLock acceptance requires at least 15% more center-weighted
tracking points in both pure and mixed profiles, with no regression in false mode
exits or false stops and no more than 5% P95 jerk regression.

The benchmark will also sweep camera response and slowdown strength. A change that
only wins at the default plant is rejected as laboratory overfitting.

## TDD and rollout

1. Add small-target and response-sweep benchmark tests before controller changes.
2. Add failing unit tests for estimator eligibility, robust convergence, persistence,
   slowdown adaptation, and ambiguous-sample rejection.
3. Add failing ADS tests for radial direction, time-budget urgency, moving-target
   feedforward, near-target braking, and force envelopes.
4. Integrate ADS, run unit tests and the official A/B matrix, and retain it only if
   every ADS acceptance condition passes.
5. Add failing BodyLock tests and integrate the shared response estimate.
6. Run the full benchmark matrix and existing controller/autofire/recoil regressions.

Production defaults are changed only after the benchmark evidence passes. Debug
telemetry records response estimate, confidence, requested arrival horizon, radial
demand, and limiting reason; normal logging remains unaffected when debug logging
is disabled.

## Scope exclusions

- no weapon database or disk-persisted weapon calibration;
- no additional vision model, inference pass, or rendered visual effect;
- no target-selection or autofire policy changes;
- no second output shaper, brake chain, or distance-band state machine;
- no deletion of `range_px` during the compatibility phase.
