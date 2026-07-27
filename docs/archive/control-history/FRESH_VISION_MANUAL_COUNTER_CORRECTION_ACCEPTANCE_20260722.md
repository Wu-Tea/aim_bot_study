# Fresh-Vision Manual Counter-Correction Acceptance — 2026-07-22

## Decision

Accept the BodyLock-only candidate with
`fresh_vision_wrong_way_manual_floor = 0.35`.

The policy is active only for a fresh, reliable, single-target observation and
only while the physical input has a sub-escape radial component opposite the
observed target error. Tangential manual input remains at weight `1.0`; ADS,
tracker-only/coasting evidence, multi-target frames, target changes,
reacquisition and physical manual escape retain their prior behavior.

## Reproducible comparison

- Baseline revision: `b124014`
- Seeds: `1337`, `7331`, `20260722`
- Profile/cohorts: `mixed`, `ads` and `bodylock`
- Duration: 60 seconds per seed/cohort
- Fusion: `vector`; counterfactual: `off`
- Baseline: `artifacts/benchmarks/fresh-vision-manual-constraint-20260722/baseline.json`
- Accepted candidate: `artifacts/benchmarks/fresh-vision-manual-constraint-20260722/accepted-v4.json`

Three-seed means:

| Metric | ADS baseline | ADS candidate | BodyLock baseline | BodyLock candidate |
|---|---:|---:|---:|---:|
| Additive total | 24,828.61 | 24,654.48 (-0.70%) | 87,360.51 | 87,907.58 (+0.63%) |
| Acquire points | 9,288.63 | 9,356.81 (+0.73%) | 58,000.00 | 58,000.00 |
| Tracking points | 14,867.50 | 14,648.36 (-1.47%) | 28,325.41 | 28,843.01 (+1.83%) |
| Acquired targets | 31.67 | 32.67 (+3.16%) | 58.00 | 58.00 |
| Missed targets | 61.33 | 57.33 (-6.52%) | 0.00 | 0.00 |
| Settled targets | 11.00 | 12.67 (+15.15%) | 30.67 | 33.00 (+7.61%) |
| Stall-ring ms | 6,842.00 | 7,166.00 (+4.74%) | 16,060.00 | 16,295.00 (+1.46%) |
| Overshoot area px·ms | 39,677.77 | 42,857.73 (+8.01%) | 56,889.08 | 45,394.41 (-20.21%) |
| False interruptions | 0.00 | 0.00 | 0.33 | 0.00 |

The ADS cohort includes the post-acquisition BodyLock phase, so it is not an
ADS-policy-only measurement. The dedicated unit contract confirms the ADS
wrong-way policy itself is unchanged.

## Alternatives rejected

- Floor `0.20`: stronger acquisition and BodyLock tracking, but ADS stall and
  crossing burden rose too far for the intended hand-feel risk.
- Floor `0.50`: safer but gave away most BodyLock tracking and overshoot gains.
- Hard `18 px` gate: introduced a distance discontinuity and caused a large
  overshoot regression on seed `7331`.
- Distance-interpolated floor: reduced the accepted candidate's BodyLock gain
  without improving aggregate stall.

## Residual risk and live validation

The accepted point trades a small increase in simulated stall-ring time for
more acquisitions, more settled targets, lower BodyLock overshoot burden and no
false interruption. Live validation should focus on whether the radial-only
restriction is perceptible around 10–20 px. Setting the new floor to `1.0`
disables the stronger path without changing code.
