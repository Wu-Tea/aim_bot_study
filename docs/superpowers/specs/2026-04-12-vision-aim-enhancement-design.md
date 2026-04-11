# Vision Aim Enhancement Design

**Goal:** Keep YOLO responsible for base target observation while adding a small, testable enhancement pipeline that improves lateral and high-speed target tracking without reading left-stick input.

**Non-goals:**
- No model retraining work
- No left-stick compensation
- No controller-specific prediction logic inside `controllers/`

## Architecture

The vision stack will be split into three stages:

1. `vision.targeting.TargetSelector` selects the best target and returns a structured target observation instead of only a raw `(dx, dy)`.
2. `vision.enhancement.AimEnhancementPipeline` converts that observation into an enhanced aim delta using bounded stateful plugins.
3. `vision.runner.process_vision()` sends the final enhanced delta to the controller and resets pipeline state whenever aiming stops or target tracking is lost.

This keeps the detector, selector, and motion logic independent. YOLO remains the source of truth for where the target is right now, while prediction and catch-up logic operate only on recent observation history.

## Components

### Target Observation

`TargetSelector` will expose a structured result containing:

- absolute target point
- screen center
- base `dx/dy`
- selection score

`find_best_target()` can remain as a compatibility wrapper returning only `(dx, dy)`.

### Aim Enhancement Pipeline

`AimEnhancementPipeline` owns short-lived tracking memory:

- previous target observation
- filtered target velocity
- consecutive error-growth counters
- per-axis temporary boost state

It builds a mutable `AimEnhancementState` for each frame, then applies plugins in sequence.

### Plugin 1: Lead Predictor

Purpose: compensate for detection latency by adding a bounded feedforward term derived from filtered target velocity.

Rules:
- Uses only target history from recent vision frames
- Applies independent X/Y lead
- Clamps lead contribution so sudden glitches cannot create large flicks

### Plugin 2: Catch-Up Boost

Purpose: when the target error keeps growing in the same direction, temporarily raise correction strength so the final output catches up faster.

Rules:
- Tracks same-sign error growth separately for X and Y
- Builds temporary bounded gain only after several consecutive frames
- Decays quickly when the error converges, flips sign, or target is lost

### Plugin 3: Near-Target Damping

Purpose: reduce overshoot near center without fully killing the final correction.

Rules:
- Applies a soft scale based on radial distance
- Keeps a non-zero minimum scale so close-range tracking does not stall
- Runs after prediction and catch-up so it is the final safety layer

## Data Flow

`detections -> selected target -> enhancement pipeline -> final dx/dy -> controller.update()`

Reset conditions:

- user stops aiming
- capture timeout / no frame
- no valid selected target
- selector rejects a large jump and drops tracking

## Error Handling

- First observation after reset uses zero velocity and zero boost
- Very small or zero `dt` produces no velocity update
- Any invalid observation resets only enhancement state, not the capture thread or model state

## Testing

Add pure unit tests for the enhancement layer:

- lead predictor adds bounded forward correction on sustained movement
- catch-up boost raises output after consecutive non-converging frames
- near-target damping reduces output but preserves a non-zero floor
- reset clears velocity and boost state

Also keep import/syntax verification for the updated `vision` package.
