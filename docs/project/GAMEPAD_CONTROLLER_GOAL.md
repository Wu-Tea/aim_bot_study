# Gamepad Controller Goal

Last updated: 2026-05-11

## Goal

Make the gamepad AI aim feel cooperative under heavy user input: the controller should recover from wrong-stick bursts without taking over aligned player input, while keeping ADS behavior no worse than the current baseline.

## Completion Indicators

- High-intensity manual-mix wrong-input recovery improves by at least 30%.
- High-intensity manual-mix overshoot events improve by at least 30%.
- ADS `wrong_input_recovery_after_ads_frames`, `time_to_under_20px`, `wrong_target_snap_rate`, and `max_single_frame_camera_delta` do not regress.
- Any recovery/settle metric must be interpreted together with its coverage ratio.

## Current Result

Compared with the pre-goal body-lock baseline on `full-eval` / seed `12345`:

| Suite | Metric | Baseline | Current | Change |
| --- | --- | ---: | ---: | ---: |
| High-intensity manual-mix | `wrong_input_recovery_frames` | `15.9712` | `11.1406` | `+30.25%` |
| High-intensity manual-mix | `overshoot_events` | `42` | `29` | `+30.95%` |
| High-intensity manual-mix | `p95_error_px` | `15.8251` | `13.9578` | `+11.80%` |
| High-intensity manual-mix | `conflict_frames_ratio` | `0.03896` | `0.03549` | `+8.91%` |
| ADS | `wrong_input_recovery_after_ads_frames` | `1.7407` | `1.7407` | `0.00%` |
| ADS | `time_to_under_20px` | `93.1159` | `93.1159` | `0.00%` |
| ADS | `lock_loss_after_ads_rate` | `0.02174` | `0.00725` | `+66.67%` |
| Phase1 | `p95_error_px` | `12.0610` | `9.0613` | `+24.87%` |
| Phase1 | `max_overshoot_px` | `5.0405` | `4.3968` | `+12.77%` |

## Current Tradeoff

The target is met for the high-intensity user-input mix. Phase1 still has one more overshoot event than the baseline (`10` vs `9`), even though p95 error and max overshoot are better. Treat phase1 overshoot count as the next tightening target rather than increasing force further.
