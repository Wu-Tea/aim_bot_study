# Causal Pre-Recoil Mix Acceptance — 2026-07-26

## Outcome

The controller-rate causal mix candidate passes the V1 obsolete-input target
for ADS and BodyLock separately. It does so without changing the matched
ordinary or mixed-input guardrails.

This is a controller/tracker change, not a Vision-FPS optimization. Vision
refreshes target evidence. Between publications, a reliable same-target
`Coasting` plan, the physical right-stick input, and a fixed-size ledger of
actually delivered pre-recoil output are sufficient to continue arbitration at
controller frequency.

## What Changed

1. `PendingControlMotion` records only output that was actually delivered to
   the virtual gamepad, before recoil. Delivery failure, disabled output,
   target change, insufficient history, or time disorder invalidates it.
2. A strong manual approach can start a bounded same-target intent episode
   from reliable tracker geometry. It does not wait for every control tick to
   receive a new Vision publication.
3. `CausalMixEvaluator` uses the 40/80/120/160 ms tracker route and pending
   delivered motion. At the 80 ms planning horizon it solves the continuous
   radial manual weight needed to produce the required net motion. Tangential
   manual input remains at weight `1.0`.
4. Target change, reacquisition, reliability below `0.65`, direction change,
   no target, and non-finite input retain the existing safe exits. Recoil is
   downstream and unchanged.

## Benchmark Integrity Fix

The first V1 stationary fixture was invalid: the zero-velocity override was
accidentally located between `switch` labels, so several motion labels still
moved. The scenario test now asserts zero velocity and acceleration for every
motion label. Only artifacts using these corrected hashes are authoritative:

- `2026072601`: `7481121514708199325`
- `2026072602`: `18367094591559841943`
- `2026072603`: `17809423750831113173`

Earlier July 26 V1 artifacts with other script hashes are retained experiment
history and must not be used for the stationary-obsolete conclusion.

## Matched V1 Results

Config: `config.native.example.toml`

Config fingerprint: `12651784513756940117`

Duration: 60 seconds per seed, three seeds, ADS and BodyLock separately

Baseline mode: `vector-baseline` (same build, causal evaluator disabled)

Candidate mode: `vector`

| Cohort | Metric | Baseline | Candidate | Change |
|---|---:|---:|---:|---:|
| ADS | Maximum vertical overshoot | 34.5319 px | 6.4938 px | **-81.19%** |
| ADS | Post-cross error area | 647,804.07 px·ms | 141,249.24 px·ms | **-78.20%** |
| ADS | Wrong-way output integral | 11,553.49 stick·ms | 3,119.00 stick·ms | **-73.00%** |
| ADS | Targets acquired | 123 | 126 | +2.44% |
| ADS | Tracking points | 95,832.44 | 106,187.76 | +10.81% |
| ADS | Smooth bonus | 7,301.19 | 7,213.28 | -1.20% |
| BodyLock | Maximum vertical overshoot | 34.5229 px | 5.2867 px | **-84.69%** |
| BodyLock | Post-cross error area | 885,753.81 px·ms | 179,971.66 px·ms | **-79.68%** |
| BodyLock | Wrong-way output integral | 15,883.18 stick·ms | 4,375.96 stick·ms | **-72.45%** |
| BodyLock | Targets acquired | 174 | 174 | 0.00% |
| BodyLock | Tracking points | 144,758.48 | 167,715.53 | +15.86% |
| BodyLock | Smooth bonus | 10,512.18 | 10,398.75 | -1.08% |

The requested “reduce interference to 30% of baseline” threshold is satisfied
by all three primary debt metrics in both cohorts.

## Guardrails

Matched `pure` and `mixed` profiles used the same three seeds and config for
ADS and BodyLock. The baseline and candidate JSON metrics are byte-for-byte
equivalent at the run-metric level. The causal path does not activate for
ordinary no-manual input or the existing sub-escape mixed-input fixtures.

Residual risk remains visible in V1: direction-discontinuity counts and P95
output delta/jerk are higher than the old V1 baseline even though smooth bonus
is within 1.2%. The V1 fixture deliberately injects a 0.60–1.00 manual command
and ends it abruptly after 80–140 ms, so this is not automatically a live-feel
regression, but it must be checked in a gameplay smoke test before merging to
`dev`.

## Verification

- Focused evaluator, fuser, pending-motion, controller, scenario, simulator and
  score executables: PASS.
- Release runtime target: built successfully.
- Full registered native CTest suite: 26/26 PASS, including the new evaluator,
  fuser and delivered-motion tests.
- `git diff --check`: PASS.

## Evidence

- Baseline:
  `artifacts/benchmarks/sustained_aimlab/causal-mix-v1-stationary-baseline-20260726.json`
- Candidate:
  `artifacts/benchmarks/sustained_aimlab/causal-mix-v1-stationary-controller-rate-20260726.json`
- Guardrail baseline:
  `artifacts/benchmarks/sustained_aimlab/causal-mix-guardrail-vector-baseline-20260726.json`
- Guardrail candidate:
  `artifacts/benchmarks/sustained_aimlab/causal-mix-guardrail-controller-rate-20260726.json`
