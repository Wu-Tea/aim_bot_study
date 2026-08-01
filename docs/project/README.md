# Project Documentation

This directory contains maintained project references. Read historical
baselines, completed acceptance stages and paused research through
[the archive](../archive/README.md).

## Read first

1. [Current State](CURRENT_STATE.md) — current runtime facts, active directions,
   open validation and non-regression boundaries.
2. [Control-Chain Jump Stability Acceptance](CONTROL_CHAIN_JUMP_STABILITY_ACCEPTANCE_20260801.md)
   — accepted Task 1–4 contracts, contextual manual/AI arbitration, runtime
   identity and rollback evidence.
3. [ADS Long-Session Diagnosis](ADS_LONG_SESSION_DIAGNOSIS_20260801.md) —
   latest 25.5-minute bot-log analysis separating motion/handoff timing from
   learning drift.
4. [Project Overview](PROJECT_OVERVIEW.md) — component and data-flow map.
5. [Native C++ Runtime](NATIVE_CPP_RUNTIME.md) — default runtime build, launch
   and fallback.
6. [Native Vision](NATIVE_VISION.md) — TensorRT Vision implementation and
   smoke tests.
7. [Controller Overview](CONTROLLER_OVERVIEW.md) — controller ownership and
   runtime modes.

## Runtime and architecture

- [Gamepad Overview](GAMEPAD_OVERVIEW.md) — main hand-controller path.
- [Vision Overview](VISION_OVERVIEW.md) — shared Python/native Vision contract.
- [Mouse Overview](MOUSE_OVERVIEW.md) — secondary mouse-output path.
- [Mouse Telemetry Debugging](MOUSE_TELEMETRY_DEBUGGING.md) — mouse diagnostics.
- [Native Log Sessions](NATIVE_LOG_SESSIONS.md) — structured session logs and
  cleanup.
- [Recoil Record/Replay Validation](RECOIL_RECORD_REPLAY_VALIDATION.md) —
  recoil operation and acceptance.

## Vision data and training

- [Person Detector Training](PERSON_DETECTOR_TRAINING.md)
- [Person Detector Training Results](PERSON_DETECTOR_TRAINING_RESULTS.md)
- [Person Detector Valid/Train Loop](PERSON_DETECTOR_VALID_TRAIN_LOOP.md)
- [Roboflow Visible-Body Data](ROBOFLOW_VISIBLE_BODY_DATA.md)

## Engineering method

- [Evidence-Driven Real-Time Control Optimization](EVIDENCE_DRIVEN_REALTIME_CONTROL_OPTIMIZATION.md)
- [Cross-project adoption entry](../methods/REALTIME_CONTROL_OPTIMIZATION_START_HERE.md)

## Other indexes

- [All documentation](../README.md)
- [Benchmarks](../benchmarks/README.md)
- [Historical archive](../archive/README.md)
- `../superpowers/specs/` and `../superpowers/plans/` — historical design and
  implementation records

If documents disagree, prefer current code and effective configuration, then
`CURRENT_STATE.md`, then the maintained domain reference. Archived records do
not override current behavior.
