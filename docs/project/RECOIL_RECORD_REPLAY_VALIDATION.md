# Recoil Record Replay Validation

## Goal

Validate one weapon and one aim mode before trusting runtime compensation.

## Record

1. Start `scripts\launch\recoil_app_start.bat` in record mode.
2. Confirm the current weapon with `Y`.
3. Fire one full magazine at a static wall without touching the right stick.
4. Repeat at least three times for the same weapon and aim mode.
5. Run `py -3 -B tools\audit_recoil_profiles.py --profile-dir artifacts\recoil_profiles`.

## Pass Criteria

- `accepted_episode_count >= 2`
- `support_below_min` absent
- `vertical_direction_reversal` absent
- `vertical_recovery_tail` absent for a clean recording; this finding is now an audit warning and does not block runtime trial playback by itself
- `horizontal_episode_disagreement` absent
- `confidence` is diagnostic only for magazine curves; low confidence alone does not block trial playback
- recoil and timeline plots show a stable curve, not a large recovery tail
- calibration file exists for the same game and aim mode for measured playback; without it, profile playback can still be trialed with the uncalibrated mapping

Runtime trial playback is deliberately permissive: when the recognized weapon id, stance, and aim mode match a saved profile, the gamepad runtime uses that profile. Audit findings explain recording quality, but they do not block the profile from being selected.

## Replay

1. Dry-run the selected profile through the same gamepad recoil plugin path:

   ```powershell
   py -3 -B tools\dry_run_recoil_playback.py --profile artifacts\recoil_profiles\profile-cod22-<weapon>-ads-standing-current.json --frame-count 12
   ```

   Confirm the JSON reports the expected `profile_amount`, `profile_x_amount`, `profile_lead_ms`, `feedback_amount`, `calibrated`, and `right_x`/`right_y` curve.
2. Start `scripts\launch\gamepad_start.bat` with recoil runtime enabled.
3. Confirm startup logs show the selected profile. If logs show `uncalibrated`, treat the first replay as a strength/direction smoke test.
4. Fire a magazine without touching the right stick.
5. Record residual impact trail.

## Fail Interpretation

- No profile selected: inspect the weapon id, aim mode, stance, profile path, and current recognizer state in `[Recoil]` logs.
- `vertical_recovery_tail`: the profile rose to a peak and then dropped too far before the recording ended; runtime can still trial the profile, but treat this as bad recording/fit evidence when deciding whether to keep the profile.
- `horizontal_episode_disagreement`: repeated full-magazine episodes disagree too much in final X direction; runtime can still trial the profile, but re-record clean episodes before trusting it.
- Profile selected but view dives: calibration scale is wrong.
- Profile selected but weapon still climbs: profile curve is too small or calibration underestimates stick response.
- Profile selected with `uncalibrated` in the log: adjust `[gamepad.recoil].profile_amount` carefully or capture a measured calibration.
- Profile selected but bullet impacts still jump left/right: tune `[gamepad.recoil].profile_x_amount` in small positive or negative steps; X uses per-sample profile delta only, not cumulative horizontal profile position, and negative values invert the horizontal correction direction.
- Profile selected but the first bullets jump before compensation catches up: increase `[gamepad.recoil].profile_lead_ms` in small steps, such as `10`, `20`, then `30`.
- Profile selected but the sight dips or horizontal correction arrives too early: reduce `[gamepad.recoil].profile_lead_ms`.
- Profile selected but vertical pull feels too strong: lower `[gamepad.recoil].profile_amount`; it scales profile-driven output before the extra X-axis boost is applied.
- Profile curve looks jagged or sign-reversing: discard the recording set and re-record.
