# Causal Vector Intent Fusion Design

## Purpose

Replace the independent X/Y intent arbitration with one causal two-dimensional mixer that combines physical right-stick input and shaped AI assistance exactly once. The change targets the mixed-input defects exposed by the sustained AimLab benchmark while preserving manual escape, smooth output, ADS acquisition strength, and BodyLock follow behavior.

The first implementation stage is an analytical vector fuser. A bounded in-memory residual learner is a separately measured second stage. Multi-target selection remains outside this component.

## Current problem

The current `AxisIntentArbiter` makes independent X and Y decisions. An intervention both clears the corresponding manual-intent confidence before ADS or BodyLock computes assistance and later attenuates the physical stick with `manual_retention`, while the AI contribution remains fully applied. This turns one judgment into two control effects and can make the camera feel blocked or abruptly AI-owned.

The current stability input is also incomplete: `target_innovation_px` is hard-coded to zero at the controller boundary. The arbiter can therefore treat uncertain evidence as stable. Axis separation also cannot represent radial correction, tangential target following, or diagonal manual escape as one decision.

The deterministic 60-second baseline confirms that the dominant defect is mixed input rather than pure controller strength. Relative to pure cohorts, mixed ADS loses acquisition, tracking, and smoothness; mixed BodyLock retains most acquisitions but loses tracking quality and produces much larger post-cross error. Counterfactual episodes also show AI-helpful suppression and periods where both contributions are harmful.

## Non-goals

- Do not add weapon identifiers or a weapon database.
- Do not add vision inference or visual effects.
- Do not change recoil or autofire behavior.
- Do not move ADS/BodyLock lifecycle ownership out of `TargetCoordinator`.
- Do not let BodyLock pass through ADS Brake.
- Do not implement multi-target policy learning in this change.
- Do not persist learned policy state to disk in the first version.
- Do not add another gate after the existing axis arbiter; replace it.

## Architecture

The production path becomes:

```text
IntentFilter
    -> Tracker / TargetCoordinator
    -> ADS Acquire or BodyLock Follow controller
    -> AimAssistDynamics
    -> VectorIntentFuser
    -> ADS Brake (ADS only)
    -> Recoil
    -> final stick
```

`VectorIntentFuser` has one responsibility: select and smoothly apply a causal two-dimensional user/AI mix. It does not generate assistance, track targets, switch control modes, brake ADS, apply recoil, fire, or write logs.

The controller passes the original intent to ADS/BodyLock and Dynamics. It does not zero per-axis confidence. The fuser then applies the selected user and AI weights once to the physical right stick and shaped AI stick.

## Interface and state

The component consumes:

- physical right-stick vector `M`;
- shaped AI vector `A`;
- current `TargetPlan`, including error, error rate, causal horizon, lifecycle, motion, reliability, target ID, right-stick response, and learned left-motion response;
- previous final aim output;
- elapsed time.

It returns:

- fused two-dimensional stick;
- selected candidate;
- target and applied manual/AI weights;
- predicted costs and selection margin;
- fallback or eligibility reason.

Its complete persistent state is:

- current manual weight;
- current AI weight;
- previous fused output;
- current target ID.

No X/Y hold timers or axis-specific retention states remain.

## Candidate set

The analytical stage evaluates a fixed, versioned set:

| Candidate | Nominal output |
| --- | --- |
| Existing mix | `M + A` |
| Manual-supported | `M + 0.5A` |
| AI-supported | `0.5M + A` |
| Manual-only | `M` |
| AI-only | `A` |
| Reduced mix | `0.5M + 0.5A` |

The set is deliberately small and contains no continuous optimizer. Output remains clamped by the existing controller boundary.

The fuser may reduce both contributions through `Reduced mix` when both are predicted harmful. It does not emit a zero-output BodyLock brake. ADS capture and stopping remain the responsibility of ADS Brake.

## Causal prediction and cost

Candidates are evaluated over 40, 80, and 160 ms using only information available at the decision time. The prediction uses `TargetPlan` horizon samples when available, then its error rate and response estimates as bounded fallbacks. Left-stick movement is already represented by the learned left-motion response in the plan error rate; the fuser does not multiply `left_x` by a weapon constant.

The versioned candidate cost contains:

1. integrated predicted error across the horizons;
2. terminal residual error;
3. predicted continued push after a center crossing;
4. output magnitude and direction change from the previous fused output;
5. expected reversal burden;
6. manual-ownership loss weighted by deliberate input confidence and magnitude.

The online selector does not use hindsight data. Hindsight remains benchmark-only headroom evidence.

When candidate costs are within a fixed margin, selection prefers the previous weights, then the candidate that preserves more deliberate manual input. This prevents insignificant model noise from changing ownership.

## Direction and ownership policy

- Aligned manual and AI input retains manual input and normally permits the existing mix.
- Orthogonal input is evaluated as a complete vector so tangential user tracking can coexist with radial AI correction.
- Small or medium manual input may be attenuated when stable causal evidence predicts that it increases future error.
- A deliberate manual escape at or above the configured threshold disallows AI-owned candidates and preserves the physical escape vector.
- Low-confidence manual noise does not receive the same ownership penalty as deliberate input.
- If manual and AI contributions are both harmful, BodyLock may select the reduced mix but may not manufacture a zero-output brake.

## Reliability and fallback

The fuser cannot newly attenuate manual input when:

- there is no valid target;
- the plan contains non-finite data;
- the target ID has just changed;
- lifecycle is `Reacquiring` or `None`;
- tracker reliability is below the tested eligibility threshold;
- response confidence is insufficient for forward prediction.

In these states, manual weight returns toward `1.0` and AI weight releases smoothly. A target switch resets candidate memory but must not emit an output discontinuity. Any non-finite computed result falls back to the physical right stick and increments a debug counter.

ADS to BodyLock on the same target preserves current fusion weights. The fuser neither triggers nor delays that handoff.

## Smoothness

Candidate selection sets target manual/AI weights. Applied weights approach them over `weight_transition_ms`; final stick is not independently low-pass filtered a second time. This keeps the smoothing authority in one state and avoids recreating multiple brake/hold paths.

The first version exposes only:

```toml
[gamepad.intent_fusion]
manual_escape_threshold = 0.45
weight_transition_ms = 24
```

Cost constants, reliability thresholds, candidate scales, and selection margins remain versioned algorithm constants until benchmark evidence justifies a user-facing control.

## Configuration migration

Remove:

- `AxisIntentArbiter` production wiring and tests tied only to its retired behavior;
- per-axis intervention, wrong-way, stability, and retention output fields;
- `wrong_way_manual_preservation_floor` and its parsing/documentation;
- the controller's hard-coded `target_innovation_px` arbitration input.

Move the existing effective manual escape threshold to `[gamepad.intent_fusion]`. The migration must update checked-in configuration examples and benchmark configuration readers in the same change. Unknown retired keys follow the project's existing unknown-key policy; no permanent compatibility gate is added.

## Bounded residual learning

The second stage adds an optional `FusionResidualLearner` behind the analytical selector. It does not directly produce stick output. It learns only the bounded residual between the analytical predicted cost and the causally observed 160-500 ms outcome for a context/candidate pair.

Context is deliberately coarse:

- ADS or BodyLock mode;
- error-radius band;
- radial closing-speed band;
- target motion class;
- manual/AI angle and magnitude relationship;
- left-motion direction band;
- size and reliability band;
- response-scale/confidence band;
- selected candidate.

Outcome includes realized error area, center crossing and continued push, reversal burden, settle delay, interruption, and handoff residual. Updates occur only after the outcome window closes and only while the same target evidence remains eligible.

The learned residual:

- uses a slow exponentially weighted update with sample confidence and decay;
- is zero until a minimum sample count is reached;
- is bounded to at most 10-15% of the analytical candidate cost;
- loses confidence when response scale changes materially;
- cannot override manual escape or reliability fallback;
- remains in memory and resets on process restart;
- performs no deliberate live-game exploration.

Stage two is accepted only by comparing analytical-only and analytical-plus-learning long runs. It can be disabled independently without changing the analytical fuser.

## Multi-target extension boundary

Within-target future burden belongs to the fuser. Cross-target opportunity cost belongs to `TargetCoordinator`. This change exposes a compact `FusionDecision` and delayed `FusionOutcome` contract so a later target-selection policy can measure handoff burden, but it does not change target selection or learn a multi-target policy.

This boundary prevents input mixing and target ownership from becoming one coupled state machine.

## Diagnostics

Normal runtime logging remains quiet. Debug mode may record sampled decisions and counters for:

- selected candidate and weights;
- predicted costs and winner margin;
- fallback reason;
- manual escape;
- both-harmful reduction;
- target/mode handoff;
- learned residual and sample confidence in stage two.

The benchmark JSON records aggregate selection counts and per-episode causal regret. No per-tick production log is required.

## Verification and acceptance

Unit tests cover aligned, opposing, orthogonal, diagonal escape, both-harmful, target change, unreliable/reacquiring, non-finite data, and weight transitions.

Controller integration tests prove:

- user intent is not cleared before ADS/BodyLock computation;
- fusion is applied once;
- ADS Brake remains ADS-only;
- same-target ADS to BodyLock preserves weights;
- recoil and autofire boundaries are unchanged.

The deterministic benchmark uses the existing three seeds, pure/mixed profiles, ADS/BodyLock cohorts, and counterfactual scoring. Additional stress cases cover stronger target reversals, half-body occlusion, jump-to-fall, diagonal motion, and classic erroneous manual input.

A production candidate is rejected unless all hard gates pass relative to the current checked-in baseline:

- mixed ADS tracking improves by at least 5%;
- mixed BodyLock tracking improves by at least 8%;
- mixed 80 ms regret and future burden each fall by at least 20%;
- maximum post-cross error does not regress, with a 15% reduction target;
- pure acquisition and tracking regress by no more than 2%;
- smoothness bonus regresses by no more than 3%;
- false interruption, false stop, and handoff residual do not regress;
- deliberate manual escape is fully preserved.

Stage-two learning has a separate gate: it must outperform the accepted analytical fuser on long mixed runs without violating any pure, escape, smoothness, or lifecycle guardrail. Failing a gate keeps the analytical fuser and disables the learner; thresholds are not relaxed to force adoption.

## Delivery sequence

1. Add analytical fuser tests and benchmark experiment wiring without changing production selection.
2. Tune only versioned algorithm constants against the fixed baseline.
3. If all acceptance gates pass, replace the axis arbiter and remove retired code/configuration.
4. Re-run the full native test and pipeline contract suite plus deterministic benchmark.
5. Add the bounded in-memory learner behind an independent benchmark switch.
6. Adopt stage two only if its separate long-run gate passes.
