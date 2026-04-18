# Vision Occlusion Compensation Design

**Goal:** Improve target continuity when the aim scope or weapon model partially or briefly occludes the detected target by adding short-horizon reconstruction and prediction inside `vision/targeting.py`, without changing controller logic.

**Scope:**
- Add a dedicated vision-side occlusion compensation component for `TargetSelector`.
- Support both partial-occlusion box reconstruction and short-horizon X/Y prediction.
- Preserve the current detector, runner, controller, and aim-enhancement pipeline responsibilities.
- Keep prediction conservative and limited to short scope-crossing timing gaps.

**Non-goals:**
- No controller-force shaping or gamepad policy changes.
- No model retraining or detector confidence retuning as part of this design.
- No long-horizon target tracking or full Kalman-style tracker.
- No change to the newly added capture/inference latest-only pipeline.

## Problem

The current detector-first targeting path works well when a target remains fully visible, but it degrades in two common ADS cases:

1. **Partial occlusion:** the scope or weapon model clips the upper body. A detection may still be present, but the box top drops and height shrinks, which causes the current upper-chest target point to collapse downward.
2. **Short total occlusion:** after 2-3 stable target frames, the target may disappear for a very short timing window while crossing the scope border or weapon model. The current implementation can only hold the old target for a small number of missing frames, which does not account for target motion.

For this problem, the user wants a conservative vision-side continuity mechanism:

- use recent stable target motion to bridge very short occlusion windows
- predict both X and Y
- only activate after 2-3 stable detections
- stop quickly if the target is not reacquired

## Approaches Considered

### 1. Increase the existing target hold window only

Pros:
- minimal code change
- no new component

Cons:
- does not adapt to target motion
- causes visible lag if the target is moving during occlusion
- does not solve partial-box collapse

### 2. Add short-horizon point prediction only

Pros:
- addresses brief total occlusion
- relatively small change

Cons:
- does not fix malformed current-frame boxes
- target point can still collapse when a clipped box is present

### 3. Add partial-box reconstruction plus short-horizon X/Y prediction

Pros:
- addresses both clipped-box and brief missing-box cases
- matches the real scope-edge / weapon-occlusion behavior the user described
- stays local to `vision/targeting.py`

Cons:
- requires an explicit new component and source tracking
- needs careful guardrails to avoid over-predicting

## Chosen Design

Choose **Approach 3**.

The vision runtime will gain a small dedicated occlusion-compensation component that plugs into `TargetSelector`:

1. **Observed target path:** if the current frame produces a normal valid target, keep existing behavior and mark the result as `observed`.
2. **Reconstructed target path:** if the current frame has a candidate that still looks like the same person but its upper box geometry is clearly clipped, rebuild a virtual box from current bottom/width plus recent stable height, then compute the target from that reconstructed box and mark it as `reconstructed`.
3. **Predicted target path:** if the current frame has no valid target, but the previous 2-3 samples were stable, use a strictly capped 2-frame constant-velocity prediction on both X and Y, along with predicted bottom/height, then mark the result as `predicted`.
4. **Lost path:** if no observation is reacquired within the 2-frame prediction budget, reset and fall back to the existing lost-target behavior.

This preserves the principle:

> Favor real current-frame observations first, then conservative reconstruction, then very short prediction.

## Component Boundary

Add a new module:

- `D:\work\AI\yolo-study-001\vision\occlusion_compensation.py`

This module will own:

- recent stable target samples
- source tagging (`observed`, `reconstructed`, `predicted`)
- partial-occlusion box reconstruction
- strictly bounded 2-frame prediction

It will not own:

- detector inference
- candidate scoring policy
- controller output shaping
- aim-enhancement force tuning

`TargetSelector` remains the orchestrator. The compensation component acts as a helper that can:

- record stable observations
- reconstruct a malformed same-target box
- produce a short prediction when a target suddenly disappears

## Data Model

### `TargetSource`

Represent the target origin using one of:

- `observed`
- `reconstructed`
- `predicted`

### `TrackSample`

Store only the recent stable track samples required for compensation:

- `target_x: float`
- `target_y: float`
- `selected_box: tuple[float, float, float, float]`
- `bottom_y: float`
- `height: float`
- `timestamp: float`
- `source: TargetSource`

Only `observed` and `reconstructed` samples are allowed into this history. `predicted` samples must not be fed back into the motion model.

### `CompensationResult`

Represent a compensated target proposal returned to `TargetSelector`:

- `point: tuple[float, float]`
- `selected_box: tuple[float, float, float, float]`
- `slow_zone: tuple[float, float, float, float] | None`
- `fire_zone: tuple[float, float, float, float] | None`
- `source: TargetSource`

### `SelectedTarget`

Extend `SelectedTarget` in `vision/targeting.py` with:

- `source: str = "observed"`

This will make target provenance available to downstream vision/controller logic without changing current controller behavior in this project.

## Plugin Interface

`TargetOcclusionCompensator` will expose:

- `reset()`
- `record_observation(target: SelectedTarget, timestamp: float)`
- `try_reconstruct(candidate_box, timestamp) -> CompensationResult | None`
- `try_predict(timestamp: float) -> CompensationResult | None`

Behavioral rules:

- keep at most the most recent 3 stable samples
- only allow 2 consecutive predicted frames
- disallow prediction until at least 2 recent stable samples exist
- clear prediction state immediately once a real target is reacquired

## TargetSelector Integration

`TargetSelector.select_target()` keeps ownership of candidate scanning, scoring, switching, and commit behavior. The compensation component slots into the flow as follows:

1. scan raw detector candidates
2. identify candidates that may correspond to the current active target
3. for those same-target candidates, attempt `try_reconstruct(...)` before rejecting them on malformed upper-box geometry
4. let normal observed or reconstructed candidates participate in the existing selection/scoring flow
5. if no valid candidate remains and the stable-track prerequisites are satisfied, attempt `try_predict(...)`
6. if prediction succeeds, return a `predicted` target for this frame
7. if prediction fails or the 2-frame budget is exhausted, fall back to the existing lost-target path

This means:

- `reconstructed` is used when the current frame still contains a plausible same-target box
- `predicted` is used only when the current frame does not contain a valid target result

## Partial Occlusion Reconstruction

### When to attempt reconstruction

Only attempt reconstruction if all are true:

- there is an active target with a valid `selected_box`
- the new candidate is spatially close to that active target
- the candidate still matches on current-frame lower geometry

### Partial-occlusion signature

Treat a candidate as partially occluded when:

- its `center_x` is close to the active target or recent stable sample
- its `bottom_y` is close to the active target or recent stable sample
- its `top_y` drops noticeably downward compared with the recent stable box
- or its `height` shrinks noticeably compared with the recent stable box

In plain terms:

> The lower edge still looks like the same player, but the upper part of the box appears clipped away.

### Reconstruction rule

Build a virtual box using:

- current `left/right`
- current `bottom_y`
- recent stable height estimate

Then compute:

- `top_y = bottom_y - estimated_height`

Use this reconstructed box to compute:

- target point
- slow zone
- fire zone

The reconstructed result should be marked as `reconstructed`.

### Height estimate

Use a conservative stable-height estimate from recent samples:

- prefer the median of the last 2-3 stable heights
- if fewer exist, use the most recent stable height

Do not derive reconstructed height from predicted samples.

## Short-Horizon Prediction

### Preconditions

Only allow prediction when all are true:

- an active target exists
- at least 2 recent stable samples exist
- those samples are `observed` or `reconstructed`
- the current frame yields no valid target after normal and reconstruction attempts
- the prediction budget has not been exhausted

### Prediction budget

The first version will use:

- maximum `2` consecutive predicted frames

This replaces the practical continuity budget that was previously coming from pure missing-frame hold. It must not stack on top of an additional independent 2-frame hold budget.

### Predicted quantities

Predict all of:

- `target_x`
- `target_y`
- `bottom_y`
- `height`

This allows the system to predict both the aim point and a plausible box geometry.

### Motion model

Use a simple constant-velocity model from the last 2 stable samples:

- estimate X/Y velocity from target-point motion
- estimate bottom and height velocity from stable geometry motion
- extrapolate one frame at a time

### Guardrails

Prediction must remain conservative:

- clamp maximum single-frame predicted displacement
- clamp height drift
- never feed `predicted` samples back into the motion model
- expire immediately after 2 predicted frames if no real target is reacquired

## Reacquisition

When the system is in `predicted` state and a real candidate reappears:

- compare current candidates against the predicted point and predicted geometry
- prefer candidates whose point and lower-box geometry are closest to the predicted state
- once a candidate is accepted, exit prediction immediately
- record the reacquired target as `observed` or `reconstructed`

Prediction should therefore act only as a bridge across a short occlusion timing window, not as a replacement for observation.

## Interaction With AutoFire and Controller

This project does not change controller behavior, but the design intentionally exposes target provenance:

- `observed`
- `reconstructed`
- `predicted`

This supports later downstream policy changes such as:

- suppressing autofire on `predicted`
- reducing controller authority on `predicted`
- treating `reconstructed` more confidently than `predicted`

Those policies are explicitly deferred.

## File-Level Design

### `vision/occlusion_compensation.py`

New file.

Responsibilities:

- define source/sample/result types
- manage stable sample history
- reconstruct partially occluded same-target boxes
- perform 2-frame short prediction
- clear its own state on reset or reacquisition

### `vision/targeting.py`

Changes:

- extend `SelectedTarget` with `source`
- instantiate and reset the compensator inside `TargetSelector`
- route same-target malformed candidates through `try_reconstruct(...)`
- route sudden short target loss through `try_predict(...)`
- record stable observations after commit

### `tests/test_vision_targeting.py`

Add tests for:

- partial-occlusion reconstruction
- 2-frame prediction compensation
- prediction budget expiry
- prediction not feeding itself back into history
- reacquisition from predicted state
- source tagging for observed/reconstructed/predicted

## Testing Strategy

The first implementation should be fully test-driven.

Required test scenarios:

1. **Partial reconstruction:** after stable tracking, provide a same-target candidate whose `bottom_y` is stable but whose `top_y` drops and `height` shrinks; verify the returned target is `reconstructed` and its `target_y` is closer to stable history than to the raw clipped box.
2. **Two-frame prediction:** after 2 stable frames, remove detections; verify the next 2 frames can return `predicted`, but the third cannot.
3. **Prediction no feedback:** repeated missing frames must not reuse predicted samples as new ground truth.
4. **Reacquisition:** after a predicted frame, reintroduce a nearby valid candidate and verify the selector returns to `observed` or `reconstructed`.
5. **Source tagging:** verify the `SelectedTarget.source` field is correct for all three cases.
6. **No false activation on unstable targets:** prediction must not activate if there were not enough stable samples first.

## Risks and Mitigations

### Risk: over-predicting wrong targets

Mitigation:

- require stable-track prerequisites
- cap prediction to 2 frames
- do not feed predictions back into history

### Risk: malformed boxes incorrectly treated as occlusion

Mitigation:

- reconstruction requires both lower-edge continuity and upper-box collapse
- keep thresholds conservative and tied to recent stable geometry

### Risk: behavior drift in current targeting

Mitigation:

- normal observed path remains primary
- compensation activates only on short occlusion signatures
- tests must cover both existing normal behavior and the new compensation paths

## Rollout

Phase 1:

- add `vision/occlusion_compensation.py`
- extend `SelectedTarget` with `source`
- wire reconstruction and 2-frame prediction into `TargetSelector`
- add unit tests

Phase 2:

- validate in real gameplay against scope-edge occlusion timing
- observe whether `predicted` needs later downstream policy in controller/autofire

Deferred:

- controller-specific response to `predicted`
- longer-horizon or probabilistic trackers
- model-side tuning for scope-specific occlusion robustness
