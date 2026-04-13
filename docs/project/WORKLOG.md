# Worklog

## 2026-04-14 (vision detector-first tuning)

### Detector-first pipeline

- Switched the default vision model path from pose to detect:
  - `models/yolo26n.engine`
  - fallback `models/yolo26n.pt`
- Moved the runtime defaults to a rectangular capture profile:
  - `896 x 512`
  - `70 FPS`
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
