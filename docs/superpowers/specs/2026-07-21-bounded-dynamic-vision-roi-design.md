# Bounded Dynamic Vision ROI Design

Date: 2026-07-21
Status: accepted
Scope: native 480x416 DXGI capture, vision selector continuity, tracker guidance, benchmark and telemetry contracts

## 1. Objective

Keep the existing 480x416 TensorRT input and inference cost while allowing the capture window to make a bounded, temporary offset around the screen center. The offset exists only to preserve an already committed target until the controller moves the camera far enough to bring that target back into the centered crop.

This is a finite sniffing extension, not a second camera controller and not an unbounded target-following viewport.

The design addresses live video evidence from 2026-07-21:

- A partially occluded first target stayed inside the centered crop, but its confidence and body-box geometry changed sharply as visibility changed.
- A close second target crossed the left and top crop boundaries. Its confidence fell from 0.795 to 0.385 at the clipped frame, below the current 0.40 decode floor.
- Replaying a 25% wider source region improved that close clipped frame from 0.385 to 0.822, but reduced confidence for the small partially visible first target. Permanently resizing a larger field of view into 480x416 is therefore not a general solution.

## 2. Non-goals

- Do not change the 480x416 engine or increase inference count.
- Do not resize a larger image into the engine input.
- Do not search for new targets with an off-center crop.
- Do not let raw right-stick or left-stick input directly position the crop.
- Do not add a second target lifecycle, occlusion hold, motion estimator, or controller gate.
- Do not change ADS, BodyLock, AutoFire, recoil, or target-switch authority.
- Do not make weak, predicted-only, or cue-only evidence eligible to fire.

## 3. Ownership

Add one stateful owner: `CaptureRoiPlanner`.

Its only responsibility is to compute the ROI offset for the next captured frame. It consumes the existing committed-target lifecycle, tracker prediction and user aim intent, but cannot select targets or produce controller output.

Existing ownership remains unchanged:

- `TargetCoordinator` owns target identity, lifecycle and confirmed switches.
- The tracker owns target motion estimation and short-horizon prediction.
- ADS and BodyLock own aim output for their respective phases.
- `CaptureRoiPlanner` owns only current ROI offset and rate limiting.

No committed target means the ROI is centered. A challenger cannot move the ROI until the coordinator confirms the switch.

## 4. Stable Coordinate Contract

The fixed centered crop defines the stable coordinate system. Its top-left corner is the canonical origin and the crosshair remains `(240, 208)`.

For a frame captured with offset `(offset_x, offset_y)`:

```text
stable_x = detection_local_x + offset_x
stable_y = detection_local_y + offset_y

aim_error_x = stable_target_x - 240
aim_error_y = stable_target_y - 208
```

The detector and color-cue readback use ROI-local coordinates. After local pixel-dependent processing is complete, detections, boxes and cue points are translated into stable coordinates before selector, tracker and controller reasoning.

Each captured frame must carry the exact offset that produced its texture:

```text
CapturedFrame {
    frame_id
    captured_at_ns
    roi_offset_x
    roi_offset_y
    texture
}
```

Consumers must never combine a frame with the latest global offset. This prevents asynchronous processing from turning viewport motion into false target velocity.

Selector operations that depend on visible bounds use the frame's stable-coordinate viewport:

```text
[offset_x, offset_y]
to
[offset_x + 480, offset_y + 416]
```

## 5. Finite Sniffing Envelope

The actual inference crop always remains 480x416. One `sniff_scale` defines how far that crop may move around the center:

```text
max_offset_x = crop_width  * (sniff_scale - 1) / 2
max_offset_y = crop_height * (sniff_scale - 1) / 2
```

The initial candidate is `sniff_scale = 1.50`, which gives:

```text
max_offset_x = +/-120 px
max_offset_y = +/-104 px
effective sniffing envelope = 720x624
```

This keeps the crosshair inside the actual crop at local X `120..360` and local Y `104..312`, while leaving target pixel size unchanged.

The benchmark must compare at least `1.25`, `1.50` and `1.75`. Production selection is based on matched fixed-seed and video-replay evidence, not the initial recommendation alone.

## 6. Offset Policy

### 6.1 Centered default

When there is no committed target, when ADS closes, when the tracker guidance is stale, or when capture is rebuilt, the planner clears its state and the next ROI is centered.

Every new physical LT/ADS epoch also performs an unconditional synchronous reset before accepting target guidance. The first capture of an ADS epoch is centered even if the previous epoch ended through an abnormal path, a dropped frame or an out-of-order reset. The planner cannot inherit offset, prediction or intent-alignment state across ADS epochs.

Existing tracker/coordinator occlusion hold counts as a committed target. The ROI planner does not add its own hold timer.

### 6.2 Protected target region

The planner protects a predicted upper-body envelope rather than requiring the complete body box to fit. A close target may be larger than the crop; preserving the head, chest and canonical aim point is the useful objective.

Initial protected geometry:

- middle 70% of body-box width;
- top 55% of body-box height;
- includes the configured canonical aim point.

Initial centered-crop safety margin:

- 56 px horizontally;
- 48 px vertically.

These are benchmark parameters, not independent runtime control gates.

### 6.3 Minimal necessary offset

The planner always evaluates the predicted protected region against the centered 480x416 viewport, not against the already shifted viewport.

- If the region fits inside the centered safe area, `desired_offset = (0, 0)`.
- If it would cross an edge, compute only the minimum offset needed to put it back inside the safe area.
- Clamp the result to the finite sniffing envelope.

As the controller moves the camera and returns the target to the centered crop, the required offset naturally shrinks back to zero. The ROI does not remain attached to the target.

### 6.4 Tracker prediction

Use the existing filtered tracker state with an initial 50 ms horizon:

```text
predicted_position = current_position + filtered_velocity * prediction_horizon
```

The planner must not estimate a second velocity. Jump, fall, reversal and strafe behavior come from the tracker.

### 6.5 User intent

User intent can scale how quickly the ROI approaches the tracker-requested offset, but cannot create an offset direction.

- Intent aligned with predicted motion: up to 25% faster.
- Neutral intent: normal planner speed.
- Opposing low-strength input: slower movement, without reversing the requested direction.
- Existing confirmed manual escape ends target authority; the ROI then centers.

Left-stick input is not added directly. Its visual consequence is already present in tracker-relative target motion, so adding it again would double compensate.

### 6.6 Motion shaping

Use one two-dimensional rate limiter:

```text
current_offset = move_toward(
    current_offset,
    desired_offset,
    max_speed_px_per_sec * dt)
```

Do not stack springs, acceleration curves, low-pass filters or per-axis arbitration. Initial maximum speed is 1200 px/s and must be benchmarked.

## 7. Weak-association Decode Correction

The vision engine currently decodes at 0.40, while the selector defines weak association for confidence `0.20 <= conf < 0.40`. The engine therefore deletes the entire weak-association range before the selector can use it.

Lower the decode floor to 0.20 while preserving authority rules:

- A 0.20-0.40 detection can only associate with the current committed target.
- It cannot pick up a new target.
- It cannot confirm a switch.
- It cannot grant AutoFire authority.
- It must pass geometry, position and motion-continuity checks.
- Weak detections do not directly authorize ROI movement; the planner still consumes tracker/coordinator guidance.

This change does not add inference work, but candidate decoding, selector CPU and color-readback behavior must be measured.

## 8. Failure Handling

- Clamp ROI origin to the selected DXGI output before copying.
- Record `edge_clamped` when the requested sniffing offset cannot be applied.
- Reject stale or out-of-order tracker guidance.
- Reset offset on every ADS epoch start and end, DXGI access loss, duplication rebuild or output-size change.
- Preserve physical input passthrough regardless of ROI state.
- Keep AutoFire's existing fresh-evidence and authority rules.
- A coordinate-transform failure must fail centered, not retain an unverified offset.

## 9. Telemetry

Debug telemetry should record enough information to reconstruct the decision without becoming a second policy surface:

- applied and desired ROI offset;
- sniff scale and clamp state;
- stable viewport bounds;
- committed target ID;
- current and predicted protected region;
- tracker guidance age;
- intent alignment and resulting speed scale;
- local and stable selected aim point;
- target confidence, weak-association status and hold status.

Logging remains opt-in and follows existing fresh-session cleanup rules.

## 10. Benchmark Plan

### 10.0 Ground-truth and evidence contract

Turn the two short engagements in the 2026-07-21 full-screen recording into a small, independently corrected evaluation fixture:

- first engagement: approximately 0.70-1.50 seconds;
- second engagement: approximately 3.60-4.60 seconds.

Annotations use full-screen coordinates and include target identity, visible body box, protected upper-body box, canonical chest point, visibility and occlusion state. Keyframes are labeled every 3-5 video frames, intermediate frames may be propagated with optical flow or an offline tracker, and all boundary, occlusion and identity transitions are visually reviewed.

The current TensorRT engine may propose initial boxes, but its output cannot be used unchanged as ground truth. Otherwise the benchmark would use the system under test to certify itself. This fixture is evaluation data, not model distillation or a production training change.

Evidence is separated into three layers:

1. Full-frame video replay validates capture coverage, detection continuity, coordinate transforms, selector identity and tracker input quality.
2. A deterministic closed-loop scenario derived from the annotated target motion, scale and occlusion validates tracker, ROI planner and controller interaction.
3. A live smoke test validates hand feel after automated gates pass.

The recorded camera trajectory is exogenous and cannot prove counterfactual controller behavior. Video replay results must not be reported as closed-loop ADS or BodyLock gains.

### 10.1 Coordinate invariance

Hold a world target fixed while moving the ROI through deterministic offsets. The translated stable target must move by no more than 0.5 px. Include frame/offset reordering to catch metadata mismatches.

### 10.2 2026-07-21 video replay

Replay full 1920x1080 frames with:

1. fixed centered 480x416;
2. bounded dynamic 480x416 at each sniff scale;
3. larger-region resize as a diagnostic upper bound only.

Measure clipped frames, confidence below 0.40, complete misses, weak associations, tracker holds, target-point jumps and time the protected chest region spends outside the ROI.

The fixed and dynamic paths consume the same source frames, annotations and engine. The diagnostic larger-region resize is never eligible as a production winner.

### 10.3 Non-edge partial target

Reproduce the first partially occluded target. With no committed edge threat, dynamic ROI must stay centered and match the fixed-crop result.

### 10.4 Reversal and vertical motion

Include lateral reversal near an edge, jump apex, falling motion and diagonal close crossing. ROI movement must not create a second false reversal or false tracker velocity.

### 10.5 User intent

Test aligned input, neutral input, short opposing mistakes, confirmed manual escape, and simultaneous left-strafe/right-stick correction.

### 10.6 Multiple targets and occlusion

Keep a committed edge target through short occlusion while a challenger appears near the centered crop. The challenger cannot move the ROI before a confirmed coordinator switch.

### 10.7 Controller matched A/B

Build a deterministic closed-loop trajectory from the annotated edge crossing rather than treating the recorded camera movement as counterfactual output. Preserve observed target direction, scale growth, diagonal motion, occlusion timing and reversal, then let the simulated controller output alter subsequent relative target position.

Run the trajectory with aligned input, neutral input, short opposing mistakes and confirmed manual escape. Measure acquisition, sustained error, interruption, jerk, overshoot/brake burden, user-fight, ROI recentering and delivered output. ROI movement must not alter controller output when stable target evidence is identical.

### 10.8 Replay artifact

The retained acceptance artifact records:

- source video identity and annotated time ranges;
- annotation schema version and reviewer status;
- runtime revision, engine identity and effective config fingerprint;
- fixed seeds and scenario semantics;
- baseline and dynamic ROI metrics;
- sniff-scale matrix;
- coordinate-invariance and ADS-epoch reset results;
- performance timings;
- known limitations, including the lack of counterfactual camera response in raw video replay.

## 11. Acceptance Criteria

- ROI is centered whenever no committed target exists.
- The first captured frame of every physical LT/ADS epoch is centered and carries no planner state from the previous epoch.
- No inference dimension, engine, or inference-count change.
- ROI movement introduces at most 0.5 px stable-coordinate error.
- Edge-crossing clipped and missed frames improve by at least 50% in the video-derived scenario.
- The non-edge first engagement keeps an exact zero offset and does not regress detection continuity.
- Non-edge fixed-crop scenarios show no statistically meaningful ADS or BodyLock regression.
- No increase in incorrect target switches, incorrect BodyLock interruption or AutoFire activation.
- No additional lifecycle, hold, motion-estimation or controller owner is introduced.
- Added vision/selector/planner CPU P95 is at most 0.15 ms on the acceptance machine.
- Full fixed-seed acceptance cannot trade broad ordinary-scene regression for the edge-case gain.

## 12. Initial Configuration Shape

```toml
[vision.dynamic_roi]
enabled = true
sniff_scale = 1.50
prediction_ms = 50
max_speed_px_per_sec = 1200
intent_speed_bonus = 0.25
```

Safety margins and protected-body ratios should remain internal constants until evidence shows that users need to tune them. This keeps the production configuration small and prevents another collection of interacting micro-gates.
