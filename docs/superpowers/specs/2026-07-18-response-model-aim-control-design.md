# Response-Model Aim Control Design

## Goal

Replace ADS's fixed `error / range_px` gain and BodyLock's mismatched feedback/feed-
forward calculation with one closed-loop, full-direction control model that
estimates the live right-stick camera response in memory. Improve ordinary ADS
acquisition and small-visible-target tracking without weapon tables, extra vision
inference, additional mode gates, or loss of the current smooth feel.

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

## Confirmed BodyLock defects

The investigation found five concrete defects or measurement gaps that must be
addressed rather than hidden with a higher `strength` value:

1. The sustained simulator starts its 1000 ms tracking score immediately after the
   true error first enters the 24 px circle. The controller may still be in ADS.
   `false_mode_exit` only arms after BodyLock has been seen, so a run that never
   enters BodyLock can report zero false interruptions. Current BodyLock scores and
   zero-event counts are therefore not valid isolation evidence.
2. BodyLock computes `error_rate / response_scale`, adds it to position feedback,
   and then multiplies the entire sum by `max_force * authority`. The velocity term
   is already in stick units, so multiplying it by the force cap again systematically
   under-delivers motion compensation.
3. Horizontal feedback range expands from the configured tolerance-derived value
   toward a hard-coded 75 px as `left_x` grows. A larger range produces less
   feedback precisely while self-strafing creates more correction demand.
4. The combined request is clamped to the sign of current positional error. When a
   moving target requires lead opposite the instantaneous residual, trusted
   feed-forward is deleted until visible lag appears.
5. `TargetPlan::response_scale` currently comes from a left-stick relative-motion
   estimator. Using it as the right-stick/weapon camera response assumes two
   unrelated game responses have the same scale, reproducing the weapon-dependent
   multiplier problem the new design is meant to remove.

The baseline also aggregates tracking points across targets acquired by ADS. Better
ADS creates more BodyLock scoring opportunities even if BodyLock is unchanged.
BodyLock acceptance therefore requires a dedicated warm-start cohort and normalized
per-active-millisecond metrics.

## Confirmed ADS braking and handoff defects

The new control path also has a configuration/ownership mismatch around ADS
completion:

1. `[gamepad.ads] completion_radius_px`, `completion_fresh_frames`, and
   `max_acquisition_ms` are parsed and validated, but `coordinator_config()` does
   not use them. It takes completion radius and confirmation frames from BodyLock
   tolerance/confidence instead, and takes the acquisition timeout from
   `snap_duration_ms`. The public ADS completion settings therefore do not control
   the new state machine.
2. ADS "braking" is only `error + target_error_rate * 12 ms` when the target appears
   to be approaching center. It does not use measured reticle/camera closing
   velocity, response strength, slowdown, or stopping distance. It is target-motion
   lookahead, not a physical braking calculation.
3. Transition into BodyLock uses position-in-radius for a number of fresh frames,
   but does not require that residual closing velocity is small enough for BodyLock
   to absorb. A fast pass through the radius can therefore hand off with excessive
   kinetic demand.
4. The timeout fallback can enter BodyLock inside its large activation radius only
   when normalized target height exceeds 0.25. This is strongly biased toward very
   large/close targets and is not a general completion condition.
5. BodyLock returns to ADS above three times the settle radius only while not
   coasting. Large projected error during an evidence gap can remain semantically
   BodyLock, while the inverse transition uses different evidence rules.

These defects explain why changing BodyLock tolerance altered perceived ADS snap,
and why documented ADS completion parameters appeared ineffective.

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

ADS braking is not implemented as another post-controller brake. The arrival-time
law continuously reduces requested velocity as predicted terminal error and closing
velocity fall. This produces one request followed by the existing single dynamics
shaper; it does not recreate the old stack of snap, carry brake, terminal limiter,
and BodyLock corrections.

## ADS-to-BodyLock transition

ADS and BodyLock use the same response-normalized vector solver with different
arrival horizons and prediction emphasis. ADS uses a short horizon for acquisition;
BodyLock uses a longer horizon with more trusted motion feed-forward. The mode still
exists for ownership, telemetry, autofire, and tracker lifecycle semantics, but it
does not switch between unrelated force formulas.

The semantic transition occurs only when consecutive fresh evidence satisfies a
capture-set test:

- observed reliability is sufficient and target identity is stable;
- current error is inside the ADS completion radius;
- predicted error at the BodyLock handoff horizon remains inside its capture radius;
- estimated radial closing velocity is within the amount BodyLock can stop without
  leaving the capture region.

Confirmation uses elapsed observed milliseconds rather than a raw frame count, so
80 and 100 Hz vision have equivalent behavior. For compatibility,
`completion_fresh_frames=N` maps to `N * 10 ms` of stable fresh evidence. The other
existing ADS completion values are wired directly to this test.
`max_acquisition_ms` is a diagnostic/fallback deadline, not
permission to hand a large residual to BodyLock; a timed-out target remains under
the bounded acquisition law or yields according to target authority.

On transition, the common solver retains its response state and the existing
`AimDynamicsShaper` retains its delivered state while the requested horizon changes.
There is no zero tick, output reset, interpolation stage, or second handoff brake.
BodyLock-to-ADS uses
the same predicted capture-set hysteresis in reverse, with a wider exit boundary;
coasting reliability can reduce authority but does not bypass the boundary test.

Three alternatives were considered:

- repairing the current hard switch preserves more code but retains two mismatched
  controllers and makes handoff tuning a permanent maintenance cost;
- removing ADS/BodyLock modes entirely simplifies force generation but entangles
  autofire and lifecycle semantics;
- the selected common solver plus semantic state transition removes duplicated
  force logic while keeping the modes other subsystems require.

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

The estimator compares consecutive fresh-vision intervals. Differencing observed
error rates cancels approximately smooth target velocity; projecting that rate
change onto the corresponding change in average delivered stick identifies camera
response without assuming tracker target velocity is pure camera motion. High target
acceleration, sign-inconsistent, and outlier samples are rejected before a bounded
robust EWMA update. It starts from the existing
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
the same right-stick response estimate, while the existing left-stick estimator
continues to describe self-strafe-induced target motion. It does not create a gate,
own a mode transition, or write final output.

Manual input remains in the final mixer. The new radial controller does not add
opposing/cooperative manual multipliers. Existing manual escape and wrong-way
arbitration remain unchanged in the first implementation so A/B results isolate
the new control law.

## BodyLock control law

BodyLock uses the same response units as ADS but a longer arrival horizon. Position
feedback and target/self-strafe feed-forward remain separate until both have been
converted into stick units:

```
position_stick = error / (arrival_horizon * right_stick_response)
motion_stick   = relative_target_velocity / right_stick_response
requested      = position_stick + trusted_motion_weight * motion_stick
```

The force envelope clamps the final vector; it does not multiply the already
normalized motion term. A single radial limiter applies the horizontal/vertical
force ellipse after vector composition. Trusted feed-forward may briefly oppose
the current residual when it prevents predicted lag. Prediction is bounded by the
existing tracker horizon and reliability, not by a new lead-mode gate.

Left-stick magnitude does not widen the position feedback range. It changes the
relative-motion term through tracker/left-motion estimation. This distinguishes
synchronized target/self movement from opposite movement without weapon data or a
`left_x * constant` shortcut.

## Independent BodyLock benchmark

BodyLock is measured separately from ADS. Each target receives an unscored warm-up
inside the settle region. Scoring begins only after `BodyLockFollow` is confirmed.
Failure to enter within a bounded handoff window is recorded as a BodyLock entry
failure, not silently scored as ADS. Every successful target then receives exactly
1000 scored BodyLock milliseconds, making results comparable when ADS acquisition
rates change.

The benchmark records:

- BodyLock entry success and handoff latency;
- active-mode ratio and unexpected ADS/manual fallback milliseconds;
- center-weighted points and visible-circle retention per 1000 active ms;
- projected along-motion lag (mean and P95) and wrong-direction output ms;
- longest continuous outside-circle interval, exits, reacquisition count and time;
- demanded-but-near-zero output ms and false stop events;
- ADS-to-BodyLock first-tick output delta;
- predicted terminal error and radial closing velocity at every handoff;
- capture-set entry, rejected handoff, timeout, and reverse-transition reasons;
- output delta, jerk, direction discontinuity, and control effort;
- occlusion hold drift, stale output after target stop, and release latency;
- manual escape preservation and non-escape wrong-input correction.

Metrics are reported per motion family and target radius as well as in aggregate.
Event rates use BodyLock-active time as the denominator; raw totals remain in JSON.

## Small-visible-target phase

Before changing BodyLock, extend the sustained benchmark with a deterministic
small-target profile:

- visible radius varies from 8 to 14 px;
- normal strafes, reversals, jump/fall, stops, and short occlusion are represented;
- the same 1.0 -> 0.5 -> 0.4 slowdown model applies from the small circle edge;
- pure and mixed-manual versions use identical target scripts per seed.

Metrics emphasize center-weighted tracking points, time inside the visible circle,
circle exits, false BodyLock interruption, false stops, projected lag, wrong-
direction output, output delta, and jerk. ADS and BodyLock retain separate score
cards and are optimized sequentially so each gain remains attributable. Both must
pass before the production change is accepted.

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

BodyLock acceptance requires at least 15% more normalized center-weighted tracking
points and 15% less P95 projected lag in both pure and mixed profiles. Entry success
must not fall, longest continuous outside time must improve or remain within 5%,
false mode exits and false stops must not regress, wrong-direction output time must
fall, and P95 jerk may not regress by more than 5%. The same gates apply separately
to the 8–14 px small-target cohort; aggregate ordinary-target gains cannot hide a
small-target regression.

The benchmark will also sweep camera response and slowdown strength. A change that
only wins at the default plant is rejected as laboratory overfitting.

## TDD and rollout

1. Correct the ADS/BodyLock score boundary and add mode-occupancy tests so a run
   that never enters BodyLock fails the BodyLock cohort rather than scoring zero
   interruptions.
2. Add small-target, BodyLock warm-start, and response-sweep benchmark tests before
   controller changes.
3. Add failing unit tests for estimator eligibility, robust convergence, persistence,
   slowdown adaptation, and ambiguous-sample rejection.
4. Add failing ADS tests for radial direction, time-budget urgency, moving-target
   feedforward, near-target braking, force envelopes, and correct use of every ADS
   completion config value.
5. Integrate ADS, run unit tests and the official A/B matrix, and retain it only if
   every ADS acceptance condition passes.
6. Add failing transition tests for high-speed radius crossing, stable capture,
   80/100 Hz equivalence, timeout behavior, coasting exit hysteresis, and bumpless
   ADS-to-BodyLock delivery.
7. Add failing BodyLock tests for correct feed-forward units, predictive lead,
   self-strafe compensation, small-target retention, and force-envelope behavior;
   then integrate the shared response estimate.
8. Run the full benchmark matrix and existing controller/autofire/recoil regressions.

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
