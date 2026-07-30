# Tracker Remaining-Work Control Plan

Date: 2026-07-29
Status: Planned
Scope: native tracker, `TargetPlan`, ADS, BodyLock, delivered-output accounting,
benchmark and telemetry

## Product Goal

The tracker is a control-oriented tracker. Its primary responsibility is not
only to estimate where the target is or how it is moving. It must continuously
estimate the net two-dimensional screen displacement still required to bring
the crosshair to the intended target point and keep it there.

The controller consumes that remaining work. ADS and BodyLock choose different
consumption profiles, but must not independently reconstruct or duplicate the
same work.

The core state is:

```text
remaining work
= latest authoritative Vision error
+ exogenous target and player-POV motion after capture
- camera motion actually delivered after capture
```

Fresh Vision is authoritative and replaces the old work anchor. Predictions
bridge the interval between observations; they must not accumulate as a second
owner of the same displacement.

## Why This Is Needed

The current tracker publishes position, relative velocity, acceleration,
terminal error, player-motion forecast and confidence. ADS and BodyLock then
turn those values into local control requests.

However, controller-rate propagation does not consistently subtract camera
motion already delivered since the latest Vision capture. Relative screen
velocity can therefore contain target motion, manual camera motion, AI camera
motion and player-POV motion at the same time. Extrapolating that mixed velocity
can preserve an obsolete direction after the user or AI has already moved the
camera.

This explains several observed failure families through one missing state:

- left-stick movement is followed lazily and recovery waits until movement ends;
- upward acquisition starts weak and later overshoots;
- manual and AI movement can complete the same displacement twice;
- ADS-to-BodyLock handoff inherits obsolete directional debt;
- short occlusion extends old relative motion after the camera has changed;
- jump and slide POV motion is mistaken for target motion;
- a controller can stop its stick signal immediately but does not know the
  correct remaining distance at which it should stop.

## Existing Components To Reuse

No parallel planner or second controller owner should be introduced.

- `pipeline_contract::TargetPlan` already carries target error, velocity,
  terminal prediction, response scale and player-motion forecast.
- `TargetCoordinator` already owns target identity, lifecycle, capture timing,
  mode transition and player-action forecasts.
- `NativeGamepadController` already has physical input, AI proposal, final
  pre-recoil output and output-delivery reporting.
- `PendingControlMotion` already integrates delivered pre-recoil stick samples,
  but is currently benchmark-only.
- `ControlResponseEstimator`, `AimResponseEstimator` and the shadow causal
  response learner provide response estimates and confidence.
- sustained AimLab, player-motion, left-strafe, short-occlusion and handoff
  benchmarks already provide most acceptance scenarios.

## Proposed Contract

Add a single work-state section to `TargetPlan`:

```cpp
struct TargetPlan {
    // Existing target and lifecycle fields remain.

    Vec2f work_anchor_error_px{};
    Vec2f exogenous_motion_since_capture_px{};
    Vec2f delivered_camera_motion_since_capture_px{};
    Vec2f remaining_work_px{};
    float remaining_work_confidence = 0.0f;
    bool remaining_work_valid = false;
};
```

The exact names may change during implementation, but the semantics must not:

- `work_anchor_error_px` is the error at the authoritative Vision capture.
- `exogenous_motion_since_capture_px` is motion not caused by delivered
  right-stick camera movement.
- `delivered_camera_motion_since_capture_px` is based on output that was
  actually delivered, not requested AI output.
- `remaining_work_px` is the only displacement ADS and BodyLock are allowed to
  consume when valid.
- `remaining_work_confidence` expresses response and timing uncertainty.
- invalid or low-confidence work state falls back to the existing controller;
  it must not silently apply a partial correction.

## Required Invariants

1. Fresh Vision replaces the anchor in the same controller tick.
2. Target ID, ADS epoch or coordinate discontinuity resets accumulated work.
3. Failed/disabled output is never counted as delivered motion.
4. Delivered motion uses final pre-recoil right-stick output so manual and AI
   work are counted once.
5. Recoil remains a final feed-forward owner and cannot read tracker state.
6. Prediction may add exogenous work but cannot duplicate delivered work.
7. ADS and BodyLock share one remaining-work vector.
8. ADS-to-BodyLock transfers remaining distance, not the previous ADS output or
   an implicit velocity.
9. The controller may stop immediately when estimated remaining work reaches
   zero; it must not add a second shared brake gate.
10. All work-state operations are vector operations. Independent X/Y gates are
    not reintroduced.
11. No weapon database, persistent learning, active calibration input or extra
    Vision inference is added.
12. Benchmark and production use the same work-state calculation.

## Coordinate and Timing Semantics

The work anchor must use the Vision capture timestamp, not inference completion
or controller-consume time.

Delivered output is integrated over:

```text
[authoritative capture time, current controller decision time]
```

The implementation must explicitly preserve screen/control sign conversion:

- screen X right is positive;
- screen Y down is positive;
- controller right-stick Y up is positive;
- the conversion occurs once at the response boundary.

The first implementation must not reinterpret relative screen velocity as
target-only velocity. Observation-to-observation target-motion estimation must
account for known delivered camera motion, otherwise the same camera movement
will be subtracted twice.

## Implementation Phases

### Phase 0 — Matched Baseline

Purpose: freeze acceptance evidence before changing production output.

- Retain fixed seeds and the effective config fingerprint.
- Run ordinary, left-strafe, jump, slide, combined player/enemy movement,
  short-occlusion, sensitivity/slowdown and ADS-to-BodyLock cohorts.
- Record current score, error, overshoot, continued push, circle exits,
  undertracking, handoff residual and output-discontinuity metrics.
- Add an ideal simulator residual only as an oracle; it cannot be used by
  production.

Exit condition:

- baseline artifacts have identical scenario, config, controller and seed
  provenance;
- current controller output reproduces without drift.

Estimated time: 30–45 minutes.

### Phase 1 — Shadow Remaining-Work State

Purpose: prove the work calculation before it can affect output.

- Move delivered pre-recoil output history out of the benchmark-only build
  boundary.
- Extend delivery samples with the identity and epoch information needed to
  reject discontinuities.
- Integrate delivered motion from capture to decision time.
- Add tracker/coordinator work-state fields.
- Publish work-state telemetry and benchmark trace values.
- Do not change ADS, BodyLock, fusion or final output.

Required tests:

- constant delivered motion reduces work monotonically;
- zero/failed delivery does not reduce work;
- manual and AI final output are not counted separately;
- fresh Vision re-anchors without carrying old debt;
- target switch, ADS epoch change and output disable reset safely;
- 80/100/200 Hz calculations are time-consistent;
- screen/controller Y signs are correct;
- new shadow code preserves exact current output.

Exit condition:

- shadow output drift is exactly zero;
- simulator remaining-work error is bounded and improves over raw stale error
  on held-out player-motion and reversal scenarios;
- no double subtraction appears at fresh Vision boundaries.

Estimated time: 1.5–2.5 hours.

### Phase 2 — ADS Work Consumer

Purpose: use remaining distance for point acquisition without changing the
single-owner pipeline.

- ADS consumes `remaining_work_px` when valid.
- Low-confidence work state falls back to the existing `error_px` path.
- The response solver retains configured force limits and the 120 ms arrival
  target.
- Commanded displacement is capped so the estimated delivered displacement
  cannot pass through the remaining work before new evidence.
- Aim dynamics shaping remains responsible for output smoothness.
- Vector intent fusion still decides manual/AI ownership exactly once.

Exit condition:

- ordinary ADS acquisition and tracking do not regress materially;
- moving/POV-motion ADS tracking improves;
- overshoot area and continued obsolete push decrease;
- acquisition misses do not increase;
- output jerk and user-fight guardrails remain within baseline tolerance.

Estimated time: 1.5–2 hours.

### Phase 3 — BodyLock and Handoff

Purpose: make trajectory following and mode transition consume the same work.

- BodyLock uses remaining work plus bounded exogenous target-motion lead.
- Fresh Vision position dominates prediction.
- Coasting may increase prediction share only within the existing hold budget.
- ADS-to-BodyLock preserves remaining work but resets ADS output-shaping state.
- Remove the need for a shared handoff brake; retain ADS-only brake boundaries.

Exit condition:

- handoff residual, rebound and wrong-way output do not regress;
- false interruption and false stop do not increase;
- BodyLock does not swallow small correct manual adjustments;
- left-strafe, jump, slide and short-occlusion cohorts improve or remain within
  predefined guardrails;
- no new near-target stall ring is introduced.

Estimated time: 1.5–2.5 hours.

### Phase 4 — Cleanup After Live Acceptance

Purpose: reduce complexity only after the new contract proves itself.

- Identify fields and branches made redundant by remaining work.
- Consolidate terminal-error, forecast and handoff-debt calculations.
- Delete only behavior proven redundant by matched A/B evidence.
- Update architecture and operations documentation.

This phase is deliberately separate. It must not be bundled with the first
production rollout because simultaneous cleanup would make regressions harder
to attribute.

Estimated time: 3–6 hours after live validation.

## Expected Files

Primary production changes:

- `native/pipeline_contract/target_plan.h`
- `native/controller_native/target_coordinator.h`
- `native/controller_native/target_coordinator.cpp`
- `native/controller_native/native_gamepad_controller.h`
- `native/controller_native/native_gamepad_controller.cpp`
- `native/controller_native/pending_control_motion.h`
- `native/controller_native/pending_control_motion.cpp`
- `native/controller_native/ads_acquisition_controller.cpp`
- `native/controller_native/bodylock_follow_controller.cpp`

Tests and benchmark:

- corresponding focused `*_tests.cpp` files;
- sustained AimLab simulator, adapter, types and score files;
- telemetry schema/collectors only for work-state observability;
- runtime config only if a temporary rollout/fallback flag is required.

Expected production MVP impact:

- 14–18 files;
- approximately 800–1,300 changed or added lines including tests;
- approximately 150–250 lines of new production calculation;
- no new thread, heap-heavy structure, vision pass or persistent store.

## Acceptance Scorecard

No capped total score is used. The new controller must be evaluated on a
balanced scorecard.

Primary positive metrics:

- acquisition points;
- tracking points;
- mean and P95 error;
- acquisition misses;
- time to first entry and settle.

Defect metrics:

- overshoot area;
- continued push after crossing;
- circle exits;
- correction reversals;
- false interruption and false stop;
- stall-ring time;
- handoff residual, rebound and wrong-way output;
- post-occlusion recovery;
- output delta, jerk and controller-residual discontinuities.

Required cohorts:

- ADS and BodyLock;
- pure AI and mixed human/AI;
- stationary and moving target;
- left strafe off/full reversal;
- jump, slide and combined player motion;
- short occlusion 0/24/36/48 ms;
- strong and weak slowdown;
- camera response 350/500/700 px/s;
- ordinary, near and small target profiles;
- fixed seeds plus held-out seeds.

Initial guardrails:

- no material ordinary-cohort score regression;
- no increase in acquisition misses;
- no increase in false interruption/stop;
- no unexplained increase in output discontinuity or user-fight;
- improvement must survive more than one cohort and more than one seed;
- production rollout requires the rollback flag and a freshly built DEV
  executable.

Numeric thresholds should be frozen from the Phase 0 baseline rather than
invented before the matched artifacts exist.

## Rollout and Rollback

- Implement in a clean worktree based on current `dev`.
- Keep current controller behavior as the fallback during Phases 1–3.
- Shadow mode is output-identical by contract.
- Production actuation is enabled only after the shadow acceptance gate.
- Build and retain the pre-change runtime before replacing the DEV executable.
- A single config or compile-time rollout switch must restore the current
  `error_px` path without changing tracker identity or AutoFire behavior.
- Do not merge exploratory raw benchmark output unless it is selected as
  retained evidence.

## Risks and Stop Conditions

Stop production rollout if any of these occurs:

- delivered motion is counted twice;
- response confidence remains too low to bound work;
- capture and decision timestamps cannot be aligned;
- ordinary ADS loses acquisition capability;
- BodyLock gains score by suppressing correct user input;
- output looks smoother only because authority was reduced;
- one cohort improves while broad sensitivity/slowdown or movement cohorts
  regress materially;
- implementation creates a second planner, brake or fusion owner.

## Work Estimate

- Baseline plus shadow proof: 2–3 hours.
- Production MVP through ADS and BodyLock: 5–8 hours total.
- Full cleanup after live acceptance: an additional 3–6 hours.

The production MVP is one substantial task, not a parameter tweak. The formula
is small; most work is preserving timing, identity, ownership, fallback,
telemetry and broad behavioral verification.
