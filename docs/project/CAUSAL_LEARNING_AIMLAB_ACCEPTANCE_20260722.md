# Causal Learning AimLab Acceptance — 2026-07-22

Status: **REJECTED for live output**

## Question

Does the memory-only `CausalOnlineResponseLearner`, when retained across several
AimLab-style rounds, improve the score of the current production controller?

## Experiment contract

- Production `NativeGamepadController`, current `config.toml`, causal vector fusion.
- Five 60-second rounds per run.
- Fixed seeds: `1337`, `7331`, `20260722`; each later round uses the same
  deterministic seed offset for every compared policy.
- Mixed manual-input profile.
- Separate ADS and BodyLock cohorts.
- Delayed plant response: 25, 45, and 70 ms.
- Policies: unchanged controller baseline, learner reset every round, and learner
  retained across rounds.
- Ground-truth response and delay are used only by the simulated plant and
  post-run scoring. `ground_truth_used_for_policy=false` in every learning report.
- A learned action is applied only when the existing response and delay confidence
  gates make `ShortHorizonRollout` valid. Raw manual input is preserved and the
  controller's residual assist contribution is scaled by the selected action.

The old sustained AimLab plant remains unchanged by default. Its new
`control_response_delay_ms` field defaults to zero; only this experiment enables
the delayed response queue.

## Paired score result

Each row aggregates 15 paired rounds (3 seeds × 5 rounds). Delta is retained
learner minus the identical-script baseline.

| Cohort | Plant delay | Baseline mean points | Retained delta | Delta % | Valid decisions | Changed actions | Mean final delay confidence | Final delay within ±5 ms |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| ADS | 25 ms | 21,606.062 | -8.882 | -0.0411% | 194 | 173 | 0.00466 | 6/15 |
| ADS | 45 ms | 19,492.859 | -89.705 | -0.4602% | 266 | 239 | 0.00423 | 7/15 |
| ADS | 70 ms | 17,237.032 | +6.778 | +0.0393% | 27 | 27 | 0.00208 | 6/15 |
| BodyLock | 25 ms | 87,046.847 | -3.340 | -0.0038% | 52 | 52 | 0.00000 | 4/15 |
| BodyLock | 45 ms | 85,408.937 | -19.510 | -0.0228% | 78 | 76 | 0.00168 | 6/15 |
| BodyLock | 70 ms | 83,604.373 | +20.289 | +0.0243% | 27 | 25 | 0.00141 | 5/15 |

At 45 ms, the reset controls were also run:

- ADS: reset delta `-39.375` points; retained delta `-89.705` points.
- BodyLock: reset delta `-7.090` points; retained delta `-19.510` points.

Retention therefore preserved bias rather than useful experience in this fixture.
The small positive 70 ms aggregate is not a learning curve: almost all gain was
concentrated in round 3, and rounds 4–5 returned to zero or negative deltas.

## Diagnosis

The response model does receive data: the 45 ms BodyLock runs accepted roughly
5,100 causal updates per round and ended near `0.74` right-response confidence.
The blocking signal is delay identifiability:

- final delay confidence is normally `0.000–0.019`, below the rollout gate `0.05`;
- the selected delay frequently lands at the 10 or 100 ms bank boundary;
- transient confidence crossings release a small number of actions, and those
  actions are net harmful in ADS and 45 ms BodyLock;
- more rounds do not improve the separation between correlated delay candidates.

The current regression target is observed error change. On a moving target that
change contains both camera response and target motion, while neighboring delayed
stick histories are highly correlated in closed loop. The learner can fit a
plausible response matrix but cannot reliably attribute *when* the response
occurred. This is why high response confidence does not translate into a safe
policy decision.

## Decision and next gate

Do not connect learned rollout actions to production output. Keep runtime in
disabled or shadow mode.

The next iteration must improve causal delay identifiability before changing
strength or confidence thresholds. It should be accepted only if all of the
following hold on the same matrix:

1. retained learning beats both baseline and reset controls on ADS and BodyLock;
2. the benefit persists into rounds 4–5 rather than appearing in one round;
3. selected delay is within ±5 ms on at least 80% of rounds;
4. no delay-bank boundary preference under 25/45/70 ms plants;
5. harmful action releases do not increase;
6. no plant truth, future observation, active calibration pulse, weapon identity,
   persistence, or additional vision inference enters the policy.

Raw reports are retained under
`artifacts/benchmarks/aimlab-learning-20260722-v4/`. The v2 schema records the
source revision/dirty flag, config path and fingerprint, plant delay, camera
response, profile, cohort, per-round seed, round-boundary control-history
isolation, per-round script hash and causal-policy provenance.
