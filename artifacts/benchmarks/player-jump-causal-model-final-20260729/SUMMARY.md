# Player-Jump Causal Motion Model — 2026-07-29

All A/B runs use seeds `2026072901`, `2026072902`, and `2026072903`,
60-second moving targets, both pure and mixed input, both ADS-lifecycle and
direct-BodyLock cohorts, vector fusion, and the current `120/220 ms` ADS
timing.

The only A/B difference is `--player-action-cues off|on`.

## Accepted model

- A rising physical `A` button marks the start of a possible player jump.
- For at most 700 ms, the tracker integrates its already measured vertical
  acceleration between Vision observations.
- The acceleration term is continuously attenuated by filtered right-stick
  magnitude and right-stick intent confidence. This prevents the tracker
  from predicting user camera input a second time.
- No weapon data, persistence, additional Vision inference, output brake,
  BodyLock gate, or injected input is added.

## Final A/B

| Scenario / cohort | Tracking | Mean error | Overshoot area | Max vertical overshoot | Stall ring |
|---|---:|---:|---:|---:|---:|
| Jump ADS, pure | +0.5% | +0.6% | **-20.5%** | **140.5 → 116.9 px** | +2.4% |
| Jump ADS, mixed | -2.0% | **-1.3%** | **-4.5%** | 90.0 → 91.0 px | **-1.8%** |
| Jump BodyLock, pure | +0.1% | -0.1% | +3.0% | 48.2 → 50.2 px | +0.6% |
| Jump BodyLock, mixed | +0.2% | -0.2% | -1.3% | 49.0 → 49.0 px | -0.2% |
| Combined ADS, pure | +0.1% | -0.1% | **-7.0%** | 166.6 → 168.5 px | **-2.7%** |
| Combined ADS, mixed | -1.3% | +0.2% | +0.2% | 66.6 → 65.8 px | +0.2% |
| Combined BodyLock, pure | 0.0% | 0.0% | +3.6% | 102.2 → 102.3 px | +0.4% |
| Combined BodyLock, mixed | +0.1% | -0.1% | -0.9% | 62.5 → 62.5 px | -0.2% |

Target-only, horizontal-strafe, and slide scenarios do not emit the jump
cue and therefore retain bit-identical controller behavior.

## Rejected candidates

- Fixed jump-phase prediction suppression: non-monotonic mixed-input
  regressions and timing leakage across ADS/BodyLock phases.
- Prediction-confidence floors `0.25` and `0.50`: reduced some overshoot but
  exceeded the mixed Tracking guardrail.
- Unconditioned constant-acceleration projection: pure-AI gains but
  duplicated mixed manual camera input.

The accepted candidate is deliberately conservative. It materially reduces
pure-AI jump overshoot and keeps every Tracking change within 2%, but it
does not solve slide or arbitrary vertical camera motion. Those require a
learned/common-mode ego-motion observer rather than another fixed window.
