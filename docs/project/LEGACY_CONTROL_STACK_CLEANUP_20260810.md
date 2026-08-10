# Legacy Control Stack Cleanup — 2026-08-10

## Purpose

This record explains why the native controller accumulated several overlapping
control paths, which parts are now unreachable or harmful, and the boundary for
removing them after the August 10 target-first runtime was accepted in live
play.

The protected pre-cleanup state was the Git index prepared on August 10. The
removal was first recorded here and in the accepted decision, then performed in
the working tree and verified before commit.

## What the product needs now

The current workload is configured for 160 Hz Vision and has prior logged/user
evidence in the roughly 160–180 Hz range under matched high-rate play. It needs
one current target plan and one final right-stick owner:

```text
latest Vision observation
  -> selector / TargetCoordinator (identity and target point)
  -> ADS acquisition or BodyLock follow (target-relative demand)
  -> AimDynamicsShaper (one bounded proposal)
  -> AssistControlStateMachine (Track / HandoverSeek / Capture / Manual)
  -> recoil feed-forward
  -> ViGEm
```

Manual input is evidence of intent. It can be passed through when AI is idle,
used to request a multi-target handover, or limited when a credible single
target is authoritative. It is not an independently protected force that must
be added to AI.

## Current production-chain audit

The call path was verified from the executable entry point rather than inferred
from class names:

1. `VisionService` polls the native engine and publishes one latest snapshot.
2. `RuntimeLoop::run_once` accepts only a unique, increasing, recent
   `Fresh` capture through `VisionDeliveryGate`.
3. The accepted snapshot is adapted once and submitted to
   `NativeGamepadController`.
4. The controller constructs one observation batch, lets `TargetCoordinator`
   publish one target plan, runs exactly one of ADS acquisition or BodyLock,
   and sends the result through the sole authority state machine.
5. Recoil is the only later right-stick feed-forward; the result is submitted
   to ViGEm in the same controller tick. Telemetry, overlay publishing and
   viewport preparation run after delivery.

This distinguishes two events that the old stack often conflated:

- **no new source frame on a 1 kHz controller tick** — keep the last immutable
  source-owned plan; do not invent a detector observation or advance projected
  geometry;
- **a fresh source frame with no selector-owned target** — remove generic aim
  authority immediately, except for explicit same-generation cue evidence.

## How the wrong path accumulated

### 1. Low Vision cadence was treated as the permanent architecture

When Vision commonly ran around 80–100 Hz, the controller saw long gaps between
fresh observations. Several local mechanisms were added to hide those gaps:

- tracker projection and retained target state;
- a post-selector native `AimEnhancementPipeline` with its own prior target and
  velocity state for lead, catch-up and near-target damping;
- ADS carry/brake and BodyLock short-plan policies;
- output smoothing, slew and fresh/non-fresh continuity state;
- background ego-motion matching (W3);
- response/pending-motion estimation and rollout experiments (W4/W5).

Each mechanism was locally reasonable under the old cadence, but the project
did not remove the previous owner when the next one was introduced.

The native enhancement layer was especially misleading: it sat after selection
but before controller ownership, changed `dx/dy` using its own temporal state,
and then handed the result to downstream response/dynamics code that also
modeled motion and convergence. It was therefore another controller hidden
inside Vision rather than source evidence.

### 2. Manual and AI became separate physical proposals

The old chain first interpreted manual input in `NativeAiAim`, then again in
`AxisIntentArbiter`, then again in `VectorIntentFuser`. Helpful-manual,
wrong-way-manual, escape, preservation floors and headroom were therefore not
one policy; they were multiple policies observing different state snapshots.

This created the behaviours repeatedly seen in live play:

- **elastic-rope swing:** manual and AI could push the same direction before a
  later limiter reacted;
- **sticky target:** one layer allowed handover while another retained or
  braked the current target;
- **lazy then sudden:** stale retained output was weak, then a fresh branch
  restored a much larger proposal;
- **manual swallowed while AI was idle:** an authority flag could suppress the
  physical stick even when the selected AI path produced no material output;
- **fresh/non-fresh rebound:** different branches updated different previous-
  output state and disagreed on the next tick.

### 3. Lifecycle state was duplicated

Selector, `TargetCoordinator`, legacy `BodylockLifecycle`, the old AI class,
the vector fuser and pending-motion code each retained some combination of
target identity, ADS epoch, prior output, manual escape or short-gap state.
Their reset boundaries were not identical. A target switch, cue continuation,
ADS transition or missing frame could reset only part of the stack and leave a
different part acting on the old target.

### 4. Shadow research became production-shaped baggage

W3 ego-motion, old `PendingControlMotion`, the later `CausalMotionLedger`,
rollout and online-response experiments were introduced as shadow research.
They either never earned actuation authority or were disabled after live tests
showed stale-position behaviour. Nevertheless, runtime callbacks, config keys,
telemetry fields, CMake sources and extensive tests remained.

Disabled code still imposed architectural cost: it enlarged the controller
state, preserved alternate meanings for `remaining_work`, kept delivery-time
callbacks alive and made benchmark-only behaviour look like a supported
production option.

### 5. Benchmarks kept obsolete production branches compilable

`COD_BENCHMARK_MIX_OVERRIDE` allowed the same controller binary to switch among
legacy axis mixing, vector mixing and target-first output. This was useful for
historical A/B work, but it prevented the obsolete branches from becoming
obviously unreachable. Tests continued to validate retired behaviour instead
of validating only the current product contract.

## Removal set

The cleanup removes code only when all of the following are true:

1. normal Release runtime cannot reach it, or the feature is disabled and has
   been explicitly rejected for actuation;
2. it duplicates a responsibility now owned by the current chain;
3. no accepted runtime behaviour depends on it;
4. its historical value is already preserved by Git history, decisions,
   regression fixtures or benchmark artifacts.

The intended removal set is:

- the benchmark-selectable legacy axis/vector fusion branches inside
  `NativeGamepadController`;
- `AxisIntentArbiter`, `VectorIntentFuser` and `CausalMixEvaluator`;
- old pre-TargetPlan controllers (`NativeAiAim`, `BodyLockMotionPolicy`,
  `NativeAimAssistDynamics`, `AdsCarryBrakePolicy` and
  `BodyLockShortPlanPolicy`) that are no longer called by Release;
- legacy `PendingControlMotion` and disabled W5 `CausalMotionLedger` runtime
  wiring, configuration and telemetry;
- W3 `EgoMotionObserver` block-matching code and its unused telemetry surface;
- native post-selector `AimEnhancementPipeline`, its pybind API, timing field
  and Python parity tests;
- VisionService last-result replay and its duplicate service-rate/config path;
- generic 96 ms target projection/hold, coasting decay, the 12.5 ms
  player-motion forecast bridge, fresh/non-fresh BodyLock divergence and the
  three-tick cue-exit carry;
- standalone rollout/online-learning experiments that are not linked into the
  accepted runtime;
- the dead `ControlResponseEstimator` path and its left-stick/camera-response
  hint fields: no production adapter supplied the hint, no caller submitted
  response observations, and the estimator therefore remained zero-valued;
- no-protocol fallback candidate election: a fresh frame without the selector
  identity protocol now fails closed instead of scoring an old point locally;
- old tracker backends and the standalone FPS tracker package;
- duplicate synchronous AimPerf logging, retired config/environment aliases
  and causal-response telemetry/session fields;
- tests and CMake targets whose only purpose is to keep those retired paths
  alive.

## Explicitly retained

This cleanup does **not** remove:

- latest-only Vision capture/inference and current selector evidence;
- hostile/corpse/cue validation and cue geometry continuity;
- `TargetCoordinator` identity and mode ownership;
- current ADS and BodyLock target-relative solvers;
- `AimResponseEstimator`, because its current observed response scale is used
  by the active solver and corresponds to the useful session learning seen in
  play;
- `AimDynamicsShaper` and `AssistControlStateMachine`;
- target-count-aware handover and per-axis manual passthrough when AI is idle;
- dynamic response-curve mapping, recoil feed-forward, autofire safety and
  ViGEm delivery;
- lightweight performance logging and the incident regression fixtures.
- the Python fallback implementation; it remains a separate compatibility/debug
  runtime and does not define native production authority.

## Verification result

Repository and contract checks passed:

- clean CMake generation and the full Release build passed;
- all remaining CTest entries passed (`43/43`); the removed entry was the dead
  `ControlResponseEstimator` unit target;
- focused native Python bridge/performance contracts passed (`32/32`);
- the flick-handover/sticky-target path remained green in the native controller
  integration target;
- its RED/GREEN artifact package was replayed against the final C++ build,
  re-hashed to the final sources and passed complete-contract validation with
  zero issues;
- retired low-rate and AimPerf config keys are diagnosed as unknown and inert;
- source/CMake audit finds one normal Release manual/AI authority owner;
- a production-only sustained AimLab smoke passed all 12 combinations (three
  seeds, ADS/BodyLock, left strafe off/full reversal, requested Vision 180 Hz,
  36 ms short occlusion).

The saved pre/post sustained reports are **not a matched performance A/B**. The
pre-cleanup binary serialized and exercised legacy benchmark surfaces including
`remaining_work`, causal memory, tracker and counterfactual state. The new
binary explicitly reports `control_path: production-only`, omits those branches
and has a different report schema. Even with the same requested scenario and
seeds, lifecycle-dependent target counts differ.

Shared aggregate metrics are recorded to prevent an optimistic retelling:

| Shared metric | Pre-cleanup legacy harness | Post-cleanup production-only | Change |
| --- | ---: | ---: | ---: |
| synthetic score | 32,467.48 | 31,423.75 | -3.2% |
| mean error | 11.50 px | 16.33 px | +42.0% |
| P95 error | 24.10 px | 34.22 px | +42.0% |
| P95 output delta | 0.0702 | 0.0519 | -26.0% |
| P95 jerk | 0.0750 | 0.0557 | -25.7% |
| stale output after stop | 27.50/run | 4.08/run | -85.2% |
| P95 reveal-to-stable | 287.16 ms | 261.50 ms | -8.9% |
| synthetic oscillation active time | 602.92 ms/run | 7,554.08 ms/run | +1,152.9% |

This supports only a mixed conclusion: subtraction removed stale carry and
reduced output step/jerk in this harness, while the simplified production path
now exposes worse synthetic tracking/error counters that the legacy harness may
have masked or influenced. Smoke PASS is structural, not proof of no
performance regression. Matched live high-rate validation remains required.

The cleanup is accepted as an architectural and contract change under the
user's explicit deletion instruction. It is not accepted as a measured live
performance improvement. Any follow-up tuning must reproduce a defect against
the current single-owner path and must not restore the removed stack wholesale.
