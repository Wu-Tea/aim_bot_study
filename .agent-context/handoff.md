# Agent Handoff

Last updated: 2026-05-01T20:27:51+08:00
Updated by: Codex
Staleness: stale after the default native baseline moves away from commit `708c253` plus the 2026-05-01 compensation-removal cleanup and cue-hold follow-up, the controller-facing runtime reintroduces synthetic `reconstruct/predict` target handling, or yellow-cue / body-state / ego-motion experiments are reintroduced into the default runtime

## Current Objective

Live-validate the simplified native YOLO baseline after the rollback to `708c253`, the compensation-removal cleanup, and the short yellow-cue hold addition:

1. confirm the reverted-and-simplified native runtime behaves better on real COD22 ADS material than the later experiment branch
2. keep native as the default controller-facing backend; do not silently substitute the Python runtime
3. treat synthetic `reconstruct/predict` target generation as intentionally removed from the live baseline
4. use yellow cue only as short continuation evidence on top of native YOLO rather than as independent target authority

## Current State

Native vision remains the accepted default runtime through `vision/native_runner.py`, with Python vision still available as the fallback. Do not reopen the full-controller or whole-project C++ rewrite unless the user explicitly asks.

The live native controller-facing path is now:

- `ROI capture -> TensorRT person detection -> VisionTargetSelector -> AimEnhancement -> controller`

Implementation status aligned with the current accepted rollback decision:

- the default code state has been restored to commit `708c253` (`Prune mouse benchmark artifacts`, 2026-04-30 13:22:09 +0800) for the native runtime files and startup scripts affected by the later experiment branch
- `gamepad_start.bat` and `mouse_start.bat` default to `VISION_BACKEND=native`
- the 2026-04-30 evening hotpath/cadence/yellow-cue experiment stack from `ac224b1` and `2b8a35b` is no longer the controller-facing baseline
- synthetic `try_reconstruct(...)` and `try_predict(...)` handling have been removed from `VisionTargetSelector`
- `AimEnhancementPipeline` no longer disables lead/catchup behavior based on `target_source == "predicted"`
- `VisionTargetSelector` now supports a narrow `cue_hold` path: when a target was previously observed with a matched yellow cue and the next frame loses person detections, selector can keep the target alive for a very short window using `cue + last(target-cue offset)`
- `cue_hold` is continuation-only: it does not create idle-state acquisition, does not authorize target switching, and returns `auto_fire = false`
- lightweight grayscale helpers are available again through `GrayFrame`, `image_ops.h`, and pybind bridge functions, but the larger `BodyStateTracker` / `CenterCueRefiner` / `EgoMotionEstimator` stack remains out of the default runtime
- the current default native runner is the simpler pre-hotpath native YOLO path that was verified as the rollback target

Native runtime defaults:

- current startup-script defaults are:
  - `backend=native`
  - `capture=140`
  - `quit_key=0`

The 2026-05-01 article-derived optimization notes remain preserved in `.agent-context/decisions/`, but they are no longer assumptions about the live baseline because the relevant experiment branch was rolled back and the motion-compensation synthetic target slice has now been explicitly removed.

Environment constraint is unchanged: the native runtime still assumes `CUDA 13.1 + TensorRT 10.15.1.29`, and the earlier `failed to create TensorRT runtime` issue was an environment mismatch rather than a model-export regression.

## Verification

Most recent verified simplified native baseline state:

- `powershell -ExecutionPolicy Bypass -File tools\build_native_vision.ps1`
  - result: native build succeeded after closing stale Python processes that were locking `vision_native_cpp.cp311-win_amd64.pyd`
- `py -3 -m unittest tests.test_startup_scripts tests.test_performance_tracker tests.test_vision_runner tests.test_native_vision_runner tests.test_native_vision_targeting_bridge tests.test_native_vision_enhancement_bridge tests.test_native_vision_image_ops_bridge -v`
  - result: `51` passed
- `py -3 -m unittest tests.test_native_vision_targeting_bridge tests.test_native_vision_enhancement_bridge tests.test_native_vision_image_ops_bridge tests.test_native_vision_runner -v`
  - result: `28` passed after adding `cue_hold`
- user live feedback on the new cue-hold slice: "效果还行"

## Next Action

1. live-validate the simplified native baseline in the real COD22 scenarios that regressed under the later experiment branch
2. tune `cue_hold` length and matching tolerance only if live play shows either premature loss or false continuation
3. decide whether to consume an application-provided yellow-cue point directly later, rather than recomputing cue centroid inside the native selector ROI path

## Blockers

- No code blocker is currently known.
- The main product risk is live mismatch between experiment-branch intuition and actual in-match recall/latency.
- If constructor/signature mismatches or link failures recur after another rollback or rebuild, first check whether an older Python process is still holding a previous `.pyd`.

## Active Questions

- Is `708c253` the correct last-known-good native baseline for live play, or should the stable base move slightly forward or backward?
- Does removing synthetic `reconstruct/predict` improve controller feel enough to keep that cleanup permanently?
- Is the current `cue_hold` window conservative enough to help on muzzle-flash / one-frame obstruction without creating sticky false holds?
- Should a later revision consume the application's existing yellow-point result directly instead of recomputing cue centroid inside `VisionTargetSelector`?
- Which exact live failure modes remain even on the simplified baseline plus `cue_hold`: distant crouched enemies, partial cover, side-running targets, or target switching?

## Relevant Decisions

- `.agent-context/decisions/DEC-2026-04-22-001-native-vision-default-hybrid-runtime.md`
- `.agent-context/decisions/DEC-2026-04-22-002-defer-full-controller-cpp-rewrite.md`
- `.agent-context/decisions/DEC-2026-04-30-003-cod22-yellow-dot-mixed-cue-acquisition.md`
- `.agent-context/decisions/DEC-2026-05-01-001-birth-path-optimization-priority.md`
- `.agent-context/decisions/DEC-2026-05-01-002-side-running-lateral-tracking-optimization-priority.md`
- `.agent-context/decisions/DEC-2026-05-01-003-revert-default-native-runtime-to-708c253.md`
- `.agent-context/decisions/DEC-2026-05-01-004-simplify-native-baseline-remove-compensation-and-restore-gray-helpers.md`
- `.agent-context/decisions/DEC-2026-05-01-005-use-yellow-cue-as-short-continuation-hold.md`

## Files To Read First

- `.agent-context/session-log.md`
- `.agent-context/decisions/DEC-2026-04-22-001-native-vision-default-hybrid-runtime.md`
- `.agent-context/decisions/DEC-2026-04-22-002-defer-full-controller-cpp-rewrite.md`
- `.agent-context/decisions/DEC-2026-05-01-003-revert-default-native-runtime-to-708c253.md`
- `native/vision_native/src/vision_engine.cpp`
- `native/vision_native/src/target_selector.cpp`
- `native/vision_native/src/aim_enhancement.cpp`
- `native/vision_native/include/vision_native/types.h`
- `native/vision_native/include/vision_native/gray_frame.h`
- `native/vision_native/src/image_ops.h`
- `native/vision_native/src/vision_native_module.cpp`
- `vision/native_runner.py`
- `vision/perf.py`
- `gamepad_start.bat`
- `gamepad_native_debug.bat`
- `tests/test_native_vision_runner.py`
- `tests/test_native_vision_targeting_bridge.py`
- `tests/test_native_vision_enhancement_bridge.py`
- `tests/test_native_vision_image_ops_bridge.py`

## Do Not Reopen Unless Needed

- Full-project C++ rewrite
- Full controller C++ rewrite
- Python vision parity refactor
- Re-introducing the full 2026-04-30 evening hotpath stack before the reverted baseline is re-validated live
- Silent backend swaps from native to python when the user asked for a native rollback
- Pose / segmentation / SLAM / VO/VIO scope creep while the baseline itself is still being re-established
- Re-adding synthetic `reconstruct/predict` target output without new live evidence that it helps more than it hurts
- Expanding yellow cue into standalone idle-state target authority before the continuation-only cue-hold slice is fully understood

## Notes

- This handoff supersedes the earlier assumption that `ac224b1` + `2b8a35b` remained the live native baseline.
- The 2026-05-01 article comparisons are still useful, but they now describe a rolled-back experiment branch rather than the default runtime.
- A mistaken intermediate attempt switched the startup defaults to `python`; that was corrected and recorded, and the committed state should remain native-default.
- A later cleanup intentionally removed the pre-hotpath synthetic compensation slice (`reconstruct/predict`) from the native selector path while preserving a lightweight grayscale utility layer for future experiments.
- A later follow-up reintroduced yellow cue only as a short continuation hold (`cue_hold`) layered on top of observed native YOLO targets, with `auto_fire` disabled during the hold window.
