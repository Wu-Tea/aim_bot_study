# Causal Response Shadow Benchmark

This benchmark evaluates a memory-only local response/delay learner and a pure short-horizon action scorer. It solves delayed-observation debt: the controller can keep issuing commands while vision is still showing an older camera state, so a locally reasonable command may create avoidable future reverse correction.

The current production controller remains the output owner. `disabled`, `shadow`, and `rollout_shadow` never pass a learned estimate or rollout label into selector, coordinator, ADS, BodyLock, fusion, AutoFire, recoil, or final output.

## Run

```powershell
& scripts/benchmarks/run-causal-response-acceptance.ps1 `
  -BuildDir native/vision_native/build `
  -Mode RolloutShadow `
  -Seeds 1337,7331,20260722
```

The script retains two byte-identical reports per seed, all M1-M10 mutation outcomes, CPU percentiles, config/revision/crop identity, and an acceptance summary under `artifacts/benchmarks/causal-response/<timestamp>-<revision>/`.

## Metric interpretation

- `top1_agreement`: causal candidate label versus the later closed-loop hindsight label.
- `causal_gain_px_ms`: error-area burden avoided relative to leaving the current 1.0 candidate unchanged. It is additive, not a capped score.
- `regret_px_ms`: remaining gap between the causal label and hindsight best label.
- `harmful_release_count`: decisions where reducing AI strength increased later error area. This must remain zero in retained synthetic acceptance.
- `single_target_aggressive_gain_px_ms`: headroom from the bounded 1.15 candidate in one strong-target/error-input episodes.
- `multi_target_aggressive_regret_px_ms`: cost of applying the same aggressive candidate under target ambiguity; the live design therefore withholds 1.15 when more than one eligible target exists.

## Status boundary

Synthetic success proves the estimator and ranking mechanism can recover known plant behavior. It does not prove a live control improvement. Until a fresh G0 session with revision/config/engine/schema/crop identity is replayed, the only allowed acceptance status is `synthetic_pass_real_replay_pending`. G4 live adjustment is not authorized by this benchmark.
