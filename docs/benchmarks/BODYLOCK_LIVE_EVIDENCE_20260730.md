# BodyLock live evidence and size-aware continuation

Date: 2026-07-30
Runtime config fingerprint: `11d2b9dafbe23a18d22e6275a47aec7c138253282564c6100dba436517500cde`
Engine fingerprint: `45fc56274ff3bbc659e534c3b7833065b0483ef8022ac5d7657cd6da7dbdeb21`

## Live evidence retained before raw-log cleanup

The five DVR clips recorded around 01:40-01:44 align with native telemetry
session `20260729T173743Z_47348_1`.

- Two-target iron-sight scenes showed target/error identity jumps and belong to
  selector/tracker continuity, not controller gain.
- Close vertical targets frequently entered BodyLock with large screen-space
  residuals while AI authority dropped to zero.
- ADS overshoot cases crossed the target while physical input continued in the
  old direction for roughly 25-160 ms. This remains a separate handoff/fusion
  problem and was not changed by the size-aware continuation patch.
- Wall-jump and climb cases combined close target scale with rapid vertical
  anchor motion. Fixed pixel activation range was therefore least suitable in
  exactly those scenes.

Raw JSONL was deleted after this summary because it was large, locally
reproducible telemetry rather than a stable benchmark artifact.

## Reproduction and patch

Legacy behavior used one fixed BodyLock activation radius. In the focused
fixture, a target with 190 px vertical error and normalized body height 0.90
was outside the 120 px radius, reducing `aim_authority` from 0.9 to 0.

The accepted patch expands the BodyLock continuation radius only while Vision
currently observes a target:

`continuation_radius = activation_radius * (1 + 0.75 * normalized_body_height)`

## Gun-kick and short-occlusion benchmark

The sustained AimLab benchmark now supports deterministic apparent target
motion from firing:

```text
--vision-disturbance gun-kick --short-occlusion-ms 36 --vision-hz 100
```

The fixture uses a 100 ms shot period, up to 8 px vertical displacement,
alternating 4 px horizontal displacement and a bounded recovery envelope.
It changes Vision observations only; target kinematics and controller plant
remain paired.

Across seeds `2026073007`, `2026073011` and `2026073019`, pure BodyLock with
compound target motion lost 30.2% tracking points and accumulated 175.6% more
continued push than the matched no-kick cohort.

A production tracker velocity alpha of `0.15` was retained after scanning
`0.05`, `0.10`, `0.12`, `0.15`, `0.20`, `0.30` and `0.40`. Compared with
`0.20`, the retained value improved the gun-kick cohort's tracking by 12.5%,
mean error by 8.1%, continued push by 7.6% and smooth bonus by 18.4%. Lower
values posted higher tracking scores but introduced worse ADS or crossing
guardrails.

## Initial ADS size-aware range

The current TargetCoordinator path now consumes `[gamepad.ads].range_px` as
the base initial-acquisition radius:

`ads_radius = range_px * (1 + 0.75 * normalized_body_height)`

This applies only while the current ADS epoch has not committed its first snap
target. A held ADS input cannot rearm wide snap, and target-box fluctuations
after commitment cannot repeatedly gate authority. Initial candidate scoring
also uses reticle distance, preventing confidence alone from preferring a
farther in-range target.

For the fixture this changes the radius from 120 px to 201 px and preserves
0.9 authority. Coasting/reacquiring states use the original fixed radius, so
stale body geometry cannot widen blind tracker pulls.

## Fixed-seed benchmark record

Command profile: 60 seconds, seed `20260730`, pure/mixed input, ADS/BodyLock,
counterfactual disabled.

The fixed-radius baseline and size-aware candidate were exactly equal in all
four standard cohorts. This is a useful guardrail result: the existing
sustained benchmark does not exercise the close-target authority cliff, and
the patch does not perturb its ordinary trajectories.

| Cohort | Acquire points | Tracking points | Smooth bonus | Undertrack | Overshoot area | Continued push |
|---|---:|---:|---:|---:|---:|---:|
| Pure ADS | 9,305.20 | 16,735.40 | 910.632 | 15 | 24,885.2 px·ms | 0 ms |
| Pure BodyLock | 58,000.00 | 32,794.30 | 1,282.32 | 39 | 67,919.8 px·ms | 5 ms |
| Mixed ADS | 4,787.88 | 5,734.70 | 275.857 | 15 | 12,912.2 px·ms | 5 ms |
| Mixed BodyLock | 58,000.00 | 17,458.70 | 625.957 | 51 | 247,797 px·ms | 12 ms |

An independent candidate run with seed `20260731` also passed. Its principal
BodyLock tracking scores were 33,976.2 (pure) and 21,381.2 (mixed).

## Verification

- TargetCoordinator focused regression: pass.
- Controller pipeline integration: pass.
- VectorIntentFuser regression: pass.
- Sustained AimLab seed `20260730`: pass.
- Full CTest: 32/34 pass.
- The two remaining failures were the pre-existing left-stick quality
  threshold and learner warm-up tests. Both still failed after temporarily
  restoring the old fixed-radius calculation, so they are not regressions from
  this patch.

## Remaining work

The patch addresses close/large target BodyLock authority loss. It does not
claim to solve:

- target-selection hesitation with two simultaneous targets;
- ADS-to-BodyLock old-direction carry after a center crossing;
- dynamic ROI/crop coverage.

Those must retain separate fixtures and acceptance criteria rather than being
folded into another BodyLock strength gate.
