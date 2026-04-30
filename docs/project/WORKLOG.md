# Worklog

## 2026-04-22 (project status snapshot)

### Current progress

- Native C++ vision is now the default runtime path for `gamepad_start.bat`.
- The native path now covers the hot vision loop end-to-end:
  - centered ROI capture
  - native preprocessing
  - TensorRT inference with `models/best.engine`
  - native selector / occlusion compensation / enhancement / auto-fire recommendation
  - `VisionResult` handoff back to the existing Python controller
- Python vision remains available as a fallback and behavior oracle through `--vision-backend python`.
- Startup defaults for the native gamepad path are now:
  - `VISION_BACKEND=native`
  - `VISION_CAPTURE_FPS=140`
  - `VISION_QUIT_KEY=0`
  - `--perf-log`
- Dedicated native debug entry points now exist:
  - `gamepad_debug.bat`
  - `gamepad_native_debug.bat`
- Recent live testing showed that the native vision path is materially faster than the old Python path, especially on:
  - `wait_ms`
  - `infer_ms`
  - `age_ms`
- The project is no longer in a "native vision scaffold only" state. Native vision is already integrated into the real gamepad startup flow.

### Decisions

- Keep the architecture hybrid for now:
  - native C++ for the hot `vision` path
  - Python for controller orchestration, config, startup scripts, debug tooling, and benchmarks
- Do **not** commit to a full-project C++ rewrite right now.
  - Native vision already delivered the biggest ROI.
  - Controller migration is still technically possible, but it now looks like a smaller performance win with much higher hand-feel regression risk.
- Do **not** commit to a full `controller` rewrite in C++ yet.
  - If controller becomes the next real bottleneck, prefer a focused native `gamepad controller` migration instead of rewriting the whole repo.
- Keep Python vision alive as the comparison baseline and fallback path while native behavior continues to be validated in live play.
- Disable the in-game keyboard quit hotkey by default for gamepad startup via `VISION_QUIT_KEY=0`.
  - This decision came from observed accidental self-exits during gameplay.
- Treat native perf fields with these meanings:
  - `loop FPS` = throughput
  - `age_ms` = end-to-end freshness from capture to controller-visible result
  - `infer_ms` = TensorRT core inference time, not the whole native frame cost by itself
- When evaluating native latency, prioritize `age_ms` over `infer_ms` alone.

### Open work

- Continue live validation of the native gamepad path:
  - long-play stability
  - targeting parity vs Python
  - auto-fire timing and stick-feel regression checks
- Consider exposing `preprocess_ms` directly in the `[Perf]` line later so native timing reads more self-consistently as:
  - `wait | pre | infer | post | age`
- Mouse work is currently in progress in the workspace:
  - ADS-entry / commit-hold / reacquire-bridge behavior
  - expanded `ControllerTarget` metadata usage on the mouse path
  - native mouse startup/debug scripts
- Treat the current mouse refactor as **in progress**, not project-complete, until its new tests and startup path are fully verified and committed.

## 2026-04-14 (vision detector-first tuning)

### Detector-first pipeline

- Switched the default vision model path from pose to detect:
  - `models/yolo26n.engine`
  - fallback `models/yolo26n.pt`
- Moved the runtime defaults to a rectangular capture profile:
  - `640 x 512`
  - `80 FPS`
- Unified runtime configuration around:
  - `capture_width`
  - `capture_height`
  - `capture_fps`
- Updated:
  - `main.py`
  - `vision/runner.py`
  - `vision/capture.py`
  - `vision/fastpath.py`
  - `tools/export_trt.py`

### Targeting changes

- Removed pose-keypoint dependence from the main targeting path.
- `TargetSelector` now derives:
  - upper-chest aim point from box geometry
  - slow zone from box geometry
  - fire zone from box geometry
- Added stricter pickup filtering plus more tolerant tracked-target filtering.
- Added target-point smoothing to reduce box jitter.
- Added sticky target retention:
  - distance-weighted tracking bonus
  - switch margin to reduce left-right target hopping between adjacent candidates

### AutoFire changes

- `AutoFire` no longer fires from raw detections alone.
- It now prefers the current `SelectedTarget.fire_zone`.
- Added a filtered center-hit fallback so short target-selection dropouts do not cause obvious burst gaps.
- Friendly-color filtering still applies to autofire fallback.
- Added an ADS entry gate:
  - suppress autofire for `120ms` after aim starts

### Aim enhancement changes

- Added motion consistency gating to `LeadPredictor`.
- Alternating box jitter no longer immediately produces lead output.
- Existing `CatchupBoost` and `NearTargetDamping` behavior were kept.

### Vision verification

- `python -m unittest tests.test_vision_targeting tests.test_vision_enhancement tests.test_vision_runner tests.test_vision_runner_config tests.test_vision_runner_autofire_gate -v`
- `python -m py_compile main.py vision\\runner.py vision\\capture.py vision\\fastpath.py vision\\targeting.py vision\\enhancement.py tools\\export_trt.py tests\\test_vision_targeting.py tests\\test_vision_enhancement.py tests\\test_vision_runner.py tests\\test_vision_runner_config.py tests\\test_vision_runner_autofire_gate.py`

### Documentation

- Added `docs/project/VISION_OVERVIEW.md`
- Added:
  - `docs/superpowers/specs/2026-04-14-vision-detector-first-design.md`
  - `docs/superpowers/plans/2026-04-14-vision-detector-first.md`

## 2026-04-12 (mouse plugin architecture)

### Mouse controller plugin refactor

- Created `controllers/mouse/` plugin package mirroring `controllers/gamepad/`:
  - `state.py` for `MouseFrame` and `MouseOutput`
  - `plugin.py` for `MousePlugin` protocol and chain helpers
  - `ai_aim.py` for AI aim correction (pixel-space gain, deadzone, EMA smoothing, manual dampening)
  - `auto_fire.py` for pulse-fire left-click (120ms hold / 30ms release)
  - `recoil_compensation.py` for downward mouse pull during fire
- Rewrote `controllers/mouse_controller.py` as a plugin host:
  - pynput listener for physical mouse movement and right-click aiming
  - Plugin chain: build MouseFrame → apply plugins → write output via mouse_event
  - AI corrections injected as additive deltas on top of physical mouse movement
- Added `tests/mouse/` with 32 tests covering all plugins and host logic

### Playtesting fixes

- Fixed feedback loop: pynput captures synthetic mouse_event injections as "manual" movement; host now subtracts injected deltas from accumulator
- Fixed deadzone ordering: deadzone was applied to post-gain values (always below threshold); moved to raw target offset
- Replaced gamepad-style AI fade (reduce AI when user moves fast) with mouse-style manual dampening (reduce user movement when AI has target)
- Added pulse fire with configurable hold/release timing instead of continuous hold
- Added LEFTUP before every LEFTDOWN to ensure clean press edge (pynput cannot distinguish synthetic vs physical clicks)
- Tuned all parameters for 1000Hz loop (values ~100x smaller than gamepad equivalents)

### Mouse verification

- `python -m unittest discover -s tests/mouse -p "test_*.py" -v` (32 tests)
- `python -m unittest discover -s tests/gamepad -p "test_*.py" -v` (23 tests, no regressions)
- `python -m py_compile controllers/mouse_controller.py`
- `python -c "from controller import ControllerFactory; print('factory ok')"`

### Documentation

- Added `docs/project/MOUSE_OVERVIEW.md` covering architecture, plugins, tuned parameters, and implementation notes

---

## 2026-04-12

### Codex changes

- Reorganized the repository into clearer top-level areas:
  - `vision/` for the vision pipeline
  - `controllers/` for controller backends and assist logic
  - `models/` for model artifacts
  - `tools/` for standalone scripts
  - `docs/project/` for project-facing notes
- Split the old monolithic `vision.py` into focused modules:
  - `vision/runner.py`
  - `vision/capture.py`
  - `vision/targeting.py`
  - `vision/perf.py`
  - `vision/fastpath.py`
- Added a new vision-side aim enhancement pipeline in `vision/enhancement.py`.
- Implemented three bounded enhancement plugins:
  - `LeadPredictor`
  - `CatchupBoost`
  - `NearTargetDamping`
- Updated `vision/targeting.py` to return structured target data through `SelectedTarget`, while keeping `find_best_target()` as a compatibility wrapper.
- Integrated the enhancement pipeline into `vision/runner.py` so prediction state resets on stop-aim, timeout, and target loss.
- Added regression coverage in `tests/test_vision_enhancement.py` and updated existing tests/imports for the new layout.
- Added `requirements.txt` and refreshed documentation and plan/spec files for the new structure and enhancement work.

### Verification

- `python -m unittest tests.test_vision_enhancement tests.test_vision_fastpath tests.test_performance_tracker tests.test_gamepad_horizontal_assist tests.test_gamepad_aim_math -v`
- `python -m py_compile main.py controller.py vision\\__init__.py vision\\runner.py vision\\targeting.py vision\\enhancement.py vision\\capture.py vision\\perf.py vision\\fastpath.py controllers\\gamepad_controller.py controllers\\kbm_controller.py controllers\\gamepad_horizontal_assist.py`
- `python -c "import main, vision, vision.runner, vision.targeting, vision.enhancement; print('imports ok')"`

### Gamepad controller refactor

- Kept `controllers/gamepad_controller.py` as the gamepad host loop and moved the enhancement logic into `controllers/gamepad/`.
- Introduced a small gamepad plugin surface:
  - `controllers/gamepad/state.py` for `GamepadFrame` and `GamepadOutput`
  - `controllers/gamepad/plugin.py` for plugin chaining and reset flow
  - `controllers/gamepad/ai_aim.py` for the main aim-assist plugin
  - `controllers/gamepad/auto_fire.py` for configurable auto-fire output
  - `controllers/gamepad/recoil_compensation.py` for recoil pull-down
  - `controllers/gamepad/horizontal_assist.py` and `controllers/gamepad/overshoot_guard.py` for AI aim sub-behaviors
- Grouped gamepad-specific tests under `tests/gamepad/` to match the new controller layout.
- Added root startup scripts:
  - `gamepad_start.bat`
  - `mouse_start.bat`
- Added `--auto-fire-output RB|RT` so `AutoFire` can target either shoulder-button fire or trigger fire, with `gamepad_start.bat` prompting for `1 = RB` or `2 = RT`.
- Fixed virtual D-pad output compatibility by sending per-button D-pad presses instead of calling `VX360Gamepad.directional_pad()`, because that API is not available in the installed `vgamepad` build on this machine.

### Gamepad verification

- `python -m unittest tests.test_main_cli tests.test_startup_scripts tests.test_vision_runner tests.gamepad.test_gamepad_plugin_chain tests.gamepad.test_gamepad_auto_fire_plugin tests.gamepad.test_gamepad_ai_aim_plugin tests.gamepad.test_gamepad_recoil_compensation tests.gamepad.test_gamepad_controller_host tests.gamepad.test_gamepad_horizontal_assist tests.gamepad.test_gamepad_overshoot_guard tests.gamepad.test_gamepad_aim_math -v`
- `python -m py_compile main.py controller.py controllers\\gamepad_controller.py controllers\\gamepad\\__init__.py controllers\\gamepad\\ai_aim.py controllers\\gamepad\\auto_fire.py controllers\\gamepad\\horizontal_assist.py controllers\\gamepad\\overshoot_guard.py controllers\\gamepad\\plugin.py controllers\\gamepad\\recoil_compensation.py controllers\\gamepad\\state.py vision\\runner.py`
