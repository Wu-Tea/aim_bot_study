# Vision Overview

Last updated: 2026-04-14

## Current Goal

The current vision path is optimized for:

- detecting human targets near screen center
- rejecting friendlies from name-color above the head
- outputting a stable upper-chest target point
- keeping controller integration unchanged
- holding end-to-end latency near real-time playability

This version is detector-first. It no longer depends on pose keypoints for the main targeting path.

## Current Pipeline

Entry:

- `main.py`
- `vision/runner.py`

Runtime flow:

1. `ScreenCaptureThread` captures a center crop in RGB.
2. `vision/fastpath.py` runs the detect model (`engine` first, `pt` fallback).
3. `TargetSelector` filters and scores person boxes.
4. `AimEnhancementPipeline` converts `SelectedTarget` into `best_target_delta`.
5. `CrosshairPersonHitDetector` decides `AutoFire`.
6. Controller receives:
   - analog aim delta
   - auto-fire on/off

## Current Defaults

From `VisionConfig` in `vision/runner.py`:

- `capture_width = 896`
- `capture_height = 512`
- `capture_fps = 70`
- `model_path = models/yolo26n.engine`
- `fallback_model_path = models/yolo26n.pt`
- `model_task = detect`
- `conf = 0.40`

CLI / env control:

- `--crop-width` / `VISION_CROP_WIDTH`
- `--crop-height` / `VISION_CROP_HEIGHT`
- `--capture-fps` / `VISION_CAPTURE_FPS`
- `--vision-debug` / `VISION_DEBUG_OVERLAY`
- `--vision-debug-save` / `VISION_DEBUG_SAVE`
- compatibility aliases still work:
  - `--crop-size`
  - `--target-fps`
  - `VISION_CROP_SIZE`
  - `VISION_TARGET_FPS`

## Target Selection

Targeting now uses box geometry only:

- aim point:
  - box center on X
  - `30%` down from box top on Y
- slow zone:
  - torso-like inner box
- fire zone:
  - tighter inner box used by `AutoFire`

Selection behavior:

- first pickup uses stricter confidence and geometry gates
- enemy-colored pickups are allowed to lock at a slightly lower confidence than neutral pickups
- first pickup must survive `2` consecutive frames before it becomes the active target
- tracked targets can survive at lower confidence if they stay near the previous center
- a confirmed target can survive `2` missing / rejected frames before it is released
- small target-point jumps are smoothed
- target switching is intentionally sticky:
  - distance-based tracking bonus
  - `TRACKING_SWITCH_MARGIN` prevents left-right target flapping on tiny score changes

## AutoFire Behavior

`AutoFire` no longer scans raw detections blindly.

Primary path:

- fire only when the crosshair is inside the `fire_zone` of the current `SelectedTarget`
- raw centered detection boxes no longer trigger `AutoFire` on their own
- short dropouts are handled by target stickiness plus detector grace frames, not by a separate centered-box fallback

ADS gate:

- after aiming starts, `AutoFire` is blocked for `120ms`
- this avoids firing before the in-game reticle fully settles

## Debug Overlay

When debug mode is enabled:

- a live rectangular crop window is shown
- detection boxes, confidence, friendly/enemy/neutral labels, lock point, `slow_zone`, `fire_zone`, crosshair, and current state text are rendered on top
- non-selected detections are labeled as `raw ...` so they are easier to distinguish from the actual locked target

When debug frame saving is enabled:

- the annotated debug frame is saved whenever the current frame contains one or more detections
- files are written asynchronously to avoid blocking the vision loop
- output path is `debug_captures/YYYY-MM-DD/`
- filenames include time plus quick state markers such as `boxes`, `lock`, and `fire`

## Files Changed In This Version

Core runtime:

- `vision/runner.py`
- `vision/debug_overlay.py`
- `vision/debug_capture.py`
- `vision/capture.py`
- `vision/fastpath.py`
- `vision/targeting.py`
- `vision/enhancement.py`
- `main.py`
- `tools/export_trt.py`

Tests:

- `tests/test_vision_debug_capture.py`
- `tests/test_vision_debug_overlay.py`
- `tests/test_vision_targeting.py`
- `tests/test_vision_enhancement.py`
- `tests/test_vision_runner.py`
- `tests/test_vision_runner_config.py`
- `tests/test_vision_runner_autofire_gate.py`

Planning notes:

- `docs/superpowers/specs/2026-04-14-vision-detector-first-design.md`
- `docs/superpowers/plans/2026-04-14-vision-detector-first.md`

## Tunable Parameters

Most useful knobs for next tuning session:

Target locking:

- `TRACKING_BONUS`
- `TRACKING_RADIUS`
- `TRACKING_SWITCH_MARGIN`
- `MAX_SMOOTHING_JUMP_PIXELS`
- `MIN_SMOOTHING_ALPHA`
- `PICKUP_CONFIRM_FRAMES`
- `TARGET_HOLD_FRAMES`

Target filtering:

- `PICKUP_CONFIDENCE_THRESHOLD`
- `PICKUP_ENEMY_CONFIDENCE_THRESHOLD`
- `TRACKING_CONFIDENCE_THRESHOLD`
- `MIN_PICKUP_HEIGHT_RATIO`
- `MIN_TRACKING_HEIGHT_RATIO`
- `MIN_PICKUP_AREA_RATIO`
- `MIN_TRACKING_AREA_RATIO`
- `MIN_ASPECT_RATIO`
- `MAX_ASPECT_RATIO`
- friendly / enemy ROI mask ratio limits in `TargetSelector._classify_color`

AutoFire:

- `FIRE_SHRINK_X`
- `FIRE_SHRINK_TOP`
- `FIRE_SHRINK_BOTTOM`
- `release_grace_frames`
- `AdsAutoFireGate(delay_seconds=0.12)`

Debug:

- `VISION_DEBUG_OVERLAY`
- `VISION_DEBUG_SAVE`
- `debug_captures/YYYY-MM-DD/`

Aim enhancement:

- `LeadPredictorConfig.min_motion_px`
- `LeadPredictorConfig.consistent_frames`
- `LeadPredictorConfig.lead_seconds`
- `CatchupBoostConfig.*`
- `NearTargetDampingConfig.*`

Gamepad vertical mapping:

- `max_ai_force_y`
- `piecewise_mid_pixels_y`
- `piecewise_max_pixels_y`
- `piecewise_mid_ratio_y`

## Verified In This Version

Commands run during this round:

- `python -m unittest tests.test_vision_targeting -v`
- `python -m unittest tests.test_vision_debug_capture tests.test_vision_debug_overlay -v`
- `python -m unittest tests.test_vision_targeting.CrosshairPersonHitDetectorTests tests.test_vision_runner_autofire_gate -v`
- `python -m unittest tests.test_vision_targeting tests.test_vision_enhancement tests.test_vision_runner tests.test_vision_runner_config tests.test_vision_runner_autofire_gate -v`
- `python -m py_compile main.py vision\\runner.py vision\\capture.py vision\\fastpath.py vision\\targeting.py vision\\enhancement.py tools\\export_trt.py tests\\test_vision_targeting.py tests\\test_vision_enhancement.py tests\\test_vision_runner.py tests\\test_vision_runner_config.py tests\\test_vision_runner_autofire_gate.py`

## Known Next Steps

Likely next tuning work:

1. continue tuning sticky target selection in real gameplay
2. tune `fire_zone` and autofire delay against real ADS behavior
3. reduce remaining false positives from non-person objects
4. playtest rectangular crop sizes for different games
5. decide whether to keep a generic detect model or train / swap to a more game-specific person detector
