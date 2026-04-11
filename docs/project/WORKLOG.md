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
