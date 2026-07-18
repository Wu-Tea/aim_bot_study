# Response-model aim control acceptance (2026-07-18)

## Scope

This change replaces independent ADS and BodyLock force heuristics with one
response-model solver. The solver estimates how much screen motion a delivered
right-stick command produces, predicts near-term residual error, and generates a
bounded two-dimensional correction. Target coordination owns the ADS capture
set and the ADS-to-BodyLock transition; the leaf controllers no longer decide
handoff by timeout.

The implementation deliberately keeps the vision pipeline unchanged. Learned
right-stick response is kept in memory across targets and is rejected during
ambiguous manual input, stale/coasting tracks, unreliable observations, or bad
sample cadence. Left-stick motion remains a separate target-relative estimate.

`gamepad.ads.range_px` remains readable for configuration compatibility but no
longer shapes controller force. `gamepad.ads.snap_duration_ms` is the ADS arrival
horizon (clamped to 60--350 ms). BodyLock exit tolerance is separate from the
ADS capture radius.

## Fixed benchmark protocol

- Duration: 60 seconds per run.
- Seeds: `1337`, `20260718`, `424242`.
- Profiles: ordinary target and small target.
- Cohorts: pure controller input and mixed classic user-error input.
- The BodyLock cohort starts motion and mixed-input faults only after a valid
  ADS capture, preventing ADS pre-roll from being scored as BodyLock behavior.
- Scores are additive; overshoot, under-follow, unexpected interruption, and
  transition failures remain diagnostic counters rather than a capped total.

## Aggregate result versus the frozen pre-response baseline

Percentages below aggregate the three fixed seeds.

| Cohort | Acquisition points | Tracking points | Acquisitions | Unexpected interruptions | Overshoot |
| --- | ---: | ---: | ---: | ---: | ---: |
| ADS ordinary, pure | +37.5% | +37.3% | +19.2% | -59.1% | 0 -> 0 |
| ADS ordinary, mixed | +13.6% | +36.6% | +22.7% | -37.5% | 0 -> 0 |
| ADS small, pure | +56.0% | +42.2% | +42.1% | -6.7% | 0 -> 0 |
| ADS small, mixed | +18.7% | +27.3% | +30.4% | +19.0% | 0 -> 0 |
| BodyLock ordinary, pure | n/a | +6.7% | +1.2% | -49.3% | 0 -> 0 |
| BodyLock ordinary, mixed | n/a | +4.9% | +0.6% | -0.7% | 5 -> 5 |
| BodyLock small, pure | n/a | +0.9% | +1.2% | -60.6% | 1 -> 1 |
| BodyLock small, mixed | n/a | +2.3% | +1.2% | +4.4% | 17 -> 16 |

The remaining regressions are confined to the diagnostic interruption count in
mixed small-target simulations. They do not reduce acquisition count, tracking
points, or overshoot performance in those cohorts. They should be checked
against real gameplay before any further gate is added; the current design
prefers one observable response model over another special-case control path.

## Verification

- CTest Release suite: 7/7 passed.
- Target pipeline integration tests: passed.
- Native gamepad benchmark self-test: passed.
- Native pipeline contract verification: passed.
- `git diff --check`: no whitespace errors (line-ending conversion warnings
  only).

Raw before/after JSON files are stored in
`artifacts/benchmarks/sustained_aimlab/` with the same date and profile names.
