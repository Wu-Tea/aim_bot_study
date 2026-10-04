# Project Documentation

This directory contains maintained project references. Read historical
baselines, completed acceptance stages and paused research through
[the archive](../archive/README.md).

## Read First

1. [Current State](CURRENT_STATE.md) — source-backed project model, capability boundaries, lifecycle, current refactor progress and unknowns.
2. [Aim Control Product Contract V1](AIM_CONTROL_PRODUCT_CONTRACT_V1_20260811.md) — product intent and ownership; apply current AGENTS.md and later confirmed decisions where older thresholds differ.
3. [Desktop Assistant](DESKTOP_ASSISTANT.md) — the Python configuration and process-control entry used with the native runtime.

Historical implementation rationale is in [Legacy Control Stack Cleanup](LEGACY_CONTROL_STACK_CLEANUP_20260810.md) and [the archive](../archive/README.md). Historical executable identities, acceptance reports and old handoffs do not define current continuation work. SHA256 provenance and benchmark acceptance tooling were retired on 2026-10-04; ordinary functional tests and numerical simulations remain.

## Runtime and Architecture

- [Gamepad Overview](GAMEPAD_OVERVIEW.md) - main hand-controller path.
- [Vision Overview](VISION_OVERVIEW.md) - shared Python/native Vision contract.
- [Mouse Overview](MOUSE_OVERVIEW.md) - current mouse entry points and historical implementation.
- [Mouse Input Replacement Route](MOUSE_ROUTE_INPUT_REPLACEMENT_20260908.md) - physical capture, one virtual output owner and AI runtime integration gates.
- [Mouse Virtual Relay Desktop Acceptance](MOUSE_VIRTUAL_RELAY_DESKTOP_VERIFIED_20260908.md) - measured movement, left/right buttons, vertical wheel and normal-exit recovery.
- [Mouse Diagnostics and Recoil](MOUSE_DIAGNOSTICS_RECOIL_20260908.md) - current JSONL logs, fixed downward recoil and slow-drag regression.
- [Mouse BodyLock Range and Curves](MOUSE_BODYLOCK_RANGE_CURVE_20260908.md) - configurable assist range and acceleration/braking.
- [Mouse Target Point Control](MOUSE_TARGET_POINT_CONTROL_20260908.md) - point tolerance, velocity bounds and RED/GREEN evidence.
- [Mouse Telemetry Debugging](MOUSE_TELEMETRY_DEBUGGING.md) - historical Python CSV diagnostics.
- [Native Log Sessions](NATIVE_LOG_SESSIONS.md) - structured session logs and
  cleanup.
- [Recoil Record/Replay Validation](RECOIL_RECORD_REPLAY_VALIDATION.md) -
  recoil operation and acceptance.

## Vision Data and Training

- [Person Detector Training](PERSON_DETECTOR_TRAINING.md)
- [Person Detector Training Results](PERSON_DETECTOR_TRAINING_RESULTS.md)
- [Person Detector Valid/Train Loop](PERSON_DETECTOR_VALID_TRAIN_LOOP.md)
- [Roboflow Visible-Body Data](ROBOFLOW_VISIBLE_BODY_DATA.md)

## Engineering Method

- [Evidence-Driven Real-Time Control Optimization](EVIDENCE_DRIVEN_REALTIME_CONTROL_OPTIMIZATION.md)
- [Cross-project adoption entry](../methods/REALTIME_CONTROL_OPTIMIZATION_START_HERE.md)

## Other Indexes

- [All documentation](../README.md)
- [Benchmarks](../benchmarks/README.md)
- [Historical archive](../archive/README.md)
- `../superpowers/specs/` and `../superpowers/plans/` - historical design and
implementation records

## Repository Layout

- [Python and native repository layout, 2026-09-30](REPOSITORY_LAYOUT_20260930.md) -
  source ownership, current build paths, preserved local assets and verification.

When the question is what runs today, prefer current code and effective
configuration, then `CURRENT_STATE.md`. When the question is intended product
behavior or acceptance, prefer the accepted product contract. A disagreement
between the two is an implementation gap to review, not a reason to rewrite
the requirement around the current code. Archived records do not override
either current behavior or accepted product intent.
