# ADS Near-Crosshair Audit — 2026-07-29

All runs use seeds `2026072901`, `2026072902`, and `2026072903`,
60-second targets, the vector intent fuser, and the current
`config.native.example.toml`.

## What the benchmark adds

`--target-profile near` starts each target 8–40 px from the crosshair. It
separates terminal ADS/BodyLock behavior from long-range acquisition.

## Baseline

| Profile | Tracking points | Mean error | Overshoot area | Undertrack | Stall ring |
|---|---:|---:|---:|---:|---:|
| Pure AI | 24,982 | 14.56 px | 60,043 px·ms | 38.7 | 9,016 ms |
| Mixed | 30,662 | 12.33 px | 19,950 px·ms | 35.0 | 6,915 ms |

The stationary control run has zero pure-AI overshoot and undertrack. The
defect is therefore associated with moving-target state estimation rather
than the near-distance position controller alone.

## Prediction ablation

Setting tracker velocity alpha to zero keeps vision position, association,
and hold behavior, but removes velocity/acceleration guidance.

| Profile | Tracking points | Mean error | Overshoot area |
|---|---:|---:|---:|
| Pure AI, prediction on | 24,982 | 14.56 px | 60,043 px·ms |
| Pure AI, position only | 16,749 | 17.54 px | 145,015 px·ms |
| Mixed, prediction on | 30,662 | 12.33 px | 19,950 px·ms |
| Mixed, position only | 19,395 | 22.45 px | 142,733 px·ms |

Prediction is essential. Disabling it is not a viable fix.

## Rejected experiments

- Fixed 8 px ADS capture radius: tracking fell 2–3% and overshoot rose
  10.8–13.4%.
- Faster velocity alpha (`0.35`, `0.50`): error, overshoot, undertrack, and
  stall-ring time all regressed.
- Global alpha `0.15`: improved the near profile, but regressed generalized
  mixed ADS tracking and increased generalized BodyLock overshoot.
- Distance-scheduled alpha (`0.15` near, `0.20` far): retained the same
  generalized regressions, so it was reverted.

The next controller experiment should estimate prediction confidence from
innovation consistency / maneuver state and attenuate only unreliable
velocity feed-forward. It should not gate prediction by target distance or
replace prediction with raw vision position.
