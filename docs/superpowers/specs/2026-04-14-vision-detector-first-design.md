# Vision Detector-First Design

**Goal:** Replace the current pose-based vision path with a detector-first pipeline that improves half-body and truncated-person recall while keeping screenshot-to-`best_target_delta` latency near the current real-time budget.

**Scope:**
- Replace the default `pose` model path with a `detect` model path.
- Keep controller boundaries unchanged.
- Keep the existing name-color friendly filtering logic.
- Keep the existing target tracking and aim enhancement pipeline.
- Remove the requirement for keypoints when generating `aim_point` and `slow_zone`.

**Non-goals:**
- No head detector in this phase.
- No segmentation or whole-body parsing.
- No controller/gamepad changes.
- No model training in this phase.

## Problem

The current vision path assumes a pose model is the primary source of both person detection and target geometry. In practice, the current pose route performs poorly on half-body and truncated targets, which are common in FPS gameplay. The project only needs:

- reliable person detection near screen center
- friendly filtering using the name-color region above the head
- a stable upper-chest aim point
- a torso-like slow zone for near-target damping

Those needs do not require keypoints.

## Approaches Considered

### 1. Keep pose and tune thresholds

Lower confidence, enlarge crop, and continue using keypoint-driven aim points.

Pros:
- smallest code change

Cons:
- does not address the main weakness: truncated-person recall
- keeps the project dependent on keypoints it no longer really needs

### 2. Detector-first with box geometry

Use a detection model that outputs person boxes only. Compute upper-chest aim point and slow zone directly from the detected box. Keep the existing friendly filter and target selection logic.

Pros:
- smallest architecture change that attacks the real failure mode
- simpler output schema
- easier to hit latency targets than pose
- no controller changes

Cons:
- aim point is geometric, not anatomical

### 3. Detector plus head detector

Use person detection first, then optionally run a second model for head localization.

Pros:
- potentially higher precision for peeking targets

Cons:
- more complexity and higher latency
- unnecessary for the current “upper chest first” requirement

## Chosen Design

Choose **Approach 2**.

The new vision path stays detector-first and box-based:

1. `vision/runner.py` defaults to a detection model instead of a pose model.
2. `vision/fastpath.py` treats model output as detection-only output.
3. `vision/targeting.py` generates:
   - `aim_point` from box geometry
   - `slow_zone` from box geometry
4. The friendly filter remains unchanged: it still samples the name-color ROI above the detected person box.
5. `vision/enhancement.py` and controller integration remain unchanged because they already consume only `SelectedTarget`.

## Data Flow

1. Capture cropped RGB frame.
2. Run person detector with `classes=(0,)`.
3. Decode boxes and confidences.
4. For each box:
   - reject invalid geometry
   - run existing name-color friendly filter above the box
   - compute upper-chest target point from box geometry
   - compute torso slow zone from box geometry
5. Score and select a target with the existing center/tracking logic.
6. Feed the selected target into the existing aim enhancement pipeline.
7. Output `best_target_delta` to the controller.

## Geometry Rules

### Aim point

Use the box center on X and an upper-chest Y on the vertical axis.

Recommended initial rule:

```python
target_x = x1 + (box_w * 0.5)
target_y = y1 + (box_h * 0.30)
```

This is intentionally simple. It prioritizes stable upper-torso aim over precise body-part localization.

### Slow zone

Use a torso-biased interior box derived from the detected person box.

Recommended initial rule:

```python
slow_left = x1 + (box_w * 0.22)
slow_top = y1 + (box_h * 0.18)
slow_right = x2 - (box_w * 0.22)
slow_bottom = y2 - (box_h * 0.20)
```

These values match the existing fallback torso zone closely enough to preserve near-target damping behavior.

## Files

### `vision/runner.py`

- Change default model paths from `yolo26n-pose.*` to `yolo26n.pt`-style detect defaults.
- Change `model_task` default from `"pose"` to `"detect"`.
- Keep controller, performance, and target-selection flow unchanged.

### `vision/fastpath.py`

- Keep the loader and warmup structure.
- Remove the pose-specific assumption that decoded outputs may contain keypoints.
- Return `ParsedDetections(boxes, confs, keypoints=None)` for detection outputs.

### `vision/targeting.py`

- Keep `ParsedDetections` shape compatible.
- Remove keypoint-based target point selection.
- Remove keypoint-based slow-zone construction.
- Keep color filtering, scoring, tracking bonus, and jump gating.

## Error Handling

- If the detect model fails to load, keep the current fallback pattern.
- If no detections are produced, behavior remains unchanged: reset tracking and controller output.
- If box geometry is degenerate, ignore that candidate.

## Testing

Add or update tests to prove:

1. Target selection works with detection-only boxes and no keypoints.
2. Upper-chest aim point is derived from box geometry.
3. Slow zone is derived from box geometry.
4. Friendly filtering above the box still works.
5. Vision enhancement still accepts the new `SelectedTarget` shape without change.

## Rollout

Phase 1:
- Switch default model/task to detector-first.
- Keep all downstream logic intact except keypoint-derived geometry.

Phase 2:
- Playtest half-body and truncated targets.
- Tune:
  - detection confidence
  - crop size
  - geometric chest Y ratio
  - torso slow-zone ratios

Future extension:
- Add an optional head detector only if detector-first geometry is still insufficient.
