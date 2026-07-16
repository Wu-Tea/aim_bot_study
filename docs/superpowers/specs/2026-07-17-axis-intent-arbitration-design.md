# Per-Axis Intent Arbitration Design

Date: 2026-07-17
Status: proposed for implementation

## Purpose

Reduce ADS and BodyLock overshoot caused by short-lived wrong-way user input without
weakening the unaffected axis or reintroducing sticky BodyLock braking. The controller
must treat X and Y independently and must keep one explainable manual/AI arbitration
stage instead of accumulating mode-specific gates.

## Evidence and Current Defect

Real telemetry shows that input errors are temporally structured rather than random:

- observation gaps cluster around 27/42/65/110/161 ms at P25/P50/P75/P90/P95;
- wrong-way input at reacquisition commonly corrects after roughly 122 ms median and
  177 ms at P75;
- one-axis conflicts occur without the other axis being wrong;
- center-crossing inertia commonly corrects after roughly 62 ms median;
- stick values near 0.0118 are consistent with drift, while material error windows are
  usually much larger.

The current runtime already filters X and Y separately, but collapses their confidence
with `max(right_x.confidence, right_y.confidence)`. ADS, BodyLock, and the dynamics
shaper then reuse that shared confidence. Manual opposition may therefore affect the
unrelated axis and may attenuate assist once in the mode controller and again in the
dynamics shaper.

The partial-occlusion benchmark also shows why a single whole-stick decision is unsafe:
the mixed-error scenario raises X/Y maximum overshoot to about 207/104 px and mode
changes from 11 to 61, while normal combat remains materially better.

## Goals

1. Judge X and Y independently using axis-specific input, confidence, error, and error
   rate.
2. Preserve far-error ADS acquisition force while reducing near-target stacking and
   center-crossing overshoot.
3. Let AI compensate probable user error, but limit final wrong-way output only when
   current observed evidence is strong.
4. Preserve immediate per-axis manual escape.
5. Keep BodyLock smooth: no abrupt zeroing, reversal, or multi-gate chatter.
6. Reuse the existing target plan, intent filter, response estimate, and dynamics slew.
7. Replace duplicate manual attenuation rather than layering another policy on top.

## Non-Goals

- Do not infer a full 3D world model.
- Do not store weapon-specific ADS movement data.
- Do not add Vision inference work.
- Do not directly map `left_x` to right-stick output with a fixed multiplier.
- Do not alter recoil ordering or feed target/controller state into recoil.
- Do not make target selection decisions in the controller arbiter.
- Do not expose a large new configuration surface in the first implementation.

## Architecture

The runtime control path becomes:

1. `IntentFilter` produces axis-specific filtered values and confidence.
2. `TargetCoordinator` produces target error, error rate, lifecycle, reliability, short
   horizon, and the learned horizontal left-stick response contribution.
3. `AdsAcquisitionController` or `BodylockFollowController` produces pure requested AI
   control from the target plan. These controllers no longer attenuate for manual input.
4. A single `AxisIntentArbiter` independently allocates X and Y assist and applies a
   bounded wrong-way safety budget.
5. `AimDynamicsShaper` performs lifecycle-aware slew only. It no longer repeats
   cooperative/opposing manual scaling.
6. The output mixer combines physical right stick, shaped assist, and final recoil
   feed-forward in the existing order.

The arbiter is the only manual/AI policy stage. ADS and BodyLock share it and provide a
mode value; they do not own separate arbitration state machines.

## Axis Inputs and Outputs

Each axis evaluation receives:

- signed target error and error rate in controller-command coordinates;
- requested AI output;
- filtered manual input and that axis's confidence;
- target lifecycle, reliability, observation age, target id, and mode;
- current and recent target-point innovation;
- current and previous axis error;
- manual escape threshold;
- controller tick interval.

It returns:

```cpp
struct AxisDecision {
    float assist_output = 0.0f;
    float assist_scale = 1.0f;
    float wrong_way_budget = 1.0f;
    float divergence_risk = 0.0f;
    AxisDecisionReason reason = AxisDecisionReason::Neutral;
};
```

`reason` is diagnostic. Control is driven by continuous values, not by a chain of
latched reason-specific gates.

## Drift and Axis Confidence

`IntentFilter` remains responsible for adaptive neutral bias and noise estimation. The
arbiter consumes `right_x.confidence` for X and `right_y.confidence` for Y. Whole-stick
`right_confidence` remains available for compatibility but must not drive per-axis
arbitration.

Input below the learned axis deadzone has zero manual confidence and must not reduce
assist. Activity on X must not change Y arbitration, and vice versa.

## Divergence Model

For each axis, use the error energy:

```text
V = 0.5 * error^2
```

The arbiter predicts whether the proposed combined axis output will increase `V` over a
short horizon. It does not label the user's intent as semantically correct or wrong.
Instead, it estimates `divergence_risk` from continuous evidence:

- current error is growing or predicted to grow;
- error recently crossed zero while manual input retains the pre-crossing direction;
- combined manual and AI output adds motion away from the target;
- error is close enough that the predicted stopping margin is small;
- target identity and observed geometry are stable;
- target lifecycle and reliability permit intervention.

Risk has fast attack and slower release smoothing. This prevents one-frame spikes while
avoiding a fixed-duration brake latch. Exact attack, release, and prediction horizons
are benchmark constants in the first implementation, not user-facing configuration.

## Control Allocation

### Helpful manual input

When manual input is reducing axis error, AI fills only the residual demand. Manual and
AI must not blindly stack to the controller's full request near the target. Far from the
target, ADS retains a mode-specific assist floor so acquisition force is not lost.

### Probable wrong-way manual input

When manual input appears to increase error but intervention evidence is incomplete,
the arbiter may allow bounded AI opposition. It must not limit final manual output.
This applies during target-point ambiguity, reacquisition, and low-confidence tracking.

### Confirmed divergence

Final wrong-way output may be limited only when all of these conditions hold:

1. lifecycle is `Observed`, not tracker-only `Coasting`;
2. target id is stable;
3. target-point innovation and normalized-size change are within continuity bounds;
4. error has crossed zero or has grown consistently over the evidence window;
5. manual magnitude is below the per-axis escape threshold.

The limiter reduces wrong-way output smoothly toward a small nonzero budget. It must not
create an automatic reversal. Corrective output toward the target remains unrestricted.

### Manual escape

If one axis reaches the existing manual escape threshold, final-output limiting is
disabled immediately for that axis. The other axis remains independently controlled.
Large sustained input is therefore treated as user takeover even if it is inconsistent
with the currently selected target.

## Evidence Ambiguity

The controller must not use an unstable target point to declare user error. High target
innovation, abrupt normalized-size change, target-id transition, or reacquisition lowers
intervention permission continuously. During `Coasting`, the arbiter may decay or bound
AI but must never limit physical manual output.

This protects cases where partial occlusion changes the detected box and moves the
resolved chest point even though the user's aim remains correct relative to the real
body.

## ADS and BodyLock Profiles

Both modes use the same algorithm:

- ADS keeps a higher far-error residual-assist floor and attacks predicted crossing risk
  earlier, because its objective is fast acquisition with bounded settle overshoot.
- BodyLock gives manual input more ownership and permits less AI opposition, because its
  objective is smooth follow and easy escape.
- Neither mode owns a separate manual brake, owner hold, or preservation state machine.

Mode differences are a small fixed policy profile in code for the initial benchmark.
Only values that prove necessary for live tuning may later become configuration.

## Left-Stick Contribution

The existing `ControlResponseEstimator` learns a horizontal `left_x` to screen-response
scale during clean windows and carries it in `TargetPlan`. Continue using that estimate
to improve X-axis error-rate prediction. It must not directly generate right-stick
output.

`left_y` is not treated as direct Y control. Forward/back motion changes distance,
apparent size, and perspective; until telemetry can isolate those effects, `left_y` may
only lower prediction certainty during material movement. This avoids inventing a false
weapon-independent multiplier.

## State and Reset Rules

The arbiter stores only per-axis previous error, smoothed risk, and target continuity
identity. Reset both axes on ADS epoch change, target-id change, manual mode, or runtime
reset. Do not carry crossing evidence between targets or across ADS release.

## Removal and Consolidation

Implementation must remove or neutralize duplicate behavior in the active target-plan
runtime path:

- manual-opposition reduction inside ADS acquisition;
- manual-opposition reduction inside BodyLock follow;
- cooperative/opposing manual scaling inside the dynamics shaper.

The shaper retains lifecycle handling and slew. Legacy policies that are not on the
active target-plan path are not extended; they are covered only to ensure the new path
does not accidentally call them.

## Diagnostics

Debug telemetry records, per axis:

- manual filtered value and confidence;
- requested and arbitrated assist;
- predicted error and divergence risk;
- wrong-way budget and decision reason;
- target continuity/intervention permission;
- whether manual escape bypassed limiting.

Diagnostics are debug-only and add no always-on file logging requirement.

## Benchmark-First Validation

Before controller behavior changes, extend the partial-occlusion benchmark with realistic
time profiles:

1. stale input holds the historical vector and decays instead of mirroring ideal input;
2. wrong-X leaves Y correct;
3. wrong-Y leaves X correct;
4. crossing inertia holds and then gradually corrects;
5. target-switch carryover continues toward the old target briefly;
6. geometry jump makes user input correct for world truth but apparently wrong for the
   transient observed point;
7. drift-only input remains below the learned axis threshold;
8. strong manual escape intentionally rejects the selected target.

Reports must include per-case metrics, not only scenario aggregates.

## Acceptance Criteria

Using the same profile-faithful config and fixed seed:

- X-error cases must not materially reduce Y acquisition/follow performance, and Y-error
  cases must not materially reduce X performance;
- drift-only input must not change assist beyond numerical tolerance;
- normal combat ADS acquisition time and far-error force must not materially regress;
- mixed-error maximum X/Y overshoot, recovery time, and mode changes must improve;
- no scenario may introduce abrupt output reversal or new output-delta spikes;
- `Coasting`, geometry-jump, and strong manual-escape cases must never limit physical
  manual output;
- BodyLock moving-follow and close-target grip must remain within the accepted baseline
  envelope;
- focused intent, ADS, BodyLock, dynamics, controller, and pipeline contract tests pass.

Thresholds for “material” improvement/regression are set from the pre-change repeated
benchmark distribution before implementation, rather than chosen after seeing the
candidate result.

## Rollout

1. Establish repeated baseline distributions and per-case benchmark output.
2. Correct the human-error time profiles without changing runtime behavior.
3. Add a pure, unit-tested `AxisIntentArbiter` behind a benchmark-only integration.
4. Compare axis-isolation and normal-combat results.
5. Replace duplicate active-path attenuation and enable the arbiter in the runtime.
6. Run full native verification and live-test behind one kill switch.
7. Expose additional configuration only if benchmark and live evidence require it.

