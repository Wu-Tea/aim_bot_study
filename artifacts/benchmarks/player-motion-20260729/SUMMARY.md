# Player POV motion benchmark

Date: 2026-07-29  
Benchmark revision: `afdeaf5`  
Controller baseline: `657a5b7`  
Config: `config.native.example.toml`  
Seeds: `2026072901`, `2026072902`, `2026072903`  
Duration: 60 seconds per run  
Runs: 120

## Contract

Every player-motion case is evaluated with stationary and moving enemies,
pure-AI and mixed right-stick input, and isolated ADS and BodyLock cohorts.
Pure AI still receives the physical left-stick signal and experiences the
resulting camera motion; it only removes manual right-stick aiming.

Player trajectories:

- `strafe`: seeded full-scale left-stick onset, one reversal and release;
  player top speed is sampled from 125-232 px/s with 100-180 ms inertia.
- `slide`: seeded 32-72 px camera drop, 70-140 ms descent and 120-300 ms hold;
  standing recovery is randomly instant or linear over 100-220 ms.
- `jump`: seeded 28-64 px smooth camera rise/fall over 420-700 ms.
- `combined`: strafe plus a seeded per-target choice of slide or jump.

Stationary and moving enemy pairs share target positions, deadlines, player
scripts and observation schedules. Stationary mode removes only enemy velocity,
acceleration and maneuvers.

## Moving-enemy degradation versus the matched no-player-motion baseline

Values are percentage changes. Tracking and smoothness are higher-is-better;
mean error and overshoot area are lower-is-better.

| Input / cohort | Player motion | Tracking | Mean error | Overshoot area | Smoothness |
|---|---|---:|---:|---:|---:|
| Pure AI / ADS | Strafe | -28.2% | +34.0% | +93.4% | -27.1% |
| Pure AI / ADS | Slide | -35.8% | +59.5% | +215.5% | -26.3% |
| Pure AI / ADS | Jump | -31.6% | +19.4% | +308.8% | -29.1% |
| Pure AI / ADS | Combined | -43.4% | +58.9% | +278.9% | -40.4% |
| Mixed / ADS | Strafe | -27.5% | +29.3% | +850.9% | -28.4% |
| Mixed / ADS | Slide | -40.3% | +59.8% | +1616.8% | -33.2% |
| Mixed / ADS | Jump | -30.0% | +19.6% | +1979.7% | -27.2% |
| Mixed / ADS | Combined | -50.3% | +63.9% | +1885.2% | -46.1% |
| Pure AI / BodyLock | Strafe | -20.0% | +25.2% | +73.7% | -18.8% |
| Pure AI / BodyLock | Slide | -45.3% | +77.3% | +310.6% | -31.5% |
| Pure AI / BodyLock | Jump | -27.4% | +32.6% | +147.2% | -21.5% |
| Pure AI / BodyLock | Combined | -46.3% | +79.6% | +265.5% | -41.4% |
| Mixed / BodyLock | Strafe | -20.5% | +30.5% | +102.7% | -26.6% |
| Mixed / BodyLock | Slide | -41.5% | +95.2% | +536.9% | -27.6% |
| Mixed / BodyLock | Jump | -26.1% | +41.5% | +287.9% | -22.6% |
| Mixed / BodyLock | Combined | -46.6% | +94.6% | +471.0% | -43.6% |

The very large mixed-ADS overshoot percentages use a low 2,839 px*ms moving
baseline. The absolute combined burdens below are the safer comparison.

## Simultaneous player and enemy motion

| Input / cohort | Tracking | Mean error | P95 error | Overshoot area | Continued push | Stall ring |
|---|---:|---:|---:|---:|---:|---:|
| Pure AI / ADS | 7,117 | 23.89 px | 62.20 px | 80,151 px*ms | 13.0 ms | 2,761 ms |
| Mixed / ADS | 9,936 | 19.73 px | 51.53 px | 56,358 px*ms | 18.3 ms | 2,894 ms |
| Pure AI / BodyLock | 16,852 | 22.49 px | 50.57 px | 251,116 px*ms | 71.7 ms | 7,634 ms |
| Mixed / BodyLock | 19,880 | 20.38 px | 49.33 px | 213,999 px*ms | 60.7 ms | 7,382 ms |

Mixed input is not simply harmful. Against pure AI in the simultaneous case:

- ADS tracking improves 39.6%, mean error falls 17.4% and overshoot falls
  29.7%, but continued obsolete push rises 41.0%.
- BodyLock tracking improves 18.0%, mean error falls 9.4%, overshoot falls
  14.8% and continued push falls 15.3%.

The controller therefore benefits from useful manual input, but ADS does not
consistently dispose of manual/AI momentum after vertical POV disturbances.

## Main findings

1. Left-strafe remains a real defect, but vertical POV movement is more severe.
   Slide is the worst mean-error case; jump creates the sharpest ADS crossing
   burden.
2. Combined player motion with a moving enemy roughly halves tracking score in
   every input/cohort combination.
3. BodyLock under moving combined motion shifts from a pure overshoot problem
   toward undertracking: undertrack events rise from zero with stationary
   enemies to 64.0 (pure) and 60.7 (mixed) per 60-second run.
4. Pure AI also fails badly, proving the defect is not only wrong right-stick
   intent. The tracker/controller currently attributes player vertical camera
   motion to target motion.
5. Mixed input improves the main accuracy scores, so a blanket reduction of
   user authority would remove useful corrections. Future work should estimate
   POV motion and pending camera debt before changing fusion strength.

## Artifacts

- `off_stationary.json`, `off_moving.json`
- `strafe_stationary.json`, `strafe_moving.json`
- `slide_stationary.json`, `slide_moving.json`
- `jump_stationary.json`, `jump_moving.json`
- `combined_stationary.json`, `combined_moving.json`

Reproduce with:

`scripts/benchmarks/run-player-motion-matrix.ps1 -Revision <commit>`
