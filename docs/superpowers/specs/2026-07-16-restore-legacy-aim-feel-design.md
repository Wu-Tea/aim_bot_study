# Restore Legacy ADS and BodyLock Feel Design

## Objective

Restore the strong ADS pull and the near-target braking/grip visible in the
2026-07-14 runtime without restoring its duplicated completion, carry-brake,
authority, lifecycle, or output-validation stages.

The current `dev` commit `3406889`, the current `config.toml`, and
`runs/native_perf/force_sweep_current_20260716_seed1337.json` are the immutable
pre-change backup. Work occurs only on `codex/restore-legacy-aim-feel`.

## Evidence

The current pipeline improves user ownership but is materially weaker than the
July 14 strong profile. Moving-target mean error increased from `13.39` to
`31.32 px`; slide-visible error increased from `26.90` to `38.97 px`; jump error
increased from `24.04` to `43.09 px`. Close-assist output in slide/jump cases fell
from about `0.18-0.24` to `0.05-0.07`.

The current runtime still parses ADS and BodyLock smoothing keys, but the new
controllers do not consume them. Therefore gain alone cannot restore the old
braking feel.

## Design

### Keep one control path

The production flow remains:

`TargetCoordinator -> ADS or BodyLock -> AimDynamicsShaper -> AutoFire -> Recoil`

No legacy policy or gate is reintroduced.

### Recover force with an offline sweep

Run a deterministic matrix from the same seed and scenarios:

- ADS force multiplier relative to the current active force: `1.0`, `1.2`, `1.4`.
- BodyLock horizontal/vertical force multiplier: `1.0`, `1.35`, `1.7`.

Select the strongest pair that improves moving, slide, occlusion, and jump error
without exceeding the July 14 strong-profile overshoot or raising adversarial
manual-fight above `20` frames. The user explicitly selected the legacy-feel
direction, so matching old pull is preferred over preserving the current very
low force.

### Restore braking as one controller term

ADS keeps its existing stopping lookahead. BodyLock receives one bounded
near-target stopping lookahead inside `BodylockFollowController`; it reduces a
command only when error and error-rate show that the reticle is already closing
on the target. It must never create a force opposite to the remaining error and
must not become another lifecycle or brake gate.

The single output shaper continues to own slew limiting and user arbitration.
No second smoothing pipeline is added.

### Remove dead configuration

Remove runtime/config-file keys that the new production controller does not use:

- `gamepad.ads.sustain_smoothing`
- `gamepad.ads.acquisition_smoothing`
- `gamepad.ads.fov_scale`
- `gamepad.ads.manual_opposition_suppression`
- `gamepad.bodylock.smoothing`
- `gamepad.bodylock.lead_strength`

Keep force, range, handoff, tolerance, target-age, AutoFire, and recoil keys that
have an active production consumer. Legacy benchmark-only struct fields may stay
until those fixtures are retired, but they are not exposed as active runtime
configuration.

### Acceptance

- Fresh fixed-seed artifacts for every sweep point record the exact config.
- Selected profile improves current moving/slide/jump mean error by at least 15%.
- BodyLock close-assist output materially increases toward the July 14 range.
- Continuity benchmark remains PASS, with no blind force rise during occlusion.
- Left-stick benchmark remains 5 scenarios / 0 defects.
- AutoFire tests remain PASS.
- Adversarial manual-fight stays at or below 20 frames.
- Production runtime builds and completes a real-model smoke test.

## Non-goals

- Restoring exact internal outputs of the July 14 multi-stage controller.
- Adding weapon-specific ADS-speed data.
- Adding vision work or weapon tables.
- Optimizing small/far target authority in this change.
