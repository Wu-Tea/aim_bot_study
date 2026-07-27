# Predictive Dynamic Viewport Design

Date: 2026-07-27
Status: proposed; design direction approved, written-spec review pending
Canonical candidate for: native `480x416` DXGI capture, committed-target viewport prediction, mathematical optimization, offline evidence and staged runtime integration
Intended to supersede after written-spec approval:

- `2026-07-22-bounded-dynamic-roi-master-design.md`
- `2026-07-21-bounded-dynamic-vision-roi-design.md`
- `2026-07-21-bounded-dynamic-vision-roi.md`

The older documents remain historical rationale. They must not be used as an
independent implementation authority after this design is accepted.

## 1. What this is

The production detector currently consumes one centered physical `480x416`
crop. The existing dynamic-ROI plans proposed moving that fixed-size crop to
retain an already committed target near an edge, but no dynamic capture code
has been implemented.

This design replaces the planned offset-only component with a predictive
dynamic viewport. The viewport may make a bounded center offset and a bounded
near-range zoom-out while the detector input remains exactly `480x416`.

The mathematical objective is:

> Given an identity-safe prediction of the committed target's aim/head/chest
> region at the next capture time, choose the reachable offset and zoom-out
> that preserve that region with the smallest viewport intervention.

The first work product is not a DXGI or TensorRT change. It is an isolated
mathematical benchmark, named M0, which must prove solver correctness,
prediction value and theoretical headroom before production capture is
modified.

## 2. Problem being solved

Two related failures occur before a close target becomes completely
unrecognizable:

1. A target of roughly stable size approaches one side of the centered crop
   faster than camera motion returns it to center. A bounded viewport offset can
   retain it without reducing target pixels.
2. A rapidly approaching target grows until several crop edges are threatened.
   Translation alone cannot fit a region that is larger than the crop. A
   bounded zoom-out can temporarily widen the physical field before resizing
   it to the existing detector input.

The design does not promise detection after an extreme face-to-face crossing.
When only non-semantic body texture is visible, even a full-screen detector may
fail. The supported operating region ends before that point.

## 3. Primary outcome

The design succeeds when it establishes a causal and testable chain:

```text
identity-safe stable observations
        -> committed geometry predictor
        -> predicted protected region with uncertainty
        -> minimum-change viewport solver
        -> frame-owned physical viewport
        -> stable detector observations
```

The eventual runtime must:

- keep TensorRT input dimensions at `480x416`;
- keep inference count and requested capture FPS unchanged;
- use only the current committed target to guide the viewport;
- retain edge and rapidly growing close targets longer;
- preserve stable selector, tracker, controller and AutoFire semantics;
- return to centered `offset=(0,0), scale=1` without target authority;
- remain completely disabled until offline, closed-loop, video and live gates
  pass.

## 4. First-version scope

The first production candidate includes:

- one committed target;
- bounded X/Y viewport offset;
- bounded near-range zoom-out;
- base physical crop `480x416`;
- maximum candidate zoom-out `1.50`;
- one short causal prediction horizon derived from real pipeline timestamps;
- a constant-velocity Kalman predictor for position and log scale;
- exact interval projection for offset and enumerated scale;
- continuous-QP, matrix and hindsight oracles for verification;
- centered reset on every physical ADS epoch;
- default-off runtime integration only after M0 and real-video evidence pass.

## 5. Non-goals

The first version does not:

- search behind the player after a target crosses the camera;
- perform full-screen reacquisition after complete loss;
- zoom in for small or distant targets;
- add another inference pass or change the TensorRT engine dimensions;
- resize an arbitrary aspect ratio into `480x416`;
- let raw detections, challengers, cues or stick input choose a target;
- let the viewport planner own target identity, target lifecycle or motion
  estimation;
- add target hold, coast or reacquire time beyond `TargetCoordinator`;
- change ADS, BodyLock, slowdown, brake, vector fusion or recoil;
- grant aim authority or fire authority;
- require a camera-response learner for the baseline predictor;
- introduce reinforcement learning, POMDP control or a production online MPC;
- enable multiple-target utility optimization or no-target directional sniffing.

## 6. Current state and intended change

Current production behavior:

```text
centered physical 480x416 crop
    -> 480x416 TensorRT
    -> ROI-local detector output
    -> existing selector/tracker/controller
```

No `CaptureRoiPlanner`, dynamic coordinate path, dynamic crop benchmark or
runtime configuration from the older plans is implemented.

The intended eventual behavior is:

```text
previous stable observation batch
    -> TargetCoordinator commits identity/lifecycle
    -> identity-safe geometry predictor
    -> CaptureViewportGuidance
    -> CaptureViewportPlanner
    -> next physical source rectangle
    -> resize to 480x416 when scale > 1
    -> TensorRT local output
    -> stable transform
    -> existing selector/tracker/controller
```

M0 implements only the predictor, solver, oracles, synthetic fixtures and
metrics. It does not modify the production chain.

## 7. Ownership

One component owns each stateful concern:

| Concern | Sole owner |
|---|---|
| Target identity and lifecycle | `TargetCoordinator` |
| Stable committed box association | committed-geometry cache |
| XY/log-scale state estimation | `CommittedGeometryPredictor` |
| Requested offset and scale | `CaptureViewportPlanner` |
| Physically applied source rectangle | future DXGI capture component |
| Frame-to-viewport interpretation | captured frame metadata |
| Target selection and cue policy | `VisionTargetSelector` |
| ADS/BodyLock output | existing controller pipeline |
| Fire permission | existing `TargetPlan` and AutoFire gate |

The planner receives one predicted protected region. It does not keep a history
of detector boxes and does not estimate another velocity.

## 8. Coordinate contract

### 8.1 Spaces

The original centered `480x416` crop defines stable-centered coordinates.
Using top-left notation, the stable crosshair remains `(240,208)`. For solver
math, subtracting the crosshair gives a convenient origin `(0,0)`.

The detector input always uses local dimensions:

```text
W = 480
H = 416
```

Let the physically applied viewport center in stable-centered coordinates be
`c=(offset_x,offset_y)` and let its applied scale be `s>=1`.

Stable to local:

```text
local_x = (stable_x - offset_x) / s + 240
local_y = (stable_y - offset_y) / s + 208
```

Local to stable:

```text
stable_x = offset_x + s * (local_x - 240)
stable_y = offset_y + s * (local_y - 208)
```

Box widths and heights also transform:

```text
stable_width  = local_width  * s
stable_height = local_height * s
```

Tracker position, velocity, acceleration and size trend are computed only after
this transform.

### 8.2 Frame-owned transform

Every produced frame must eventually own the exact transform that created it:

```text
frame_id
captured_at_ns
source_left / source_top
source_width / source_height
applied_offset_x / applied_offset_y
applied_scale_x / applied_scale_y
viewport_sequence
output_width / output_height
```

No consumer may interpret an old frame using the latest global viewport. The
requested viewport and the physically applied, screen-clamped viewport are
distinct values.

### 8.3 Aspect ratio

`480:416` reduces to `15:13`. A strict integer physical source rectangle has:

```text
source_width  = 15k
source_height = 13k
```

The base crop is fixed:

```text
k=32 -> 480x416 -> scale 1.0
```

The initial maximum is:

```text
k=48 -> 720x624 -> scale 1.5
```

M0 supports all integer `k` in `32..48`. Coarser production candidates may be
selected by evidence, but X and Y may never be stretched independently.

## 9. Identity-safe predictor input

The predictor accepts only geometry proven to belong to the committed target:

```text
target_id
source_observation_id
source_frame_id
captured_at_ns
stable aim point
stable reliable body geometry
confidence / source tier
clipping state
lifecycle
```

Newly observed geometry must match the committed source observation. An
unmatched newest box cannot update the predictor or guide the viewport.

Measurement rules:

- strong, fresh and non-clipped observations may update position and scale;
- strong but clipped observations may update position with increased noise but
  do not update scale trend;
- weak associated observations may update position with high measurement
  covariance but do not update scale trend;
- cue-only and predicted-only evidence do not update the filter;
- coasting performs prediction only and grows uncertainty;
- target change, invalid identity, ADS release or invalid geometry resets the
  predictor state used for guidance.

## 10. Baseline motion and scale predictor

### 10.1 State

The baseline is a six-dimensional constant-velocity Kalman filter:

```text
x = [px, py, q, vx, vy, vq]
```

where:

```text
px, py = committed canonical aim position in stable coordinates
q      = log geometric target scale
vx, vy = stable image-plane velocity
vq     = log-scale growth rate
```

The scale observation is:

```text
q = 0.5 * (log reliable_width + log reliable_height)
```

The predictor does not independently track head width, head height, chest width
and chest height in M0. Protected shape comes from the last reliable committed
geometry and is multiplied by the predicted common scale growth.

### 10.2 Transition

For elapsed time `dt`:

```text
px' = px + vx * dt
py' = py + vy * dt
q'  = q  + vq * dt
vx' = vx
vy' = vy
vq' = vq
```

Process and measurement covariance are configurable in the algorithm fixture,
not exposed as production user tuning in M0.

### 10.3 Causal horizon

The prediction horizon is not simply one game frame. It is:

```text
expected next affected capture time - source frame captured_at
```

It includes result publication, planner consumption and capture scheduling.
M0 exercises horizons from measured timing fixtures and a bounded `0..100 ms`
range. The predictor must never use future observations.

### 10.4 Initialization

- First reliable observation initializes position and scale.
- Second reliable observation permits a low-confidence velocity estimate.
- Three to five reliable observations allow normal trend-driven guidance.
- Direct geometric edge or size pressure may still request a viewport change
  before trend confidence is established.

### 10.5 Uncertainty

The predictor emits mean and covariance. The protected region expands by a
bounded uncertainty margin derived from position and log-scale covariance.

Gaussian `k-sigma` is an algorithmic approximation, not a claimed real-world
coverage probability. M0 reports empirical containment calibration by
confidence, clipping and lifecycle bucket.

## 11. Camera-response-aware predictor candidate

M0 includes an offline/closed-loop ablation:

```text
P0 = six-dimensional relative-screen CV Kalman
P1 = CV Kalman plus validated delivered/pending camera-response input
```

P1 cannot become a production dependency from this design. Existing causal
response K2-K4 and acquisition guardrails must authorize it separately.

P1 must avoid double counting camera motion. Its test contract must explicitly
state whether filter velocity represents observed relative motion or
target-local motion with past camera response removed.

## 12. Predicted protected region

The predictor output used by the solver is one axis-aligned stable rectangle:

```text
R = [left, right, top, bottom]
```

It contains:

- the committed canonical aim point;
- the middle `70%` of the last reliable body width;
- the upper `55%` of the last reliable body height;
- predicted XY translation;
- predicted common scale growth;
- bounded uncertainty inflation.

The committed canonical aim point remains authoritative. The viewport design
does not create another aim-height policy.

## 13. Viewport variables and bounds

The next viewport is:

```text
u = (offset_x, offset_y, scale)
```

Initial mathematical bounds:

```text
1.0 <= scale <= 1.50
```

Offset bounds use the finite envelope from the older design:

```text
abs(offset_x) <= 120 px
abs(offset_y) <= 104 px
```

M0 may sweep smaller envelopes, but it cannot silently evaluate a larger
production envelope. Screen output bounds and per-step reachability further
restrict the requested center.

## 14. Exact single-step solver

### 14.1 Safe viewport

Let local safety margins be:

```text
margin_x = 56 px
margin_y = 48 px
```

At scale `s`, safe stable half-extents are:

```text
Ax(s) = (240 - margin_x) * s = 184s
Ay(s) = (208 - margin_y) * s = 160s
```

### 14.2 Containment center interval

For:

```text
R = [left, right, top, bottom]
```

full safe containment at fixed scale requires:

```text
offset_x in [right - Ax(s), left + Ax(s)]
offset_y in [bottom - Ay(s), top + Ay(s)]
```

These intervals are intersected with:

- configured offset bounds;
- source-rectangle screen bounds at that scale;
- per-step offset reachability;
- any reset or ADS-epoch restriction.

A fixed-scale candidate is feasible only when both intersections are nonempty.

### 14.3 Preferred center

The solver first computes the unconstrained minimum of the configured convex
quadratic viewport objective. It then projects that preferred center onto the
feasible X/Y intervals.

For a pure minimum-change objective, the preferred center is the previous
applied center. If a center-return or second-difference term is present, the
preferred center is the unconstrained minimizer of that complete objective; it
is not automatically the previous center.

### 14.4 Scale candidates

M0 enumerates `k=32..48`, subject to scale rate limits. For each candidate:

1. compute safe extents;
2. compute containment intervals;
3. intersect configuration, screen and reachable intervals;
4. project the preferred center;
5. evaluate viewport cost;
6. retain feasibility, saturation and overflow diagnostics.

Runtime complexity is `O(N_scale)` with constant memory.

### 14.5 Candidate selection

Visibility constraints are not traded against motion cost when a feasible
candidate exists.

Candidates are selected in this order:

1. aim point contained;
2. protected region fully contained;
3. minimum normalized viewport intervention;
4. minimum temporal movement and scale switching.

The normalized intervention cost includes:

```text
zoom cost from scale=1
center offset cost from offset=(0,0)
motion cost from previous applied viewport
optional second-difference cost
```

X/Y pixels and dimensionless scale are normalized before being combined.

## 15. Continuous-QP oracle

With continuous `scale`, base dimensions `480x416` and linear containment,
screen and rate constraints, the one-step problem is a small convex quadratic
program in:

```text
offset_x, offset_y, scale
```

M0 implements a continuous oracle independently of the enumerated solver. It is
not the production runtime algorithm.

The oracle measures:

- enumeration approximation loss;
- incorrect interval endpoints or signs;
- objective inconsistencies;
- infeasible/feasible classification mismatches.

## 16. Infeasible and saturated behavior

If no candidate fully contains the region, the solver returns a bounded
`SaturatedButSafe` result.

The ranking is lexicographic:

```text
aim_point_outside
maximum normalized side overflow
total normalized overflow
viewport intervention cost
temporal motion cost
```

The continuous-QP oracle uses hierarchical or separately weighted nonnegative
slacks for the same priority. Saturation does not:

- extend target lifecycle;
- grant aim or fire authority;
- permit scale or offset beyond configured bounds;
- initiate full-screen or behind-camera search.

Invalid guidance and no target request the home viewport.

## 17. Rate limiting and hysteresis

Offset and scale changes are bounded by elapsed time. Stalls use a capped `dt`.

Zoom-out may react immediately to a predicted containment failure within its
rate limit. Returning to a smaller scale requires:

- the next smaller candidate to contain the protected region;
- an additional exit reserve;
- continuous satisfaction for `80..120 ms` in M0 sweeps;
- no strong positive log-scale growth;
- no new edge threat.

Offset returns toward center when safe. ADS release or unsafe frame
interpretation resets synchronously to home; ordinary active-target return is
rate-limited.

## 18. Detector-quality evidence

Geometric containment is not detector success. Offline video evidence measures:

```text
Pdetect(target model height,
        visible ratio,
        clipping ratio,
        viewport scale,
        occlusion bucket)
```

The same full-screen frame and TensorRT engine are replayed through:

- fixed `480x416`;
- pan only;
- zoom only;
- pan plus zoom.

The first production solver remains geometry-first and selects the smallest
feasible scale. A candidate scale may be rejected by an empirically established
minimum detector-size guard. More general `Pdetect` utility optimization is
outside the first version.

## 19. Mathematical oracles

### 19.1 Matrix Oracle

The Matrix Oracle enumerates a dense discrete grid:

```text
offset_x x offset_y x scale
```

It evaluates hard containment or a weighted importance map using an integral
image. It provides an independent brute-force answer and supports non-rectangular
soft protection maps.

For a single rectangle, the analytic solution is continuous-exact. The matrix
result is exactly equal only when the grid contains the continuous optimum;
otherwise it is a bounded grid approximation.

### 19.2 Hindsight Oracle

Given the complete realized target trajectory, the continuous hindsight path is
one convex QP over all viewport states with per-step containment, screen and
rate constraints.

A separately discretized hindsight oracle uses a time-layered shortest path or
dynamic program.

The causal policy is compared with the same admissible hindsight class:

```text
regret = causal realized loss - hindsight optimal loss
```

Hindsight results measure headroom only and cannot authorize future-information
use in production.

## 20. M0 mathematical benchmark

M0 is the first implementation milestone and is isolated from production.

It contains:

- six-dimensional Kalman predictor;
- naive last-observation and two-frame-linear predictors;
- perfect-future predictor;
- enumerated PMVP solver;
- continuous-QP oracle;
- matrix brute-force oracle;
- continuous and discrete hindsight oracles;
- deterministic synthetic trajectories;
- observation-noise and clipping mutations;
- closed-loop camera-response fixture;
- per-scenario and aggregate metrics.

M0 does not contain:

- DXGI dynamic source rectangles;
- GPU resize in the live engine;
- production service wiring;
- production configuration enablement;
- controller policy changes;
- AutoFire changes;
- live gameplay output.

## 21. Scenario suite

The deterministic suite covers:

- stationary centered target;
- constant-size horizontal left/right edge crossing;
- constant-size vertical top/bottom crossing;
- diagonal edge crossing;
- centered constant-rate approach;
- accelerating approach;
- diagonal approach with simultaneous scale growth;
- approach then stop;
- approach then retreat;
- lateral reversal;
- jump apex and fall;
- short partial occlusion;
- short total observation gap;
- clipped strong box;
- weak associated observation;
- target identity change;
- ADS release and repress;
- output screen boundary clamp;
- irregular capture/result intervals;
- old frame with new-transform mutation;
- wrong offset sign mutation;
- missing scale compensation mutation;
- extreme close saturation.

Every scenario uses fixed seeds and retains truth, observation, predictor,
viewport and metric traces.

## 22. Ablation matrix

M0 compares:

| Mode | Predictor | Offset | Zoom |
|---|---|---:|---:|
| Fixed | none | no | no |
| Perfect Pan | future truth | yes | no |
| Perfect Zoom | future truth | no | yes |
| Perfect Unified | future truth | yes | yes |
| Naive Unified | linear | yes | yes |
| Kalman Unified | 6D CV Kalman | yes | yes |
| Response Candidate | Kalman plus response input | yes | yes |
| Hindsight | complete truth path | yes | yes |

This separates solver headroom, predictor loss, response-model headroom and
causality regret.

## 23. Metrics

Predictor metrics:

- next-capture aim error;
- next-capture scale error;
- protected-region empirical containment;
- uncertainty calibration by quality/clipping/lifecycle;
- correction time after reversal;
- prediction error during observation gaps.

Viewport metrics:

- aim-point visible time;
- protected-region full-containment time;
- maximum and integrated side overflow;
- maximum continuous outside duration;
- average and maximum scale;
- time above scale `1`;
- nonzero-offset time;
- cumulative viewport movement;
- zoom movement;
- offset and scale reversal counts;
- scale switches;
- saturated decisions;
- recenter time.

Oracle metrics:

- enumerated solver cost gap to continuous QP;
- matrix grid approximation error;
- causal regret to hindsight;
- perfect-to-Kalman gain retention.

Closed-loop metrics:

- acquisition/reacquisition;
- target error `px*ms`;
- controller interruption;
- viewport/controller oscillation;
- stable-output invariance;
- wrong target and fire-authority changes.

## 24. M0 hard gates

M0 fails if:

- any output is non-finite;
- stable/local/stable residual exceeds `0.5 px`;
- viewport motion appears as target velocity beyond numerical tolerance;
- an old frame can be interpreted with a newer transform without detection;
- a feasible enumerated result violates any constraint;
- the enumerated solver reports infeasible when an allowed enumerated scale has
  a feasible interval;
- deterministic replay is not bit-stable;
- a challenger or unmatched identity changes guidance;
- a new ADS epoch begins with non-home viewport state;
- a noise-free centered ordinary target requests viewport intervention;
- no-target or invalid guidance increases offset or scale;
- saturated behavior exceeds configured bounds;
- weak, cue-only, predicted-only or viewport state changes fire authority;
- the benchmark omits seed, fixture version, revision or effective parameter
  identity.

M0 may proceed to real-video evidence only if:

- all hard gates pass;
- Perfect Unified reduces protected-region outside burden by at least `50%`
  relative to Fixed on designated edge/approach scenarios;
- Kalman Unified reduces the same burden by at least `50%` relative to Fixed
  across the non-adversarial designated suite;
- ordinary centered scenarios use non-home viewport for at most `1%` of
  evaluated time, attributable only to injected observation noise;
- Pan, Zoom and Unified contributions are reported separately;
- failure and saturation cases are retained rather than removed from aggregates.

These gates authorize only real-video investigation, not runtime integration.

## 25. Real-video gate

Full-screen retained recordings must include independently reviewed:

- ordinary centered tracking;
- horizontal and vertical edge pressure;
- centered rapid approach;
- diagonal rapid approach;
- partial occlusion during approach;
- multi-target scene with one committed target;
- extreme close failure.

Matched fixed/pan/zoom/unified modes use the same engine, source frames and
annotations.

Before production integration:

- designated clipped/missed-frame burden must fall at least `50%` versus fixed;
- the last valid committed observation must be extended by at least `50 ms`
  versus fixed capture in the designated close episodes;
- ordinary non-edge detection continuity must not regress;
- wrong switches and unauthorized fire evidence must not increase;
- the smallest passing scale/envelope must be selected;
- planner/transform/selector added CPU P95 must not exceed `0.15 ms`;
- enabled capture-to-result P95 must increase by no more than both `0.5 ms`
  absolute and `10%` relative to the matched fixed mode;
- requested capture FPS and inference count must remain unchanged;
- GPU resize, capture-to-result latency and vision age must be reported
  separately rather than hidden in one average;
- source, engine, revision, config, fixture and script identities must be
  retained.

## 26. Staged engineering after evidence

Only after M0 and real-video gates pass:

1. Dormant coordinate and frame-metadata foundation.
2. Dormant variable-source capture and resize path.
3. Default-off predictor and planner runtime wiring.
4. Closed-loop native controller acceptance.
5. Development-only enablement and live smoke.
6. Explicit user decision before treating it as a personal production
   baseline.

Every stage must leave disabled operation equivalent to the current centered
runtime.

## 27. Configuration boundary

M0 uses benchmark parameters, not production configuration.

The eventual runtime section remains default-off and bounded:

```toml
[runtime.vision.dynamic_viewport]
enabled = false
max_scale = 1.50
max_offset_x_px = 120.0
max_offset_y_px = 104.0
prediction_max_ms = 100.0
```

Rate, margin, uncertainty and hysteresis parameters remain internal until
evidence shows which must be operationally configurable. Existing local user
configuration is never rewritten automatically.

## 28. Rollback and authority

Runtime rollback is configuration-only:

```toml
[runtime.vision.dynamic_viewport]
enabled = false
```

Disabled mode must request and apply:

```text
offset=(0,0)
scale=1
```

No viewport result is target identity, aim authority or fire authority.

## 29. Evidence precedence

Implementation and acceptance precedence is:

1. this design after written-spec approval;
2. the M0 implementation plan;
3. current tested repository ownership and safety contracts;
4. retained benchmark artifacts with identity;
5. older dynamic-ROI documents as historical rationale only;
6. external research reports as non-authoritative mathematical references.

External formulas are not project evidence until their assumptions match the
fixed `480x416` base crop, stable coordinates, frame-owned transforms and
identity-safe lifecycle.

## 30. Decision summary

The selected first-version algorithm is:

```text
identity-safe committed stable geometry
    -> 6D CV Kalman on XY and log scale
    -> uncertainty-inflated predicted aim/head/chest rectangle
    -> enumerate exact-aspect-ratio scale candidates
    -> intersect containment, offset, screen and reachable center intervals
    -> project the complete objective's preferred center
    -> choose the minimum-intervention feasible viewport
    -> bounded saturation when none is feasible
```

The selected validation structure is:

```text
continuous QP oracle
+ matrix brute-force oracle
+ continuous/discrete hindsight oracle
+ deterministic synthetic and closed-loop fixtures
+ real TensorRT full-screen video replay
```

The selected start is M0. Production vision work does not begin until M0 proves
mathematical correctness and meaningful headroom, and real-video replay proves
that the existing detector benefits from the proposed physical viewport.
