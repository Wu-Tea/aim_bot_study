# COD Y-Switch Text Recoil Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the current mixed image/text runtime weapon recognition path with a minimal Y-button-triggered text-only switch recognizer that is robust to rapid main/secondary swaps.

**Architecture:** Keep recoil profiles and the sidecar profile-selection contract, but move weapon recognition into the `gamepad` runtime. The controller listens for rising-edge `Y` presses, flips between two cached weapon slots, launches a short OCR sampling burst for the newly active slot, and writes a `current_weapon` state payload only when the latest slot/epoch still matches. This intentionally trades breadth for reliability and fits a single-user COD workflow.

**Tech Stack:** Python 3.11, `pygame`, existing DXGI full-screen capture helper, existing OCR helper, JSON identity/profile assets, `unittest`

---

## File Map

### New files

- `controllers/gamepad/weapon_switch_recognition.py`

### Existing files to modify

- `controllers/gamepad_controller.py`
- `controllers/gamepad/__init__.py`
- `tools/recoil_runtime_launcher.py`
- `tests/gamepad/test_gamepad_controller_host.py`
- `tests/recoil_collection/test_tooling.py`
- `docs/project/GAMEPAD_OVERVIEW.md`

## Task 1: Add Failing Fast-Switch And Runtime Wiring Tests

**Files:**
- Modify: `tests/gamepad/test_gamepad_controller_host.py`
- Modify: `tests/recoil_collection/test_tooling.py`

- [x] Add a failing controller-host test that proves a `Y` rising edge flips the active slot and immediately replays the cached weapon for that slot without waiting for OCR.
- [x] Add a failing controller-host test that proves a stale OCR result from an older `switch_epoch` can update the slot cache but cannot override the currently active slot state after a fast double-switch.
- [x] Add a failing launcher test that proves gamepad runtime env now includes:
  - `RECOIL_GAME`
  - `RECOIL_SIGNATURE_DIR`
  - a marker enabling controller-local switch recognition
- [x] Run:
  - `python -m unittest tests.gamepad.test_gamepad_controller_host tests.recoil_collection.test_tooling -v`
- [x] Expected: FAIL before implementation because no switch recognizer exists and launcher env lacks the new values.

## Task 2: Implement Y-Button Text Switch Recognition

**Files:**
- Create: `controllers/gamepad/weapon_switch_recognition.py`
- Modify: `controllers/gamepad/__init__.py`
- Modify: `controllers/gamepad_controller.py`

- [x] Implement a focused helper that:
  - loads `WeaponIdentityRecord` values from the existing asset directory
  - knows the configured game adapter name ROI
  - tracks `slot_a`, `slot_b`, `active_slot`, and monotonically increasing `switch_epoch`
  - writes `RecognizerState` JSON payloads to the configured runtime state path
- [x] Use a short OCR burst after each `Y` press:
  - wait a small configurable delay
  - capture a few full-screen frames
  - crop the weapon-name ROI
  - OCR each frame
  - majority-vote or frequency-rank matching `canonical_weapon_id`
- [x] On each `Y` press:
  - toggle the active slot immediately
  - if the new slot already has cached identity, write it as current weapon right away
  - launch the OCR burst for that slot and epoch
- [x] When OCR resolves:
  - always update the target slot cache
  - only publish the result as active when both slot and epoch still match the latest active state
- [x] Integrate the helper into `GamepadController.run()` using `Y` rising-edge detection from the already-read `buttons` snapshot.

## Task 3: Update Runtime Launcher For Controller-Local Recognition

**Files:**
- Modify: `tools/recoil_runtime_launcher.py`

- [x] Keep the existing `--recognizer-only` path available for manual debugging.
- [x] Change the normal gamepad runtime path so it no longer depends on a continuous recognizer subprocess to drive recoil switching.
- [x] Ensure controller env includes:
  - `RECOIL_GAME`
  - `RECOIL_SIGNATURE_DIR`
  - `RECOIL_SWITCH_RECOGNITION_MODE=y_button_text`
  - the existing profile/state paths
- [x] Keep `state_file` creation conservative and compatible with `RecoilSidecarService`.

## Task 4: Verify And Document The New Workflow

**Files:**
- Modify: `docs/project/GAMEPAD_OVERVIEW.md`

- [x] Run:
  - `python -m unittest tests.gamepad.test_gamepad_controller_host tests.gamepad.test_gamepad_recoil_compensation tests.recoil_collection.test_tooling -v`
- [x] Update the gamepad docs so the direct-use workflow now reads:
  - record text identities
  - collect recoil profiles
  - launch gamepad runtime
  - press `Y` to let the controller resolve and cache the active weapon slots
- [x] Document the initial limitations clearly:
  - single-user oriented
  - `Y`-triggered only
  - `COD20/21/22` all use text-only recognition
  - fast main/secondary swaps are protected by slot caches plus `switch_epoch`

## Validation Notes

- Local OCR ROI validation now uses the real screenshot set under `artifacts/weapon_examples/`.
- `COD20`, `COD21`, and `COD22` weapon-name ROIs were recalibrated against those images after the original lower-right crops were shown to miss the title text.
- Multi-pass OCR now runs grayscale, upscaled, CLAHE, and thresholded variants, then augments split-line results such as `KT-3` + `勇士` into joined candidates like `KT-3勇士`.
- Resolver fallback now accepts unique partial-name matches inside switch windows, which closes real examples like `最后通 -> 最后通牒` without letting non-switch `cod22` text alone override cached weapons.
- Latest verification:
  - `python -m unittest tests.weapon_identity.test_text tests.weapon_identity.test_resolver tests.gamepad.test_gamepad_controller_host tests.gamepad.test_gamepad_recoil_compensation tests.recoil_collection.test_tooling -v`
  - result: `53` passed
