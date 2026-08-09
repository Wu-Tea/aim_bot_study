# DEC-2026-08-07-001: Solve Target-First Final Output With Causal Work Accounting

Status: accepted
Date: 2026-08-07
Confirmed by: user explicitly clarified that targetX/Y correctness is the final
objective, manual input may be weakened or ignored, and requested that the
discussion be recorded
Related sessions:

- 2026-08-07 pure-AI smoothness video review, causal-memory discussion and
  target-first manual/AI clarification

Related files:

- `native/controller_native/vector_intent_fuser.cpp`
- `native/controller_native/native_gamepad_controller.cpp`
- `native/controller_native/response_model_aim_solver.cpp`
- `native/pipeline_contract/target_plan.h`
- `docs/project/CURRENT_STATE.md`

Supersedes: none
Superseded by: none
Refines:

- `DEC-2026-08-02-002-predictive-manual-ai-control-envelope.md`
- `DEC-2026-08-03-002-target-count-aware-manual-exit-authority.md`

## Context

The accepted predictive envelope already treats manual and AI as fallible
proposals to one final output, but repeated shorthand such as "AI fills the
manual remainder" and `T = M + AI` can still imply that raw manual force must be
preserved. That implication is incorrect.

The user clarified that the controller's obligation is to keep the selected
targetX/Y trajectory correct. Manual input is evidence of possible intent, not
an output contribution with guaranteed ownership. It may be reduced, cancelled
or ignored when it would deepen target error or produce excessive travel.

An August 7 target-range clip provides a useful boundary: the user confirmed
that no right-stick input was applied, so right-stick motion was pure AI
(`M = 0`). The clip still showed visible same-direction speed losses. This
removes manual irregularity and manual/AI force mixing as explanations for that
case and focuses diagnosis on target demand, output continuity, missing causal
work accounting and the game response model.

## Decision

The production controller will be designed around one target-first final output:

```text
D = desired target-relative camera motion for the selected target/aim point
P = recent final-output motion still scheduled or in flight
R = D - P
T = FinalControllerSolve(R, raw_manual, target/lifecycle evidence, limits)
```

- `T` is solved directly and remains the only physical right-stick output.
- Raw manual input is an observed intent signal and constraint input. It is not
  a protected summand and has no fixed preservation floor.
- With one credible target and strong evidence, wrong-direction or excessive
  manual input may be attenuated, cancelled, zeroed or outweighed so final `T`
  continues toward the correct targetX/Y result.
- With multiple credible targets, stable manual intent may alter target
  selection through selector/`TargetCoordinator` ownership. The fuser does not
  choose another identity or preserve raw force to simulate a handover.
- Stable target-internal micro intent may later update the desired aim-point
  offset, thereby changing `D`; it should not depend on preserving a small raw
  manual force after the target is chosen.
- If telemetry writes `T = M + AI`, this is an accounting identity only, with
  `AI = T - M`. The accounting term may oppose or exceed `M`; the runtime must
  not implement two independent forces and add them afterward.
- W5 `CausalMotionLedger` tracks the delivered final `T` through
  `scheduled -> in-flight -> realized`. Manual/AI attribution may remain as
  diagnostic tags but cannot create separate physical ledgers or owners.
- Short-term memory is causal work accounting, not a low-pass filter, generic
  output hold or assumption that a submitted ViGEm command is already realized.

## Reasons

- Correct targetX/Y behavior is the product objective; preserving a bad input
  conflicts with that objective.
- Solving one final `T` removes the recurring conceptual and implementation
  drift toward manual-plus-AI force stacking.
- Keeping target selection and aim-point intent upstream preserves meaningful
  user choice without making raw manual magnitude authoritative inside the
  within-target controller.
- Tracking final delivered work matches the single physical actuator and gives
  W5 a causal quantity that W3 background motion can later reconcile.
- The same target-first residual model can extend to future mouse-to-gamepad and
  recoil work without adding output owners.

## Rejected Alternatives

### Preserve manual first and let AI use only leftover headroom

Rejected because it guarantees that wrong-direction or excessive manual input
survives and recreates elastic-rope behavior.

### Generate independent manual and AI commands, then add or scalar-clamp them

Rejected because the decomposition does not determine correct direction or
remaining travel and creates two de facto output owners.

### Ignore manual everywhere whenever one target exists

Rejected because manual may still carry legitimate target-selection, target-
internal aim-point, escape or physical ADS intent. Those meanings belong in
their owning upstream state machines rather than raw-force preservation.

### Treat smoothing or output hold as the 150-200 ms memory system

Rejected because smoothing cannot distinguish scheduled, in-flight and realized
motion and can preserve a stale command after the target demand has changed.

## Evidence

- User-confirmed requirement: final targetX/Y correctness outranks preservation
  of manual input; the application may directly weaken or ignore manual.
- User-confirmed video boundary: no right-stick input was applied in the August
  7 smoothness clip, so visible camera motion was pure AI.
- Video-side evidence: all 1,198 decoded frames were unique; frame spacing had
  no interval above 20 ms; background-only motion around 10.3-11.4 seconds
  contained repeated same-direction V-shaped speed losses of roughly 32-54%
  that recovered within about one or two video frames.
- **Inferred/open:** that pulse shape is compatible with the proven
  fresh-clamp/non-fresh-rebound defect and missing causal work accounting, but
  detailed controller telemetry was disabled for the clip and does not identify
  the responsible tick or field.
- Repository state: W5 is not implemented; current PendingMotion and rollout
  components are diagnostics and do not reconcile final work as realized.
- User-identified refresh-rate split in session
  `20260805T194515Z_35884_1`: the game ran at 240 Hz before clock minute 49 and
  180 Hz afterward. ADS-only counter slopes measured about `135.5` and `160.5`
  submitted Vision frames/s respectively; accumulated-frame `>1` incidence
  fell from `40.5%` to `8.4%`.
- In the 180 Hz game section, W3 matcher compute was `1.98/2.21 ms` P50/P95 and
  timestamped feedback take-age was `3.15/6.20 ms` P50/P95. The worker recorded
  no pending-frame replacement in usable ADS episodes, so the matcher kept up,
  but 2.21 ms is about 40% of a 5.56 ms 180 Hz frame period.
- Code-path audit: matching runs asynchronously in `EgoMotionObserver`, but
  Vision still performs a grayscale CUDA kernel, D2H copy and
  `cudaStreamSynchronize` on its inference stream before result publication.
  Telemetry does not separately expose this serial staging cost.

## Consequences

- Continuity repair remains P0 and must preserve one target-relative final
  envelope across bounded non-fresh gaps without another hold or brake.
- Low-overhead telemetry should expose target demand `D`, pending work `P`,
  remaining `R`, raw manual `M`, final `T`, freshness, identity and lifecycle
  boundaries. Any manual/AI split is explicitly diagnostic.
- Add a pure-AI `M = 0` regression using the same target-range pattern. Score
  same-direction speed loss and rapid rebound, not only overshoot or sign flip.
- W3/W4 must establish usable realized camera motion and response timing before
  W5 gains actuation authority.
- Optical-flow/background-motion work must remain a timestamped, latest-only
  observer. The current Vision result and final `T` must never wait for realized
  feedback; W5 predicts scheduled/in-flight work immediately and reconciles a
  later W3 observation when it arrives.
- Before promoting W3/W5, instrument the grayscale staging cost and run a
  telemetry-off/on, W3-off/on whole-pipeline A/B at 180 Hz. Remove or overlap the
  main-stream D2H synchronization if it materially reduces Vision cadence or
  tail latency; isolated matcher `compute_ms` is not a sufficient budget test.
- Future mouse-to-gamepad should map mouse intent into desired camera motion and
  reuse the same target-first final solver and causal ledger.

## Review Triggers

- TargetX/Y or target identity is wrong often enough that stronger target-first
  authority locks the wrong point or person.
- Correct multi-target handover or physical ADS release becomes sticky.
- Target-internal micro aim cannot be expressed without preserving raw force.
- A continuity-fixed, instrumented pure-AI run shows stable `D/P/R/T` while
  background motion still pulses, shifting responsibility to the response model
  or game behavior.
- W4/W5 evidence requires a different pending-work horizon or representation.
