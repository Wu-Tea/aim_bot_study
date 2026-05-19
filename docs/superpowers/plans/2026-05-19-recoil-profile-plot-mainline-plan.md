# Recoil Profile Plot Mainline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make recoil recording produce one current fitted profile per weapon/aim mode, make plot artifacts show usable recoil and anti-recoil trajectories, and make the main gamepad entry point wire recoil_app into virtual gamepad output.

**Architecture:** Raw full-magazine recordings stay in `artifacts/recoil_profiles/_episodes/` as fitting inputs. The fitted profile in `artifacts/recoil_profiles/` uses a stable `*-current` id and replaces older fits for the same `(game, weapon, stance, aim_mode)`. The main gamepad launcher enables the existing in-process recoil_app bridge when recoil runtime is requested.

**Tech Stack:** Python dataclasses and unittest, OpenCV/Numpy plot generation, Windows batch launcher, existing `RecoilRuntime`, `RecoilProfileStore`, and `GamepadController` integration.

---

### Task 1: Stable Current Profile

**Files:**
- Modify: `recoil_app/runtime.py`
- Test: `tests/recoil_app/test_runtime.py`

- [x] Write a failing test proving two recordings for one weapon leave one stable `profile-...-current.json` in the profile root while preserving `_episodes`.
- [x] Implement stable magazine profile ids and prune superseded profile/summary files for the same weapon/aim mode.
- [x] Run `py -3 -B -m unittest tests.recoil_app.test_runtime -v`.

### Task 2: Trajectory Plots

**Files:**
- Modify: `recoil_app/runtime.py`
- Test: `tests/recoil_app/test_runtime.py`

- [x] Write a failing test proving learning writes `.recoil.png` and `.anti_recoil.png` plot files instead of only a statistical chart.
- [x] Replace `_write_plot` with a final-profile trajectory renderer that draws recoil and inverse recoil paths.
- [x] Run `py -3 -B -m unittest tests.recoil_app.test_runtime -v`.

### Task 3: Mainline Recoil App Wiring

**Files:**
- Modify: `gamepad_start.bat`
- Modify: `tools/recoil_runtime_launcher.py`
- Modify: `docs/project/GAMEPAD_OVERVIEW.md`
- Test: `tests/recoil_collection/test_tooling.py`

- [x] Write failing launcher tests for `ENABLE_RECOIL_APP=1`, `RECOIL_APP_MODE=recoil`, and the default weapon identity dir `artifacts/recoil_app/weapons`.
- [x] Update `gamepad_start.bat` and launcher env defaults.
- [x] Run `py -3 -B -m unittest tests.recoil_collection.test_tooling tests.gamepad.test_gamepad_controller_host -v`.

### Task 4: Final Verification

- [x] Run recoil/gamepad focused tests together.
- [x] Run `py_compile` for touched Python files.
- [x] Run `git diff --check` for touched files.
