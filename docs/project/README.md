# Project Documentation

This directory contains maintained project references. Read historical
baselines, completed acceptance stages and paused research through
[the archive](../archive/README.md).

## Read First

1. [Current State](CURRENT_STATE.md) - current runtime facts, W0-W6 boundary,
   next live validation and non-regression rules.
2. [August 3 Live-Accepted Native Runtime](LIVE_ACCEPTED_RUNTIME_20260803.md) -
   protected executable/config identity, live evidence, restore set and the
   explicit statement that W5 memory is not implemented.
3. [Five-Case and Schema-13 Control Audit](FIVE_CASE_SCHEMA13_CONTROL_AUDIT_20260803.md) -
   current command-continuity defect, five marked cases, latency comparison,
   W3/W4 gates and next acceptance order.
4. [Control-Chain Jump Stability Acceptance](CONTROL_CHAIN_JUMP_STABILITY_ACCEPTANCE_20260801.md) -
   earlier Task 1-4 contracts and historical rollback evidence.
5. [ADS Long-Session Diagnosis](ADS_LONG_SESSION_DIAGNOSIS_20260801.md) -
   25.5-minute bot-log analysis separating movement/handoff timing from
   cumulative learning drift.
6. [Project Overview](PROJECT_OVERVIEW.md) - component and data-flow map.
7. [Native C++ Runtime](NATIVE_CPP_RUNTIME.md) - default runtime build, launch
   and fallback.
8. [Native Vision](NATIVE_VISION.md) - TensorRT Vision implementation and
   smoke tests.
9. [Controller Overview](CONTROLLER_OVERVIEW.md) - controller ownership and
   runtime modes.

## Runtime and Architecture

- [Gamepad Overview](GAMEPAD_OVERVIEW.md) - main hand-controller path.
- [Vision Overview](VISION_OVERVIEW.md) - shared Python/native Vision contract.
- [Mouse Overview](MOUSE_OVERVIEW.md) - secondary mouse-output path.
- [Mouse Telemetry Debugging](MOUSE_TELEMETRY_DEBUGGING.md) - mouse diagnostics.
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

If documents disagree, prefer current code and effective configuration, then
`CURRENT_STATE.md`, then the maintained domain reference. Archived records do
not override current behavior.
