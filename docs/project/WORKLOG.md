# Worklog

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
