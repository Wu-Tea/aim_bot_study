# Sustained AimLab Full-Speed Left-Strafe Acceptance — 2026-07-23

## Outcome

The sustained AimLab benchmark now measures the production controller while
full-scale horizontal left-stick movement changes the POV-target relative
position. The paired benchmark exposes a material movement-related tracking and
braking deficit in the current controller. This change does not tune or alter
production controller policy.

## Runtime Identity

- Artifact schema: `sustained-aimlab-v2`
- Revision: `7877473436699adbf176882f2399162223743cf5`
- Dirty at run start: `false`
- Config: current local `config.toml`
- Config fingerprint FNV-1a64: `10938806554613864784`
- Seeds: `2026072301`, `2026072302`, `2026072303`
- Duration: 60,000 ms per run
- Manual profiles: pure, mixed
- Cohorts: ADS, BodyLock
- Left-strafe modes: off, full reversal
- Intent fusion: vector, candidate set 4
- Slowdown edge/center: `0.50 / 0.40`
- Camera response: `500 px per stick-second`

The retained artifact contains 24 runs and 12 exact off/on pairs. Every pair
shares seed, profile, cohort, and script hash.

## Player-Motion Contract

- Requested input: `left_x = -1, 0, or +1`
- Direction changes: one reversal per completed target event
- Hidden full-speed range: `125–232 px/s`
- Hidden first-order response time constant: `100–180 ms`
- Observed sampled range: `125.128–231.609 px/s`
- Maximum realized speed: `221.020 px/s`
- Full-reversal active input across all runs: `378,050 ms`
- Full-reversal events observed: `546`
- Off-mode active input/player speed: exactly zero

ADS begins the movement clock at target spawn. The isolated BodyLock cohort
begins it only after BodyLock entry, preserving neutral warm-up semantics.

## Aggregate Paired Result

Scores are additive across the 12 runs in each mode. Mean/p95 error rows are the
mean of the 12 per-run metrics.

| Metric | No strafe | Full reversal | Change |
|---|---:|---:|---:|
| Acquisition points | 397,799.25 | 395,862.84 | -0.49% |
| Tracking points | 211,860.63 | 186,074.53 | **-12.17%** |
| Smooth bonus | 8,744.00 | 7,224.98 | **-17.37%** |
| Targets acquired | 529 | 527 | -2 |
| Targets missed | 390 | 396 | +6 |
| Mean error | 15.875 px | 17.279 px | **+8.84%** |
| Mean run-p95 error | 27.915 px | 29.236 px | +4.73% |
| Undertrack events | 444 | 472 | **+6.31%** |
| Circle exits | 264 | 429 | **+62.50%** |
| Center crossings | 218 | 311 | +42.66% |
| Overshoot area | 680,085 px·ms | 1,143,664 px·ms | **+68.16%** |
| Continued push after crossing | 42 ms | 236 ms | **+461.90%** |
| Stall-ring time | 138,106 ms | 115,397 ms | -16.44% |
| False interruption events | 0 | 0 | unchanged |
| False stop events | 0 | 0 | unchanged |

The old coarse `over_events` counter changed only from zero to one and remains a
poor discriminator. Center crossing, overshoot area, circle exits, and continued
push capture the defect.

## ADS Result

| Metric | No strafe | Full reversal | Change |
|---|---:|---:|---:|
| Acquisition points | 49,799.25 | 47,862.84 | **-3.89%** |
| Tracking points | 65,604.63 | 55,446.67 | **-15.48%** |
| Smooth bonus | 3,069.30 | 2,622.29 | -14.56% |
| Targets acquired | 181 | 179 | -2 |
| Mean error | 16.856 px | 18.377 px | **+9.02%** |
| Mean run-p95 error | 30.153 px | 31.617 px | +4.86% |
| Undertrack events | 132 | 148 | +12.12% |
| Circle exits | 88 | 135 | +53.41% |
| Overshoot area | 275,871 px·ms | 564,248 px·ms | **+104.53%** |
| Maximum post-cross error mean | 21.197 px | 37.272 px | **+75.84%** |

Pure ADS is the worst score cohort: acquisition is `-6.11%`, tracking is
`-19.13%`, mean error is `+10.38%`, and overshoot area is `+125.89%`.

## BodyLock Result

| Metric | No strafe | Full reversal | Change |
|---|---:|---:|---:|
| Tracking points | 146,256.01 | 130,627.87 | **-10.69%** |
| Smooth bonus | 5,674.70 | 4,602.69 | **-18.89%** |
| Mean error | 14.894 px | 16.180 px | **+8.64%** |
| Mean run-p95 error | 25.676 px | 26.856 px | +4.59% |
| Undertrack events | 312 | 324 | +3.85% |
| Circle exits | 176 | 294 | **+67.05%** |
| Overshoot area | 404,214 px·ms | 579,416 px·ms | **+43.34%** |
| Continued push after crossing | 32 ms | 193 ms | **+503.12%** |
| Handoff residual mean | 8.094 px | 8.105 px | +0.13% |

BodyLock entry and handoff remain stable. The loss occurs during sustained
relative-motion tracking rather than at entry.

## Interpretation

The benchmark now reproduces the missing practical condition:

1. full-speed left input is visible to the controller;
2. weapon-like mobility varies without identifying a weapon;
3. the same input moves the hidden player plant;
4. vision observes the resulting POV-target relative motion;
5. off/on results remain exactly paired.

The current controller does not falsely interrupt or stop under strafe. Its
primary deficit is insufficient or late relative-motion compensation followed
by excess center crossing and braking debt. Strafe reduces stall-ring time,
which means player movement can break the previous near-target sticky state, but
the controller converts part of that mobility into overshoot and circle exits.

The result does not yet distinguish whether the best correction belongs in
tracker relative-velocity estimation, BodyLock feed-forward, ADS stopping
prediction, or fusion authority. Those alternatives require isolated
counterfactual comparisons before any production change.

## Reproduction

```powershell
scripts\verify\run_sustained_aimlab_left_strafe.ps1 -SkipBuild
```

The script refuses to overwrite an existing retained artifact and validates:

- 24 runs;
- 12 off/on pairs;
- paired script hashes;
- zero player motion in off runs;
- full-scale left input and non-zero player speed in strafe runs.

Artifact:
`artifacts/benchmarks/sustained_aimlab/left-strafe-20260723.json`

## Limitations

- Player speed and inertia use a bounded synthetic range derived from the
  existing left-stick benchmark, not recorded weapon telemetry.
- Movement is horizontal and contains at most one reversal per target.
- Vision timing/noise remains synthetic.
- Mean run-p95 is not the p95 of all raw frames combined.
- This artifact diagnoses current behavior; it is not an authorization to tune
  production controller parameters.
