# Gamepad Manual-Mix Benchmarks

## Baseline Definition

- Baseline Run Key: `manual-mix-baseline-20260417`
- Timestamp: `2026-04-17T03:35:20Z`
- Artifact: `artifacts/benchmarks/gamepad_manual_mix/manual-mix-baseline-20260417.json`
- Git Commit: `e15beec34b07a2c22a0f3baafd98e5c8463e8640`
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
- `conflict_manual_threshold`: `2000`
- `conflict_ai_threshold`: `2000`
- `wrong_input_recovery_threshold_px`: `8.0`
- `wrong_input_recovery_consecutive_frames`: `3`
- `scenario_count`: `24`
- `steady_turns`: `8`
- `turn_then_decel`: `8`
- `decel_resume`: `8`
- `manual_seed_count`: `3`
- `manual_seeds`: `[1, 2, 3]`
- `manual_input_config`: `{'max_manual_ratio': 0.72, 'full_scale_x': 90.0, 'full_scale_y': 80.0, 'aligned_scale': 0.62, 'wobble_scale': 0.18, 'opposing_scale': 0.55, 'recover_scale': 0.48, 'vertical_jitter_scale': 0.12, 'near_target_radius_px': 18.0, 'wobble_period_frames': 6, 'event_window_frames': 16, 'opposing_burst_min_frames': 2, 'opposing_burst_max_frames': 5, 'overshoot_recover_frames': 3, 'reference_frame_dt': 0.016666666666666666}`

## Scenario Logic

- steady_turns: 8 scenarios with one or more heading changes and no hard stop
- turn_then_decel: 8 scenarios with a turn followed by a deceleration event
- decel_resume: 8 scenarios with a deceleration event and optional resume
- manual-mix: 3 deterministic manual-input seeds per manifest using aligned, wobble, opposing, recover, and vertical-jitter modes

## Latest Run

### Latest Run Summary

- Run Key: `gamepad-20260417T055418Z`
- Timestamp: `2026-04-17T05:54:18Z`
- Artifact: `artifacts/benchmarks/gamepad_manual_mix/gamepad-20260417T055418Z.json`
- Git Commit: `8d686d7d1fe0e08a17b47ca3d93da1c404425714`
- Dirty Worktree: `true`
- Baseline Comparison Key: `manual-mix-baseline-20260417`

| Metric | Value | Delta vs Baseline |
| --- | --- | --- |
| `mean_error_px` | `12.254988860240134` | `+8.05%` |
| `p95_error_px` | `17.69228933306781` | `+7.04%` |
| `p99_error_px` | `18.49768858728809` | `+1.24%` |
| `overshoot_events` | `21` | `-8.70%` |
| `max_overshoot_px` | `5.655001233824673` | `+23.39%` |
| `mean_recovery_frames_after_turn` | `58.5` | `-8.59%` |
| `mean_settle_frames_after_decel` | `18.166666666666668` | `-4.39%` |
| `conflict_frames_ratio` | `0.010648148148148148` | `-14.81%` |
| `wrong_input_recovery_frames` | `16.892857142857142` | `-0.63%` |
| `manual_yield_score` | `0.0621000206110675` | `+257.56%` |
| `harmful_input_suppression_ratio` | `0.7524565095120014` | `n/a` |
| `aligned_input_preservation_ratio` | `0.9328265534218533` | `n/a` |
| `opposing_burst_hold_error_px` | `13.824449879830473` | `n/a` |
| `lock_survival_rate` | `0.5461538461538461` | `n/a` |

## History vs Baseline

| Run Key | Timestamp | Artifact | Dirty | Mean Error Delta | P95 Delta | P99 Delta | Overshoot Delta | Max Overshoot Delta | Turn Recovery Delta | Decel Settle Delta | Conflict Delta | Wrong Input Recovery Delta | Manual Yield Delta | Harmful Suppression Delta | Aligned Preservation Delta | Burst Hold Error Delta | Lock Survival Delta |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `gamepad-20260417T055418Z` | 2026-04-17T05:54:18Z | `artifacts/benchmarks/gamepad_manual_mix/gamepad-20260417T055418Z.json` | dirty | +8.05% | +7.04% | +1.24% | -8.70% | +23.39% | -8.59% | -4.39% | -14.81% | -0.63% | +257.56% | n/a | n/a | n/a | n/a |
| `gamepad-20260417T053348Z` | 2026-04-17T05:33:48Z | `artifacts/benchmarks/gamepad_manual_mix/gamepad-20260417T053348Z.json` | dirty | +10.71% | +3.81% | -1.08% | +130.43% | +94.26% | -2.60% | +25.66% | -8.33% | -32.72% | +381.81% | n/a | n/a | n/a | n/a |
| `gamepad-20260417T053248Z` | 2026-04-17T05:32:48Z | `artifacts/benchmarks/gamepad_manual_mix/gamepad-20260417T053248Z.json` | dirty | +12.29% | +3.80% | -1.66% | +191.30% | +4.94% | -38.02% | +38.25% | -33.33% | -23.53% | +267.16% | n/a | n/a | n/a | n/a |
| `gamepad-20260417T053032Z` | 2026-04-17T05:30:32Z | `artifacts/benchmarks/gamepad_manual_mix/gamepad-20260417T053032Z.json` | dirty | +2.81% | -2.44% | -7.19% | +60.87% | +5.21% | -5.21% | +3.51% | -77.78% | -61.76% | +1068.86% | n/a | n/a | n/a | n/a |
