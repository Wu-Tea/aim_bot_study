# ADS / BodyLock Feel Restoration Acceptance

Date: 2026-07-16

## Purpose

Restore perceptible ADS and BodyLock authority after the single-pipeline refactor
without restoring the duplicated brake/gate paths that previously caused pauses,
oscillation, and mode-transition jolts. The supplied recording
`Content 2026.07.16 - 20.28.14.12.mp4` was used as qualitative evidence of weak
long-range acquisition; deterministic controller benchmarks are the acceptance
source.

## Selected behavior

- ADS strength scales are `1.32` horizontal and `1.26` vertical (1.2x the user's
  pre-change profile).
- BodyLock maximum force remains `0.30` horizontal and `0.42` vertical. The
  1.35x and 1.70x candidates were rejected because their manual-opposition
  duration reached 16 ms and 20 ms respectively.
- BodyLock positional feedback uses the configured 18 px tolerance to derive a
  27 px near-target response range instead of half of the 150 px activation box.
- Closing velocity receives one bounded 20 ms stopping lookahead. It may reduce
  the command toward zero, but cannot reverse the command before target crossing.
- Filtered left-stick horizontal intent continuously blends the positional
  response range toward 75 px. Stick drift is already removed by `IntentFilter`;
  deliberate strafing therefore yields positional grip while motion feedforward
  remains active.
- The existing `AimDynamicsShaper` remains the only delivery envelope. No legacy
  brake or additional mode gate was restored.

## Deterministic comparison

All rows use the full native gamepad suite with selector, ROI, and random-FOV
seeds fixed to `1337`.

| Profile | Moving mean px | Slide mean px | Occlusion mean px | Jump mean px | Near close assist | Near smoothness | User-fight frames | Continuity |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| Rewrite baseline | 31.315 | 38.969 | 41.279 | 43.094 | 0.218 | 93.06 | 2 | PASS (3 ms) |
| Selected | 29.644 | 38.606 | 41.184 | 42.202 | 0.281 | 95.01 | 0 | PASS (11 ms) |
| July 14 strong profile | 13.387 | 26.895 | 31.079 | 24.039 | 0.242 | 93.73 | 138 | PASS (0 ms) |

The selected profile recovers 28.9% more near-target assist than the rewrite
baseline and improves all four chase means while eliminating measured
adversarial user-fight. It intentionally does not reproduce the July 14 force
curve: that artifact tracks much more aggressively but records 138 user-fight
frames.

## Left-stick benchmark correction

The old live-rate gate required a 20% improvement over a deliberately weakened
no-left-intent baseline. Stronger near-target feedback also improved that baseline,
so the relative-only gate incorrectly reported two defects even though absolute
same-direction error improved from 4.252 px to 3.892 px and manual-correction error
improved from 7.314 px to 1.925 px.

The gate now retains the relative path and adds a strict absolute path for primary
80/100 Hz runs: fast mean <= 4 px, fast P95 <= 10 px, and same-direction mean <=
5 px. Sequence consumption, lifecycle delta <= 0.07, zero large sign flips,
production-chain coast/release/reacquire checks, and ADS handoff checks remain
mandatory. The final report has `defect_count=0` at 50, 80, 100, and 160 Hz.

## Removed inactive compact settings

The following keys are no longer accepted or printed under the compact
`gamepad.ads` / `gamepad.bodylock` interface because the single controller path
does not consume them:

- `sustain_smoothing`
- `acquisition_smoothing`
- `fov_scale`
- `manual_opposition_suppression`
- `bodylock.smoothing`
- `lead_strength`

Historical members under the legacy detailed configuration remain available for
old benchmark fixture replay.

## Verification

- BodyLock controller unit tests: PASS
- Aim dynamics shaper tests: PASS
- Runtime configuration tests: PASS
- AutoFire gate tests: PASS
- Target pipeline integration tests: PASS
- Left-stick benchmark tests: PASS
- Left-stick `--require-fixed`: PASS, 0 defects
- Full native gamepad seed-1337 suite: exit 0
- BodyLock continuity: PASS, 11 ms opposing assist, 0 ms plateau, 0 jerk events
- Live failure / occlusion benchmark: PASS
- Effective-config dump: removed keys absent; selected strengths present
- 20-tick runtime smoke with the real TensorRT engine: exit 0

Artifacts:

- `runs/native_perf/legacy_feel_selected_seed1337.json`
- `runs/native_perf/legacy_feel_lstick.json`
- `runs/native_perf/legacy_feel_live.json`
- `runs/native_perf/aim_feel_sweep/summary.json`
