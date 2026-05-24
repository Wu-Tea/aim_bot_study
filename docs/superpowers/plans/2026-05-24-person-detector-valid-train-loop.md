# Person Detector Valid/Train Loop Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add stable script entrypoints and a repeatable loop for COD person-detector baseline validation, conservative training, and trained-model validation.

**Architecture:** Keep Ultralytics calls inside real `.py` entrypoints so Windows multiprocessing never tries to reload `<stdin>`. Use conservative defaults (`workers=0`, `cache=false`) and a loop orchestrator that runs each valid/train step as a subprocess with logs.

**Tech Stack:** Python 3.11, Ultralytics YOLO, `unittest`, PowerShell.

---

### Task 1: Make training defaults safe

**Files:**
- Modify: `tools/train_person_detector.py`
- Test: `tests/test_person_training_tools.py`

- [x] **Step 1: Change default workers and cache**

Set default workers to `0`, default cache to `false`, and normalize cache strings so `false` becomes Python `False`.

- [x] **Step 2: Add help test**

Run:

```powershell
py -3 -B -m unittest tests.test_person_training_tools -v
```

Expected: training script help opens without starting a YOLO run.

### Task 2: Add stable validation entrypoint

**Files:**
- Create: `tools/validate_person_detector.py`
- Test: `tests/test_person_training_tools.py`

- [x] **Step 1: Add `validate_person_detector.py`**

The script calls `YOLO(...).val(...)` from a real file entrypoint and defaults to `workers=0`.

- [x] **Step 2: Add help test**

Run:

```powershell
py -3 -B -m unittest tests.test_person_training_tools -v
```

Expected: validation script help opens without importing from stdin.

### Task 3: Add valid/train loop orchestrator

**Files:**
- Create: `tools/run_person_detector_loop.py`
- Test: `tests/test_person_training_tools.py`

- [x] **Step 1: Add subprocess loop**

Run baseline valid, then repeat train and valid steps. Write logs and `summary.json` under `artifacts/person_detector_loops/<run_id>/`.

- [x] **Step 2: Add dry-run test**

Run:

```powershell
py -3 -B -m unittest tests.test_person_training_tools -v
```

Expected: dry-run completes without launching training.

### Task 4: Document the workflow

**Files:**
- Create: `docs/project/PERSON_DETECTOR_VALID_TRAIN_LOOP.md`

- [x] **Step 1: Write the operator workflow**

Document stable rules, single valid, single train, loop command, escalation path, and stuck-process checks.
