# Gamepad ADS Benchmarks

## Baseline Definition

- Baseline Run Key: `ads-baseline-20260418`
- Timestamp: `2026-04-18T14:10:36Z`
- Artifact: `artifacts/benchmarks/gamepad_ads/ads-baseline-20260418.json`
- Git Commit: `490d6733eb5ffdcd99a9fe2cdd453861238f37e8`
- Dirty Worktree: `true`

## Benchmark Parameters

- `frame_dt`: `0.016666666666666666`
- `target_sample_hz`: `120.0`
- `sim_frames`: `90`
- `max_reticle_speed_pps`: `1500.0`
- `stick_max`: `32767`
- `response_delta_threshold_px`: `1.0`
- `response_improvement_threshold_px`: `0.5`
- `under_target_threshold_px`: `20.0`
- `under_target_consecutive_frames`: `2`
- `lock_loss_window_frames`: `12`
- `lock_loss_grace_frames`: `2`
- `wrong_target_margin_px`: `2.0`
- `scenario_count`: `36`
- `single_static_offset`: `8`
- `single_strafe_then_decel`: `8`
- `single_diagonal_then_decel`: `8`
- `reacquire_after_gap`: `6`
- `dual_target_disambiguation`: `6`
- `input_profile_count`: `4`
- `input_profiles`: `['none', 'aligned_follow', 'opposing_burst', 'overshoot_recover']`
- `manual_input_config`: `{'max_manual_ratio': 0.72, 'full_scale_x': 90.0, 'full_scale_y': 80.0, 'aligned_scale': 0.62, 'opposing_scale': 0.55, 'recover_scale': 0.48, 'vertical_tail_scale': 0.16, 'early_window_start_frame': 2, 'early_window_end_frame': 12, 'opposing_burst_min_frames': 2, 'opposing_burst_max_frames': 4, 'overshoot_aligned_frames': 3, 'overshoot_recover_frames': 3}`

## Scenario Logic

- single_static_offset: 8 scenarios with ADS engaged on a stationary offset target
- single_strafe_then_decel: 8 scenarios with lateral target motion that brakes during ADS
- single_diagonal_then_decel: 8 scenarios with diagonal motion and a short settle phase
- reacquire_after_gap: 6 scenarios where the engagement target disappears and reappears mid-ADS
- dual_target_disambiguation: 6 scenarios with an engagement target plus a distractor and a localization schedule

## Latest Run

### Latest Run Summary

- Run Key: `post-tracker-ads-120hz-20260522`
- Timestamp: `2026-05-22T04:03:29Z`
- Artifact: `artifacts/benchmarks/gamepad_ads/post-tracker-ads-120hz-20260522.json`
- Git Commit: `0ada8f27ea953d49fb42b52473976eb6058c8f68`
- Dirty Worktree: `true`
- Baseline Comparison Key: `ads-baseline-20260418`

| Metric | Value | Delta vs Baseline |
| --- | --- | --- |
| `wrong_target_snap_rate` | `0.16666666666666666` | `-50.00%` |
| `max_single_frame_camera_delta` | `26.338331333838717` | `+0.31%` |
| `lock_loss_after_ads_rate` | `0.0` | `-100.00%` |
| `target_localization_latency_ms` | `0.0` | `+0.00%` |
| `time_to_under_20px` | `105.55555555555556` | `+9.80%` |
| `time_to_body_lock` | `79.83091787439614` | `-0.30%` |
| `reacquire_time_after_occlusion` | `22.222222222222225` | `+12.00%` |
| `harmful_input_suppression_during_ads` | `1.0` | `+0.00%` |
| `wrong_input_recovery_after_ads_frames` | `1.8846153846153846` | `-37.18%` |

## History vs Baseline

| Run Key | Timestamp | Artifact | Dirty | Wrong Target Delta | Max Camera Delta | Lock Loss Delta | Localization Latency Delta | Under 20px Delta | Body Lock Delta | Reacquire Delta | ADS Suppression Delta | Wrong Input Recovery Delta |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `post-tracker-ads-120hz-20260522` | 2026-05-22T04:03:29Z | `artifacts/benchmarks/gamepad_ads/post-tracker-ads-120hz-20260522.json` | dirty | -50.00% | +0.31% | -100.00% | +0.00% | +9.80% | -0.30% | +12.00% | +0.00% | -37.18% |
| `pre-tracker-ads-120hz-20260522` | 2026-05-22T03:48:20Z | `artifacts/benchmarks/gamepad_ads/pre-tracker-ads-120hz-20260522.json` | dirty | -50.00% | +0.31% | -100.00% | +0.00% | +11.06% | +1.21% | +12.00% | +0.00% | -1.33% |
