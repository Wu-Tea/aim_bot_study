# Gamepad Benchmarks

## Baseline Definition

- Baseline Run Key: `baseline-20260416-2`
- Timestamp: `2026-04-16T05:42:41Z`
- Artifact: `artifacts/benchmarks/gamepad/baseline-20260416-2.json`
- Git Commit: `e6fc89f87bc6e6fa58b6d756ef43065616a8203b`
- Dirty Worktree: `true`

## Benchmark Parameters

- `frame_dt`: `0.016666666666666666`
- `sim_frames`: `180`
- `measure_from_frame`: `60`
- `max_reticle_speed_pps`: `1500.0`
- `stick_max`: `32767`
- `overshoot_threshold_px`: `2.0`
- `turn_recovery_threshold_px`: `6.0`
- `settle_threshold_px`: `5.0`
- `settle_consecutive_frames`: `4`
- `scenario_count`: `24`
- `steady_turns`: `8`
- `turn_then_decel`: `8`
- `decel_resume`: `8`

## Scenario Logic

- steady_turns: 8 scenarios with one or more heading changes and no hard stop
- turn_then_decel: 8 scenarios with a turn followed by a deceleration event
- decel_resume: 8 scenarios with a deceleration event and optional resume

## Latest Run

### Latest Run Summary

- Run Key: `tuned-compare-20260416-2`
- Timestamp: `2026-04-16T06:22:05Z`
- Artifact: `artifacts/benchmarks/gamepad/tuned-compare-20260416-2.json`
- Git Commit: `d5221d4c6a0a01259b5e1200eabb8006ce70bd5b`
- Dirty Worktree: `true`
- Baseline Comparison Key: `baseline-20260416-2`

| Metric | Value | Delta vs Baseline |
| --- | --- | --- |
| `mean_error_px` | `9.254711251341941` | `-5.38%` |
| `p95_error_px` | `12.517222189216609` | `-5.91%` |
| `p99_error_px` | `13.157292291445213` | `-6.01%` |
| `overshoot_events` | `19` | `+11.76%` |
| `max_overshoot_px` | `5.473333263982482` | `+8.11%` |
| `mean_recovery_frames_after_turn` | `49.666666666666664` | `+0.00%` |
| `mean_settle_frames_after_decel` | `14.0` | `-1.01%` |

## History vs Baseline

| Run Key | Timestamp | Artifact | Dirty | Mean Error Delta | P95 Delta | P99 Delta | Overshoot Delta | Max Overshoot Delta | Turn Recovery Delta | Decel Settle Delta |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `tuned-compare-20260416-2` | 2026-04-16T06:22:05Z | `artifacts/benchmarks/gamepad/tuned-compare-20260416-2.json` | dirty | -5.38% | -5.91% | -6.01% | +11.76% | +8.11% | +0.00% | -1.01% |
| `tuned-compare-20260416-1` | 2026-04-16T06:16:13Z | `artifacts/benchmarks/gamepad/tuned-compare-20260416-1.json` | dirty | -1.11% | -2.05% | -2.65% | -11.76% | +18.14% | +1.34% | -3.37% |
| `sweep-20260416-e` | 2026-04-16T06:05:53Z | `artifacts/benchmarks/gamepad/sweep-20260416-e.json` | dirty | +11.68% | +10.99% | +11.80% | -41.18% | +10.34% | +63.09% | +20.99% |
| `sweep-20260416-f` | 2026-04-16T06:05:53Z | `artifacts/benchmarks/gamepad/sweep-20260416-f.json` | dirty | +5.95% | +7.11% | +7.04% | +17.65% | +23.61% | +10.07% | +12.12% |
| `sweep-20260416-g` | 2026-04-16T06:05:53Z | `artifacts/benchmarks/gamepad/sweep-20260416-g.json` | dirty | -1.60% | +0.96% | -0.45% | -29.41% | -7.01% | -14.83% | +2.53% |
| `sweep-20260416-h` | 2026-04-16T06:05:53Z | `artifacts/benchmarks/gamepad/sweep-20260416-h.json` | dirty | +2.69% | +8.25% | +9.38% | -11.76% | +30.79% | +12.35% | +11.85% |
| `sweep-20260416-a` | 2026-04-16T06:03:01Z | `artifacts/benchmarks/gamepad/sweep-20260416-a.json` | dirty | -2.70% | -1.79% | -1.54% | -17.65% | +30.16% | +23.15% | +10.30% |
| `sweep-20260416-b` | 2026-04-16T06:03:01Z | `artifacts/benchmarks/gamepad/sweep-20260416-b.json` | dirty | +5.28% | +3.35% | +5.95% | -17.65% | +23.07% | +16.38% | +34.34% |
| `sweep-20260416-c` | 2026-04-16T06:03:01Z | `artifacts/benchmarks/gamepad/sweep-20260416-c.json` | dirty | +15.55% | +13.54% | +18.03% | +35.29% | +53.51% | +17.79% | +25.51% |
| `sweep-20260416-d` | 2026-04-16T06:03:01Z | `artifacts/benchmarks/gamepad/sweep-20260416-d.json` | dirty | +13.94% | +13.85% | +13.27% | -11.76% | +54.43% | +44.56% | +11.56% |
| `compare-20260416-1` | 2026-04-16T05:55:02Z | `artifacts/benchmarks/gamepad/compare-20260416-1.json` | dirty | +0.00% | +0.00% | +0.00% | +0.00% | +0.00% | +0.00% | +0.00% |
| `baseline-20260416` | 2026-04-16T05:39:16Z | `artifacts/benchmarks/gamepad/baseline-20260416.json` | dirty | n/a | n/a | n/a | n/a | n/a | n/a | n/a |
