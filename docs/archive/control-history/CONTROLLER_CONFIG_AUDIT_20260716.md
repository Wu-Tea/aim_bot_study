# Controller configuration audit — 2026-07-16

## Result

The live controller has one target plan and one output shaper. The documented public
profile is now limited to controls that reach the active tracker, ADS, BodyLock,
AutoFire, dynamics, or recoil construction path. Historical benchmark artifacts that
do not contain a `configuration` object are **configuration-unproven**: their raw
numbers remain intact, but they are not valid evidence for a live-profile comparison.

## Canonical live controls

| Owner | Canonical keys | Active consumer |
|---|---|---|
| Tracker | `backend`, `projection_age_ms`, `max_velocity_px_per_sec`, `lead_seconds`, `lead_max_px`, `aim_height_ratio` | `TargetCoordinator` and fresh-observation geometry in `native_gamepad_controller.cpp` |
| ADS | `strength_scale`, `vertical_strength_scale`, `range_px`, `snap_duration_ms`, completion controls | `AdsAcquisitionController` and `TargetCoordinator` |
| BodyLock | `strength`, `vertical_strength`, `activation_range_px`, `tolerance_px`, manual escape/takeover controls | `BodylockFollowController`, `IntentFilter`, and `TargetCoordinator` |
| Dynamics | recoil jitter guard and manual curve shaping controls | `AimDynamicsShaper` |
| AutoFire | output, aim-only, age/readiness and takeover controls | `AutoFireGate` |
| Recoil | `[gamepad.recoil]` keys | recoil recognizer and compensation policy |

`range_px`, `activation_range_px`, and `tolerance_px` are deliberately separate:
the first is ADS error-to-output slope, the second is the timed ADS→BodyLock fallback
limit, and the third is the settle radius.

## Compatibility aliases

| Legacy spelling | Canonical destination | Status |
|---|---|---|
| `runtime.gamepad.body_lock_upper_body_ratio` | `gamepad.tracker.aim_height_ratio` | deprecated alias |
| `gamepad.ai_aim.body_lock_upper_body_ratio` | `gamepad.tracker.aim_height_ratio` | deprecated alias |
| `runtime.gamepad.tracker_backend` | `gamepad.tracker.backend` | retained parser compatibility |
| `runtime.gamepad.auto_fire_output` | `gamepad.auto_fire.fire_output` | retained parser compatibility |
| `runtime.gamepad.xinput_*` | `runtime.input.*` | retained parser compatibility |

Canonical geometry always wins over either ratio alias regardless of file order. A
legacy-only ratio is translated once and reported as `deprecated_alias`; a conflict is
reported as `ignored_alias`.

## Internal/legacy-only controls

The large `GamepadAiAimConfig` aggregate remains as temporary storage because retained
legacy tests and helper policies still compile against it. The following families are
not advertised in `config.native.example.toml` unless they have an active component
owner: legacy smoothing/deadzones, old FOV transition/time-to-go controls, old overlap
suppression, old lateral-tail/vertical-tail gates, and old target-match thresholds.
References from `NativeAiAim`, `BodyLockMotionPolicy`, or historical tests do not by
themselves make a field a public live control.

This migration intentionally avoids deleting the storage fields in the same release:
removing them before the legacy test fixtures are retired would create a broad rewrite
unrelated to live behavior. They are classified `internal` and excluded from the normal
template and profile-faithful artifact.

## Benchmark contract

- Default mode is `profile-faithful`.
- Active controller tuning loaded from the supplied config is immutable to scenarios.
- Artifact root records mode, effective tracker/ADS/BodyLock values, seeds, and an
  explicit empty override list.
- The previous anonymous scenario tuning is isolated behind `stress-fixture`.
- `stress-fixture` currently fails closed until each old override has a field, loaded
  value, replacement value, scenario, fixture name, and reason. It cannot emit a
  misleading artifact.

## Approved baseline profile

Tracker geometry is `0.365`. ADS is `1.54 / 1.26 / 150 px / 160 ms`. BodyLock is
`0.45 / 0.50 / 80 px / 8 px`, with manual escape `0.45 / 0.55`. These are candidate
baseline values, not claims that every gameplay case is already optimal.
