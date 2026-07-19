# Global Aim Policy Learning Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Learn bounded causal tail value for target-selection and handoff sequences so the controller can trade small local losses for lower 500-1000 ms global correction cost.

**Architecture:** This is an independent post-fusion workstream. A bounded decision/outcome journal feeds sequential counterfactual benchmark replay; a shadow `TargetTailValueEstimator` learns prediction residuals before a capped score adjustment is allowed into `TargetCoordinator`. No live exploration or disk persistence is included.

**Tech Stack:** C++20, native target pipeline contracts, deterministic sustained AimLab sequence benchmark, bounded in-memory tables, CMake/MSBuild, JSON/PowerShell comparison tooling.

---

## Dependency

Do not start production integration until the analytical `VectorIntentFuser` has passed its acceptance plan. G0 benchmark/debug journaling may be developed earlier, but it must identify the fusion policy version in every record.

### Task G0: Bounded decision/outcome journal

**Files:**
- Create: `native/pipeline_contract/target_decision_outcome.h`
- Create: `native/controller_native/target_decision_journal.h`
- Create: `native/controller_native/target_decision_journal.cpp`
- Create: `native/controller_native/target_decision_journal_tests.cpp`
- Modify: `native/controller_native/target_coordinator.h/.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] Write failing tests for bounded ring capacity, stable target identities, 160/500/1000 ms delayed attachment, target-switch closure, and disabled-mode zero overhead state.
- [ ] Run the focused journal test and confirm RED.
- [ ] Define `TargetDecisionRecord` with candidate-set snapshot, selected target, plan state, fusion summary, intent summary, response identity, and policy version.
- [ ] Define `TargetDecisionOutcome` with acquisition/settle time, integrated error, centered time, cross/reversal/smoothness burden, loss/interruption/switch flags, and handoff residual.
- [ ] Implement a fixed-capacity ring whose normal-runtime disabled path allocates and records nothing.
- [ ] Run focused tests and commit `bench: journal target decisions and delayed outcomes`.

### Task G1: Sequential counterfactual benchmark oracle

**Files:**
- Create: `native/controller_native/global_aim_counterfactual.h/.cpp/.tests.cpp`
- Modify: `native/controller_native/sustained_aimlab_scenario.h/.cpp`
- Modify: `native/controller_native/cod_native_sustained_aimlab_benchmark.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] Write failing fixtures where keep-target wins locally but switch-target wins by 1000 ms, and where hindsight switching must lose to the causal oracle because the alternative was not yet observable.
- [ ] Run tests and confirm RED.
- [ ] Define legal sequence actions `Keep`, `Switch(target_id)`, and `Release`; reject stale/ineligible targets before search.
- [ ] Implement deterministic bounded beam replay for 500/1000/1500 ms with a versioned beam width and no runtime dependencies.
- [ ] Report local regret, causal sequence regret, hindsight headroom, next-stable-acquisition time, unnecessary switches, and opportunity cost.
- [ ] Prove fixed-seed determinism and commit `bench: replay global aim decision sequences`.

### Task G2: Shadow tail-value estimator

**Files:**
- Create: `native/controller_native/target_tail_value_estimator.h/.cpp/.tests.cpp`
- Modify: `native/controller_native/target_decision_journal.cpp`
- Modify: `native/controller_native/cod_native_sustained_aimlab_benchmark.cpp`

- [ ] Write failing tests for coarse context bucketing, minimum sample confidence, slow residual update, confidence decay after response-scale change, 15% correction cap, and process-reset empty state.
- [ ] Run tests and confirm RED.
- [ ] Implement `estimate(context, action)` returning residual cost and confidence; implement delayed `observe(prediction, outcome)` with no direct controller output.
- [ ] Run the estimator in benchmark/runtime shadow mode and serialize prediction error/calibration without changing target selection.
- [ ] Require shadow calibration to improve held-out fixed-seed prediction error before proceeding.
- [ ] Commit `feat: learn shadow target tail value`.

### Task G3: Bounded coordinator score adjustment

**Files:**
- Modify: `native/controller_native/target_coordinator.h/.cpp/.tests.cpp`
- Modify: `native/controller_native/native_gamepad_controller.cpp`
- Modify: `native/controller_native/cod_native_sustained_aimlab_benchmark.cpp`

- [ ] Write failing tests proving manual reject, escape, eligibility, lifecycle, and reliability override learned value; require insufficient confidence to reproduce the analytical result exactly.
- [ ] Run tests and confirm RED.
- [ ] Add `tail_value_adjustment` and confidence to the coordinator's existing target score, capped to 10-15% of analytical cost.
- [ ] Reuse the coordinator's existing evidence window for switch margin; add no independent timer or ownership state machine.
- [ ] Run full sequence benchmarks and reject integration unless every global acceptance gate in the design spec passes.
- [ ] Commit a gate-passing result as `feat: bound global target tail value` or leave shadow mode enabled with production selection unchanged.

### Task G4: Learning lifetime and handoff

**Files:**
- Create: `docs/project/GLOBAL_AIM_POLICY_ACCEPTANCE_20260719.md`
- Modify: `docs/benchmarks/sustained-aimlab.md`
- Modify: `.agent-context/` only after an approved SyncSet

- [ ] Run two empty-state fixed-seed full benchmarks and require identical policy decisions and metrics.
- [ ] Run warm in-memory long tests and compare against analytical-only and shadow-only identities.
- [ ] Verify restart clears learned state and response-scale changes decay confidence.
- [ ] Record every global gate, revision, seed, policy version, config fingerprint, artifact, and rejected variant.
- [ ] Propose context synchronization that preserves the next persistence decision as pending; do not add persistence, weapon IDs, or live exploration.
