# Brake episode benchmark acceptance (2026-07-19)

## Scope

This acceptance pass replaces the sustained benchmark's inert severe-only
overshoot signal with target-relative brake diagnostics. It also verifies that
the native launcher/runtime has no keyboard termination shortcut; normal users
close the console window, while finite `--max-ticks` test runs remain available.

No gameplay configuration was changed by this pass. Raw ignored artifacts are
under `runs/native_perf/brake_sweep_20260719/` in the experiment worktree.

## Reproduction matrix

Finalists ran for 60 seconds with seeds `1337`, `20260718`, and `424242`.
Every finalist covered:

- pure and mixed manual-input profiles;
- isolated ADS and BodyLock cohorts;
- ordinary and small targets;
- camera response `400`, `500`, and `650` px per stick-second;
- slowdown edge/center `0.65/0.55`, `0.50/0.40`, and `0.40/0.30`.

The staged screen also covered ADS arrival horizons `100`, `120`, `140`, and
`160` ms; ADS strength scales `1.40`, `1.54`, and `1.68`; effective ADS peak
caps `0.45` and `0.60`; and BodyLock X/Y force `0.45/0.50`, `0.50/0.56`, and
`0.52/0.58`. Script hashes remain configuration-independent within every
environment and seed.

The reusable runner is `scripts/verify/run_brake_episode_sweep.ps1`. It accepts
named `name=path` configurations, seeds, slowdown triples, camera responses,
target profiles, and duration without editing the user's active config.

## Configuration finding

`gamepad.ads.strength_scale` is a peak-force cap in the response-model
controller, not a response gain. The ADS solver also reserves `sqrt(2)` vector
headroom. Consequently `1.40`, `1.54`, and `1.68` all sit above the physical
stick range and are behaviorally saturated in this benchmark. They are not
three distinct strength levels.

Values `0.60` and `0.45` do create real peak limits. The `0.60` screen reduced
output delta, but the full response-mismatch matrix lost acquisition and
tracking performance. This pass therefore does not reinterpret or silently
rewrite the existing setting. Future config cleanup should expose the value as
an explicit peak cap or remove saturated examples rather than present it as a
linear strength multiplier.

## Final aggregate comparison

The table compares the selected `120 ms + BodyLock 0.52/0.58` candidate against
the current `160 ms + BodyLock 0.45/0.50` baseline across the complete final
matrix. Area, stall, and exit values are normalized per acquired target so a
faster candidate is not penalized merely for completing more targets.

| Cohort | Metric | Baseline | Candidate | Change |
| --- | --- | ---: | ---: | ---: |
| ADS | acquisition points | 414,407 | 518,152 | +25.0% |
| ADS | tracking points | 421,045 | 592,828 | +40.8% |
| ADS | acquired targets | 1,644 | 2,078 | +26.4% |
| ADS | settled targets | 562 | 731 | +30.1% |
| ADS | post-cross area / acquired target | 3,426 | 2,618 | -23.6% |
| ADS | circle exits / acquired target | 0.714 | 0.638 | -10.6% |
| ADS | mean run P95 error | 75.89 px | 62.86 px | -17.2% |
| ADS | mean run P95 output delta | 0.0979 | 0.0975 | -0.4% |
| ADS | mean run P95 jerk | 0.0719 | 0.0720 | +0.1% |
| BodyLock | tracking points | 1,547,134 | 1,677,175 | +8.4% |
| BodyLock | settled targets | 2,084 | 2,192 | +5.2% |
| BodyLock | post-cross area / acquired target | 2,181 | 2,072 | -5.0% |
| BodyLock | circle exits / acquired target | 0.876 | 0.841 | -4.0% |
| BodyLock | mean run P95 error | 50.48 px | 49.52 px | -1.9% |
| BodyLock | mean run P95 output delta | 0.0573 | 0.0602 | +5.1% |
| BodyLock | mean run P95 jerk | 0.0679 | 0.0692 | +1.9% |

All final candidates retained zero legacy severe overshoot events and zero
AI-attributed continued-push milliseconds after a center crossing. The new
geometry still records small center crossings, their amplitude and area,
circle re-exits, 10--20 px stall residence, correction reversals, settle state,
and ADS-to-BodyLock handoff residuals.

## Decision

The synthetic gameplay candidate is:

```toml
[gamepad.ads]
strength_scale = 1.54
vertical_strength_scale = 1.26
range_px = 150
snap_duration_ms = 120

[gamepad.bodylock]
strength = 0.52
vertical_strength = 0.58
activation_range_px = 120
tolerance_px = 16
manual_escape_threshold = 0.45
manual_escape_preservation = 0.55
```

The `100 ms` ADS variant wins raw speed but raises the worst post-cross
excursion relative to the current baseline, so it fails the brake-first
selection rule. `0.50/0.56` remains the lower-force fallback if gameplay finds
the candidate's roughly five-percent BodyLock output-variation increase too
visible.

The candidate is deliberately not installed into the user's active
`config.toml`. Benchmark acceptance establishes a reproducible candidate; a
gameplay session still decides whether its additional BodyLock authority feels
helpful inside COD19's unusually strong slowdown ring.
