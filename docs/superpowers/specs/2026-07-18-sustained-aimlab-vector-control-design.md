# Sustained AimLab Benchmark and Vector Control Design

Date: 2026-07-18  
Status: approved in conversation; awaiting written-spec review

## 1. Purpose

Build a deterministic, one-minute, closed-loop AimLab-style benchmark before changing the production aim controller. The benchmark must reward only the two outcomes that matter most:

1. acquire a moving target faster; and
2. remain closer to its center for longer.

Smoothness, overshoot, undertracking, false interruption, and false stopping are secondary diagnostics. They must not be combined into a 0-100 score that can hide worse acquisition or tracking.

After the current controller has a frozen baseline, replace the current per-axis player/AI arbitration with one continuous two-dimensional vector arbiter and add velocity-aware braking. Candidate control changes may enter production only after an identical-seed A/B run improves the primary additive scores without introducing control defects.

## 2. Current State and Problem

The production controller currently handles player/AI conflict in several places:

- `AxisIntentArbiter` classifies X and Y independently and reduces manual retention per axis.
- `BodylockFollowController` reduces AI output again when a manual axis opposes it.
- `AimDynamicsShaper` performs another per-axis cooperative/opposing reduction.
- `tolerance_px` influences both state transitions and BodyLock feedback scaling.

This can fragment diagonal input, apply several reductions to the same conflict, and leave the reticle 10-20px from the target with insufficient player and AI authority. It also makes the cause of a stop difficult to inspect.

The existing `aimlab_benchmark` is not a suitable optimization oracle. Several scenarios feed hand-authored before/after errors and controller outputs into a scorer instead of running the production controller in a sustained closed loop, and its smoothness score is currently constant. It remains a compatibility test, but its numeric score is not the new baseline.

## 3. Scope and Order of Work

The work is deliberately split into two phases.

### Phase 1: benchmark only

- Add a deterministic virtual target, observation stream, virtual game response, and scorer.
- Exercise the current production `NativeGamepadController` for a simulated 60 seconds.
- Add only the clock/input seam needed to run the existing controller deterministically; do not change its control policy.
- Validate the scorer with known fake controllers.
- Run and commit the current-controller baseline, including configuration, seeds, raw metrics, and result JSON.

### Phase 2: candidate control policy

- Replace per-axis arbitration with a continuous vector arbiter.
- Remove duplicate opposing-manual policy from BodyLock and the dynamics shaper.
- Add velocity-aware braking near the target without weakening normal chase.
- Run baseline and candidate against the same scenario data and seeds.
- Promote the candidate only if the predefined primary and diagnostic criteria pass.

## 4. Benchmark Architecture

The benchmark is a closed loop:

```text
deterministic target trajectory
        |
        v
80-100Hz synthetic observations -> production tracker/TargetCoordinator
                                      |
                                      v
                           NativeGamepadController at 1000Hz
                                      |
                                      v
                    final combined right-stick output
                                      |
                                      v
             virtual game response + native aim slowdown
                                      |
                                      v
                         next reticle position
```

The neural detector is not run. The observation generator supplies target geometry with deterministic timestamp jitter and small deterministic measurement noise. This tests the tracker and controller without adding GPU load or detector nondeterminism.

The production controller needs a test-injectable monotonic clock so simulated time, state holds, and slew behavior are reproducible. Runtime construction continues to use the real monotonic clock by default.

The new benchmark is separate from the existing adversarial AimLab executable, but it reuses common result and JSON conventions where practical. It does not alter the old score or historical results.

## 5. One-Minute Target Lifecycle

Each run lasts exactly 60 simulated seconds. A seeded random generator controls every target property and timing decision. The same scenario description is reused byte-for-byte for baseline and candidate.

Each target follows this lifecycle:

1. Spawn a radius-24px circular target at a legal random position.
2. Begin a seeded maneuver immediately; the target may already be moving during ADS acquisition.
3. Draw an ADS acquisition deadline uniformly from 250ms through 330ms.
4. If the reticle does not enter the circle before the deadline, record `missed_target`, award no acquisition or tracking points for that target, and despawn it.
5. If the reticle enters the circle, record the acquisition and immediately begin a 1000ms BodyLock tracking window. No artificial 40ms hold is required.
6. Continue the same target trajectory and motion state through the ADS-to-BodyLock handoff.
7. At the end of the tracking window, despawn the target.
8. After a fixed 50ms inter-target interval, spawn the next target.

The initial target center is sampled 48-140px from the current reticle at a uniformly sampled angle, then clamped to keep the complete circle on screen. Constant-speed profiles use 80-160px/s. Accelerating profiles add 60-160px/s^2 along or against the velocity direction. Reversal profiles reverse one velocity component once, 80-180ms after spawn. Jump/fall profiles start at 160-240px/s upward and use 600px/s^2 downward acceleration. Stop profiles decelerate to zero once, 80-200ms after spawn. Each motion profile is reported separately so an aggregate cannot hide a directional failure.

Observation intervals are sampled deterministically from 10.0-12.5ms, giving 80-100Hz delivery. Independent deterministic position noise is bounded to +/-0.75px per axis. The controller and plant advance at exactly 1ms per tick.

## 6. Virtual Game Aim Slowdown

The target circle is also the native aim-assist bubble. Slowdown affects the final combined stick output, so it applies equally to physical player input and AI output.

- Outside the transition band, response multiplier is `1.00`.
- Across a narrow 3px band immediately outside the target circumference, it changes smoothly from `1.00` to `0.50`.
- At the circumference, response multiplier is `0.50`.
- From the circumference to the center, it changes smoothly from `0.50` to `0.40`.
- Moving back outward follows the same curve in reverse.

The 3px band avoids a simulator-created velocity discontinuity. The scoring region still begins exactly at the target circumference. Slowdown parameters belong to the benchmark scenario and cannot be read from or changed by the controller configuration.

The first benchmark uses one canonical virtual camera response and no weapon table:

```text
reticle_velocity_px_per_second = final_stick * 500 * response_multiplier
```

The model is applied independently in control-space X and Y after the slowdown multiplier. It has no hidden acceleration or weapon-specific coefficient. Additional response profiles are outside the initial baseline scope.

## 7. Additive Scoring

There is no 0-100 score and no declared maximum for a 60-second run. More completed targets produce more opportunities to add points.

### 7.1 Acquisition points

For a target acquired at `entry_ms` with deadline `deadline_ms`:

```text
acquire_points += 1000 * (deadline_ms - entry_ms) / deadline_ms
```

The value is clamped at zero. A missed target adds zero. Earlier acquisition earns more points and also allows more target cycles to complete within the fixed minute.

### 7.2 Tracking points

Let `d` be reticle-to-target-center distance and `R = 24px`. For each millisecond of the 1000ms tracking window:

```text
if d < R:
    accuracy = (1 - (d / R)^2)^2
    tracking_points += accuracy
else:
    tracking_points += 0
```

A perfect 1000ms center track adds approximately 1000 points. A reticle near the circumference earns little, a brief pass through the circle cannot imitate sustained lock, and a 10-20px stall scores substantially below a center track.

### 7.3 Smoothness bonus

Smoothness remains a small, separately visible additive bonus. It cannot compensate for worse acquisition or tracking.

For a tracking tick inside the circle, only when center distance is not worsening:

```text
variation_quality = exp(-(|output[t] - output[t-1]| / 0.04)^2)
smooth_bonus += 0.1 * accuracy * variation_quality
```

The maximum contribution is approximately 100 points per perfect 1000ms tracking window, one tenth of the tracking score. Raw P95 output delta, acceleration, and jerk are always reported beside the bonus.

### 7.4 Displayed totals

The report displays these values independently:

```text
acquire_points
tracking_points
smooth_bonus
combined_points = acquire_points + tracking_points + smooth_bonus
points_per_second
```

Optimization decisions prioritize `acquire_points` and `tracking_points`, not `combined_points`.

## 8. ADS Acquisition and Correction Metrics

Each target records:

- `first_entry_ms`: spawn to first circle entry;
- `first_pass_success`: no circle exit during the first 100ms after first entry;
- `initial_direction_error_deg`: angle between the first final output of magnitude at least `0.02` and the current interception direction;
- `closure_efficiency`: `(spawn_distance - entry_distance) / sum(abs(delta_distance))` before entry, clamped to `[0, 1]`;
- `control_effort_to_lock`: integral of absolute final right-stick output before entry;
- `missed_target`: no entry by the seeded deadline;
- `correction_detect_ms`: error growth or overshoot to correct output reversal;
- `correction_recover_ms`: correct reversal to circle re-entry;
- `correction_reversals`: extra direction reversals during recovery;
- `residual_error_peak_px`: largest residual error after the first failed approach;
- `recovered_before_deadline`: whether correction succeeds before timeout.

A correction does not create an extra score category. It only recovers part of the acquisition opportunity that the failed first approach was losing. An algorithm cannot earn more by deliberately overshooting.

## 9. BodyLock Tracking Metrics

The primary BodyLock result is `tracking_points`. The report also includes:

- mean, P50, P95, and maximum center error;
- time within 6px, 12px, and 18px, for diagnosis only;
- target-turn phase lag;
- circle exit and re-entry time;
- `over_events` and maximum overshoot;
- `undertrack_events` and total undertrack duration;
- P95 output delta, acceleration, and jerk;
- meaningless direction reversals near the target.

### 9.1 Overshoot event

An overshoot begins after the reticle reaches the inner third of the circle. It is counted if, within 150ms, the reticle crosses to the opposite side of the moving target and error grows back beyond two thirds of the radius while output continues in the crossing direction. The event remains latched until error returns to the inner third, preventing one overshoot from being counted every tick.

### 9.2 Undertrack event

When true target speed is at least 40px/s, undertracking is counted if reticle error projected onto the target velocity direction exceeds half the radius and fails to close for at least 40ms. The event remains latched until projected lag falls below one quarter radius.

## 10. False Interruption and False Stop Metrics

These defects are explicit diagnostics, not arbitrary score penalties. Their effect also appears naturally as lost tracking points.

### 10.1 False interruption

An interruption is false when the target ID is unchanged, the target remains observed and reliable, no manual escape is active, and the tracking plan still demands control, but the assist chain unexpectedly exits or drops.

Report:

- `false_mode_exit_events`;
- `target_continuity_breaks`;
- `assist_dropout_events`;
- `interruption_total_ms`;
- `interruption_recover_ms`;
- `handoff_bounce_events`.

Valid target loss, low tracker reliability, authorized target switching, manual escape, and planned coasting do not count as false interruptions.

### 10.2 False stop

A stop is false when target speed is at least 40px/s or center error exceeds half the radius, the oracle interception direction still demands movement, manual input has not already supplied it, effective final output in that direction remains below `0.05` for at least 20ms, and no valid interruption condition exists.

Report:

- `false_stop_events`;
- `false_stop_total_ms`;
- `false_stop_recover_ms`;
- `stall_ring_ms` for non-converging 10-20px dwell;
- `premature_brake_events`;
- `zero_output_while_demanded_ms`;
- `stale_output_after_stop_events` for failure to stop after a real target stop or loss.

This paired definition prevents a controller from eliminating undertracking by never braking, or eliminating overshoot by stopping prematurely.

## 11. Mixed Player Input

The benchmark has a pure-controller run and a mixed-player run over the same target scripts. Seeded manual profiles include correct assistance, delayed response, stick drift, early release, continued push into overshoot, a diagonal vector with only one harmful component, a short wrong-way pulse, and an explicit escape above the configured escape threshold.

Report:

- `wrong_input_corrected_events`;
- `false_correction_events`;
- `correction_to_convergence_ms`;
- `manual_escape_return_ms`;
- `direction_discontinuities`;
- direction-angle buckets for diagnosis only.

Direction buckets never enter production control logic.

## 12. Candidate Vector Arbitration

This section defines the later production change so the benchmark is built for the intended behavior, but Phase 1 does not implement it.

The new pipeline has one player/AI conflict policy:

```text
IntentFilter ---------------------------+
                                        |
observations -> TargetPlan -> raw ADS/BodyLock request
                                        |
                                        v
                              VectorIntentArbiter
                                        |
                                        v
                         DynamicsShaper (slew only)
                                        |
                                        v
                             final right-stick output
```

All geometry is normalized once into right-stick control coordinates. The arbiter evaluates continuous two-dimensional projections rather than X/Y or eight discrete direction sectors.

- Helpful radial and tangential manual components are preserved.
- A harmful component is eligible only with stable observation, adequate reliability, stable geometry, worsening error or predicted overshoot, and manual magnitude below the escape threshold.
- Initial intervention is light and increases smoothly over roughly 20-30ms while evidence persists.
- The harmful projection may be reduced continuously and bounded AI correction may make the final vector point against the user's low-confidence wrong-way input.
- Evidence loss releases intervention smoothly.
- Explicit manual escape immediately restores player control.

`BodylockFollowController` will produce the raw desired request without its own opposing-manual reduction. `AimDynamicsShaper` will retain slew and coasting continuity but remove cooperative/opposing policy. The vector arbiter replaces `AxisIntentArbiter`; it is not added beside it.

## 13. Candidate Braking Ring

Candidate braking is not a distance-only force multiplier. It limits excessive radial closing speed:

- outside the braking region, preserve existing ADS and BodyLock chase strength;
- near the target, compute an allowed radial closing speed from remaining distance;
- brake only when measured closing speed exceeds that allowance;
- do not brake when the target is receding, the reticle is stalled, or more chase is required;
- do not reduce tangential target-following authority;
- do not uniformly scale player input;
- release braking when error stops converging, preventing a soft dead zone.

Configuration should expose only a clear settle radius and slowdown outer radius. Existing escape threshold and harmful-manual preservation floor are reused. Attack/release timing starts as tested internal constants rather than a new collection of public knobs.

## 14. Determinism, Output, and Failure Handling

The command-line tool accepts at least a seed, duration override for developer smoke tests, and JSON output path. The official baseline always uses 60 seconds and the committed seed list.

Every result records:

- git revision and dirty-state marker;
- complete production config hash and relevant resolved values;
- benchmark schema version;
- seed and generated target script hash;
- simulation frequencies and slowdown parameters;
- aggregate scores and diagnostics;
- per-target lifecycle, trajectory profile, deadline, points, and defects.

Invalid numeric output, a target leaving legal bounds, a missed simulation tick, non-monotonic time, or baseline/candidate scenario-hash mismatch fails the run rather than producing a score. JSON is written only after a successful complete run; partial diagnostic output uses a distinct filename.

## 15. Scorer Verification

Before recording the production baseline, deterministic fake controllers must prove:

1. perfect center tracking scores above fast but offset tracking;
2. fast but offset tracking scores above slow or stopped tracking;
3. earlier acquisition scores above later acquisition;
4. a miss scores zero acquisition and tracking for that target;
5. a high-speed pass through the circle cannot imitate a sustained lock;
6. deliberate overshoot and recovery cannot outscore a clean first pass;
7. smooth zero output cannot outscore useful tracking;
8. false-stop, undertrack, overshoot, interruption, and stale-output detectors each trigger only on their intended fixture;
9. identical seeds produce identical target-script hashes and scores;
10. baseline and candidate consume identical scripts.

The existing unit and benchmark suite must also remain green.

## 16. Promotion Criteria

No 0-100 aggregate gate is used. A candidate may replace production behavior only when identical-seed A/B results show:

- at least 5% higher `acquire_points` overall, with no motion-profile bucket more than 2% lower;
- at least 5% higher `tracking_points` overall, with no motion-profile bucket more than 2% lower;
- fewer missed targets or no increase;
- false interruptions and false stops each do not increase;
- overshoot and undertrack event counts each do not increase;
- explicit manual escape returns control within 20ms;
- correct player input is not corrected more often than baseline;
- P95 jerk is no more than 5% above baseline;
- all existing tests remain green.

The initial baseline freezes the exact numeric results. These thresholds and scoring formulas cannot change during candidate tuning. Any future benchmark-schema change starts a new baseline series and retains the prior series for comparison.

## 17. Non-Goals

- No weapon database or manual weapon calibration.
- No additional neural vision workload.
- No eight-direction production state machine.
- No parallel legacy and new arbitration gates.
- No candidate tuning before the scorer and current-controller baseline are frozen.
- No replacement of existing adversarial benchmarks; they remain regression checks.
