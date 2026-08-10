# DEC-2026-08-10-001: Retire the Low-Rate Legacy Control Stack

Status: accepted
Date: 2026-08-10
Confirmed by: user explicitly requested reading the current Vision-to-controller
chain, deleting compensation/judgment/blocking measures encoded for the former
approximately 80 Hz Vision path, recording the stacking failure, and committing
the result
Related sessions:

- 2026-08-10 high-rate Vision-to-controller source audit and legacy cleanup

Related files:

- `docs/project/LEGACY_CONTROL_STACK_CLEANUP_20260810.md`
- `native/runtime_app/vision_service.cpp`
- `native/runtime_app/runtime_loop.cpp`
- `native/controller_native/native_gamepad_controller.cpp`
- `native/controller_native/target_coordinator.cpp`
- `native/controller_native/bodylock_follow_controller.cpp`
- `native/vision_native/CMakeLists.txt`

Supersedes: none
Superseded by: none
Related:

- `DEC-2026-08-07-001-target-first-final-output.md`
- `DEC-2026-08-03-001-protect-live-baseline-defer-w5.md`
- `DEC-2026-08-05-001-adopt-fixed-shape-cuda-graph-vision.md`

## Context

The former native Vision path commonly delivered approximately 80-100 results
per second. The controller runs at 1000 Hz, so long source gaps were treated as
a permanent product constraint. Projection, retained targets, generic coasting,
fresh/non-fresh solver branches, output carry/slew, manual-preservation layers,
ego-motion matching and pending-motion/rollout experiments accumulated around
the same physical right-stick output.

The fixed-shape CUDA Graph path and later runtime work now deliver a latest-only
stream in the approximately 128-180 Hz measured range, with a configured
160-200 Hz workload. RuntimeLoop already admits only a unique, forward-moving,
recent fresh capture and immediately calculates and submits the corresponding
output to ViGEm. Replayed detections and alternate output owners no longer fit
that contract.

## Decision

Use one high-rate, latest-only production path:

```text
fresh unique Vision result
  -> selector-owned observation / cue evidence
  -> TargetCoordinator identity and mode decision
  -> ADS acquisition or BodyLock target-relative solve
  -> AimDynamicsShaper
  -> AssistControlStateMachine (sole manual/AI authority owner)
  -> recoil feed-forward
  -> ViGEm
```

- VisionService does not synthesize a detector update by replaying the previous
  result, and it does not own a second active cadence independent of the
  configured capture cadence.
- A controller tick without a new source frame is not a missing detection and
  does not create a projected observation, a new lifecycle decision or a
  decaying authority lease. It may only continue the last source-owned plan
  until the next fresh result.
- A fresh selector result with no selected target removes generic target
  authority. Explicit same-generation yellow-cue continuation remains the only
  bounded visual continuity source.
- Generic target projection/coasting, the approximately 12.5 ms player-motion
  bridge, fresh/non-fresh alternate BodyLock solves, cue-exit output carry and
  benchmark-selectable legacy mixing are retired.
- The native post-selector `AimEnhancementPipeline` is retired. Vision publishes
  source-owned target geometry; response modeling and dynamics remain
  downstream under controller ownership.
- `AssistControlStateMachine` is the sole manual/AI authority owner. Retired
  axis/vector fusers, manual-preservation floors, legacy AI/body-lock policies
  and post-output carry/brake layers are removed rather than kept selectable.
- W3 ego-motion, pending-control/W5 ledger wiring, rollout and online-response
  experiments that never earned production authority are removed from the
  runtime/build contract. Their historical evidence remains recoverable from
  Git history, accepted/rejected decisions and regression artifacts.
- The zero-valued `ControlResponseEstimator` branch is removed with its
  left-stick/camera-response hint fields: the production adapter never supplied
  that hint and no runtime caller submitted response observations. The active
  fresh-observation `AimResponseEstimator` remains.
- Frames without the selector identity protocol fail closed. The coordinator
  no longer performs a fallback candidate election against an old local point.
- Unique-frame, source-age, control-epoch, selector-identity, hostile/cue,
  target-count, ADS lifecycle, auto-fire and output-delivery safety gates are
  retained because they validate evidence or ownership; they are not
  low-cadence interpolation.

## Reasons

- Multiple local gap compensators act from different state snapshots and
  cannot agree on target identity, freshness, prior output or reset timing.
- Retaining alternate output owners allows old behavior to re-enter through a
  benchmark flag, config key or future patch even when normal Release does not
  call it.
- At the current cadence, stale projection and delayed release can span several
  new authoritative observations, turning a former gap bridge into lag,
  overtravel or a rebound source.
- Removing rejected shadow paths makes the supported production contract
  inspectable: one source observation, one target plan and one final stick.

## Rejected Alternatives

### Keep the old paths disabled behind configuration or compile flags

Rejected because disabled code still preserves duplicate contracts, build
dependencies and misleading tests, and can silently regain authority.

### Retune every hold, decay, smoothing and prediction window for 160-200 Hz

Rejected because it keeps the same overlapping ownership model with shorter
constants. The problem is duplicated state and arbitration, not only the
numeric duration.

### Remove all freshness and age checks together with interpolation

Rejected because stale/duplicate rejection is evidence validation, not a
low-rate compensation. Late or repeated captures must still be prevented from
becoming new control input.

### Remove explicit yellow-cue continuation

Rejected because it is current visual evidence scoped to one selector
generation, not blind projection of an absent target. It remains aim-only,
bounded and unable to self-train.

## Evidence

- Repository audit: `RuntimeLoop::run_once` submits only
  `VisionSnapshotFreshness::Fresh` results accepted by `VisionDeliveryGate`,
  then calls `build_output` and ViGEm in the same controller tick.
- Repository audit: the old coordinator configuration explicitly described a
  production `80-100 Hz` stream and a historical approximately `91 Hz` velocity
  reference; player-motion forecast authority was bridged over one `12.5 ms`
  frame.
- Repository audit: generic target hold/projection was configured for `96 ms`,
  while controller state also carried slew, cue-exit release, legacy fusion,
  pending motion and shadow observer contracts.
- Historical repository evidence: fresh clamp followed by a non-fresh branch
  reproduced opposite output rebounds; separate manual-preservation and
  selection owners reproduced elastic-rope and sticky-target behavior.
- Cleanup verification: full Release build PASS, `43/43` CTest PASS and focused
  Python native-boundary/performance contracts `32/32` PASS.
- Cleanup verification: all 12 production-only sustained AimLab smoke
  combinations passed at requested 180 Hz Vision cadence and 36 ms short
  occlusion; the report explicitly identifies `control_path: production-only`.
- Measurement boundary: the staged pre-cleanup report used a different legacy
  harness/schema with remaining-work, causal-memory, tracker and
  counterfactual surfaces. Shared aggregates are mixed: output delta/jerk and
  stale stop carry improved, while synthetic mean/P95 error and oscillation
  counters worsened. This is not a matched A/B and does not establish a live
  performance gain.
- **Inferred/open:** the exact live contribution of each retired layer is not
  isolated by a single matched A/B. The deletion decision is based on the
  verified ownership conflict, direct source references and the user's explicit
  scope instruction, not on an invented per-layer performance percentage.

## Consequences

- Old benchmark/config compatibility is intentionally broken instead of
  silently accepted.
- Native `VisionResult` and Python bridge compatibility intentionally lose the
  `enhance_ms` field and projected-source interpretation.
- CTest count and available historical benchmark executables may decrease as
  tests whose only subject is a retired path are removed.
- Rollback remains available from Git history and the protected executable/
  config snapshots; removed code is not copied into a second archive tree.
- Future W3/W5 work, if revisited, must begin as a new isolated design with an
  explicit promotion gate and may not restore a parallel output owner.
- Matched live validation remains open. The architectural cleanup is accepted;
  synthetic smoke success must not be presented as no-regression evidence.

## Review Triggers

- Measured high-rate Vision cadence falls back below the accepted workload for
  sustained periods.
- A production-equivalent regression shows a required behavior that cannot be
  expressed by the retained selector, target plan and sole authority owner.
- Explicit cue evidence proves insufficient for a real occlusion class and a
  new evidence-backed continuation contract is proposed.
- A future causal-motion implementation has scheduled/in-flight/realized
  evidence strong enough for a separately reviewed shadow-to-actuation gate.
