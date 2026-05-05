# Agent Handoff

Last updated: 2026-05-05T16:27:45+08:00
Updated by: Codex
Staleness: stale after the default native baseline moves away from commit `708c253` plus the 2026-05-01 compensation-removal cleanup and cue-hold follow-up, the controller-facing runtime reintroduces synthetic `reconstruct/predict` target handling, yellow-cue / body-state / ego-motion experiments are reintroduced into the default runtime, the cue-source precedence changes away from `explicit -> controller -> sidecar`, or the runtime starts a full controller C++ rewrite without first exhausting the accepted higher-ROI hotpath optimizations

## Current Objective

Live-validate the simplified native YOLO baseline after the rollback to `708c253`, the compensation-removal cleanup, and the short yellow-cue hold addition, while treating native hotpath copy reduction as the next optimization priority:

1. confirm the reverted-and-simplified native runtime behaves better on real COD22 ADS material than the later experiment branch
2. keep native as the default controller-facing backend; do not silently substitute the Python runtime
3. treat synthetic `reconstruct/predict` target generation as intentionally removed from the live baseline
4. use yellow cue only as short continuation evidence on top of native YOLO rather than as independent target authority
5. prioritize native hotpath copy / scan reduction and direct cue-input opportunities before reopening full controller C++ rewrite work

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
- native hotpath color/cue work no longer requires unconditional full-frame host copies:
  - `VisionEngine` computes a selector-requested color ROI and downloads only that subframe when CPU-side pixels are needed
  - `VisionResult.perf.color_copy_ms` surfaces the host color/cue copy cost separately from preprocess and infer
- selector-side color classification and yellow-cue extraction over normal detection ROIs are now merged into a single CPU scan
- native runtime now accepts frame-level external yellow cue input, and `cue_hold` can consume that cue directly on empty-detection frames without requiring an image subframe
- `vision/native_runner.py` resolves yellow cue input in this order:
  1. explicit `cue_provider`
  2. controller hook (`get_external_cue`, `get_targeting_cue`, `get_yellow_cue`)
  3. built-in `ScreenCaptureCueProvider` sidecar from `vision/yellow_cue.py`
- `ControllerTarget` now preserves native `target_source`, including `cue_hold`
- same-target matching in `VisionTargetSelector` tolerates vertical truncation / upper-body-only follow-up detections so the active target can update immediately and drop `auto_fire` as soon as the observed box leaves the fire zone
- lightweight grayscale helpers are available again through `GrayFrame`, `image_ops.h`, and pybind bridge functions, but the larger `BodyStateTracker` / `CenterCueRefiner` / `EgoMotionEstimator` stack remains out of the default runtime
- the current default native runner is the simpler pre-hotpath native YOLO path that was verified as the rollback target

Native runtime defaults:

- current startup-script defaults are:
  - `backend=native`
  - `capture=140`
  - `quit_key=0`

The 2026-05-01 article-derived optimization notes remain preserved in `.agent-context/decisions/`, but they are no longer assumptions about the live baseline because the relevant experiment branch was rolled back and the motion-compensation synthetic target slice has now been explicitly removed.

Optimization-priority conclusion from the 2026-05-01 three-explorer review:

- do not treat full controller-in-C++ migration as the next default move; keep the prior "defer full rewrite" decision in force
- if controller-side native work is reconsidered later, prefer a narrow host/output transport migration over porting the entire controller state machine
- the largest remaining likely wins are still in the native hot path:
  - avoid full-frame host download when only box-top color or cue-hold windows are needed
  - merge duplicated color / yellow-cue CPU scans over the same ROI
  - consume an application-provided yellow cue point directly if it already exists cheaply
  - only after that, revisit pickup-confirm conservatism or partial native controller hosting

Environment constraint is unchanged: the native runtime still assumes `CUDA 13.1 + TensorRT 10.15.1.29`, and the earlier `failed to create TensorRT runtime` issue was an environment mismatch rather than a model-export regression.

## Verification

Most recent verified simplified native baseline state:

- `powershell -ExecutionPolicy Bypass -File tools\build_native_vision.ps1`
  - result: native build succeeded after closing stale Python processes that were locking `vision_native_cpp.cp311-win_amd64.pyd`
- `py -3 -m unittest tests.test_startup_scripts tests.test_performance_tracker tests.test_vision_runner tests.test_native_vision_runner tests.test_native_vision_targeting_bridge tests.test_native_vision_enhancement_bridge tests.test_native_vision_image_ops_bridge -v`
  - result: `51` passed
- `py -3 -m unittest tests.test_native_vision_targeting_bridge tests.test_native_vision_enhancement_bridge tests.test_native_vision_image_ops_bridge tests.test_native_vision_runner -v`
  - result: `28` passed after adding `cue_hold`
- `powershell -ExecutionPolicy Bypass -File tools\build_native_vision.ps1`
  - result: native build succeeded after adding ROI-only color copy, external cue bridge, default sidecar cue provider, and the upper-body same-target fix
- `py -3 -m unittest tests.test_native_vision_runner tests.test_native_vision_targeting_bridge tests.test_performance_tracker tests.test_vision_runner tests.test_yellow_cue -v`
  - result: `55` passed after fixing a one-frame stale-target regression where upper-body-only follow-up detections could keep `auto_fire` active for one extra frame
- user live feedback on the new cue-hold slice: "效果还行"

## Next Action

1. live-validate the simplified native baseline in the real COD22 scenarios that regressed under the later experiment branch
2. measure whether the new `color_copy_ms` metric actually drops enough in live play after the ROI-only host-copy change
3. validate whether the default sidecar cue source is cheap and stable enough, or whether the real application-provided cue point should displace it in the default runtime
4. tune `cue_hold` length and matching tolerance only if live play shows either premature loss or false continuation
5. decide whether additional pickup-confirm relaxation is needed now that cue input and copy reduction are in place

## Blockers

- No code blocker is currently known.
- The main product risk is live mismatch between experiment-branch intuition and actual in-match recall/latency.
- If constructor/signature mismatches or link failures recur after another rollback or rebuild, first check whether an older Python process is still holding a previous `.pyd`.

## Active Questions

- Is `708c253` the correct last-known-good native baseline for live play, or should the stable base move slightly forward or backward?
- Does removing synthetic `reconstruct/predict` improve controller feel enough to keep that cleanup permanently?
- Is the current `cue_hold` window conservative enough to help on muzzle-flash / one-frame obstruction without creating sticky false holds?
- Should a later revision consume the application's existing yellow-point result directly instead of recomputing cue centroid inside `VisionTargetSelector`?
- How much hidden time is currently being spent on the full-frame host download used for color and cue logic, given that it is not surfaced cleanly in `post_ms`?
- Is a narrow native controller host/output slice worth testing later, or will copy/sync cleanup and external cue input absorb the most meaningful latency first?
- Does the built-in sidecar cue provider add acceptable overhead compared with a true upstream application cue signal?
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
- `.agent-context/decisions/DEC-2026-05-01-006-prioritize-native-hotpath-copy-reduction-over-full-controller-cpp-rewrite.md`
- `.agent-context/decisions/DEC-2026-05-05-001-add-external-yellow-cue-input-and-sidecar-fallback.md`

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
- `native/vision_native/src/dxgi_capture.cpp`
- `native/vision_native/src/tensorrt_engine.cpp`
- `vision/yellow_cue.py`
- `vision/native_runner.py`
- `vision/perf.py`
- `controllers/gamepad_controller.py`
- `controllers/mouse_controller.py`
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
- Starting a full controller C++ rewrite before measuring and exhausting the accepted copy/sync/cue-input optimizations in the current native hot path
- Letting the sidecar cue provider silently diverge from a cheaper or more authoritative application-provided cue source without measuring the cost

## Notes

- This handoff supersedes the earlier assumption that `ac224b1` + `2b8a35b` remained the live native baseline.
- The 2026-05-01 article comparisons are still useful, but they now describe a rolled-back experiment branch rather than the default runtime.
- A mistaken intermediate attempt switched the startup defaults to `python`; that was corrected and recorded, and the committed state should remain native-default.
- A later cleanup intentionally removed the pre-hotpath synthetic compensation slice (`reconstruct/predict`) from the native selector path while preserving a lightweight grayscale utility layer for future experiments.
- A later follow-up reintroduced yellow cue only as a short continuation hold (`cue_hold`) layered on top of observed native YOLO targets, with `auto_fire` disabled during the hold window.
- A later three-explorer review concluded that full controller-in-C++ work is still lower priority than native hotpath copy reduction, duplicated ROI scan cleanup, and direct external cue-input support.
- A later implementation pass actually landed ROI-only host color copies, merged color/cue ROI scans, frame-level external cue ingestion, a default sidecar cue provider, and a same-target matching fix for vertically truncated follow-up detections.
