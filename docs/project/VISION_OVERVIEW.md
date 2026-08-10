# Vision Overview

Last updated: 2026-06-11

## Goal

The current vision stack is optimized for:

- center-crop person detection with fast ADS pickup
- box-based target selection for controller handoff
- friendly rejection from color cues above the box
- short-horizon occlusion recovery and motion-aware target deltas
- keeping the vision-to-controller boundary small in both the default native C++ runtime and Python fallback paths

The main targeting path is detector-first. It does not depend on pose keypoints.

## Entry Points

- `main.py`
- `vision/__init__.py`
- `vision/runner.py`
- `vision/native_runner.py`
- `native/vision_native/`
- `tools/export_trt.py`

`main.py` creates the controller, then dispatches to:

- `process_vision(controller=...)` for `--vision-backend python`
- `process_native_vision(controller=...)` for `--vision-backend native`

That `main.py` path is now the Python fallback/debug path for gamepad. The
normal gamepad runtime starts `cod_native_runtime.exe`, which calls
`VisionEngine` directly in process.

## Current Backends

There are two Python-visible vision backends today, plus the default native C++
runtime path.

### Python backend

Implemented in:

- `vision/runner.py`
- `vision/capture.py`
- `vision/inference.py`
- `vision/fastpath.py`

Behavior:

- Python owns capture, inference orchestration, target selection, occlusion handling, enhancement, and debug overlay work
- TensorRT `best.engine` is used through the Python fast path when available
- `best.pt` remains the fallback if engine loading fails

### Native backend

Implemented in:

- `vision/native_runner.py`
- `native/vision_native/include/vision_native/*`
- `native/vision_native/src/*`

Behavior:

- native C++ owns capture, preprocessing, TensorRT inference, selection, occlusion compensation, enhancement, and auto-fire recommendation
- in the default gamepad runtime, native results go directly to `NativeGamepadController`
- in Python fallback/debug paths, native results are bridged back into Python as a compact `VisionResult`-shaped payload

## Runtime States

The default C++ runtime and Python fallback backends follow the same practical vision states:

1. `Idle`
   - the controller is not actively aiming
   - targeting state is reset
   - auto-fire is off
2. `ADS active`
   - the vision loop is live
   - fresh results flow into controller updates
3. `Inference gap`
   - the runner keeps state bounded instead of inventing detections
   - auto-fire is disabled for that iteration
   - debug output can still show waiting / bridge state

## Current Pipeline

### Python backend pipeline

1. `ScreenCaptureThread` captures a centered RGB crop through `vision/dxgi_capture.py`.
2. `DXGIRegionCaptureBackend` copies only the requested ROI into a small staging surface.
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
11. The controller receives compact target metadata only.

### Default native C++ gamepad pipeline

1. `cod_native_runtime.exe` creates `VisionEngine`.
2. `VisionEngine` performs centered ROI capture natively.
3. Native preprocessing maps the ROI into TensorRT input tensors.
4. TensorRT inference runs in C++ against `models/best.engine`.
5. Native target selection, authority fields and the auto-fire recommendation run in C++; the result contains current source-owned geometry rather than a second post-selector lead/damping pass.
6. `NativeGamepadController` consumes `VisionResult` directly.

### Python-hosted native backend pipeline

1. `vision/native_runner.py` loads `vision_native_cpp` from `native/vision_native/build/Release`.
2. `NativeVisionEngine` performs centered ROI capture natively.
3. Native preprocessing maps the ROI into TensorRT input tensors.
4. TensorRT inference runs in the native module against `models/best.engine`.
5. Native target selection and the auto-fire recommendation publish observed, weak-observed, or explicit same-generation cue evidence; they do not publish blind predicted/projected targets.
6. Python receives compact result fields such as:
   - `dx`
   - `dy`
   - `target_source`
   - `wait_ms`
   - `preprocess_ms`
   - `infer_ms`
   - `post_ms`
   - `age_ms`
7. The Python fallback controller consumes that compact payload through the same controller boundary.

The important rule is unchanged: vision sends compact intent, not raw frames, into the controller layer.

## Current Defaults

Launcher-level defaults can live in the local project-root `config.toml` under
`[runtime.vision]`. `main.py` applies them before starting either vision
backend. Existing `VISION_*` environment variables and explicit CLI arguments
still take precedence. If `config.toml` is absent, the runtime uses code
defaults.

Default gamepad runtime baseline:

- `GAMEPAD_RUNTIME = "native"`
- executable: `native\vision_native\build\Release\cod_native_runtime.exe`
- `capture_fps = 140`
- `crop_width = 640`
- `crop_height = 512`
- `native_cue_sidecar = false`
- `perf_log = true`
- `quit_key = "0"`

Current `VisionConfig` defaults in `vision/runner.py`:

- `capture_width = 640`
- `capture_height = 512`
- `capture_fps = 80`
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

Current startup-script behavior:

- `scripts\launch\gamepad_start.bat`
  - defaults to full native C++ gamepad runtime
  - set `GAMEPAD_RUNTIME=python` for the older Python fallback
- `scripts\launch\gamepad_native_cpp_start.bat`
  - starts `cod_native_runtime.exe`
  - uses `config.toml` runtime defaults
  - prompts for auto-fire output and recoil profile selection
- `scripts\launch\debug\gamepad_debug.bat`
  - native by default, Python selectable
  - `VISION_CAPTURE_FPS=140`
- `scripts\launch\debug\gamepad_native_debug.bat`
  - native only
  - `VISION_CAPTURE_FPS=140`
- `scripts\launch\mouse_start.bat`
  - `VISION_BACKEND=native`
  - `VISION_CAPTURE_FPS=140`
- `scripts\launch\debug\mouse_native_debug.bat`
  - native only
  - `VISION_CAPTURE_FPS=140`

Useful CLI and env controls:

- `--vision-backend`
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
- `VISION_DEBUG_OVERLAY`
- `VISION_DEBUG_SAVE`
- `VISION_PERF_LOG`
- `VISION_QUIT_KEY`

## Python Fast Path Status

The Python fast path is still important:

- it is the fallback runtime when native is unavailable
- it remains useful for comparison and bisecting regressions
- it still uses TensorRT `best.engine` when possible

What is already implemented:

- direct fast-path backend use through `_fast_predict(...)`
- ROI-based DXGI capture
- latest-only inference threading

Current limitation:

- Python preprocessing still includes CPU-side tensor shaping and upload work that the native backend avoids

## Native Vision Status

Native vision is no longer just a scaffold:

- it is integrated into the default full native C++ gamepad runtime
- it is also integrated into `main.py` through `--vision-backend native` for fallback/debug paths
- current mouse startup scripts also default to native
- Python remains available as a fallback

Build and smoke helpers:

- `tools/build_native_vision.ps1`
- `tools/run_native_vision_smoke.ps1`
- `tools/run_native_vision_infer_smoke.ps1`
- `tools/run_native_vision_capture_smoke.ps1`
- `tools/run_native_vision_debug.ps1`

## Capture Recovery

The Python backend uses `DXGIRegionCaptureBackend` to survive transient Desktop Duplication failures by retrying after short backoff windows.

The native backend keeps its own capture loop inside `NativeVisionEngine`, but the same practical concern remains: temporary desktop-capture failures should not force a full app restart when the OS later allows capture again.

## Target Selection

The current targeting path is box-based.

Important behavior shared by the production design:

- main target point is posture-aware from the selected body box:
  standing and visible upper-body boxes use an upper-chest point, crouched boxes use a slightly lower middle-upper-body point, and wide-low prone/side boxes use the body midpoint
- `slow_zone` and `fire_zone` come from the selected body box
- green above-box color is treated as friendly and rejected
- yellow and red above-box color add enemy confidence bonus
- first pickup confirmation requires `2` frames
- target switch confirmation requires `2` frames
- target switching is intentionally sticky to reduce left-right flapping

Current native `TargetSource` values are:

- `observed`
- `associated_weak`
- `weak_observed`
- `cue_hold`
- empty/no target

The Python fallback retains its own historical `reconstructed` and `predicted`
source values. They are not accepted as native production authority.

## Occlusion Continuity

The Python fallback still has reconstructed/predicted short-horizon recovery.
The native production path does not mirror it. Native continuity requires
current cue pixels tied to the same selector generation, remains aim-only and
expires without that evidence. A fresh empty result otherwise clears target
authority.

## Aim Enhancement

The Python fallback `AimEnhancementPipeline` applies:

- `LeadPredictor`
- `CatchupBoost`
- `NearTargetDamping`

The native post-selector equivalent was retired on 2026-08-10. Native Vision
now publishes current target geometry; ADS/BodyLock, the response model and the
single `AimDynamicsShaper` own control shaping downstream. This avoids two
stateful layers independently modifying the same target error.

## AutoFire

Current behavior:

- fire is based on the selected target's `fire_zone`
- `CrosshairPersonHitDetector` keeps a short hold window
- `AdsAutoFireGate` blocks the first `120ms` of auto-fire after aiming starts
- the final fire button or click output is still chosen by the controller layer, not by vision

## Perf Logs

`vision/perf.py` emits two windowed log lines:

- `[Perf][ADS]`
- `[Perf][TRACK]`

Important fields:

- `wait`
- `infer`
- `post`
- `age`
- `boxes`

The native path also surfaces `preprocess_ms`, which is especially useful when debugging native timing breakdowns.

## Debug Features

Debug is optional and not part of the hot-path design target.

Current capabilities:

- live overlay window through `VisionDebugOverlay`
- annotated frame saving through `DebugFrameCapture`
- native synthetic debug canvas through `NativeVisionDebugOverlay`

These are useful for tuning and investigation, but they are not required for normal runtime.

## Core Files

Main Python runtime:

- `vision/runner.py`
- `vision/capture.py`
- `vision/dxgi_capture.py`
- `vision/fastpath.py`
- `vision/inference.py`
- `vision/targeting.py`
- `vision/occlusion_compensation.py`
- `vision/enhancement.py`
- `vision/perf.py`

Native bridge:

- `vision/native_runner.py`

Optional debug utilities:

- `vision/debug_overlay.py`
- `vision/debug_capture.py`

Native implementation:

- `native/vision_native/include/vision_native/*`
- `native/vision_native/src/*`

## Current Boundaries

The important architectural boundary today is:

- vision owns capture, inference, target selection, short-horizon recovery, delta shaping, and fire recommendation
- controller owns input reading, output mapping, AI/manual mixing, and final actuation

That boundary stays intentionally small so the project can keep evolving native and Python vision without constantly redesigning controller host code.
