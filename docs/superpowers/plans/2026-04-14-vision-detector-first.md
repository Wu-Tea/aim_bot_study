# Vision Detector-First Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the current pose-based vision model path with a detect-based person pipeline while preserving the existing friendly filter, target scoring, aim enhancement, and controller interface.

**Architecture:** Keep the current `runner -> targeting -> enhancement -> controller` flow, but switch model defaults and decoded outputs to detection-only boxes. Compute `aim_point` and `slow_zone` purely from bounding-box geometry so the system no longer depends on keypoints for half-body targeting.

**Tech Stack:** Python 3, `unittest`, Ultralytics YOLO, existing vision/controller pipeline

---

### Task 1: Lock the new box-only targeting behavior with tests

**Files:**
- Modify: `D:\work\AI\yolo-study-001\tests\test_vision_targeting.py`
- Test: `D:\work\AI\yolo-study-001\tests\test_vision_targeting.py`

- [ ] **Step 1: Write the failing tests**

Add tests that prove:
- a plain detection box with no keypoints still produces an upper-chest aim point
- the slow zone is still present with no keypoints
- existing friendly-color filtering above the box still rejects friendlies

- [ ] **Step 2: Run test to verify it fails**

Run: `python -m unittest tests.test_vision_targeting -v`
Expected: FAIL because the old expectations still assume pose-derived geometry

- [ ] **Step 3: Write minimal implementation**

Update `vision/targeting.py` so target point and slow zone are derived from the detection box only.

- [ ] **Step 4: Run test to verify it passes**

Run: `python -m unittest tests.test_vision_targeting -v`
Expected: PASS

### Task 2: Switch the default vision model path from pose to detect

**Files:**
- Modify: `D:\work\AI\yolo-study-001\vision\runner.py`
- Modify: `D:\work\AI\yolo-study-001\vision\fastpath.py`
- Test: `D:\work\AI\yolo-study-001\tests\test_vision_targeting.py`

- [ ] **Step 1: Write the failing test expectation**

Add or adjust tests so the vision path no longer depends on keypoints being present in `ParsedDetections`.

- [ ] **Step 2: Run test to verify it fails**

Run: `python -m unittest tests.test_vision_targeting tests.test_vision_enhancement -v`
Expected: FAIL if code still assumes pose-derived outputs for geometry

- [ ] **Step 3: Write minimal implementation**

Change:
- default model path to `models/yolo26n.pt`
- fallback model path to the same detect model path or matching detect fallback
- `model_task` to `"detect"`
- detection decoding to return boxes and confidences without keypoint assumptions

- [ ] **Step 4: Run tests to verify they pass**

Run: `python -m unittest tests.test_vision_targeting tests.test_vision_enhancement -v`
Expected: PASS

### Task 3: Verify the full vision slice

**Files:**
- Verify only: existing `vision/` modules and tests

- [ ] **Step 1: Run focused vision verification**

Run: `python -m unittest tests.test_vision_targeting tests.test_vision_enhancement -v`
Expected: PASS

- [ ] **Step 2: Run syntax verification**

Run: `python -m py_compile vision\\runner.py vision\\fastpath.py vision\\targeting.py vision\\enhancement.py`
Expected: no output, exit code 0
