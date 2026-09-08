# Project Documentation

This directory contains maintained project references. Read historical
baselines, completed acceptance stages and paused research through
[the archive](../archive/README.md).

## Read First

1. [Aim Control Product Contract V1](AIM_CONTROL_PRODUCT_CONTRACT_V1_20260811.md) -
   accepted product behavior, user rights, control ownership, safety rules and
   COD-family adaptation boundary. Numeric thresholds remain to be measured.
2. [Current State](CURRENT_STATE.md) - current native production chain,
   verification status, next live validation and non-regression rules.
3. [Legacy Control Stack Cleanup](LEGACY_CONTROL_STACK_CLEANUP_20260810.md) -
   why the former 80-100 Hz compensation stack was harmful, what was removed
   and the evidence boundary of the cleanup.
4. [Cue Selector and Manual Acceptance](CUE_SELECTOR_MANUAL_ACCEPTANCE_20260810.md)
   - current selector-generation, cue, handover and manual-control contracts.
5. [August 3 Live-Accepted Native Runtime](LIVE_ACCEPTED_RUNTIME_20260803.md) -
   protected executable/config identity, live evidence, restore set and the
   explicit statement that W5 memory was not present in that historical binary.
6. [Five-Case and Schema-13 Control Audit](FIVE_CASE_SCHEMA13_CONTROL_AUDIT_20260803.md) -
   historical evidence for the command-continuity defect and W3/W4 gates that
   motivated later single-owner work.
7. [Control-Chain Jump Stability Acceptance](CONTROL_CHAIN_JUMP_STABILITY_ACCEPTANCE_20260801.md) -
   earlier Task 1-4 contracts and historical rollback evidence.
8. [ADS Long-Session Diagnosis](ADS_LONG_SESSION_DIAGNOSIS_20260801.md) -
   25.5-minute bot-log analysis separating movement/handoff timing from
   cumulative learning drift.
9. [Project Overview](PROJECT_OVERVIEW.md) - component and data-flow map.
10. [Native C++ Runtime](NATIVE_CPP_RUNTIME.md) - default runtime build, launch
   and fallback.
11. [Native Vision](NATIVE_VISION.md) - TensorRT Vision implementation and
   smoke tests.
12. [Controller Overview](CONTROLLER_OVERVIEW.md) - controller ownership and
   runtime modes.

The W3/W5 causal-memory and Gate 2.5 documents remain historical research
records. Their runtime implementations were retired on 2026-08-10 and they are
not current continuation instructions.

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

When the question is what runs today, prefer current code and effective
configuration, then `CURRENT_STATE.md`. When the question is intended product
behavior or acceptance, prefer the accepted product contract. A disagreement
between the two is an implementation gap to review, not a reason to rewrite
the requirement around the current code. Archived records do not override
either current behavior or accepted product intent.
