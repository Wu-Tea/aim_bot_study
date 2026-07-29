# Causal player-motion bridge — 2026-07-29

## Purpose

Measure and reduce aim error caused by the player's own jump/slide camera
motion without adding Vision inference, weapon data, persistent profiles, or a
second control gate.

The accepted implementation:

- detects physical jump/slide rising edges already present in the controller;
- keeps one small in-memory amplitude estimate for each event;
- exports a 32 ms vertical error forecast without rewriting tracker state;
- gives fresh Vision observations full authority;
- ramps forecast authority only across the following 12.5 ms (one 80 Hz
  frame);
- bounds ADS forecast so it cannot command through the currently observed
  point;
- gives BodyLock 65% of the forecast as short inertia compensation.

## Rejected variants

- Applying modeled player motion directly to tracker state degraded mixed ADS
  and increased overshoot. It remains benchmark-selectable as `state`, but is
  disabled in production.
- Applying the forecast on every controller tick improved some tracking scores
  but duplicated fresh Vision evidence. In the bounded trial, mixed jump ADS
  tracking regressed 10.4%.
- A point-only ADS clamp did not solve that duplication by itself. The accepted
  version therefore gates forecast authority by time since the last unique
  Vision observation.

## Fixed benchmark protocol

- Seeds: `2026072901`, `2026072902`, `2026072903`
- Duration: 60,000 ms per run
- Profiles: pure and mixed
- Cohorts: ADS and BodyLock
- Target motion: moving
- Player vertical motion: off/slide/jump/random (`all`)
- Vision: 80 Hz
- Player action cues: enabled
- Baseline: `--player-motion-model current`
- Candidate: `--player-motion-model forecast`

Raw JSON matrices are retained locally and excluded from Git under
`artifacts/benchmarks/causal-player-motion-*.json`.

## 80 Hz combined result

| Profile / cohort | Tracking | Mean error | Overshoot area | Max vertical overshoot | Stall-ring |
|---|---:|---:|---:|---:|---:|
| Pure ADS | +0.66% | -0.56% | +4.03% | -0.63% | -3.86% |
| Mixed ADS | **+4.29%** | **-2.02%** | **-5.19%** | -0.04% | -0.42% |
| Pure BodyLock | -0.11% | -0.11% | -0.84% | +0.58% | -0.36% |
| Mixed BodyLock | -0.02% | -0.06% | -0.04% | 0.00% | +0.52% |

The main intended case is mixed ADS during player motion: tracking rises while
mean error and overshoot both fall. Pure ADS overshoot area rises 4.03%, but
maximum vertical overshoot falls and wrong-way output falls 9.25%; this remains
inside the 5% acceptance guard.

Slide-only mixed ADS improves tracking by 7.2%, mean error by 0.7%, and
overshoot area by 2.6%. Jump-only mixed ADS improves tracking by 2.0%, mean
error by 4.8%, and overshoot area by 15.2%.

## 80 Hz plus 36 ms short-occlusion result

| Profile / cohort | Tracking | Mean error | Overshoot area | Post-occlusion error area |
|---|---:|---:|---:|---:|
| Pure ADS | +1.37% | -1.10% | -2.74% | -1.85% |
| Mixed ADS | **+4.89%** | **-1.51%** | **-4.75%** | +0.27% |
| Pure BodyLock | +0.11% | -0.37% | -0.08% | -0.60% |
| Mixed BodyLock | +0.12% | -0.06% | +0.15% | -0.11% |

The candidate does not create a large short-occlusion regression. Mixed ADS
post-occlusion error area rises 0.27%, while overall tracking, mean error, and
overshoot all improve.

## Interpretation and limitations

This is a causal controller optimization, not the Shadow Oracle result. It
uses only physical button timing, controller intent, target innovation, and
existing Vision timestamps.

It currently models vertical jump/slide camera motion. Ordinary strafing is
left to the existing left-stick response path because the Shadow Oracle showed
little useful horizontal headroom. Slide detection assumes the physical `B`
binding; remapped slide controls safely receive no forecast rather than an
incorrect one.

Amplitude learning is memory-only and conservative. It resets with the
controller, does not identify weapons, and does not persist across application
restarts.
