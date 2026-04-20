# Vision Overview

Last updated: 2026-04-20

## Goal

The current vision stack is optimized for:

- center-crop person detection with low idle cost
- box-based target selection for controller handoff
- friendly rejection from color cues above the box
- short-horizon occlusion recovery and motion-aware target deltas
- keeping the controller boundary small and stable

The current production path is detector-first. The main targeting path does not depend on pose keypoints.

## Entry Points

- `main.py`
- `vision/__init__.py`
- `vision/runner.py`
- `tools/export_trt.py`

`main.py` creates the controller, then calls `process_vision(controller=...)`.

## Runtime States

The runner has three practical states:

1. `Idle`
   - `ScreenCaptureThread` stays alive but runs at `idle_capture_fps`
   - `InferenceThread` is paused
   - targeting, enhancement, hit detection, and perf windows are reset
2. `ADS active`
   - capture jumps to `capture_fps`
   - inference resumes
   - fresh results flow through targeting and controller update
3. `Inference gap`
   - no fresh inference result arrived before `frame_timeout`
   - the runner does not invent new detections
   - controller fire is disabled for that iteration
   - debug mode can still show a waiting state or the last materialized frame

## Current Pipeline

Runtime flow:

1. `ScreenCaptureThread` captures a centered RGB crop through `vision/dxgi_capture.py`.
2. `DXGIRegionCaptureBackend` returns either:
   - a materialized RGB frame, or
   - a lazy `CapturedFrame` with native handle plus on-demand loaders
3. `InferenceThread` runs latest-only inference.
4. `vision/fastpath.py` uses:
   - TensorRT `best.engine` when available
   - `best.pt` fallback if engine load fails
5. Fast-path output is decoded into `ParsedDetections`.
6. `TargetSelector` filters, classifies, scores, and confirms candidates.
7. `TargetOcclusionCompensator` may reconstruct or predict a short-lived target.
8. `AimEnhancementPipeline` transforms the selected target into a controller delta.
9. `CrosshairPersonHitDetector` decides whether the selected target is inside the `fire_zone`.
10. `AdsAutoFireGate` blocks auto-fire during the first `120ms` after aiming begins.
11. The controller receives:
   - `controller.update(dx, dy, target=ControllerTarget | None)`
   - `controller.set_auto_fire(on_or_off)`
   - `controller.reset()` when no correction should be applied

The runner only sends compact target metadata to the controller. It does not push raw frames into the controller layer.

## Current Defaults

Current `VisionConfig` defaults in `vision/runner.py`:

- `capture_width = 640`
- `capture_height = 512`
- `capture_fps = 80`
- `idle_capture_fps = 10`
- `fast_preprocessor = "cpu"`
- `model_path = models/best.engine`
- `fallback_model_path = models/best.pt`
- `model_task = "detect"`
- `classes = (0,)`
- `conf = 0.40`
- `half = True`
- `device = 0`
- `frame_timeout = 0.10`
- `idle_sleep = 0.01`
- `perf_log_interval = 2.0`

Useful CLI and env controls:

- `--crop-size`
- `--crop-width`
- `--crop-height`
- `--capture-fps`
- `--target-fps`
- `--vision-debug`
- `--vision-debug-save`
- `--perf-log`
- `VISION_CROP_WIDTH`
- `VISION_CROP_HEIGHT`
- `VISION_CAPTURE_FPS`
- `VISION_IDLE_CAPTURE_FPS`
- `VISION_FAST_PATH`
- `VISION_FAST_PREPROCESSOR`
- `VISION_DEBUG_OVERLAY`
- `VISION_DEBUG_SAVE`
- `VISION_PERF_LOG`

`gamepad_start.bat` currently sets `VISION_PERF_LOG=1`, enables the fast path by default, sets capture to `80 FPS`, idle capture to `10 FPS`, and prompts for:

- auto-fire output: `RB` or `RT`
- vision preprocessor: `cpu` or `native`

## Fast Path And Native Status

The current fast path is real, but the current fully native preprocessor path is not.

What is already implemented:

- direct fast-path backend use through `_fast_predict(...)`
- ROI-based DXGI capture
- lazy full-frame materialization
- ROI-only CPU extraction for color classification when a full frame is not needed

What is only prewired right now:

- `vision/native_fastpath.py`
- `VISION_FAST_PREPROCESSOR=native`
- `CapturedFrame.native_frame`

Current behavior:

- if `VISION_FAST_PREPROCESSOR=native` is selected and `vision_native` is missing, startup logs a fallback and uses the CPU preprocessor
- the CPU preprocessor still performs:
  - `torch.from_numpy(...)`
  - CPU-to-GPU upload
  - HWC-to-CHW reorder
  - dtype conversion
  - `/255.0`

So the repository already contains native integration hooks, but not a finished always-on GPU-resident preprocess module.

## Target Selection

The current targeting path is box-based.

Important behavior in `TargetSelector`:

- main target point is upper-chest oriented
- `slow_zone` and `fire_zone` are derived from the selected body box
- green above-box color is treated as friendly and rejected
- yellow and red above-box color add enemy confidence bonus
- first pickup confirmation requires `2` frames
- target switch confirmation requires `2` frames
- missing-target hold requires `2` frames before the active target is dropped
- current tracking can survive at lower confidence than first pickup
- target switching is intentionally sticky to reduce left-right flapping

Current targeting does not require pose keypoints. `ParsedDetections.keypoints` remains optional and is not the main production signal.

## Occlusion Compensation

`vision/occlusion_compensation.py` currently supports two short-horizon recovery paths:

- `reconstructed`
  - used when a still-visible box looks clipped or scope-occluded
  - rebuilds a taller box from recent stable samples when center X and bottom Y remain plausible
- `predicted`
  - used when detections disappear briefly after stable recent motion
  - linearly extrapolates from the recent sample history
  - limited to a short burst of predicted frames

Current `TargetSource` values are:

- `observed`
- `reconstructed`
- `predicted`

## Aim Enhancement

The current enhancement layer is separate from target selection.

`AimEnhancementPipeline` currently applies:

- `LeadPredictor`
  - adds small motion lead after consistent observed motion
- `CatchupBoost`
  - increases output if error keeps growing frame over frame
- `NearTargetDamping`
  - reduces correction as the reticle converges near the target

Predicted targets are treated differently:

- they do not advance the normal observed-motion history
- they only go through the near-target damping path

This keeps short prediction compensation from polluting longer-lived motion estimates.

## AutoFire

Auto-fire is no longer driven by generic centered boxes.

Current behavior:

- fire is based on the selected target's `fire_zone`
- `CrosshairPersonHitDetector` keeps a short hold window with `release_grace_frames = 4`
- `AdsAutoFireGate` blocks the first `120ms` of auto-fire after aiming starts
- the final output button is chosen by the controller layer, not by vision

## Perf Logs

`vision/perf.py` emits two windowed log lines:

- `[Perf][ADS]`
  - average across all aimed frames in the current log window
- `[Perf][TRACK]`
  - average across only the subset of frames where a selected target existed

Field meanings:

- `loop`
  - average iterations per second for that window
- `wait`
  - time spent waiting for a fresh inference result
- `infer`
  - time spent inside the inference path for that frame
- `post`
  - time spent on target selection, enhancement, fire gating, and controller-side handoff prep
- `age`
  - freshness from `captured_at` to result consumption in the main loop
- `boxes`
  - average detection boxes seen per frame in that window

`[Perf][TRACK]` is not a separate tracker backend. It is a subset view over the same ADS loop, restricted to frames where tracking was active.

## Debug Features

Debug is optional and not part of the hot path design target.

Current capabilities:

- live overlay window through `VisionDebugOverlay`
- annotated frame saving through `DebugFrameCapture`
- async frame save path for detection-bearing frames

These are useful for tuning and investigation, but they are not required for normal runtime.

## Core Files

Main runtime:

- `vision/runner.py`
- `vision/capture.py`
- `vision/dxgi_capture.py`
- `vision/fastpath.py`
- `vision/inference.py`
- `vision/targeting.py`
- `vision/occlusion_compensation.py`
- `vision/enhancement.py`
- `vision/perf.py`

Optional debug utilities:

- `vision/debug_overlay.py`
- `vision/debug_capture.py`

## Current Boundaries

The important architectural boundary today is:

- vision owns capture, inference, target selection, short-horizon recovery, delta shaping, and fire recommendation
- controller owns input reading, output mapping, and final actuation

That boundary is intentionally small so vision can evolve without repeatedly redesigning controller host code.
