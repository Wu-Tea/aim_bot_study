# Control-chain jump stability matched run — 2026-08-01

> Historical pre-final checkpoint. Later verification closed the remaining
> guardrails and Task 4, followed by runtime installation and
> [live acceptance](../../../docs/project/CONTROL_CHAIN_JUMP_STABILITY_ACCEPTANCE_20260801.md).

This is a Task 3 evidence artifact, not an acceptance record. The current
user-facing runtime was not rebuilt, replaced, restarted, or deployed.

## Identity

- Source `HEAD`: `5d9f6d37703d5b0d2af009ae5389ee5d95671710`
- Relevant dirty source diff SHA-256 after Task 1–2: `2F668D891778AA10684C20E6A2B32CAD5A8250C754AA260D36F69C8FB30DF8D1`
- Current runtime SHA-256 (unchanged): `3D9ED74CBCD7007F9EEF3CACC6B17EB5763F7056047BE35AC37495BF1CEB7402`
- Baseline benchmark binary SHA-256: `79B14D3215D90E7935F9A98B6BE64F8BBA189CD8CBD151572E6DDFC2FCE3DCE3`
- Candidate benchmark binary SHA-256: `B57282D09A5816A914E125AE0FE0BBD261260C640CD9CF8C3B1F428E1D3E647C`
- `config.toml` SHA-256: `6CA2D349D13F45645BF93ADB7D7793BDC360D649A5553203DDD62BAD9A63E1CB`
- `models/best_480x384.engine` SHA-256: `45FC56274FF3BBC659E534C3B7833065B0483EF8022AC5D7657CD6DA7DBDEB21`
- Fixed seed: `20260801`; duration: `60,000 ms`; Vision: `100 Hz`; short occlusion: `36 ms`;
  intent fusion: `vector`; remaining work: `current`.

## Direct control-chain result

The candidate removed final-vector direction discontinuities in all matched
rows:

| Cohort | Baseline direction discontinuities | Candidate |
|---|---:|---:|
| ordinary moving / pure ADS | 63 | 0 |
| ordinary moving / pure BodyLock | 163 | 0 |
| ordinary moving / mixed ADS | 141 | 0 |
| ordinary moving / mixed BodyLock | 249 | 0 |
| full-reversal / mixed ADS | 109 | 0 |
| full-reversal / mixed BodyLock | 119 | 0 |
| jump / pure ADS | 71 | 0 |
| jump / pure BodyLock | 156 | 0 |
| jump / mixed ADS | 174 | 0 |
| jump / mixed BodyLock | 232 | 0 |

The maximum handoff `abs(shaped_ai_radial / requested_ai_radial)` was
`6.004`, `12.017`, and `2.621` in the three baseline fixtures, versus
`1.006`, `1.010`, and `1.000` in the candidate. This satisfies the Task 2
handoff envelope in the matched runs.

## Remaining guardrail regressions

The full Task 3 acceptance condition is not granted yet:

- ordinary moving / mixed ADS overshoot area increased from `3741.5` to
  `8296.1 px·ms`; false interruptions increased from `0` to `1`;
- ordinary moving / mixed BodyLock overshoot area increased `1.8%` and false
  interruptions increased from `2` to `3`;
- full-reversal / mixed BodyLock continued push increased from `39` to `49 ms`
  and false interruptions from `1` to `2`;
- jump rows retain continued-push burden, including pure BodyLock `43→50 ms`
  and mixed ADS `10→21 ms`.

These results do not authorize lowering ADS/BodyLock strength or changing
Vision/tracker admission in this round. Task 4 was not started.

## Produced matched artifacts

- `baseline-ordinary-moving-occlusion-both.json`
- `candidate-ordinary-moving-occlusion-both.json`
- `baseline-compound-full-reversal-mixed.json`
- `candidate-compound-full-reversal-mixed.json`
- `baseline-jump-both.json`
- `candidate-jump-both.json`

## Verification limitation

The five focused executables passed directly. The registered Release CTest
run executed the already-built tests successfully, but reported 27 other
tests as `Not Run` because their executables are absent from the existing build
directory. `ALL_BUILD` was intentionally not used because it could rebuild or
overwrite the user runtime, contrary to the task boundary.
