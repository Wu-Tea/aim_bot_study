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

- Run Key: `manual-mix-baseline-20260417`
- Timestamp: `2026-04-17T03:35:20Z`
- Artifact: `artifacts/benchmarks/gamepad_manual_mix/manual-mix-baseline-20260417.json`
- Git Commit: `e15beec34b07a2c22a0f3baafd98e5c8463e8640`
- Dirty Worktree: `true`
- Baseline Comparison Key: `manual-mix-baseline-20260417`

| Metric | Value | Delta vs Baseline |
| --- | --- | --- |
| `mean_error_px` | `11.341939741911927` | `n/a` |
| `p95_error_px` | `16.528182965317725` | `n/a` |
| `p99_error_px` | `18.270894348786168` | `n/a` |
| `overshoot_events` | `23` | `n/a` |
| `max_overshoot_px` | `4.583213104398634` | `n/a` |
| `mean_recovery_frames_after_turn` | `64.0` | `n/a` |
| `mean_settle_frames_after_decel` | `19.0` | `n/a` |
| `conflict_frames_ratio` | `0.0125` | `n/a` |
| `wrong_input_recovery_frames` | `17.0` | `n/a` |
| `manual_yield_score` | `0.017367585762183002` | `n/a` |

## History vs Baseline

No comparison runs recorded yet.
