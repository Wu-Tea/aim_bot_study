# DEC-2026-05-01-005: Use Yellow Cue as Short Continuation Hold on the Native Baseline

Status: accepted
Date: 2026-05-01
Confirmed by: user
Related sessions:
- 2026-05-01T20:27:51+08:00
Related files:
- native/vision_native/include/vision_native/target_selector.h
- native/vision_native/include/vision_native/types.h
- native/vision_native/src/target_selector.cpp
- native/vision_native/src/vision_engine.cpp
- tests/test_native_vision_targeting_bridge.py
Supersedes: none
Superseded by: none

## Context

After the simplified native baseline removed selector-side `reconstruct/predict` target synthesis, the user still wanted to exploit COD22's yellow enemy marker because it remained informative during brief occlusions and muzzle-flash-like interruptions.

The key clarification was that yellow cue should not become an independent acquisition or controller-facing authority. Instead, it should help only after a real person target has already been observed and associated with a cue.

At the time of this change, the live native baseline still had:

- box-top color classification and `color_bonus`
- no cue-point coordinate channel
- no short evidence-based continuation layer once detections dropped to zero

## Decision

Add a narrow continuation-only `cue_hold` path to the native selector:

1. while a target is `observed` and a valid yellow cue is found above the box, record cue centroid plus `target - cue` offset
2. when person detections briefly disappear, allow selector to return a short-lived `cue_hold` target if the cue is still found near the previous cue position
3. reconstruct the held aim point as `cue + last(target-cue offset)`
4. keep `cue_hold` conservative:
   - no idle-state acquisition
   - no target switching
   - `auto_fire = false`
   - very short maximum lifetime

## Reasons

- It captures the useful part of yellow cue without reopening the earlier experiment branch where cue threatened to become a parallel targeting authority.
- It is better grounded than the removed synthetic `predict/reconstruct` path because it depends on still-visible cue evidence.
- It directly addresses the user's real gameplay failure mode: one-frame obstruction or shot-effect interruption after a target has already been acquired.
- It preserves the detector-led geometry contract while adding a lightweight continuation assist.

## Rejected Alternatives

- Use yellow cue for idle-state target acquisition: rejected because it would recreate the risk of cue-only targeting authority.
- Reintroduce generic `predict/reconstruct` continuation instead: rejected because that path had already been intentionally removed as baseline complexity.
- Wait for a larger hotpath / lifecycle redesign before using yellow cue again: rejected because the user wanted a much narrower and immediately useful continuation aid.

## Evidence

- User direction:
  - yellow cue should be used as hold evidence after a target is already identified
  - person-to-cue offset can be computed when person detection succeeds, then reused during short interruption
  - post-change live feedback: "效果还行"
- Verification after implementation:
  - `powershell -ExecutionPolicy Bypass -File tools\build_native_vision.ps1`
  - `py -3 -m unittest tests.test_native_vision_targeting_bridge.NativeVisionTargetingBridgeTests.test_yellow_cue_hold_keeps_target_through_short_empty_gap tests.test_native_vision_targeting_bridge.NativeVisionTargetingBridgeTests.test_cue_hold_disables_autofire_during_short_gap -v`
  - `py -3 -m unittest tests.test_native_vision_targeting_bridge tests.test_native_vision_enhancement_bridge tests.test_native_vision_image_ops_bridge tests.test_native_vision_runner -v`

## Consequences

- The native selector now has a short evidence-based continuation mode distinct from both full detection and the earlier synthetic prediction slice.
- `VisionEngine` must keep color-frame access available on empty-detection frames when selector still wants cue-hold recovery.
- The live baseline now contains a small amount of cue-specific state again, but only in continuation scope.
- A future optimization opportunity exists to feed an application-provided cue point directly instead of recomputing cue centroid inside selector-side ROI scanning.

## Review Triggers

- Revisit if `cue_hold` causes sticky false continuation or target confusion in crowded scenes.
- Revisit if live validation shows the hold window is too short to help or long enough to hurt.
- Revisit if the application already exposes a stable cue-point result that should replace selector-side cue centroid scanning.
