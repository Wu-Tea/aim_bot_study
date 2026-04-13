# Gamepad Manual Intent Guard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an X-axis-only manual intent guard so sustained wrong right-stick input no longer suppresses AI correction during aiming, while aligned manual input still blends normally.

**Architecture:** Introduce a focused `controllers/gamepad/manual_intent_guard.py` helper that tracks a short history of target revisions and classifies recent manual X input as aligned, opposed, or unstable. Integrate it into `controllers/gamepad/ai_aim.py` so the plugin can attenuate only conflicting manual X input and compute AI fade from the corrected manual signal instead of the raw user stick.

**Tech Stack:** Python 3, `unittest`, existing gamepad plugin pipeline

---

### Task 1: Add regression tests for manual-intent-aware blending

**Files:**
- Create: `D:\work\AI\yolo-study-001\tests\gamepad\test_gamepad_manual_intent_guard.py`
- Modify: `D:\work\AI\yolo-study-001\tests\gamepad\test_gamepad_ai_aim_plugin.py`

- [ ] **Step 1: Write the failing tests**

Add tests that prove:
- repeated stable target motion plus opposed manual X input keeps AI correction active
- repeated stable target motion plus aligned manual X input keeps normal blending
- unstable or low-confidence target history does not attenuate manual X input

- [ ] **Step 2: Run tests to verify they fail**

Run: `python -m unittest tests.gamepad.test_gamepad_ai_aim_plugin tests.gamepad.test_gamepad_manual_intent_guard -v`
Expected: FAIL because `manual_intent_guard` does not exist and the new AI-mixing expectations are not implemented

- [ ] **Step 3: Write minimal implementation**

Implement a helper with:
- config dataclass for thresholds, history, and attenuation
- `observe_target()` to track short-window X-direction stability
- `compute_adjustment()` returning attenuated manual X plus AI-fade input X
- `reset()` to clear history on target loss

- [ ] **Step 4: Run tests to verify they pass**

Run: `python -m unittest tests.gamepad.test_gamepad_ai_aim_plugin tests.gamepad.test_gamepad_manual_intent_guard -v`
Expected: PASS

### Task 2: Integrate the helper into AI aim mixing

**Files:**
- Create: `D:\work\AI\yolo-study-001\controllers\gamepad\manual_intent_guard.py`
- Modify: `D:\work\AI\yolo-study-001\controllers\gamepad\ai_aim.py`
- Modify: `D:\work\AI\yolo-study-001\controllers\gamepad\__init__.py`

- [ ] **Step 1: Wire helper lifecycle into the AI aim sub-plugin chain**

Instantiate the helper as a new default sub-plugin, feed it target observations via `observe_target()`, and let it update `AIAimContext` before stick output is combined.

- [ ] **Step 2: Apply the X-axis correction at the blend point**

Use the helper result to:
- scale conflicting manual X input instead of hard locking it
- drive `AI fade` from corrected manual X, not raw manual X
- leave Y-axis behavior unchanged

- [ ] **Step 3: Run targeted tests**

Run: `python -m unittest tests.gamepad.test_gamepad_ai_aim_plugin tests.gamepad.test_gamepad_manual_intent_guard -v`
Expected: PASS

### Task 3: Verify the full gamepad slice

**Files:**
- Verify only: existing gamepad plugin modules and tests

- [ ] **Step 1: Run broader gamepad verification**

Run: `python -m unittest tests.gamepad.test_gamepad_ai_aim_plugin tests.gamepad.test_gamepad_manual_intent_guard tests.gamepad.test_gamepad_horizontal_assist tests.gamepad.test_gamepad_overshoot_guard tests.gamepad.test_gamepad_adaptive_delta_gain -v`
Expected: PASS

- [ ] **Step 2: Run syntax verification**

Run: `python -m py_compile controllers\gamepad\manual_intent_guard.py controllers\gamepad\ai_aim.py`
Expected: no output, exit code 0
