# Agent Handoff

Last updated: 2026-05-01T17:54:45+08:00
Updated by: Codex
Staleness: stale after the default native baseline moves away from commit `708c253` plus the 2026-05-01 compensation-removal cleanup, the controller-facing runtime reintroduces synthetic `reconstruct/predict` target handling, or yellow-cue / body-state / ego-motion experiments are reintroduced into the default runtime

## Current Objective

Live-validate the simplified native YOLO baseline after the rollback to `708c253` and the follow-up cleanup that removed synthetic motion-compensation target generation:

1. confirm the reverted-and-simplified native runtime behaves better on real COD22 ADS material than the later experiment branch
2. keep native as the default controller-facing backend; do not silently substitute the Python runtime
3. treat synthetic `reconstruct/predict` target generation as intentionally removed from the live baseline
4. reuse the restored grayscale helpers only as lightweight utility infrastructure unless a later experiment proves a concrete runtime win

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

## Next Action

1. live-validate the simplified native baseline in the real COD22 scenarios that regressed under the later experiment branch
2. confirm whether removing `reconstruct/predict` actually improves real-match stability and feel, especially on target loss / reacquire transitions
3. only after the baseline is trusted again, decide whether the restored grayscale helpers should support a new lightweight auxiliary experiment or remain bridge-only utilities

## Blockers

- No code blocker is currently known.
- The main product risk is live mismatch between experiment-branch intuition and actual in-match recall/latency.
- If constructor/signature mismatches or link failures recur after another rollback or rebuild, first check whether an older Python process is still holding a previous `.pyd`.

## Active Questions

- Is `708c253` the correct last-known-good native baseline for live play, or should the stable base move slightly forward or backward?
- Does removing synthetic `reconstruct/predict` improve controller feel enough to keep that cleanup permanently?
- When yellow cue work returns, should it sit in the native YOLO path as a lightweight auxiliary scorer rather than reopening the full hotpath experiment stack?
- Which exact live failure modes remain even on the simplified baseline: distant crouched enemies, partial cover, side-running targets, or target switching?

## Relevant Decisions

- `.agent-context/decisions/DEC-2026-04-22-001-native-vision-default-hybrid-runtime.md`
- `.agent-context/decisions/DEC-2026-04-22-002-defer-full-controller-cpp-rewrite.md`
- `.agent-context/decisions/DEC-2026-04-30-003-cod22-yellow-dot-mixed-cue-acquisition.md`
- `.agent-context/decisions/DEC-2026-05-01-001-birth-path-optimization-priority.md`
- `.agent-context/decisions/DEC-2026-05-01-002-side-running-lateral-tracking-optimization-priority.md`
- `.agent-context/decisions/DEC-2026-05-01-003-revert-default-native-runtime-to-708c253.md`
- `.agent-context/decisions/DEC-2026-05-01-004-simplify-native-baseline-remove-compensation-and-restore-gray-helpers.md`

## Files To Read First

- `.agent-context/session-log.md`
- `.agent-context/decisions/DEC-2026-04-22-001-native-vision-default-hybrid-runtime.md`
- `.agent-context/decisions/DEC-2026-04-22-002-defer-full-controller-cpp-rewrite.md`
- `.agent-context/decisions/DEC-2026-05-01-003-revert-default-native-runtime-to-708c253.md`
- `native/vision_native/src/vision_engine.cpp`
- `native/vision_native/src/target_selector.cpp`
- `native/vision_native/src/aim_enhancement.cpp`
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

## Notes

- This handoff supersedes the earlier assumption that `ac224b1` + `2b8a35b` remained the live native baseline.
- The 2026-05-01 article comparisons are still useful, but they now describe a rolled-back experiment branch rather than the default runtime.
- A mistaken intermediate attempt switched the startup defaults to `python`; that was corrected and recorded, and the committed state should remain native-default.
- A later cleanup intentionally removed the pre-hotpath synthetic compensation slice (`reconstruct/predict`) from the native selector path while preserving a lightweight grayscale utility layer for future experiments.
