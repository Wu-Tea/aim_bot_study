# Vision cadence audit: current ~91Hz vs 200Hz

Date: 2026-07-28  
Controller revision: `21b0a7e` (`dev`)  
Controller: current production vector intent fusion  
Seeds: `2026072801` through `2026072805`  
Duration: 60 seconds per run  
Profile: mixed manual input  
Cohorts: ADS and BodyLock  
Player motion: no strafe and full reversal

## Benchmark contract

The historical sustained benchmark samples Vision every random 10-12ms
(about 91Hz). `--vision-hz 200` uses an exact 5ms interval.

Observation noise uses a target-local RNG, so changing Vision cadence cannot
silently change later target positions, velocities, maneuvers, acquisition
deadlines or left-stick scripts. A scenario contract test verifies this.

## Raw 200Hz with unchanged per-frame tracker alpha

Keeping `motion_velocity_alpha=0.2` while doubling observation frequency makes
the result substantially worse:

| Cohort | Tracking | Overshoot area | Continued push | Mean error |
|---|---:|---:|---:|---:|
| ADS, no strafe | -10.97% | +195.76% | 0 -> 12ms | +19.66% |
| ADS, full reversal | -6.84% | +32.63% | +231.37% | +7.97% |
| BodyLock, no strafe | -17.48% | +18.65% | +129.63% | +22.13% |
| BodyLock, full reversal | -12.96% | +21.73% | +135.16% | +11.01% |

Cause supported by code inspection: velocity is estimated from position
innovation divided by observation interval, then filtered with a fixed alpha
per observation. At 5ms, independent position noise creates roughly twice the
velocity impulse and the filter is updated roughly twice as often. The
effective tracker dynamics are therefore tied to Vision frequency.

## 200Hz with cadence-normalized tracker alpha

The historical `alpha=0.2` at an average 11ms cadence corresponds to about
`alpha=0.10` at 5ms. This is a benchmark-only override, not a production
change.

| Metric | ADS, no strafe | ADS, reversal | BodyLock, no strafe | BodyLock, reversal |
|---|---:|---:|---:|---:|
| Acquire points | +23.29% | +25.68% | 0.00% | 0.00% |
| Tracking points | +19.89% | +15.46% | +2.39% | +3.13% |
| Smooth bonus | +5.19% | -1.96% | -14.26% | -11.17% |
| Targets acquired | +16.28% | +14.46% | unchanged | unchanged |
| Targets missed | -39.36% | -29.75% | unchanged | unchanged |
| Undertrack events | +13.43% | +14.95% | -5.16% | -6.22% |
| Overshoot area | -16.72% | +62.73% | +4.92% | +10.13% |
| Continued push | unchanged | +223.53% | +3.70% | +164.84% |
| Stall-ring time | -3.39% | +19.88% | -3.99% | -5.03% |
| Mean error | -1.29% | +0.76% | -3.28% | -2.81% |

Consistency:

- ADS acquisition and tracking improved on 5/5 seeds in both player-motion
  cohorts.
- BodyLock tracking and mean error improved on 5/5 seeds in both
  player-motion cohorts.
- Full-reversal post-crossing burden worsened consistently.

## Interpretation

This does not support “Vision frequency has little value.” Once the tracker
time constant is normalized, 200Hz exposes large acquisition and tracking
headroom.

It does support substantial remaining tracker/controller headroom:

1. Tracker filtering is currently expressed per frame rather than per second.
2. Faster, fresher approach is not paired with a cadence-invariant arrival
   model.
3. During player reversal, the controller converts extra freshness into more
   center crossings and stale-direction tail debt instead of earlier planned
   reversal.
4. A single velocity alpha cannot simultaneously optimize acquisition,
   tracking smoothness and crossing debt.

The next optimization, when resumed, should therefore start with a
time-normalized state estimator and explicit arrival/reversal planning. Merely
raising Vision Hz, lowering global alpha, adding brake, or retuning strength
will trade one cohort against another.

## Production tracker fix

The tracker now treats `motion_velocity_alpha=0.2` as the response at the
historical 11ms reference interval and converts it for each actual Vision
capture interval:

`alpha(dt) = 1 - (1 - 0.2)^(dt / 0.011)`

The interval comes from consecutive Vision source timestamps, not the
controller update interval. Observation age and hold expiry remain based on
controller time.

Across all 20 paired runs, the fixed tracker produced:

| Comparison | Tracking | Smooth bonus | Mean error | Overshoot area | Stall-ring time |
|---|---:|---:|---:|---:|---:|
| Fixed 91Hz vs old 91Hz | -0.12% | +0.08% | +0.20% | -1.68% | +0.11% |
| Fixed 200Hz vs old raw 200Hz | +24.67% | +34.65% | -14.27% | -18.43% | -41.41% |
| Fixed 200Hz vs fixed 91Hz | +8.22% | -6.61% | -2.05% | +12.57% | -1.89% |

This meets the compatibility goal: the historical cadence is effectively
unchanged, while 200Hz no longer destabilizes the velocity filter. Higher
cadence now yields real tracking and mean-error gains.

The remaining 200Hz overshoot increase is concentrated in player-reversal
scenarios (`+18.72%` versus fixed 91Hz). It is not a filter-time-constant
regression: it points to the separate arrival/reversal planning problem already
identified above.

Verification:

- `cod_native_target_coordinator_tests.exe`: pass
- `cod_native_controller_tests.exe`: pass
- `cod_native_runtime.exe`: Release build pass

## Artifacts

- `current_91hz.json`
- `vision_200hz.json`
- `vision_200hz_alpha010.json`
- `vision_200hz_alpha004.json`
- `fixed_91hz.json`
- `fixed_200hz.json`
