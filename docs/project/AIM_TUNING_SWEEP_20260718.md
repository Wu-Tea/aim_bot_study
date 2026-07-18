# ADS and BodyLock parameter sweep (2026-07-18)

## Matrix

All runs use seeds `1337`, `20260718`, and `424242`, pure and mixed manual
profiles, isolated ADS and BodyLock cohorts, and 60-second ordinary/small target
scripts.

| Name | ADS arrival | BodyLock X/Y |
| --- | ---: | ---: |
| baseline | 160 ms | 0.45 / 0.50 |
| ads120 | 120 ms | 0.45 / 0.50 |
| balanced | 120 ms | 0.50 / 0.56 |
| strong | 120 ms | 0.52 / 0.58 |

The default slowdown is edge/center `0.50 / 0.40`. The stronger COD19-like
stress environment is `0.40 / 0.30`. Script hashes match across all controller
configurations within each seed, target profile, and slowdown environment.

## ADS 120 ms versus 160 ms

| Slowdown | Target | Manual | Acquisition points | Tracking points | Acquired targets | Overshoots | P95 error |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: |
| default | ordinary | pure | +24.7% | +53.6% | 62 -> 89 | 0 -> 0 | +6.2% |
| default | ordinary | mixed | +18.3% | +34.1% | 54 -> 69 | 0 -> 0 | -17.0% |
| default | small | pure | +52.3% | +83.0% | 27 -> 34 | 0 -> 0 | -17.6% |
| default | small | mixed | +25.0% | +5.8% | 30 -> 33 | 0 -> 0 | -12.9% |
| strong | ordinary | pure | +21.2% | +45.7% | 61 -> 85 | 0 -> 0 | +5.0% |
| strong | ordinary | mixed | +15.0% | +32.8% | 53 -> 67 | 0 -> 0 | -18.1% |
| strong | small | pure | +61.5% | +59.4% | 26 -> 33 | 0 -> 0 | -20.9% |
| strong | small | mixed | +23.7% | +10.0% | 27 -> 31 | 0 -> 0 | -16.6% |

The only consistent cost is a 5--6% increase in ordinary pure-profile P95
error. It does not create an overshoot event and is outweighed by substantially
more acquisitions and tracking score in that cohort.

## BodyLock force versus 0.45 / 0.50

Tracking-point changes are aggregated across the same three seeds.

| Config | Slowdown | Ordinary pure/mixed | Small pure/mixed | Overshoot change | Pure output-delta change |
| --- | --- | ---: | ---: | ---: | ---: |
| balanced | default | +9.0% / +5.3% | +5.5% / +5.0% | none | +7.0% to +10.4% |
| balanced | strong | +7.3% / +4.0% | +5.4% / +5.9% | none | +6.7% to +10.7% |
| strong | default | +12.0% / +7.3% | +7.2% / +6.4% | none | +9.6% to +12.9% |
| strong | strong | +10.0% / +5.7% | +6.7% / +7.3% | none | +9.6% to +14.3% |

Both stronger settings remove the small pure-profile unexpected-mode time and
slightly reduce mixed-profile unexpected time. Neither increases aggregate
overshoot counts in this matrix. The `strong` setting wins the synthetic score,
but also increases output movement the most.

## Decision

`120 ms` is the supported ADS candidate: its gain survives both target sizes,
manual profiles, and slowdown environments.

For BodyLock, `0.50 / 0.56` is the safer gameplay candidate. It captures most of
the synthetic improvement while adding less output movement than `0.52 / 0.58`.
The current benchmark models classic mixed-input mistakes, not the exact
low-amplitude micro-adjustment feel inside COD19's slowdown ring. Therefore the
strong setting should not be selected solely because it has the highest score.

No user configuration was changed during this sweep. Raw ignored artifacts are
under `runs/native_perf/aim_tuning_20260718/` in the experiment worktree.
