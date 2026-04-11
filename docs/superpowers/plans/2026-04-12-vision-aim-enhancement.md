# Vision Aim Enhancement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a vision-side enhancement pipeline that turns YOLO base aim deltas into stronger tracking output with bounded prediction, catch-up boost, and near-target damping.

**Architecture:** Keep `TargetSelector` as the target chooser, add a new `vision.enhancement` module for stateful motion plugins, and let `vision.runner` compose selector output with the enhancement pipeline before sending deltas to controllers. The pipeline uses only recent target observations and never reads player left-stick input.

**Tech Stack:** Python 3, `unittest`, existing `vision` package

---

### Task 1: Add failing tests for the enhancement layer

**Files:**
- Create: `D:\work\AI\yolo-study-001\tests\test_vision_enhancement.py`

- [ ] **Step 1: Write failing tests**
- [ ] **Step 2: Run `python -m unittest tests.test_vision_enhancement -v` and confirm failure**
- [ ] **Step 3: Cover lead prediction, catch-up boost, near-target damping, and reset behavior**

### Task 2: Add structured target output and enhancement module

**Files:**
- Modify: `D:\work\AI\yolo-study-001\vision\targeting.py`
- Create: `D:\work\AI\yolo-study-001\vision\enhancement.py`

- [ ] **Step 1: Add a structured selected-target dataclass in `vision.targeting`**
- [ ] **Step 2: Keep `find_best_target()` as a compatibility wrapper**
- [ ] **Step 3: Implement `AimEnhancementPipeline` and the three plugins with bounded state**
- [ ] **Step 4: Run `python -m unittest tests.test_vision_enhancement -v`**

### Task 3: Integrate the enhancement pipeline into the runner

**Files:**
- Modify: `D:\work\AI\yolo-study-001\vision\runner.py`

- [ ] **Step 1: Instantiate and reset the pipeline alongside existing targeting state**
- [ ] **Step 2: Feed selected target observations into the pipeline**
- [ ] **Step 3: Send only enhanced deltas to controllers**
- [ ] **Step 4: Re-run targeted tests**

### Task 4: Verify imports and regression safety

**Files:**
- Modify: `D:\work\AI\yolo-study-001\vision\__init__.py` (only if export surface changes)

- [ ] **Step 1: Run `python -m unittest tests.test_vision_enhancement tests.test_vision_fastpath tests.test_performance_tracker tests.test_gamepad_horizontal_assist tests.test_gamepad_aim_math -v`**
- [ ] **Step 2: Run `python -m py_compile main.py controller.py vision\\__init__.py vision\\runner.py vision\\targeting.py vision\\enhancement.py vision\\capture.py vision\\perf.py vision\\fastpath.py`**
- [ ] **Step 3: If both commands pass, report the new pipeline boundaries and remaining tuning risks**
