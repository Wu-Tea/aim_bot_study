# Current State

**Last reviewed:** 2026-08-10
**Scope:** default native C++ gamepad runtime after the low-rate control-stack cleanup

## Default Runtime

The default gamepad launch path is fully native:

```text
scripts/launch/gamepad_start.bat
  -> scripts/launch/gamepad_native_cpp_start.bat
  -> native/vision_native/build/Release/cod_native_runtime.exe
```

Python remains available for fallback gameplay, mouse/KBM modes, training,
export and debugging. Its older prediction/enhancement policies are not part of
the default native gamepad contract.

The example production Vision contract is a `640x512` centered capture,
isotropic resize to `480x384`, and `models/best_480x384.engine`. The controller
runs at 1000 Hz; the configured active Vision cadence is 160 Hz. Historical
measurements must keep workload and telemetry conditions attached to any rate
claim.

## Current Vision-to-Controller Chain

```text
fresh unique native Vision result
  -> native selector (observed / weak observed / same-generation cue)
  -> VisionDeliveryGate (unique, increasing, recent capture)
  -> TargetCoordinator (identity, lifecycle and one TargetPlan)
  -> ADS acquisition OR BodyLock follow
  -> AimDynamicsShaper
  -> AssistControlStateMachine (sole manual/AI authority owner)
  -> AutoFire safety gate
  -> recoil feed-forward
  -> ViGEm
```

The important timing distinction is:

- A 1 kHz controller tick with no new Vision frame retains the immutable
  source-owned plan. It does not synthesize a detector observation, move the
  target by projection, or renew authority.
- A fresh Vision frame with no selected target removes generic target
  authority immediately. Only explicit cue pixels tied to the current selector
  generation can provide bounded aim-only continuity.

Current native source values are `observed`, `associated_weak`,
`weak_observed`, `cue_hold`, and no target. Unknown or retired
`predicted/projected` labels fail closed. Fire authority remains direct-observed
only.

## Ownership Boundaries

- `TargetCoordinator` owns persistent target identity, ADS epoch, lifecycle and
  mode selection.
- ADS and BodyLock each produce one target-relative AI proposal; they do not
  run in parallel.
- `AimDynamicsShaper` shapes that proposal once.
- `AssistControlStateMachine` is the only manual/AI authority owner. It owns
  Track, HandoverSeek, Capture and Manual transitions, including multi-target
  flick handover and per-axis passthrough when AI is materially idle.
- Manual input is intent evidence, not an additive force that must survive AI.
  Helpful single-target input may alter the target-relative proposal within the
  explicit policy, while handover remains selector/coordinator owned.
- AutoFire owns only synthetic fire. Physical fire remains unconditional
  passthrough.
- Recoil is the only later right-stick feed-forward stage. It must not become a
  second target selector or generic output-continuity owner.

## Retired Low-Rate Stack

The August 10 cleanup removed the overlapping mechanisms accumulated when
Vision commonly ran around 80-100 Hz:

- generic target hold, coast, detector projection and player-motion forecast;
- the native post-selector `AimEnhancementPipeline` lead/catch-up/damping pass;
- legacy `NativeAiAim`, ADS carry/brake and short-plan BodyLock policies;
- axis/vector intent arbiters, alternate mix modes, pending output and duplicate
  output-validation layers;
- W3 ego-motion, W5 causal/pending/rollout/online-learning runtime surfaces;
- old tracker implementations and the standalone FPS tracker package;
- duplicate AimPerf logging, replay cadence, retired config aliases, telemetry
  fields and tests that existed only to keep these branches compilable.

The failure mode was ownership stacking: several layers retained different
target identities, freshness clocks, prior outputs and reset rules, then each
modified the same right-stick command. This produced sticky handover,
fresh/non-fresh rebound, delayed release, same-direction amplification and
manual suppression. Shortening every timeout would not solve the duplicated
state owners.

Full rationale and the exact removal boundary are in
[Legacy Control Stack Cleanup](LEGACY_CONTROL_STACK_CLEANUP_20260810.md) and
[DEC-2026-08-10-001](../../.agent-context/decisions/DEC-2026-08-10-001-retire-low-rate-control-stack.md).

## Telemetry

Two separate current facilities remain:

- `[runtime.performance]` is the lightweight windowed performance summary.
- `[runtime.telemetry]` is the bounded structured diagnostic stream. Its
  `enabled` flag and `directory` are the sole detailed-telemetry controls.

The synchronous `AimPerfFileLogger`, the `VISION_AIM_PERF_*` environment
aliases and the causal-response journal schema were retired. Detailed telemetry
must remain disabled for matched throughput measurements unless both cohorts
use identical logging conditions.

## Verification on 2026-08-10

- clean CMake generation and full Release build: PASS;
- CTest: `43/43` PASS;
- focused Python native-boundary/performance contracts: `32/32` PASS;
- production-only sustained AimLab smoke: 12/12 seed/cohort/strafe runs PASS at
  180 Hz requested Vision cadence and 36 ms short occlusion;
- flick-handover/sticky-target integration remains green inside the native
  controller test target.

The sustained benchmark is not a matched performance A/B. The pre-cleanup
report used a legacy harness exposing `remaining_work`, causal memory, tracker
and counterfactual branches; the post-cleanup report is explicitly
`control_path: production-only` and has a smaller schema. Shared aggregates are
mixed: P95 output delta and jerk fell about 26%, stale output after stop fell
about 85%, and reveal-to-stable P95 improved about 9%, while mean/P95 error rose
about 42% and synthetic oscillation/kick counters rose substantially. These
numbers must not be presented as proof of either a live performance gain or a
matched regression. A same-build-condition live A/B is still required.

## Next Validation

1. Run a matched live high-rate session with identical model, game cadence,
   config and logging conditions.
2. Verify acquisition, moving follow, multi-target flick handover, cue release,
   manual passthrough and stop behavior from current telemetry only.
3. If a current-path defect is reproduced, create a RED fixture against the
   single-owner chain before changing policy.
4. Do not restore a retired hold, projection, fuser, learner or output owner as
   a quick fix. Any future causal work starts as a new isolated shadow design
   with an explicit promotion gate.

Historical W3/W5 and pre-cleanup acceptance documents remain in Git for
provenance. They no longer describe active runtime work.
