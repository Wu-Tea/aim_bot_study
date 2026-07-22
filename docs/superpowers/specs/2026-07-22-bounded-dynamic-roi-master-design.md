# Bounded Dynamic ROI Master Design

Date: 2026-07-22
Status: accepted
Canonical scope: native DXGI 480x416 capture, stable vision coordinates, committed-target ROI guidance, offline and closed-loop acceptance
Supersedes for future implementation: `2026-07-21-bounded-dynamic-vision-roi-design.md` and `2026-07-21-bounded-dynamic-vision-roi.md`

## 1. What this changes

The production vision engine consumes one fixed `480x416` crop. A close target can cross the crop edge before the controller has moved the camera far enough to restore it, causing clipped geometry, confidence loss, missed detections, or a bad target handoff.

This design lets the physical DXGI crop make a bounded, temporary offset around its centered position. It preserves an already committed target for a short interval without resizing the image, adding another inference, changing the controller coordinate system, or turning the viewport into a second aiming controller.

The users of this feature are:

- DXGI capture, which applies the next requested physical crop origin;
- the native vision engine, which translates ROI-local detections into stable coordinates;
- the selector and color-cue path, which consume stable detections but local pixels;
- `TargetCoordinator`, which remains the sole owner of target identity and lifecycle;
- the controller, which supplies identity-safe committed geometry and user intent;
- benchmarks and telemetry, which prove that viewport motion is invisible to control behavior.

## 2. Primary outcome

Retain a committed edge target long enough for normal ADS/BodyLock camera motion to bring it back into the centered field, while preserving all current control and fire contracts.

Success means:

- TensorRT input remains exactly `480x416`;
- inference count and configured FPS do not increase;
- the stable crosshair remains `(240,208)`;
- an identical stable observation sequence produces identical selector, tracker, controller and AutoFire behavior regardless of physical viewport placement;
- close edge-target clipped/missed frames improve materially;
- ordinary and non-edge scenes remain centered and do not regress;
- any incomplete implementation stage leaves normal gameplay unchanged.

## 3. Non-goals

Dynamic ROI does not:

- enlarge or retrain the model;
- resize a larger field into `480x416`;
- scan the whole screen for new targets;
- follow every raw detection or challenger;
- create a target-centered camera;
- add a second tracker, hold timer, lifecycle owner or prediction stack;
- change ADS strength, BodyLock strength, slowdown, brake, intent fusion or recoil;
- grant target selection, aim authority or fire authority;
- allow yellow cue, green cue, raw left stick or raw right stick to choose a target;
- change mouse or Python fallback runtimes;
- introduce weapon data or persistent learning.

## 4. Stage-safety and uninterrupted usability

Every implementation stage must be independently mergeable and must preserve the current playable runtime.

### 4.1 Default-off invariant

Until final acceptance, absent or explicit configuration resolves to:

```toml
[runtime.vision.dynamic_roi]
enabled = false
```

With the feature disabled:

- requested and applied offsets are bit-stable zero;
- DXGI uses the existing centered `480x416` source box;
- selector, tracker, ADS, BodyLock, AutoFire and recoil receive the same values as before;
- no planner state affects timing or lifecycle;
- existing user `config.toml` files continue to run without new required keys.

### 4.2 No half-enabled state

No stage may expose a usable `enabled = true` path until every component needed by that stage is connected and tested. In particular:

- Stage 1 adds coordinate/capture capability but runtime configuration cannot activate viewport motion;
- Stage 2 adds a pure planner and offline tooling but production runtime still remains fixed-centered;
- Stage 3 connects the complete runtime behind the default-off flag;
- only the acceptance commit may enable it in a development profile;
- production/local user configuration is not rewritten automatically.

### 4.3 Failure fallback

Invalid guidance, stale identity, non-finite data, capture reinitialization, output-size change, service timeout or transform failure requests zero offset and cannot add aim/fire authority. ADS release resets immediately; failures while ADS remains held recenter at the bounded return speed unless frame interpretation itself is unsafe, in which case planner/capture reset immediately.

## 5. Ownership model

One component owns each stateful decision:

| Concern | Sole owner |
|---|---|
| Target identity, lifecycle and ADS control mode | `TargetCoordinator` |
| Matching committed body geometry | controller committed-geometry cache |
| Requested dynamic viewport offset | `CaptureRoiPlanner` |
| Physically achievable/applied viewport offset | `DxgiRoiCapture` |
| Frame-to-offset interpretation | captured frame metadata |
| Selection and cue policy | `VisionTargetSelector` |
| ADS/BodyLock output | existing controller pipeline |
| Fire permission | existing `TargetPlan` and AutoFire gate |

The planner does not own target identity, lifecycle, body-box association, prediction filtering, controller authority or fire state.

## 6. Canonical data flow

```text
previous stable VisionObservationBatch
        |
        v
TargetCoordinator commits target identity/lifecycle
        |
        v
TargetPlan(target_id, source_observation_id, prediction, velocity)
        |
        +--> controller matches source_observation_id to the same snapshot candidate
        |       and caches its stable body box
        v
immutable CaptureRoiGuidance
        |
        v
CaptureRoiPlanner computes next requested offset
        |
        v
VisionService worker applies guidance before the next capture
        |
        v
DxgiRoiCapture clamps and records actual offset on that frame
        |
        v
TensorRT local detections -> stable transform -> selector/tracker/controller
```

Guidance is causal: frame `N` and its committed plan can only guide frame `N+1` or later. Current-frame raw detections never move their own viewport.

## 7. Identity-safe guidance

### 7.1 Required plan identity

Add the source observation identity to `pipeline_contract::TargetPlan`:

```cpp
std::uint64_t source_observation_id = 0;
```

`TargetCoordinator` copies its committed internal `source_id_` into this field. The persistent `target_id` still identifies the coordinator track; `source_observation_id` identifies the exact vision candidate that supplied the currently committed geometry.

### 7.2 Committed geometry cache

After `TargetCoordinator::update`, the controller searches the same consumed observation snapshot for a valid candidate whose ID equals `plan.source_observation_id`. Only that body box may update the cache.

The cache carries:

```text
target_id
source_observation_id
source_frame_id
stable_body_box_px
captured/committed timestamp
```

It clears on controller reset, ADS release, no-target lifecycle, target-ID change, invalid geometry or expired occlusion budget.

During coasting, the last proven geometry may be retained and translated by the committed plan prediction. It may not be replaced by an unmatched current raw box.

### 7.3 Forbidden composition

This composition is invalid:

```text
committed TargetPlan A + newest raw/selected body box B
```

A challenger must first be committed by `TargetCoordinator`; until then it cannot steer capture.

## 8. Guidance contract

`CaptureRoiGuidance` is a trivially copyable immutable snapshot containing no fire permission:

```cpp
struct CaptureRoiGuidance {
    std::uint64_t ads_epoch;
    std::uint64_t committed_target_id;
    std::uint64_t source_observation_id;
    std::uint64_t source_frame_id;

    bool controller_aiming;
    bool has_committed_target;
    bool has_body_box;
    TargetLifecycle lifecycle;

    Vec2f stable_predicted_aim_px;
    Vec2f velocity_px_per_sec;
    Box2f stable_body_box_px;
    Vec2f user_intent_direction;

    float user_intent_strength;
    float observation_age_ms;
    float remaining_occlusion_budget_ms;
};
```

The guidance builder validates:

- physical controller ADS is active;
- plan and cached geometry target IDs match;
- source observation IDs match when newly observed;
- source frame is not from the future;
- all geometry and motion values are finite;
- observation age remains inside the coordinator's existing budget;
- lifecycle is `Observed`, `Reacquiring` or permitted `Coasting`;
- cached body box has positive dimensions.

It does not copy `fire_authority` or `fire_requested` into the planner contract.

## 9. Coordinate contract

### 9.1 ROI-local space

The current `480x416` texture uses local origin `(0,0)`. TensorRT output, D3D11/CUDA addresses, pixel buffers and local crop bounds remain ROI-local.

### 9.2 Stable-centered space

The existing controller coordinate system remains anchored to the original centered crop:

```text
crosshair = (240,208)
stable_x = local_x + applied_offset_x
stable_y = local_y + applied_offset_y
local_x  = stable_x - applied_offset_x
local_y  = stable_y - applied_offset_y
```

Stable coordinates may be negative or exceed `480x416`. Nominal dimensions continue to define controller normalization and target-size normalization.

### 9.3 Physical output space

```cpp
centered_left = (output_width - crop_width) / 2;
centered_top  = (output_height - crop_height) / 2;
roi_left = clamp(centered_left + requested_x, 0, output_width - crop_width);
roi_top  = clamp(centered_top + requested_y, 0, output_height - crop_height);
applied_x = roi_left - centered_left;
applied_y = roi_top - centered_top;
```

Consumers use the actual applied offset, never the unclamped request.

### 9.4 Frame-owned interpretation

Every capture attempt snapshots placement before the copy. Every produced frame atomically owns:

```text
frame_id
captured_at_ns
roi_left / roi_top
roi_offset_x / roi_offset_y
crop_width / crop_height
output_width / output_height
viewport_sequence
```

Frame `N` cannot be interpreted using offset or viewport sequence from frame `N+1`. Timeout/no-update does not advance `frame_id`; `viewport_sequence` advances when a placement is applied to a capture attempt and its behavior is covered by service tests.

### 9.5 Stable invariants

For a physically static target, changing only viewport placement must not materially change stable body box, aim point, `dx/dy`, identity, tracker velocity/acceleration, aim authority, fire authority or controller output.

Maximum independent stable-coordinate spread is `0.5 px`.

## 10. Color readback and cue processing

Detector boxes are transformed to stable coordinates before selector identity logic. Pixel data remains local.

Color readback performs these steps:

1. compute the required region in stable coordinates;
2. intersect it with the stable rectangle covered by the current viewport;
3. translate that intersection to ROI-local CUDA copy coordinates;
4. construct `ColorFrameView` with a stable-coordinate origin;
5. subtract that origin when reading the local buffer.

A stable region must not be clamped to nominal `[0,480] x [0,416]`; it is clamped to the current stable viewport.

Green friendly cue remains a hard reject. Yellow enemy cue remains auxiliary selector evidence only and cannot independently move ROI or grant fire authority.

## 11. Planner behavior

### 11.1 Activation

The planner can move only when all are true:

- runtime flag enabled;
- physical controller aiming is true;
- the current ADS epoch has already emitted its mandatory centered first update;
- committed identity-safe guidance is valid;
- observation/coasting age is within budget.

No target means desired offset zero.

### 11.2 Protected geometry

The initial protected region is:

- middle `70%` of committed body width;
- top `55%` of committed body height;
- translated from the body's canonical aim point to the committed predicted aim;
- optionally advanced once by the declared capture prediction horizon.

The controller's canonical aim height remains the existing `0.365`. The planner must not silently create a second competing aim-height policy; its protected-region calculation uses the committed predicted aim as the authority.

The planner moves only enough to keep the protected region within configurable safe margins. It does not center the target in the moved viewport.

### 11.3 Bounds

For crop dimensions `W,H` and sniff scale `S`:

```text
max_offset_x = W * (S - 1) / 2
max_offset_y = H * (S - 1) / 2
```

For `480x416`:

| Scale | Max X | Max Y |
|---:|---:|---:|
| 1.25 | 60 px | 52 px |
| 1.50 | 120 px | 104 px |
| 1.75 | 180 px | 156 px |

Acceptance selects the smallest passing scale.

### 11.4 Motion rate

Offset movement uses a vector-magnitude speed limit so diagonal movement cannot exceed the configured scalar speed by `sqrt(2)`. `dt` is finite and capped against stalls. Return-to-center may use a separately named speed but cannot snap except on ADS release, reset or unsafe frame state.

### 11.5 User intent

The planner first computes the required direction from committed protected geometry. User right-stick intent may increase movement speed only in proportion to positive vector alignment:

```text
speed_scale = 1 + bonus * intent_strength * max(0, dot(intent_dir, movement_dir))
```

Intent cannot choose or reverse direction, create movement when desired offset is zero, move without a committed target, or change target identity. Left stick remains an input to existing target/control response logic, not a direct ROI position command.

### 11.6 ADS lifecycle

Every physical ADS false-to-true transition creates one monotonically increasing epoch. The first planner update of a new epoch applies `(0,0)` regardless of available guidance. Repeated `set_aiming(true)` calls in the same epoch do not reset again. ADS release clears guidance, committed geometry and requested/applied offset immediately.

Vision keepwarm may keep the engine polling but never acts as physical ADS and never creates or preserves an active ROI epoch.

### 11.7 Missing and coasting behavior

- `Observed`: may move toward the bounded desired offset.
- `Reacquiring`: may retain or reduce the envelope using committed identity-safe geometry.
- `Coasting`: may retain or move toward center, but absolute offset on each axis cannot increase beyond the last observed envelope in the initial version.
- `None` or invalid: desired offset is zero.

Expansion from unobserved evidence requires a separately versioned design and benchmark; it is not part of this implementation.

## 12. Async service ordering

The controller/runtime thread stores intent, guidance and ADS transition requests under the `VisionService` mutex. It never mutates `VisionEngine` while the worker may be polling.

On a worker iteration, ordering is:

1. snapshot pending state under mutex;
2. outside the mutex, apply pending `begin_ads_epoch` reset;
3. apply engine aiming state;
4. apply user aim intent;
5. apply capture ROI guidance;
6. poll one frame;
7. publish result and its frame-owned viewport metadata.

This ordering guarantees that a new ADS epoch resets before its immediate first poll and that guidance computed from frame `N` only affects a later capture.

## 13. Runtime configuration

Initial schema:

```toml
[runtime.vision.dynamic_roi]
enabled = false
sniff_scale = 1.50
prediction_ms = 50.0
max_speed_px_per_sec = 1200.0
return_speed_px_per_sec = 1200.0
intent_speed_bonus = 0.25
edge_margin_x_px = 56.0
edge_margin_y_px = 48.0
```

Validation ranges:

- `sniff_scale`: `1.0..1.75`;
- `prediction_ms`: `0..100`;
- speeds: `100..3000 px/s`;
- intent bonus: `0..0.5`;
- margins: non-negative and less than `45%` of the corresponding crop dimension.

These are bounded implementation parameters, not controller-strength parameters. Unknown or invalid values fail configuration loading consistently with current strict config behavior; missing section preserves disabled centered operation.

## 14. Telemetry

Debug telemetry records enough information to reconstruct every decision:

```text
ads_epoch
controller_aiming / engine_aiming
roi_enabled
roi_reason
roi_target_id
roi_source_observation_id
roi_source_frame_id
roi_guidance_age_ms
roi_lifecycle
roi_requested_x/y
roi_applied_x/y
roi_desired_x/y
roi_max_x/y
roi_edge_clamped
roi_intent_speed_scale
viewport_sequence
frame_id
```

Logging remains opt-in and does not add synchronous high-rate file I/O to the controller path. Normal runtime does not require debug logs.

## 15. Verification model

Dynamic ROI requires two independent evidence families. A single total score cannot accept it.

### 15.1 Real-video evidence

Retained fixtures record source filename, source SHA-256, video metadata, exact episode ranges, schema version, independently reviewed annotations and identity/visibility/clipping/occlusion transitions. Model boxes are proposals, not ground truth.

Matched modes are:

- `fixed040`: centered ROI, decode floor `0.40`;
- `fixed020`: centered ROI, decode floor `0.20`;
- `dynamic020`: dynamic ROI, decode floor `0.20`.

This separates weak-decode benefit from placement benefit.

### 15.2 Corrected committed-target replay

The production-valid replay path includes:

```text
selector observations -> TargetCoordinator -> TargetPlan
-> identity-safe committed geometry -> planner
```

The selector's current result must not be substituted directly for a committed target, and all detected targets must not be assigned a fake constant ID. Multi-target, occlusion and reappearance episodes verify that challengers cannot steer before commitment.

### 15.3 Independent coordinate oracle

Coordinate residual is not calculated by algebraically subtracting and re-adding the same runtime offset. It uses at least one independent oracle:

- project stable detections back to original-video physical screen coordinates and compare with reviewed annotation; or
- infer the same static target through two known viewport placements and compare independently recovered stable geometry.

Mutation tests deliberately break offset sign, omit compensation and reinterpret an old frame with a new offset. Each mutation must fail the oracle.

### 15.4 Deterministic closed loop

Fixed-seed closed-loop scenarios cover:

- non-edge ordinary tracking;
- close target crossing left/right/top/bottom edges;
- half-body obstruction with simultaneous X/Y motion;
- brief total occlusion followed by reappearance;
- multiple targets with near committed target and farther challenger;
- physical ADS epoch release/repress;
- target strafe reversal, jump apex and fall;
- correct, delayed, opposing, stale and corrective user input;
- BodyLock continuation and AutoFire authority.

Metrics include acquisition/reacquisition time, visible recall, miss duration, clip ratio, chest error, cumulative `px*ms`, BodyLock interruptions, settle, center-cross burden, jerk, user-fight burden, recenter latency, viewport oscillations, incorrect switches and fire authorization source.

## 16. Hard acceptance gates

Reject an implementation if any condition occurs:

- non-finite output;
- independent stable-coordinate residual exceeds `0.5 px`;
- ROI motion appears as target velocity beyond numerical tolerance;
- an ADS epoch's first captured frame has nonzero offset;
- a non-edge episode uses a nonzero offset without protected-geometry need;
- raw challenger or unmatched body box moves ROI;
- identity switches increase relative to matched fixed mode;
- weak, cue-only, coasting, projected or reused evidence gains fire authority;
- controller output differs for identical stable observation and intent sequences solely because viewport placement changed;
- TensorRT dimensions, inference count or requested FPS change;
- planner/transform/selector added CPU P95 exceeds `0.15 ms`;
- vision `age_ms`, capture wait or output wait materially regresses;
- stale offset persists beyond bounded recenter behavior;
- offset oscillation/reversal increases without edge-retention benefit;
- fixture lacks independent review or provenance;
- existing runtime acceptance, controller tests, AutoFire tests or production smoke fail.

Expected benefit gate: relative to `fixed020`, `dynamic020` reduces clipped/missed frames in the designated edge episodes by at least `50%` while all hard gates pass. If evidence shows the threshold is statistically unstable, the feature remains disabled and the acceptance report records the result rather than weakening a safety gate during implementation.

## 17. Benchmark anti-cheating requirements

The suite must intentionally reject:

1. no coordinate compensation;
2. wrong offset sign;
3. old frame interpreted with latest offset;
4. target-centered viewport chase;
5. raw challenger guidance;
6. predicted/coasting fire authority;
7. persistent stale offset;
8. double prediction;
9. fake constant target identity;
10. a tautological coordinate residual that remains zero after production transform mutation.

## 18. Staged delivery

### Stage 1: Dormant capture and coordinate foundation

Add pure coordinate transforms, clamped ROI placement, frame-owned metadata and tests. Production runtime remains centered and has no activatable dynamic path. Completion means current application behavior is unchanged and all static coordinate mutations are caught.

### Stage 2: Dormant planner and offline evidence

Add the pure planner, pybind test surface, fixture tooling, corrected coordinate oracle and fixed/dynamic video ablations. Production runtime still remains fixed-centered. Completion means the planner has deterministic evidence but cannot affect gameplay.

### Stage 3: Default-off identity-safe runtime integration

Add `source_observation_id`, committed geometry cache, immutable guidance, async service ordering, stable selector/color readback, config, telemetry and closed-loop scenarios. The runtime path is complete but disabled by default. Completion means enabled and disabled acceptance matrices pass.

### Acceptance and development enablement

Run the full video, closed-loop, native runtime and live debug smoke matrix. Select the smallest passing sniff scale. Only then may an explicit development config enable dynamic ROI. User gameplay validation remains required before treating it as the new personal baseline.

At every boundary, stopping work leaves the current centered runtime fully usable.

## 19. Rollback

Runtime rollback is configuration-only:

```toml
[runtime.vision.dynamic_roi]
enabled = false
```

Disabled mode must not depend on retained planner state. If an enabled build misbehaves, disabling the flag restores centered capture without reverting ADS, BodyLock, selector, AutoFire or recoil changes. Code rollback remains separable by stage commits because coordinate foundation, planner/offline tools and runtime wiring are committed independently.

## 20. Evidence and reporting

The retained acceptance artifact includes:

- schema version;
- Git revision and dirty state;
- effective config fingerprint;
- engine SHA-256 and `480x416` identity;
- fixture SHA/version/reviewer status;
- benchmark script hashes and seeds;
- exact ablation parameters;
- per-episode and per-seed metrics;
- hard-gate results;
- selected and rejected sniff scales with reasons;
- runtime overhead;
- known limitations and user live-validation status.

Artifacts without revision, config, engine and fixture identity are diagnostic history, not authoritative baselines.

## 21. Source material and precedence

This canonical design integrates:

- the accepted repository design dated 2026-07-21;
- the repository implementation plan dated 2026-07-21;
- the external `dynamic_roi_codex_package` master, coordinate, benchmark and patch guidance;
- current repository ownership and runtime contracts.

Precedence for implementation is:

1. this master design;
2. the active stage implementation plan;
3. current tested repository contracts;
4. external reference implementation as non-authoritative starter code;
5. older design and plan as historical rationale only.

External reference tests passing outside the production repository do not constitute Windows/MSVC/CUDA/TensorRT or live-runtime acceptance.
