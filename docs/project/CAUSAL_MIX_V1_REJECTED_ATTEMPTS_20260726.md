# Causal Mix V1 Rejected Attempts — 2026-07-26

## Decision

Pause production-policy work after three candidates. None met the frozen V1
hard gate of at least 70% reduction in both maximum vertical overshoot and
post-cross wrong-way output without acquisition regression.

The branch is an experimental checkpoint, not a merge candidate.

## Frozen baseline

- controller revision before production-policy edits: `d046b4d`
- config: `config.native.example.toml`
- seeds: `2026072601`, `2026072602`, `2026072603`
- duration: 60 seconds per seed and cohort
- profile: deterministic vertical target with manual magnitude `0.60–1.00`
- obsolete-direction persistence: 80–140 ms after the true plant crossing
- authoritative artifact:
  `artifacts/benchmarks/sustained_aimlab/causal-mix-v1-baseline-v2-20260726.json`

| Cohort | Acquired | Missed | Max vertical overshoot | Post-cross area | Wrong-way output |
|---|---:|---:|---:|---:|---:|
| ADS | 88 | 199 | 144.872 px | 1,439,461.901 px·ms | 10,633.254 stick·ms |
| BodyLock | 174 | 0 | 66.170 px | 1,647,547.156 px·ms | 24,777.143 stick·ms |

The earlier artifact without the `-v2` suffix used the old brake-episode
arming boundary for V1 attribution. It is retained only as evidence of the
metric defect and is not an authoritative baseline.

## Candidate 1: full 160 ms authorization

The evaluator attenuated radial manual input whenever any 160 ms horizon
predicted crossing. This unloaded too early.

- ADS acquired fell from 88 to 42.
- BodyLock tracking fell by about 10%.
- Rejected before any further tuning.

Artifact:
`artifacts/benchmarks/sustained_aimlab/causal-mix-v1-candidate-20260726.json`

## Candidate 2: 40 ms imminent-cross authorization

The 160 ms horizon could rank candidates, but only a crossing predicted within
40 ms could authorize attenuation.

| Cohort | Max overshoot change | Area change | Wrong-way change |
|---|---:|---:|---:|
| ADS | -4.78% | -6.33% | -9.76% |
| BodyLock | -0.01% | +2.53% | -13.07% |

The trigger preserved more acquisition but returned full manual ownership as
soon as AI reversed after the crossing.

Artifact:
`artifacts/benchmarks/sustained_aimlab/causal-mix-v1-candidate2-20260726.json`

## Candidate 3: recent approach-direction memory

The fuser retained a bounded 180 ms causal fact: the strong manual direction
had just been useful for approaching the same target. After crossing, the same
manual direction could therefore be classified as obsolete rather than as a
new unconditional escape.

| Cohort | Acquired | Missed | Tracking change | Max overshoot change | Area change | Wrong-way change |
|---|---:|---:|---:|---:|---:|---:|
| ADS | 82 | 219 | +21.61% | -10.48% | -34.83% | -51.61% |
| BodyLock | 174 | 0 | +9.08% | approximately 0% | -13.04% | -26.14% |

ADS settled targets improved from 15 to 20 and BodyLock settled targets from
38 to 39, but acquisition regressed and the two 70% gates failed.

Artifact:
`artifacts/benchmarks/sustained_aimlab/causal-mix-v1-candidate3-20260726.json`

## Finding

The third candidate proves that causal ownership history is useful, but it
cannot eliminate the largest overshoot by itself. The remaining maximum
excursion is dominated by control already delivered but not yet visible in
the latest observation. A policy that waits for observed crossing is
structurally late.

The next justified implementation step is not another weight or timing
mutation. It is Task 4 from the implementation plan: expose bounded pending
pre-recoil motion from the existing delivered-control/response history, add
unit tests for its 40/80/120/160 ms displacement, and feed that evidence into
the pure evaluator. Resume only after explicitly accepting that next step.
