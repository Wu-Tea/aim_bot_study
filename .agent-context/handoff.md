# Agent Handoff

Last updated: 2026-08-10
Active scope: default native C++ Vision-to-gamepad runtime after retirement of
the low-rate legacy control stack.
Staleness trigger: refresh after the first matched live validation, a new
production-chain regression, or any proposal to restore predictive authority.

## Current Objective

Validate the simplified single-owner chain in live high-rate play. Do not
resume the retired W3/W5, projection, alternate-fusion or output-carry work from
older handoffs.

## Current Production Chain

```text
fresh unique Vision result
  -> native selector
  -> VisionDeliveryGate
  -> TargetCoordinator / one TargetPlan
  -> ADS acquisition OR BodyLock follow
  -> AimDynamicsShaper
  -> AssistControlStateMachine
  -> AutoFire safety gate
  -> recoil feed-forward
  -> ViGEm
```

- No new Vision frame on a controller tick means retain the immutable plan; it
  is not a new observation and does not advance projected geometry.
- A fresh no-target result releases generic aim authority. Same-generation
  visible cue evidence is the only bounded native continuation and is aim-only.
- Accepted native sources are `observed`, `associated_weak`, `weak_observed`
  and `cue_hold`; unknown and retired projected labels fail closed.
- `AssistControlStateMachine` is the sole manual/AI authority owner. Manual is
  intent evidence, target handover stays selector/coordinator owned, and
  per-axis passthrough is allowed only when the current decision says AI is
  materially idle.
- Recoil is a later feed-forward stage, not a target or continuity owner.

## Cleanup Completed

Removed from the native runtime/build contract:

- generic hold/coast/projection and player-motion forecast bridges;
- native post-selector lead/catch-up/near-target `AimEnhancementPipeline`;
- legacy AI, ADS carry/brake, BodyLock short-plan/lifecycle controllers;
- axis/vector arbiters, benchmark mix overrides and pending-output layers;
- old tracker backends and the standalone FPS tracker package;
- W3 ego observer, W5 pending/causal/rollout/online-learning runtime code;
- dead `ControlResponseEstimator` hint plumbing and no-protocol fallback
  candidate election; selector identity now fails closed;
- duplicate AimPerf logger/config/environment aliases and obsolete telemetry;
- tests, benchmarks and CMake targets whose only purpose was retired behavior.

Root cause: locally reasonable 80-100 Hz gap compensators accumulated without
removing the previous owner. Different layers retained different freshness,
identity, previous-output and reset state, then all modified the same stick.
Observed classes included sticky handover, elastic same-direction stacking,
fresh/non-fresh rebound, delayed release and manual suppression.

## Verification

- CMake configure + full Release build: PASS.
- CTest: `43/43` PASS.
- Focused Python native boundary/perf tests: `32/32` PASS.
- Production-only sustained AimLab smoke: 12/12 combinations PASS (three
  seeds, ADS/BodyLock, strafe off/full reversal, requested Vision 180 Hz,
  36 ms short occlusion).
- The flick-handover/sticky-target native integration contract remains green.
- The flick-handover artifact manifest was replayed and re-hashed against the
  final C++ sources/binaries; complete-contract validation reports PASS with
  zero issues.
- `native_pipeline_contract.ps1` now builds the production-only sustained
  benchmark and checks the current source-age gate instead of deleted legacy
  benchmark/runtime labels.

Benchmark caution: the saved pre-cleanup artifact used the legacy benchmark
path and schema, while the new report is `production-only`. Shared metrics are
mixed (smoother output and much less stale stop carry, but higher synthetic
error/oscillation counters), so this is not a matched performance A/B and does
not replace live validation. Raw pre/post benchmark JSON remains local and is
not part of the source commit.

## Next Action

Run one matched live session with the same model, game refresh, config and
logging conditions. Check acquisition, moving follow, multi-target flick,
cue-expiry release, manual passthrough and stop behavior. Convert any reproducible
failure into a RED fixture against the current chain before changing policy.

## Files To Read First

1. [Current State](../docs/project/CURRENT_STATE.md)
2. [Cleanup record](../docs/project/LEGACY_CONTROL_STACK_CLEANUP_20260810.md)
3. [Cleanup decision](decisions/DEC-2026-08-10-001-retire-low-rate-control-stack.md)
4. [Cue/handover acceptance](../docs/project/CUE_SELECTOR_MANUAL_ACCEPTANCE_20260810.md)
5. [Compact session log](session-log.md)

## Do Not Reopen Without New Evidence

- Do not restore additive manual-plus-AI output, alternate final-output owners,
  detector projection, generic coasting, output carry/brake or retired config
  aliases.
- Do not interpret a repeated 1 kHz controller tick as a fresh Vision sample.
- Do not let cue-derived positions self-train or grant fire authority.
- Do not compare benchmark or live artifacts without matching executable,
  config, schema, scenario and logging conditions.
- Python fallback still contains historical prediction/enhancement behavior;
  do not infer that it is active in the default native runtime.
- Keep secrets, personal media paths, raw telemetry and binaries out of context
  files and ordinary source commits.
