# Gamepad Benchmarks

## Baseline Definition

- Baseline Run Key: `baseline-20260416-2`
- Timestamp: `2026-04-16T05:42:41Z`
- Artifact: `artifacts/benchmarks/gamepad/baseline-20260416-2.json`
- Git Commit: `e6fc89f87bc6e6fa58b6d756ef43065616a8203b`
- Dirty Worktree: `true`

## Benchmark Parameters

- `frame_dt`: `0.016666666666666666`
- `target_sample_hz`: `None`
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

- Run Key: `phase1-20260417Tbenchmark`
- Timestamp: `2026-04-17T13:50:44Z`
- Artifact: `artifacts/benchmarks/gamepad/phase1-20260417Tbenchmark.json`
- Git Commit: `a4d9cc7a873db3d3229f70ee48544bdd6cc78558`
- Dirty Worktree: `false`
- Baseline Comparison Key: `baseline-20260416-2`

| Metric | Value | Delta vs Baseline |
| --- | --- | --- |
| `mean_error_px` | `7.685572085161077` | `-21.43%` |
| `p95_error_px` | `11.697831550588052` | `-12.07%` |
| `p99_error_px` | `12.137168342027815` | `-13.30%` |
| `overshoot_events` | `8` | `-52.94%` |
| `max_overshoot_px` | `5.098953394126429` | `+0.71%` |
| `mean_recovery_frames_after_turn` | `40.0` | `-19.46%` |
| `mean_settle_frames_after_decel` | `14.142857142857142` | `+0.00%` |

## Representative History vs Baseline

Kept in repo:
- `baseline-20260416-2`: legacy blended baseline reference
- `gamepad-20260416T122456Z`: early state-machine regression sample
- `gamepad-20260416T124311Z`: first stable tuning pass after Y-axis cleanup
- `phase1-20260417Tbenchmark`: latest clean controller snapshot

| Run Key | Timestamp | Artifact | Dirty | Mean Error Delta | P95 Delta | P99 Delta | Overshoot Delta | Max Overshoot Delta | Turn Recovery Delta | Decel Settle Delta |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `phase1-20260417Tbenchmark` | 2026-04-17T13:50:44Z | `artifacts/benchmarks/gamepad/phase1-20260417Tbenchmark.json` | clean | -21.43% | -12.07% | -13.30% | -52.94% | +0.71% | -19.46% | +0.00% |
| `gamepad-20260416T124311Z` | 2026-04-16T12:43:11Z | `artifacts/benchmarks/gamepad/gamepad-20260416T124311Z.json` | dirty | -11.69% | -16.74% | -19.48% | -47.06% | +2.16% | -10.26% | +28.84% |
| `gamepad-20260416T122456Z` | 2026-04-16T12:24:56Z | `artifacts/benchmarks/gamepad/gamepad-20260416T122456Z.json` | dirty | +71.43% | +75.01% | +68.28% | +188.24% | +81.47% | +37.92% | +43.54% |
