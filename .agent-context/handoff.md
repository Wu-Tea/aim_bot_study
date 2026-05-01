# Agent Handoff

Last updated: 2026-05-01T16:48:00+08:00
Updated by: Codex
Staleness: stale after the default native baseline moves away from commit `708c253`, the controller-facing runtime stops using the pre-hotpath native selector/aim-enhancement path, or yellow-cue / body-state / ego-motion experiments are reintroduced into the default runtime

## Current Objective

Re-establish and live-validate the pre-hotpath native YOLO baseline at commit `708c253` before attempting any more yellow-cue or continuity-heavy experiments:

1. confirm the reverted native runtime behaves like the last known-good baseline in live ADS use
2. keep native as the default controller-facing backend; do not silently substitute the Python runtime
3. treat the 2026-04-30 evening hotpath / yellow-cue slice as rolled back work, not the current baseline
4. revisit yellow-cue or lateral-tracking improvements only after the baseline is stable again

## Current State

Native vision remains the accepted default runtime through `vision/native_runner.py`, with Python vision still available as the fallback. Do not reopen the full-controller or whole-project C++ rewrite unless the user explicitly asks.

The live native controller-facing path is now:

- `ROI capture -> TensorRT person detection -> VisionTargetSelector -> AimEnhancement -> controller`

Implementation status aligned with the current accepted rollback decision:

- the default code state has been restored to commit `708c253` (`Prune mouse benchmark artifacts`, 2026-04-30 13:22:09 +0800) for the native runtime files and startup scripts affected by the later experiment branch
- `gamepad_start.bat` and `mouse_start.bat` default to `VISION_BACKEND=native`
- the 2026-04-30 evening hotpath/cadence/yellow-cue experiment stack from `ac224b1` and `2b8a35b` is no longer the controller-facing baseline
- `BodyStateTracker`, `CenterCueRefiner`, `EgoMotionEstimator`, grayscale-sharing, and cadence-split logic are not part of the current default runtime after the rollback
- the current default native runner is the simpler pre-hotpath native YOLO path that was verified as the rollback target

Native runtime defaults:

- current startup-script defaults are:
  - `backend=native`
  - `capture=140`
  - `quit_key=0`

The 2026-05-01 article-derived optimization notes remain preserved in `.agent-context/decisions/`, but they are no longer assumptions about the live baseline because the relevant experiment branch was rolled back.

Environment constraint is unchanged: the native runtime still assumes `CUDA 13.1 + TensorRT 10.15.1.29`, and the earlier `failed to create TensorRT runtime` issue was an environment mismatch rather than a model-export regression.

## Verification

Most recent verified rollback state:

- `powershell -ExecutionPolicy Bypass -File tools\build_native_vision.ps1`
  - result: native build succeeded after restoring the default native runtime files to `708c253`
- `py -3 -m unittest tests.test_startup_scripts tests.test_performance_tracker tests.test_vision_runner tests.test_native_vision_runner tests.test_native_vision_targeting_bridge -v`
  - result: `44` passed

## Next Action

1. live-validate the reverted native baseline in the real COD22 scenarios that regressed under the later experiment branch
2. confirm whether `708c253` is the right stable base or whether an even narrower post-`708c253` native point should be restored instead
3. only after the baseline is trusted again, decide whether to reintroduce yellow-cue support as an auxiliary layer on top of native YOLO rather than as a replacement controller path

## Blockers

- No code blocker is currently known.
- The main product risk is live mismatch between experiment-branch intuition and actual in-match recall/latency.
- If constructor/signature mismatches recur after another rollback or rebuild, first check whether an older Python process is still holding a previous `.pyd`.

## Active Questions

- Is `708c253` the correct last-known-good native baseline for live play, or should the stable base move slightly forward or backward?
- When yellow cue work returns, should it sit in the native YOLO path as a lightweight auxiliary scorer rather than reopening the full hotpath experiment stack?
- Which exact live failure modes remain even on the reverted native baseline: distant crouched enemies, partial cover, side-running targets, or target switching?

## Relevant Decisions

- `.agent-context/decisions/DEC-2026-04-22-001-native-vision-default-hybrid-runtime.md`
- `.agent-context/decisions/DEC-2026-04-22-002-defer-full-controller-cpp-rewrite.md`
- `.agent-context/decisions/DEC-2026-04-30-003-cod22-yellow-dot-mixed-cue-acquisition.md`
- `.agent-context/decisions/DEC-2026-05-01-001-birth-path-optimization-priority.md`
- `.agent-context/decisions/DEC-2026-05-01-002-side-running-lateral-tracking-optimization-priority.md`
- `.agent-context/decisions/DEC-2026-05-01-003-revert-default-native-runtime-to-708c253.md`

## Files To Read First

- `.agent-context/session-log.md`
- `.agent-context/decisions/DEC-2026-04-22-001-native-vision-default-hybrid-runtime.md`
- `.agent-context/decisions/DEC-2026-04-22-002-defer-full-controller-cpp-rewrite.md`
- `.agent-context/decisions/DEC-2026-05-01-003-revert-default-native-runtime-to-708c253.md`
- `native/vision_native/src/vision_engine.cpp`
- `native/vision_native/src/target_selector.cpp`
- `native/vision_native/src/aim_enhancement.cpp`
- `vision/native_runner.py`
- `vision/perf.py`
- `gamepad_start.bat`
- `gamepad_native_debug.bat`
- `tests/test_native_vision_body_state_bridge.py`
- `tests/test_native_vision_runner.py`
- `tests/test_native_vision_targeting_bridge.py`

## Do Not Reopen Unless Needed

- Full-project C++ rewrite
- Full controller C++ rewrite
- Python vision parity refactor
- Re-introducing the full 2026-04-30 evening hotpath stack before the reverted baseline is re-validated live
- Silent backend swaps from native to python when the user asked for a native rollback
- Pose / segmentation / SLAM / VO/VIO scope creep while the baseline itself is still being re-established

## Notes

- This handoff supersedes the earlier assumption that `ac224b1` + `2b8a35b` remained the live native baseline.
- The 2026-05-01 article comparisons are still useful, but they now describe a rolled-back experiment branch rather than the default runtime.
- A mistaken intermediate attempt switched the startup defaults to `python`; that was corrected and recorded, and the committed state should remain native-default.
