# Gamepad ADS Benchmarks

## Baseline Definition

No baseline has been recorded yet.

## Benchmark Parameters

- `frame_dt`: `0.016666666666666666`
- `target_sample_hz`: `None`
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

- Run Key: `ads-cli-verify`
- Timestamp: `2026-04-18T13:15:54Z`
- Artifact: `artifacts/benchmarks/gamepad_ads/ads-cli-verify.json`
- Git Commit: `490d6733eb5ffdcd99a9fe2cdd453861238f37e8`
- Dirty Worktree: `true`
- Baseline Comparison: no baseline available yet

| Metric | Value | Delta vs Baseline |
| --- | --- | --- |
| `wrong_target_snap_rate` | `0.3333333333333333` | `n/a` |
| `max_single_frame_camera_delta` | `26.25723493822111` | `n/a` |
| `lock_loss_after_ads_rate` | `0.014492753623188406` | `n/a` |
| `target_localization_latency_ms` | `0.0` | `n/a` |
| `time_to_under_20px` | `96.13526570048309` | `n/a` |
| `time_to_body_lock` | `80.07246376811594` | `n/a` |
| `reacquire_time_after_occlusion` | `19.841269841269842` | `n/a` |
| `harmful_input_suppression_during_ads` | `1.0` | `n/a` |
| `wrong_input_recovery_after_ads_frames` | `3.3076923076923075` | `n/a` |

## History vs Baseline

No comparison runs recorded yet.
