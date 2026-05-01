# DEC-2026-05-01-004: Simplify the Native Baseline by Removing Synthetic Compensation and Restoring Lightweight Gray Helpers

Status: accepted
Date: 2026-05-01
Confirmed by: user
Related sessions:
- 2026-05-01T17:54:45+08:00
Related files:
- native/vision_native/include/vision_native/aim_enhancement.h
- native/vision_native/include/vision_native/target_selector.h
- native/vision_native/include/vision_native/gray_frame.h
- native/vision_native/src/aim_enhancement.cpp
- native/vision_native/src/target_selector.cpp
- native/vision_native/src/image_ops.h
- native/vision_native/src/vision_native_module.cpp
- tests/test_native_vision_targeting_bridge.py
- tests/test_native_vision_enhancement_bridge.py
- tests/test_native_vision_image_ops_bridge.py
Supersedes: none
Superseded by: none

## Context

After the default runtime had already been rolled back to the pre-hotpath native baseline at commit `708c253`, the remaining native selector path still contained a synthetic compensation slice:

- `try_reconstruct(...)` could rewrite partial-occlusion detections into synthetic `reconstructed` targets
- `try_predict(...)` could emit synthetic `predicted` targets during short empty-frame gaps
- `AimEnhancementPipeline` treated `target_source == "predicted"` as a special case that disabled normal lead / catchup behavior

The user also recalled that earlier sessions had explored grayscale-side optimizations and asked whether anything useful from that work could be reused while removing redundant targeting behavior.

Historical inspection showed that the rolled-back experiment branch contained a larger grayscale-sharing stack, but the most self-contained reusable slice was the lightweight `GrayFrame` and `image_ops.h` utility layer rather than the full `BodyStateTracker` / `EgoMotionEstimator` stack.

## Decision

Keep the reverted native baseline as the controller-facing path, but simplify it further by:

1. removing selector-side synthetic `reconstruct/predict` target generation from the live baseline
2. removing aim-enhancement behavior that depends on the `"predicted"` source string
3. restoring only the lightweight grayscale helper layer (`GrayFrame`, `image_ops.h`, and pybind bridge accessors)

Do not treat this grayscale restoration as permission to reintroduce the rolled-back hotpath stack.

## Reasons

- The user explicitly approved this narrowed cleanup instead of reopening a broader architecture change.
- It reduces targeting complexity on the active native baseline before any new live validation.
- It preserves the most reusable part of the grayscale work without dragging the whole hotpath experiment back into the default runtime.
- It makes future grayscale-side experiments cheaper to start while keeping the controller-facing path simpler and easier to reason about.

## Rejected Alternatives

- Keep `reconstruct/predict` in the baseline and only restore grayscale helpers: rejected because the user specifically called out the motion-target compensation slice as removable redundancy.
- Reintroduce the full grayscale-sharing / body-state / ego-motion stack: rejected because that would conflict with the current rollback goal of keeping the live baseline simple.
- Remove compensation without restoring any grayscale helpers: rejected because the utility layer was small, self-contained, and plausibly useful for later low-cost experiments.

## Evidence

- User instruction: proceed by handling only `GrayFrame + image_ops` and the current `reconstruct/predict` chain.
- Verification after implementation:
  - `powershell -ExecutionPolicy Bypass -File tools\build_native_vision.ps1`
  - `py -3 -m unittest tests.test_native_vision_targeting_bridge tests.test_native_vision_enhancement_bridge tests.test_native_vision_image_ops_bridge -v`
  - `py -3 -m unittest tests.test_startup_scripts tests.test_performance_tracker tests.test_vision_runner tests.test_native_vision_runner tests.test_native_vision_targeting_bridge tests.test_native_vision_enhancement_bridge tests.test_native_vision_image_ops_bridge -v`
- Additional operational note:
  - the first rebuild attempt failed because a stale Python process was still locking `vision_native_cpp.cp311-win_amd64.pyd`; closing the process resolved the issue

## Consequences

- The live native baseline no longer synthesizes `reconstructed` or `predicted` targets in the selector path.
- `AimEnhancementPipeline` now behaves consistently regardless of the incoming `target_source` string.
- The repository regains a lightweight grayscale utility layer and direct bridge tests for it.
- Any future discussion about compensating target loss or partial occlusion must now be deliberate new work rather than inherited baseline behavior.

## Review Triggers

- Revisit if live COD22 validation shows that removing synthetic compensation made target loss / reacquire materially worse.
- Revisit if a future grayscale-driven experiment needs more than the restored utility layer.
- Revisit if the user decides to reopen a broader continuity-heavy native experiment branch on top of the simplified baseline.
