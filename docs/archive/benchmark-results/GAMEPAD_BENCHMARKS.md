# Gamepad Benchmarks

## Baseline Definition

- Baseline Run Key: `baseline-20260416-2`
- Timestamp: `2026-04-16T05:42:41Z`
- Artifact: `artifacts/benchmarks/gamepad/baseline-20260416-2.json`
- Git Commit: `e6fc89f87bc6e6fa58b6d756ef43065616a8203b`
- Dirty Worktree: `true`

## Benchmark Parameters

- `frame_dt`: `0.016666666666666666`
- `target_sample_hz`: `120.0`
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

- Run Key: `post-tracker-phase1-120hz-20260522`
- Timestamp: `2026-05-22T04:03:29Z`
- Artifact: `artifacts/benchmarks/gamepad/post-tracker-phase1-120hz-20260522.json`
- Git Commit: `0ada8f27ea953d49fb42b52473976eb6058c8f68`
- Dirty Worktree: `true`
- Baseline Comparison Key: `baseline-20260416-2`

| Metric | Value | Delta vs Baseline |
| --- | --- | --- |
| `mean_error_px` | `7.1056686243509155` | `-27.35%` |
| `p95_error_px` | `9.039258123352214` | `-32.05%` |
| `p99_error_px` | `9.402931501006751` | `-32.83%` |
| `overshoot_events` | `10` | `-41.18%` |
| `max_overshoot_px` | `5.394050692189042` | `+6.54%` |
| `mean_recovery_frames_after_turn` | `49.111111111111114` | `-1.12%` |
| `mean_settle_frames_after_decel` | `12.833333333333334` | `-9.26%` |
| `turn_recovery_coverage_ratio` | `0.5625` | `n/a` |
| `decel_settle_coverage_ratio` | `0.75` | `n/a` |

## History vs Baseline

| Run Key | Timestamp | Artifact | Dirty | Mean Error Delta | P95 Delta | P99 Delta | Overshoot Delta | Max Overshoot Delta | Turn Recovery Delta | Decel Settle Delta | Turn Recovery Coverage Delta | Decel Settle Coverage Delta |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `post-tracker-phase1-120hz-20260522` | 2026-05-22T04:03:29Z | `artifacts/benchmarks/gamepad/post-tracker-phase1-120hz-20260522.json` | dirty | -27.35% | -32.05% | -32.83% | -41.18% | +6.54% | -1.12% | -9.26% | n/a | n/a |
| `pre-tracker-phase1-120hz-20260522` | 2026-05-22T03:48:20Z | `artifacts/benchmarks/gamepad/pre-tracker-phase1-120hz-20260522.json` | dirty | -27.35% | -32.05% | -32.83% | -41.18% | +6.54% | -1.12% | -9.26% | n/a | n/a |
| `phase1-20260417Tbenchmark` | 2026-04-17T13:50:44Z | `artifacts/benchmarks/gamepad/phase1-20260417Tbenchmark.json` | clean | -21.43% | -12.07% | -13.30% | -52.94% | +0.71% | -19.46% | +0.00% | n/a | n/a |
| `gamepad-20260416T124311Z` | 2026-04-16T12:43:11Z | `artifacts/benchmarks/gamepad/gamepad-20260416T124311Z.json` | dirty | -11.69% | -16.74% | -19.48% | -47.06% | +2.16% | -10.26% | +28.84% | n/a | n/a |
| `gamepad-20260416T122456Z` | 2026-04-16T12:24:56Z | `artifacts/benchmarks/gamepad/gamepad-20260416T122456Z.json` | dirty | +71.43% | +75.01% | +68.28% | +188.24% | +81.47% | +37.92% | +43.54% | n/a | n/a |
