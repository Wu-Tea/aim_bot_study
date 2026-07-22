# Fresh-Vision Manual Counter-Correction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Strengthen correction of clearly wrong sub-escape manual input only while recent single-target vision evidence is authoritative.

**Architecture:** Extend the existing vector fuser input with one observation-evidence pulse and keep the short authority envelope inside the fuser. Add one radial preservation-floor config value; do not add a controller, gate chain, tracker, visual pass or live learner dependency.

**Tech Stack:** C++17, current `TargetPlan`/`VectorIntentFuser`, native sustained AimLab benchmark, CMake/MSVC Release tests.

---

### Task 1: Lock the evidence contract with failing tests

**Files:**
- Modify: `native/controller_native/vector_intent_fuser_tests.cpp`
- Modify: `native/controller_native/vector_intent_fuser.h`

- [x] Add tests proving fresh single-target BodyLock evidence uses the configured radial
  floor, preserves tangent and full AI, while coasting/no-pulse produces the
  existing decision.
- [x] Add tests proving the envelope expires after 16 ms and manual escape remains
  exact physical input.
- [x] Build and run `cod_native_vector_intent_fuser_tests`; confirm the new tests
  fail because the input pulse and candidate do not exist.

### Task 2: Implement the bounded fuser policy

**Files:**
- Modify: `native/controller_native/vector_intent_fuser.h`
- Modify: `native/controller_native/vector_intent_fuser.cpp`

- [x] Add `FreshVisionCounterCorrected`, the input pulse, the configurable floor
  and a 16 ms evidence-envelope state.
- [x] Make the new candidate eligible only under the design eligibility contract.
- [x] Clear the envelope on target/lifecycle fallback and manual escape.
- [x] Run the focused fuser tests until green; retain all old candidate tests.

### Task 3: Wire production evidence and configuration

**Files:**
- Modify: `native/controller_native/native_gamepad_controller.cpp`
- Modify: `native/controller_native/runtime_config.h`
- Modify: `native/controller_native/runtime_config.cpp`
- Modify: `native/controller_native/runtime_config_tests.cpp`
- Modify: `config.native.example.toml`

- [x] Add parser tests for the new floor, including clamping and unknown-key
  behavior; verify RED.
- [x] Construct the vector fuser from `GamepadIntentConfig` and submit a pulse only
  for one fresh candidate with observed reliable geometry.
- [x] Add the example setting and run config/controller focused tests.

### Task 4: Benchmark A/B and retain the decision

**Files:**
- Create: `docs/project/FRESH_VISION_MANUAL_COUNTER_CORRECTION_ACCEPTANCE_20260722.md`
- Retain: `artifacts/benchmarks/fresh-vision-manual-constraint-20260722/`

- [x] Run the frozen `b124014` baseline and the candidate on seeds `1337`, `7331`,
  `20260722`, mixed manual profile, ADS and BodyLock, 60 seconds each.
- [x] Compare additive score, acquisition, tracking, smoothness, undertrack,
  interruption, stall ring, crossing burden, output delta and jerk.
- [x] Reject or revise if gains come from swallowing escape, tracker-only behavior
  changes, or worse discontinuity/false interruption.
- [x] Build `cod_native_runtime` and run all registered CTest tests.
- [ ] Commit the accepted implementation and evidence to `dev`; do not push.
