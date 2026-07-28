# Controller-Rate Causal Mix on the Current Tracker — 2026-07-28

## Decision

Retain the benchmark implementation and evidence, but keep controller-rate
causal mix disabled in production.

The candidate still gives large gains in the stationary persistent-obsolete
manual-input fixture after the tracker velocity change. It also leaves the
ordinary pure/mixed guardrail byte-equivalent. However, combining persistent
obsolete manual input with full-reversal player strafe exposes a severe
BodyLock crossing regression. This moving-player result blocks production
activation.

## Identity

- Current base revision: `b85840c` (`motion_velocity_alpha` changed from
  `0.4` to `0.2`)
- Evaluation branch: `codex/benchmark-sync-controller-rate`
- Runtime config fingerprint: `10334765943132927346`
- Seeds: `2026072601`, `2026072602`, `2026072603`
- Duration: 60,000 ms per run
- Candidate mode: `vector`
- Matched baseline mode: `vector-baseline`
- Controller-rate causal mix is opt-in under the benchmark build and disabled
  by default for production callers. Production builds also omit the pending
  control-motion ledger and estimator, so retaining this benchmark does not add
  controller-rate history maintenance to the live loop.

The July 26 artifacts use a different config fingerprint and older benchmark
schema. Compare within each matched baseline/candidate pair; do not attribute
raw cross-revision score differences to one code change.

## Stationary Persistent-Obsolete Input

### Historical July 26 matched result

| Cohort | Tracking | Max vertical overshoot | Post-cross area | Wrong-way output | Smooth bonus |
|---|---:|---:|---:|---:|---:|
| ADS | +10.81% | -82.51% | -78.20% | -73.00% | -1.20% |
| BodyLock | +15.86% | -85.32% | -79.68% | -72.45% | -1.08% |

### Current tracker matched result

| Cohort | Tracking | Max vertical overshoot | Post-cross area | Wrong-way output | Smooth bonus |
|---|---:|---:|---:|---:|---:|
| ADS | +15.84% | -85.80% | -82.39% | -66.92% | +0.87% |
| BodyLock | +11.96% | -86.28% | -82.16% | -65.29% | -3.18% |

The primary obsolete-input reduction remains real. ADS acquired targets also
rose from 123 to 133; BodyLock acquired targets stayed at 174.

The new scorer exposes a smoothness risk that the old V1 artifact could not
attribute:

| Cohort | Residual kicks baseline -> candidate | Direction discontinuities baseline -> candidate | Mean P95 output delta baseline -> candidate |
|---|---:|---:|---:|
| ADS | 120 -> 6,339 | 123 -> 581 | 0.0386 -> 0.0971 |
| BodyLock | 192 -> 5,547 | 345 -> 955 | 0.0483 -> 0.0690 |

## Ordinary and Left-Strafe Guardrail

The 24 ordinary pure/mixed runs cover ADS and BodyLock with left strafe both
off and full reversal. All scalar run metrics are pairwise identical between
`vector-baseline` and `vector`. The strong obsolete-input path does not activate
for these existing sub-escape manual profiles.

## Persistent-Obsolete Input plus Full-Reversal Player Strafe

| Cohort | Tracking | Mean error | Overshoot area | Center crossings | Residual kicks |
|---|---:|---:|---:|---:|---:|
| ADS | +47.67% | -45.04% | -88.03% | 12 -> 16 | 110 -> 2,100 |
| BodyLock | +16.22% | -28.86% | **+639.09%** | **24 -> 155** | **225 -> 4,623** |

BodyLock tracking improves, but the controller crosses the center far more
often and accumulates 277,799.9 px·ms of overshoot area versus 37,586.7 px·ms
in the matched baseline. This is not an acceptable exchange. The candidate
solves strong stale right-stick ownership without understanding the simultaneous
POV shift caused by left-stick movement.

## Vertical Stress Diagnostic

With the current tracker:

- 240 px cooperative upward acquisition: overshoot `0 px`; this historical
  defect is no longer reproduced.
- Slide/down-forward target plus upward recoil and dropout: `69` stale-hold
  frames; the defect remains.
- Player POV jump: `224` outside-body frames; the defect remains.

The vertical test now validates scenario construction and finite metrics rather
than requiring a known defect to remain present after an improvement.

## Promotion Gate

Do not activate controller-rate causal mix in the runtime until a later
candidate:

1. retains the stationary obsolete-input gains;
2. does not increase full-strafe BodyLock overshoot area or center crossings;
3. materially reduces controller-residual kicks and P95 output delta;
4. preserves the exact ordinary pure/mixed guardrail;
5. passes the existing live micro-adjustment and hand-feel smoke test.

The next change should expose left-stick/POV motion to the same counterfactual
route evaluation. It should not add a separate permanent brake or enable the
rejected direct ego-motion injection.

## Evidence

- `artifacts/benchmarks/sustained_aimlab/controller-rate-current-tracker-20260728/current-tracker-vector-baseline.json`
- `artifacts/benchmarks/sustained_aimlab/controller-rate-current-tracker-20260728/current-tracker-controller-rate.json`
- `artifacts/benchmarks/sustained_aimlab/controller-rate-current-tracker-20260728/guardrail-strafe-vector-baseline.json`
- `artifacts/benchmarks/sustained_aimlab/controller-rate-current-tracker-20260728/guardrail-strafe-controller-rate.json`
- `artifacts/benchmarks/sustained_aimlab/controller-rate-current-tracker-20260728/obsolete-strafe-vector-baseline.json`
- `artifacts/benchmarks/sustained_aimlab/controller-rate-current-tracker-20260728/obsolete-strafe-controller-rate.json`
