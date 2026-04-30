# Mouse ADS Benchmarks

## Baseline Definition

- Baseline Run Key: `mouse-ads-baseline-20260428`
- Timestamp: `2026-04-28T02:53:52Z`
- Artifact: `artifacts/benchmarks/mouse_ads/mouse-ads-baseline-20260428.json`
- Git Commit: `2d5ecfece791e031a09a90325807d5b94e3ad650`
- Dirty Worktree: `true`

## Benchmark Parameters

- `frame_dt`: `0.016666666666666666`
- `target_sample_hz`: `None`
- `sim_frames`: `90`
- `response_delta_threshold_px`: `1.0`
- `response_improvement_threshold_px`: `0.5`
- `under_target_threshold_px`: `20.0`
- `under_target_consecutive_frames`: `2`
- `settle_threshold_px`: `10.0`
- `settle_consecutive_frames`: `3`
- `wrong_target_margin_px`: `2.0`
- `scenario_count`: `36`
- `input_profile_count`: `1`
- `input_profiles`: `['none']`
- `single_static_offset`: `8`
- `single_strafe_then_decel`: `8`
- `single_diagonal_then_decel`: `8`
- `reacquire_after_gap`: `6`
- `dual_target_disambiguation`: `6`

## Scenario Logic

- shared ADS manifests from the gamepad ADS suite with mouse-native actuation
- single_static_offset: 8 scenarios with ADS engaged on a stationary offset target
- single_strafe_then_decel: 8 scenarios with lateral target motion that brakes during ADS
- single_diagonal_then_decel: 8 scenarios with diagonal motion and a short settle phase
- reacquire_after_gap: 6 scenarios where the engagement target disappears and reappears mid-ADS
- dual_target_disambiguation: 6 scenarios with an engagement target plus a distractor and a localization schedule

## Latest Run

### Latest Run Summary

- Run Key: `mouse-ads-20260428Tpd6`
- Timestamp: `2026-04-28T07:54:56Z`
- Artifact: `artifacts/benchmarks/mouse_ads/mouse-ads-20260428Tpd6.json`
- Git Commit: `2d5ecfece791e031a09a90325807d5b94e3ad650`
- Dirty Worktree: `true`
- Baseline Comparison Key: `mouse-ads-baseline-20260428`

| Metric | Value | Delta vs Baseline |
| --- | --- | --- |
| `wrong_target_snap_rate` | `0.5` | `+0.00%` |
| `max_single_frame_camera_delta` | `26.570660511172846` | `+16.52%` |
| `target_localization_latency_ms` | `0.0` | `+0.00%` |
| `time_to_under_20px` | `88.0952380952381` | `-13.95%` |
| `time_to_stabilize_ms` | `105.55555555555556` | `-22.97%` |
| `reacquire_time_after_occlusion` | `30.555555555555557` | `-8.33%` |
| `settle_time_after_under_10px_ms` | `39.3939393939394` | `-45.07%` |
| `under_20_escape_count` | `0.14285714285714285` | `-28.57%` |
| `post_under_20_axis_flip_count` | `13.628571428571428` | `-41.04%` |

## Retained Artifacts

Only the benchmark artifacts still needed for later comparison are retained in-tree:

- `artifacts/benchmarks/mouse_ads/mouse-ads-baseline-20260428.json`
- `artifacts/benchmarks/mouse_ads/mouse-ads-20260428Tpd6.json`

## History vs Baseline

| Run Key | Timestamp | Artifact | Dirty | Wrong Target Delta | Max Camera Delta | Localization Latency Delta | Under 20px Delta | Reacquire Delta | time_to_stabilize_ms Delta | settle_time_after_under_10px_ms Delta | under_20_escape_count Delta | post_under_20_axis_flip_count Delta |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `mouse-ads-20260428Tpd6` | 2026-04-28T07:54:56Z | `artifacts/benchmarks/mouse_ads/mouse-ads-20260428Tpd6.json` | dirty | +0.00% | +16.52% | +0.00% | -13.95% | -8.33% | -22.97% | -45.07% | -28.57% | -41.04% |
