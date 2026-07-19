# Counterfactual Conflict Benchmark Design

## Purpose

The benchmark is an offline engineering instrument for improving controller
performance and robustness. It is not a product feature and must not add work,
state, gates, or decision logic to the production runtime hot path.

The current sustained AimLab benchmark can observe manual input, requested and
shaped AI assistance, final mixed output, target ground truth, noisy vision
observations, tracker state exposed by the controller, and the resulting closed
loop error. It can count manual/AI conflict, stalls, braking, overshoot, and mode
interruptions, but it cannot yet answer the causal question: which available
input choice would have produced a better local and downstream result?

This design adds deterministic counterfactual replay so controller changes can
be optimized against both immediate fusion quality and the future correction
burden created by current actions.

## Goals

- Identify individual moments or conflict episodes where manual input, AI input,
  or their production mix caused avoidable error.
- Measure whether an action that helps immediately creates later braking,
  reversal, reacquisition, or settling cost.
- Compare the production controller with a causal oracle that has only the
  information available at that time.
- Report a hindsight lower bound without treating future knowledge as a runtime
  requirement.
- Preserve existing ADS acquisition, BodyLock tracking, braking, smoothness, and
  robustness metrics without changing their meanings.
- Produce deterministic, seed-recorded evidence that points to controller
  changes instead of encouraging benchmark-only behavior.

## Non-goals

- Add an oracle, planner, replay mechanism, or conflict scorer to the live
  runtime.
- Make the controller copyable only for benchmarking.
- Create a single full-score or weighted leaderboard number.
- Teach the runtime future target movement unavailable from current evidence.
- Solve multi-target planning in the first implementation slice.
- Replace live validation with synthetic benchmark scores.

## Chosen Approach

Use a two-stage benchmark-only pipeline:

1. Run the normal deterministic sustained AimLab closed loop once and record a
   compact trace.
2. Scan the trace for fixed scenario anchors and dynamically discovered conflict
   episodes.
3. Re-run the scenario from its beginning to reconstruct controller, tracker,
   dynamics, and mode state naturally.
4. At the selected episode, substitute a bounded candidate action policy for a
   short interval.
5. Continue the branch until its fixed horizon or decision-stable termination
   condition.
6. Compare the resulting local and downstream costs.

The normal pass provides inexpensive screening. Deterministic replay provides
causal evidence. Replaying from the beginning avoids production-facing snapshot
or clone APIs and ensures hidden controller state is restored by executing the
same history.

## Evaluation Points

### Fixed scenario anchors

Fixed anchors are generated from `ScenarioScript`, independently of controller
output. They preserve before/after comparability even when a controller change
removes or changes an observed conflict.

The first implementation supports anchors for:

- target maneuver start;
- reversal;
- stop;
- jump apex and fall transition;
- observation loss and recovery;
- ADS acquisition and settling boundary;
- ADS-to-BodyLock handoff.

Multi-target spawn, disappearance, and intended-target switch anchors are a
later extension using the same interface.

### Dynamic conflict episodes

Dynamic episodes find defects not anticipated by the script. Adjacent qualifying
frames are merged into one episode. Initial detectors cover:

- materially opposing manual and AI vectors;
- a production mix that nearly cancels a useful component;
- a 10-20 px stall ring;
- unnecessary output direction reversals;
- circle exit after entry;
- false assist stop or false BodyLock interruption;
- continued commitment to a pre-maneuver direction after contrary evidence;
- destructive same-direction stacking that raises overshoot or future burden.

The artifact records all detected episode counts, analyzed counts, and counts
skipped by the deterministic analysis budget.

## Counterfactual Branches

Each selected evaluation point runs a bounded set of branches:

- `actual_mix`: unchanged production behavior;
- `manual_only`: physical manual input without AI contribution during the
  substitution interval;
- `ai_only`: shaped AI contribution without manual input during the interval;
- `candidate_blend`: a small, fixed set of blend ratios and directional conflict
  policies used only as benchmark candidates;
- `causal_oracle`: selects a candidate using only observations and state exposed
  up to the decision time;
- `hindsight_oracle`: selects the candidate with the best realized future cost.

The candidate set is discrete and versioned in the artifact. The benchmark does
not search arbitrary controller parameters or inject the selected policy into
production code.

## Oracle Boundary

The causal oracle is the primary comparison baseline. It may use:

- observations delivered by the scenario by the decision timestamp;
- prior delivered observations;
- manual input history;
- controller component outputs and public diagnostic state available at that
  timestamp;
- a prediction model based only on that causal history.

It may not read the future `TargetScript`, future observations, future maneuver
timestamps, or realized future target state when selecting a branch. The chosen
branch is evaluated against the actual scripted future only after selection.

The hindsight oracle may inspect the realized future and chooses the best branch
after evaluation. It is reported as a theoretical lower bound and headroom
estimate, never as a production acceptance requirement.

## Replay Horizons and Stable Termination

Every evaluation point records light counterfactual results at 40, 80, and
160 ms. High-loss fixed anchors and dynamic conflict episodes continue until the
first of:

- the target completes the relevant maneuver and tracking becomes stable;
- ADS settles;
- BodyLock returns to stable tracking;
- reacquisition completes;
- the branch reaches a 500 ms hard limit.

Stable termination uses benchmark-owned, versioned conditions derived from
existing error, settle, mode, and interruption metrics. It must not call a new
production controller gate.

## Local Metrics

- `instant_progress_px`: immediate target-relative closing contribution.
- `regret_40_px_ms`, `regret_80_px_ms`, `regret_160_px_ms`: excess integrated
  error relative to the best eligible branch at each horizon.
- `manual_helped_but_suppressed_ms`: manual action was beneficial in replay but
  the production mix materially suppressed it.
- `ai_helped_but_suppressed_ms`: AI action was beneficial in replay but the
  production mix materially suppressed it.
- `both_harmful_ms`: both isolated components increased cost.
- `destructive_stack_ms`: same-direction stacking increased overshoot or future
  cost relative to the isolated components.
- `wrong_way_commit_ms`: output remained committed to an old direction after
  causal contrary evidence was available.

Local classification reports all four combinations:

- locally beneficial and globally beneficial;
- locally beneficial and globally harmful;
- locally harmful and globally beneficial;
- locally harmful and globally harmful.

## Global and Future-Burden Metrics

- `time_to_acquire_ms`;
- `time_to_stable_track_ms`;
- `error_area_px_ms`, the primary continuous trajectory cost;
- `future_burden_px_ms`, the production branch's excess downstream integrated
  error relative to the best eligible branch;
- `future_settle_delay_ms`;
- `extra_path_px`;
- `correction_reversal_count`;
- `wrong_target_dwell_ms` when multi-target scripts are added;
- `false_interrupt_ms`;
- `reacquire_delay_ms`.

The benchmark does not collapse these into a full-score metric. Optimization
primarily reduces the gap between production behavior and the causal oracle in
`error_area_px_ms`, `future_burden_px_ms`, and `future_settle_delay_ms`, subject to
non-regression of existing acquisition, tracking, interruption, braking, and
smoothness results.

## Components and Data Flow

All new components live in the sustained AimLab benchmark module:

```text
ScenarioScript
  -> normal closed-loop run
  -> TraceRecorder
  -> AnchorDetector and ConflictDetector
  -> DeterministicReplay
  -> CounterfactualRunner
  -> ConflictScorer
  -> existing result plus counterfactual_conflict JSON section
```

Responsibilities:

- `TraceRecorder` stores the minimum deterministic inputs, public component
  outputs, true plant state, and metric observations required for screening.
- `AnchorDetector` emits scenario-defined evaluation points.
- `ConflictDetector` merges qualifying trace frames into defect episodes.
- `DeterministicReplay` runs the existing controller callback from the beginning
  and verifies the pre-branch state against the reference trace.
- `CounterfactualRunner` applies a versioned candidate policy for the bounded
  substitution interval and continues the branch.
- `ConflictScorer` calculates primitive local, future-burden, and classification
  metrics without changing existing score semantics.

Target intent and script truth remain benchmark-only. They are never passed to
the controller callback.

Accurate native replay requires the substituted delivered stick to become the
controller's recorded previous-delivered output; changing only the simulator
plant would leave tracker and response-estimator feedback on the original mix.
The implementation may therefore add one mix-transform seam guarded by
`COD_BENCHMARK_MIX_OVERRIDE`. The macro is defined only for benchmark and focused
test targets. The live runtime is compiled without the declaration, member state,
or per-tick branch, so this does not expand its API or hot path.

## Performance Budget

The 60-second primary simulation still runs once per cohort, manual profile,
seed, and configuration.

- Every fixed anchor receives light 40/80/160 ms analysis.
- Only high-loss fixed anchors and dynamic conflicts receive stable-horizon
  analysis up to 500 ms.
- Adjacent dynamic frames are merged before replay.
- Each detector class has a deterministic analysis budget and severity ordering.
- Quick mode uses a smaller fixed budget for normal regression tests.
- Full mode analyzes the complete configured budget for baselines and parameter
  evaluation.
- Artifacts record mode, budgets, candidate-set version, analyzed counts, and
  skipped counts.

No benchmark budget or replay logic is compiled into or invoked by the live
runtime path.

## Artifact Compatibility

Existing benchmark fields retain their current names and meanings. New results
are emitted in an independent `counterfactual_conflict` section containing:

- schema and candidate-set versions;
- quick/full mode and replay budgets;
- fixed-anchor summaries;
- dynamic-episode summaries;
- causal and hindsight oracle comparisons;
- per-class local and future-burden aggregates;
- bounded worst-episode details sufficient for diagnosis;
- seed, script hash, configuration fingerprint, and deterministic replay checks.

Counterfactual analysis can be disabled. With it disabled, legacy core results
must remain byte-equivalent after JSON normalization.

## Error Handling

- Fail the benchmark if replay diverges from the reference trace before the
  substitution point beyond a versioned floating-point tolerance.
- Fail if a branch produces non-finite output or state.
- Fail if a causal oracle attempts to access a future-only interface.
- Fail if the hindsight result is worse than every eligible candidate branch.
- Fail on unknown candidate-set or artifact schema versions.
- Report budget exhaustion as analyzed/skipped counts, not as benchmark failure.
- Keep incomplete dynamic coverage explicit; never silently present sampled
  episodes as complete coverage.

## Verification

Focused tests must prove:

1. The same seed and config produce field-identical traces and counterfactual
   results across repeated runs.
2. Replay reaches each branch point with the same controller outputs and plant
   state as the reference pass.
3. Deterministic fixtures classify manual-correct, AI-correct, both-harmful, and
   destructive-stack cases correctly.
4. A jump/reversal fixture is classified as locally beneficial but globally
   harmful when continued upward input raises fall-phase correction cost.
5. A causal-oracle fixture cannot inspect future maneuvers and may legitimately
   lose to the hindsight oracle.
6. The hindsight oracle selects the minimum-cost eligible branch.
7. Fixed anchors remain identical across controller variants for the same script.
8. Dynamic conflict frames merge into stable episode boundaries.
9. Quick/full budgets select episodes deterministically and report skipped
   coverage.
10. Disabling analysis preserves legacy normalized JSON results.
11. Existing sustained AimLab, controller, output-validation, and native pipeline
    contract tests remain passing.

## Delivery Slices

### Slice 1: causal measurement foundation

- trace and fixed anchors;
- dynamic conflict episodes;
- deterministic replay;
- manual-only, AI-only, actual-mix, and finite candidate blends;
- 40/80/160 ms regret;
- stable-horizon future burden;
- causal/hindsight oracle boundary tests;
- single-target reverse, stop, jump/fall, occlusion, ADS settle, and handoff cases.

### Slice 2: multi-target route planning evaluation

- scenario-owned intended target ID;
- target switch and competing-target anchors;
- wrong-target dwell and switch completion metrics;
- causal route candidates and hindsight headroom.

Slice 2 reuses Slice 1 replay and scoring interfaces and does not expand the
production controller API.

## Acceptance Criteria

- The benchmark exposes at least one deterministic fixture for every initial
  conflict classification and the jump/fall future-burden case.
- It reports production-versus-causal and production-versus-hindsight gaps
  separately.
- A deliberately harmful mix scores worse than the correct isolated component
  in local regret and future burden.
- Existing production behavior and runtime performance are unchanged by the
  benchmark implementation.
- Existing benchmark core metrics do not regress merely because the new analyzer
  is enabled.
- Results remain reproducible from recorded seed, config fingerprint, script
  hash, schema version, and candidate-set version.
