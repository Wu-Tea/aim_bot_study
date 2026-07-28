# Predictive Dynamic Viewport M0

## Purpose

This feature keeps a committed target inside the next inference viewport during
rapid close-range growth. It changes the visual field of view without changing
the coordinate system consumed by the selector, tracker, or controller.

M0 is deliberately bounded:

- one fixed 600x520 DXGI/CUDA backing texture;
- one fixed 480x416 TensorRT engine;
- centered inference viewports of 360x312, 480x416, and 600x520;
- viewport decisions owned by the already committed target only;
- feature disabled by default.

The fixed backing texture avoids resource recreation during a viewport switch.
After inference, ROI-local detections are translated into the 600x520 backing
coordinate system. All downstream X/Y errors therefore remain continuous
across zoom changes.

## Controller

`ViewportController` observes committed target body geometry and estimates:

- body center velocity;
- body width and height growth velocity;
- the box position and size 100 ms into the future.

It chooses the smallest viewport containing the predicted body box plus a
safety margin. Expansion requires two distinct fresh observations unless the
box is already outside the safe boundary. Loss immediately after edge pressure
opens the rescue viewport. Shrink is delayed, uses an extra margin, and moves
only one level at a time.

No uncommitted detection can change the viewport. A target identity change
clears velocity history.

## Configuration

The opt-in candidate is:

```toml
[runtime.vision]
capture_width = 600
capture_height = 520
tensor_width = 480
tensor_height = 416
require_isotropic_resize = true

dynamic_viewport_enabled = true
viewport_precision_width = 360
viewport_precision_height = 312
viewport_normal_width = 480
viewport_normal_height = 416
viewport_rescue_width = 600
viewport_rescue_height = 520
viewport_prediction_ms = 100
```

Every viewport must have the TensorRT input aspect ratio and must fit inside
the backing capture. Invalid combinations fail during config loading.

## Performance evidence

The BGRA benchmark now accepts a larger CUDA array plus a centered ROI. With
the current 480x416 engine, 120 measured iterations produced:

| Viewport | GPU total p50 | GPU total p95 |
|---|---:|---:|
| 360x312 | 2.570 ms | 3.369 ms |
| 480x416 | 2.725 ms | 4.824 ms |
| 600x520 | 2.482 ms | 2.775 ms |

These values are not a claim that a larger viewport is faster. Tensor inference
shape is fixed, and the small differences are run-to-run scheduling and clock
variance. The expected M0 performance effect is approximately neutral inference
cost plus the cost of the fixed 600x520 capture texture.

## Acceptance sequence

1. Keep the feature disabled and run all native tests.
2. Run recorded close-range clips through all three fixed viewport sizes to
   establish visibility and identity baselines.
3. Enable dynamic viewport in a replay and verify:
   - committed-target recall is no worse than fixed rescue;
   - identity switches do not increase;
   - no coordinate jump occurs on viewport sequence changes;
   - rescue opens before the first complete close-range miss;
   - shrink does not oscillate more than once per dwell interval.
4. Run a live visual/telemetry trial with controller output disabled before
   using it in normal play.
5. Promote the feature only after live close-range miss duration improves.

Physical capture resizing is a later optimization. It should not be combined
with the first behavior trial because it adds DXGI/CUDA lifecycle and latency
variables to the zoom decision.
