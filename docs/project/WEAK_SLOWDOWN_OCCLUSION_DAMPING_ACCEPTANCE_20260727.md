# Weak-Slowdown Short-Occlusion Acceptance — 2026-07-27

## Decision

Retain the deterministic short-occlusion benchmark, reveal-recovery scorer,
and controller-residual attribution metrics. Reject and revert the proposed
12 ms BodyLock-Coasting polar direction transition. Do not change production
control behavior from this experiment.

The short contiguous Vision occlusion fixture reproduces tracking loss and
post-reveal debt, but it does not reproduce controller-generated twitch. The
original total-output discontinuity guardrail was dominated by synthetic
physical-stick changes and was therefore not a valid controller acceptance
metric.

## Identity

- Baseline revision: `c95c9a75a0c9e762920cd861e016e1d620cd7abe`
- Rejected candidate revision: `1b302b2f5a75d3b3dc3d43186a2557ed546662a6`
- Candidate revert: `90e4043`
- Runtime config fingerprint: `15365894690118455392`
- Seeds: `2026072701`, `2026072702`, `2026072703`
- Duration: 60,000 ms per seed and occlusion duration
- Plant: response 700, slowdown edge/center `0.80/0.70`
- Occlusion durations: `0/24/36/48 ms`, two tracking-relative bursts per
  target

## What the fixture proves

Mixed-input baseline:

| Occlusion | Tracking points | Mean error | Circle exits | Post-reveal burden |
|---:|---:|---:|---:|---:|
| 0 ms | 83,972.83 | 8.683 px | 33 | 0 px·ms |
| 24 ms | 83,688.52 | 8.769 px | 35 | 137,331.06 px·ms |
| 36 ms | 82,799.96 | 8.976 px | 35 | 144,811.09 px·ms |
| 48 ms | 80,822.01 | 9.314 px | 33 | 153,739.40 px·ms |

Pure-AI isolation shows the same tracking-loss trend:

| Occlusion | Tracking points | Change vs 0 ms | Mean error |
|---:|---:|---:|---:|
| 0 ms | 59,074.3 | — | 12.446 px |
| 24 ms | 58,243.4 | -1.41% | 12.588 px |
| 36 ms | 57,341.0 | -2.93% | 12.711 px |
| 48 ms | 56,109.0 | -5.02% | 13.065 px |

Therefore short occlusion is a valid robustness fixture for undertracking and
recovery burden.

## Why the original twitch metric was invalid

The old metric counted a delivered-stick delta above `0.35` without separating
physical input from controller contribution.

| Occlusion | Final discontinuities | Same-tick physical discontinuities | Controller residual discontinuities |
|---:|---:|---:|---:|
| 0 ms | 2,673 | 2,672 | 84 |
| 24 ms | 3,157 | 3,138 | 81 |
| 36 ms | 3,422 | 3,406 | 83 |
| 48 ms | 3,834 | 3,821 | 71 |

At 36 ms, 99.5% of counted final discontinuities coincided with a synthetic
physical-stick jump. Reducing the total by 20% would require suppressing user
input, which violates the physical-input ownership contract.

The replacement metric measures the controller residual
`final_stick - manual_stick`. In the pure-AI fixture:

| Occlusion | Residual kicks >0.10 | P99 residual delta | Maximum residual delta |
|---:|---:|---:|---:|
| 0 ms | 0 | 0.0747 | 0.0905 |
| 24 ms | 0 | 0.0747 | 0.0905 |
| 36 ms | 0 | 0.0754 | 0.0905 |
| 48 ms | 0 | 0.0757 | 0.0905 |

Short contiguous occlusion does not increase controller-generated twitch under
the current fixture.

## Rejected production candidate

The 12 ms polar transition preserved AI magnitude while rotating an unsupported
Coasting reversal. Its local unit contract passed, but the end-to-end result
failed:

| Occlusion | Tracking change | Mean-error change | Direction-discontinuity change | Post-reveal burden change |
|---:|---:|---:|---:|---:|
| 0 ms | -0.22% | +0.071 px | +3.29% | — |
| 24 ms | -0.17% | +0.080 px | +3.96% | +1.37% |
| 36 ms | -0.48% | +0.036 px | +2.13% | +0.24% |
| 48 ms | -0.17% | +0.131 px | +6.16% | +3.24% |

Stage attribution showed zero `shaped_assist` discontinuities. The candidate
was modifying an already-smooth AI proposal and retaining stale directional
force, so it added trajectory debt without addressing the final-mix source.
It was reverted before acceptance.

## Retained benchmark contract

- `--short-occlusion-ms` accepts only `0`, `24`, `36`, or `48`.
- Occluded publications are withheld, not converted into fresh misses.
- The first fresh reveal starts an exact 80 ms burden window.
- Recovery requires 12 consecutive stable controller ticks.
- Reports retain raw final-output metrics plus physical-input, requested-AI,
  shaped-AI, and controller-residual attribution.
- The runner supports both `mixed` and `pure` profiles and refuses to overwrite
  a non-empty result directory.

## Next evidence needed

Do not add another controller gate from this fixture. To reproduce the observed
weak-aim-assist twitch, the next benchmark should change only the observation
fixture:

1. partial-scope obstruction with bounded box-center innovation at reveal;
2. intermittent publication cadence rather than one contiguous blind interval;
3. recorded real-session observation innovation if a matching perf log becomes
   available.

Any later controller candidate must reduce controller-residual P99/kick burden
and post-reveal error without attenuating exact physical manual escape.

## Verification

- Full Release build: passed.
- Registered native CTest suite: `26/26` passed.
- Focused scenario, scorer, simulator, fuser, and controller integration tests:
  passed.
- `git diff --check`: passed before evidence commit.
