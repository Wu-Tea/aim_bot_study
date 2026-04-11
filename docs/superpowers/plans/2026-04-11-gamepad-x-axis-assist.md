# Gamepad X-Axis Assist Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a conservative X-axis-only assist enhancement for `gamepad` mode so lateral target tracking reacts earlier and catches up better without changing Y-axis feel or reading left-stick input.

**Architecture:** Implement a small pure-Python helper module that tracks horizontal screen-space error over time and computes two bounded signals: a feedforward pixel offset and a temporary X-axis force bonus. Integrate that helper into `controllers/gamepad_controller.py` after the existing target delta scaling and before X-axis stick clamping so current soft deadzone, user-input fade, and recoil compensation remain intact.

**Tech Stack:** Python 3, `unittest`, existing `pygame`/`vgamepad` controller loop

---

### Task 1: Add a testable X-axis assist helper

**Files:**
- Create: `D:\work\AI\yolo-study-001\gamepad_horizontal_assist.py`
- Test: `D:\work\AI\yolo-study-001\tests\test_gamepad_horizontal_assist.py`

- [ ] **Step 1: Write the failing test**

Create tests that assert:
- sustained same-direction horizontal error growth produces a positive feedforward offset
- repeated non-converging error produces a positive X-axis catch-up bonus
- convergence, sign flips, and opposing manual input suppress the enhancement

- [ ] **Step 2: Run test to verify it fails**

Run: `python -m unittest tests.test_gamepad_horizontal_assist -v`
Expected: FAIL because `gamepad_horizontal_assist.py` does not exist yet

- [ ] **Step 3: Write minimal implementation**

Implement a helper with:
- config dataclass for thresholds and limits
- `observe_target()` to track filtered X velocity and catch-up state
- `compute_adjustment()` to return `(feedforward_dx, x_force_bonus)`
- `reset()` to clear all enhancement state

- [ ] **Step 4: Run test to verify it passes**

Run: `python -m unittest tests.test_gamepad_horizontal_assist -v`
Expected: PASS

### Task 2: Integrate the helper into `gamepad_controller`

**Files:**
- Modify: `D:\work\AI\yolo-study-001\controllers\gamepad_controller.py`
- Test: `D:\work\AI\yolo-study-001\tests\test_gamepad_horizontal_assist.py`

- [ ] **Step 1: Wire helper lifecycle into controller state**

Instantiate the helper in `__init__()`, feed target updates from `update()`, and clear helper state from `reset()`.

- [ ] **Step 2: Apply only X-axis enhancement in the AI blend**

In the existing AI aim block:
- keep Y-axis behavior unchanged
- compute `assist_dx = target_dx + feedforward_dx`
- allow only X-axis clamp limit to grow by the bounded temporary bonus
- preserve existing soft deadzone, smoothing, user-input fade, and recoil compensation

- [ ] **Step 3: Run targeted tests**

Run: `python -m unittest tests.test_gamepad_horizontal_assist -v`
Expected: PASS

- [ ] **Step 4: Run syntax verification**

Run: `python -m py_compile gamepad_horizontal_assist.py controllers\gamepad_controller.py`
Expected: no output, exit code 0

### Task 3: Verify behavior and document outcome

**Files:**
- Modify: `D:\work\AI\yolo-study-001\TRACKING.md` (only if needed for note)

- [ ] **Step 1: Re-check boundaries**

Confirm the implementation still satisfies:
- X axis only
- no left-stick compensation
- enhancement is bounded and resets on target loss / reset

- [ ] **Step 2: Capture verification evidence**

Run:
- `python -m unittest tests.test_gamepad_horizontal_assist -v`
- `python -m py_compile gamepad_horizontal_assist.py controllers\gamepad_controller.py`

Expected: all tests pass, compile check passes
