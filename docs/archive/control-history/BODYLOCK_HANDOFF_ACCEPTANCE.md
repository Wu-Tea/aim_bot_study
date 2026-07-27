# Bodylock manual handoff acceptance

This change fixes the case where one visible moving target remains on screen, the
telemetry association identity changes, and bodylock keeps pushing against the
player's deliberate horizontal correction.

## Deterministic benchmark

The benchmark runs the same 1 ms controller sequence with the handoff disabled
(legacy control) and enabled (fixed):

| Metric | Legacy | Fixed | Gate |
|---|---:|---:|---:|
| Manual direction preservation | 0.143 | 0.922 | >= 0.90 |
| Longest continuous reversed output | 70 ms | 10 ms | <= 20 ms |
| Manual stall | 236 ms | 10 ms | <= 40 ms |
| Manual takeover latency | 240 ms | 38 ms | <= 60 ms |
| Old-target resistance integral | 0.0985 | 0.0096 | lower than legacy |

The benchmark also requires cooperative same-direction tracking to retain AI
assist and a short manual-noise case to remain in bodylock.

## ADS/bodylock brake authority

Bodylock no longer passes through the 130ms short-plan cross brake, observed
target output validation, or ADS carry brake. ADS snap retains its near-target,
crossing, stale/candidate, and acquisition brakes. This is a mode contract, not
a threshold tune.

The repeated-error-crossing benchmark holds a `0.92` manual direction while the
bodylock error crosses every 24ms. After bodylock takeover is established:

- minimum committed output: `0.92`;
- downstream brake frames: `0`;
- ADS carry-brake frames: `0`.

The cooperative overshoot/occlusion case now deliberately allows manual
overshoot rather than hiding it with a brake: maximum overshoot is `45.11px`,
the user reverse correction takes effect on its first frame, and AI opposition
lasts `0` frames. The acceptance cap is `50px`; bodylock is judged on continuity
and prompt reacquisition, not zero overshoot.

Entering bodylock also resets pending short-plan and output-validation timers.
That prevents an ADS brake armed before bodylock from being deferred and firing
after the controller later leaves bodylock.

Run it with:

```powershell
native\vision_native\build\Release\cod_native_vlock_defect_tests.exe
```

## New evidence in telemetry

When `[runtime.telemetry] enabled = true`, controller samples now include every
control boundary needed to attribute a stall: `manual`, `ai`, `post_ai`, dynamic
adjustment, ADS brake, ADS carry brake, pre-recoil, recoil, and final output.
`manual_takeover_active` exposes the handoff state.

`telemetry_identity_track_id` is explicitly a diagnostic geometric identity.
It is recorded separately from `production_target_source`,
`production_target_tier`, `production_target_confidence`, and
`detector_box_count`; a changing diagnostic ID is therefore not evidence that
two people were simultaneously detected.

Advanced overrides are available under `[gamepad.bodylock]` but are intentionally
absent from the compact example config:

```toml
manual_takeover_enabled = true
manual_takeover_threshold = 0.22
manual_takeover_commit_ms = 18
manual_takeover_release_ms = 80
```

## Offline profile and ADS analysis

Both tools only read logs and cannot change live controller behavior:

```powershell
D:\env\python\python.exe tools\analyze_native_user_profile.py runs\native_perf\*.jsonl
D:\env\python\python.exe tools\analyze_ads_transition_model.py runs\native_perf\*.jsonl
```

The 2026-07-13 recording contains 89,241 controller samples and 7,765 opposing
manual/AI conflict samples, enough for a basic shadow user profile but not for
automatic live tuning. It contains 193 ADS events, of which only 38 are valid
and complete and none are `calibration_clean`; the ADS model correctly rejects
tracker calibration until at least 50 clean and 150 valid complete samples are
available.
