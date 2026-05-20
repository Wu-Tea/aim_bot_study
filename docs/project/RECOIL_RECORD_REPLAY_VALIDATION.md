# Recoil Record Replay Validation

## Goal

Validate one weapon and one aim mode before trusting runtime compensation.

## Record

1. Start `recoil_app_start.bat` in record mode.
2. Confirm the current weapon with `Y`.
3. Fire one full magazine at a static wall without touching the right stick.
4. Repeat at least three times for the same weapon and aim mode.
5. Run `py -3 -B tools\audit_recoil_profiles.py --profile-dir artifacts\recoil_profiles`.

## Pass Criteria

- `accepted_episode_count >= 2`
- `support_below_min` absent
- `vertical_direction_reversal` absent
- `confidence` is diagnostic only for magazine curves; low confidence alone does not block trial playback
- recoil and timeline plots show a stable curve, not a large recovery tail
- calibration file exists for the same game and aim mode

## Replay

1. Start `gamepad_start.bat` with recoil runtime enabled.
2. Confirm startup logs show the selected profile and calibration.
3. Fire a magazine without touching the right stick.
4. Record residual impact trail.

## Fail Interpretation

- No profile selected: inspect readiness reasons in `[Recoil]` logs.
- Profile selected but view dives: calibration scale is wrong.
- Profile selected but weapon still climbs: profile curve is too small or calibration underestimates stick response.
- Profile curve looks jagged or sign-reversing: discard the recording set and re-record.
