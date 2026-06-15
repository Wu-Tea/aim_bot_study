# Agent Handoff

Last updated: 2026-06-15T16:35:00+08:00
Updated by: Codex
Active scope: COD/FPS full native C++ gamepad runtime, native vision performance, native controller feel, and recoil playback.
Staleness: stale after another runtime-entry change, a new detector/model baseline, major native controller behavior changes, or live evidence that C++ runtime feel/perf regressed versus the Python fallback.

## Current Objective

Treat the default gamepad runtime as full native C++ and keep documentation, debugging, and future optimization work aligned with that reality. Python remains useful for fallback, tools, tests, training/export, recoil app workflows, and comparison, but it should not be assumed to be part of the normal live gamepad hot path.

## Current State

- Branch/workspace: `D:\work\AI\yolo-study-001`.
- Default gamepad launch path:
  - `scripts\launch\gamepad_start.bat`
  - defaults to `GAMEPAD_RUNTIME=native`
  - calls `scripts\launch\gamepad_native_cpp_start.bat`
  - starts `native\vision_native\build\Release\cod_native_runtime.exe --config config.toml --perf-log`
- Set `GAMEPAD_RUNTIME=python` only when explicitly using the older Python gamepad fallback.
- Default live gamepad runtime is now C++ end to end:
  - config loading
  - physical gamepad input
  - native ROI capture
  - CUDA preprocess
  - TensorRT inference
  - native target selector and authority fields
  - native `ai_aim`
  - native auto-fire gate
  - native aim-assist dynamics
  - native recoil profile selection/despike/playback
  - native ViGEm output
- Python gamepad host and `vision/native_runner.py` are fallback/debug/reference paths, not the default gamepad runtime.
- Mouse and `kbm_to_gamepad` still use Python-side hosts.
- User-provided C++ runtime logs on 2026-06-07 showed `[Vision][CPP]` lines with typical native GPU timing around `6-10ms` and vision `age` around `8-12ms`. Interpretation from that evidence: Python/native communication is no longer the likely live-gamepad bottleneck; remaining performance work should be measured in native C++ runtime/GPU timing first.

## Implemented In This Version

- Full native C++ runtime implementation exists under:
  - `native/runtime_app/`
  - `native/controller_native/`
- Native runtime launcher exists:
  - `scripts\launch\gamepad_native_cpp_start.bat`
- Default launcher chooses native runtime:
  - `scripts\launch\gamepad_start.bat`
  - supports `GAMEPAD_RUNTIME=python` fallback.
- Native gamepad behavior modules include:
  - `ai_aim.cpp`
  - `aim_assist_dynamics.cpp`
  - `recoil_compensation.cpp`
  - `target_tracker.cpp`
  - `virtual_gamepad.cpp`
  - `xinput_reader.cpp`
  - `sdl_gamepad_reader.cpp`
  - `weapon_recognizer.cpp`
- Native recoil path includes profile loading, calibration lookup, profile despike, profile/fallback playback, and selection-log gating.
- Recoil playback feel contract:
  - uncalibrated profile Y output uses the old dev-style per-sample delta scaled by `profile_velocity_reference_ms`, not cumulative Y from fire start
  - fallback feedback is a constant feed-forward down-pull; live config currently uses `feedback_amount = 0.30`
  - do not add timed/pulsed feedback shaping unless a native unit test proves the old linear fallback remains available
- Native tracker/controller/recoil boundary contract:
  - vision/tracker state feeds controller assistance before recoil
  - recoil remains the final feed-forward playback stage
  - recoil must not consume target dx/dy, tracker state, target freshness, or controller correction errors
  - tracker receives component-aware final camera motion for ego projection while manual/assist/dynamics/recoil/final output components remain separately attributed
  - run `scripts\verify\native_pipeline_contract.bat` before live acceptance after native tracker/controller/recoil changes
- Native perf/logging path now emits `[Vision][CPP]` and `[Perf][CPP]` style diagnostics.
- Documentation was updated on 2026-06-11 to mark full native C++ as the default:
  - `README.md`
  - `docs/project/README.md`
  - `docs/project/WORKLOG.md`
  - `docs/project/PROJECT_OVERVIEW.md`
  - `docs/project/GAMEPAD_OVERVIEW.md`
  - `docs/project/CONTROLLER_OVERVIEW.md`
  - `docs/project/NATIVE_VISION.md`
  - `docs/project/VISION_OVERVIEW.md`
  - `docs/project/NATIVE_CPP_RUNTIME.md`

## Current Debugging Posture

- For live gamepad runtime questions, read `docs/project/NATIVE_CPP_RUNTIME.md` first.
- For native gamepad behavior, inspect `native/controller_native/` before Python `controllers/gamepad/`.
- For native runtime scheduling/logs, inspect `native/runtime_app/runtime_loop.cpp` and `native/runtime_app/perf_logger.cpp`.
- For C++ controller/runtime behavior changes, add or update focused native unit tests in the same change; do not treat live tuning alone as sufficient verification.
- For tracker/controller/recoil boundary changes, run `scripts\verify\native_pipeline_contract.bat`; the broader `tools\check_native_cpp_gamepad_runtime.ps1` now calls the core pipeline contract by default.
- For vision timing, inspect C++ runtime logs first:
  - `pre`
  - `infer`
  - `gpu`
  - `wait`
  - `age`
  - controller/output timing when available
- Python fallback remains useful to compare behavior and bisect regressions, but should not drive default-path assumptions.

## Known Follow-Ups

1. Verify `[Perf][CPP]` reports actual measured runtime/window FPS rather than hard-coded loop assumptions.
2. Add or verify native output-age style fields comparable to old Python `out_age` so C++ logs can be compared cleanly.
3. Check target freshness on ADS transitions:
   - if `VisionResult.frame_updated=false`, native controller currently ignores the result
   - ensure stale `latest_vision_state_` cannot create a first-frame old-target pull when ADS resumes
4. Continue A/B tests with smaller TensorRT engines if GPU timing remains the main bottleneck.
5. Live-validate that recoil feel remains correct after the target-direction yield removal and component-aware tracker/controller boundary changes.

## Do Not Do Without New Evidence

- Do not assume Python/native handoff is the live gamepad bottleneck; default runtime no longer uses that handoff.
- Do not make Python `controllers/gamepad/` changes expecting them to affect the default gamepad runtime.
- Do not remove the Python fallback; it is still useful for comparison, tools, tests, and recovery.
- Do not give cue-only, weak-only, or predicted-only targets fire authority.
- Do not smooth final gamepad output after recoil unless live evidence shows that tuned recoil/manual feel can tolerate it.
- Do not reintroduce recoil target-direction yield, pre/post recoil tracker toggles, or controller-to-recoil target feedback without new evidence and native contract tests.
- Do not revert unrelated user or generated worktree changes.
- For training/data jobs, avoid heavy writes to `C:` and avoid RAM-backed modes unless the user explicitly approves.

## Related Decisions And Docs

- `docs/project/NATIVE_CPP_RUNTIME.md`
- `docs/project/PROJECT_OVERVIEW.md`
- `docs/project/GAMEPAD_OVERVIEW.md`
- `docs/project/CONTROLLER_OVERVIEW.md`
- `docs/project/WORKLOG.md`
- `docs/superpowers/plans/2026-06-06-native-cpp-runtime-migration.md`
- `decisions/DEC-2026-05-01-005-use-yellow-cue-as-short-continuation-hold.md`
- `decisions/DEC-2026-05-05-001-add-external-yellow-cue-input-and-sidecar-fallback.md`
- `decisions/DEC-2026-05-29-001-single-target-weak-association-authority-gating.md`
- `decisions/DEC-2026-06-04-001-recoil-despike-and-assist-dynamics.md`
