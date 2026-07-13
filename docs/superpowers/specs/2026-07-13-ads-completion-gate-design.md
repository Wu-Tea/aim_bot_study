# ADS Completion Gate Design

## Problem

ADS currently loses control as soon as the reticle enters the bodylock
activation region. That region is intentionally wider than the selected ADS
point, so bodylock can preempt acquisition before ADS has completed precise
positioning. Since bodylock no longer brakes manual overshoot, the premature
handoff increases initial-placement overshoot even though moving-target
tracking has more user authority.

## Control contract

- ADS owns initial positioning while a fresh, authoritative selected target
  exists and acquisition has not completed.
- Bodylock cannot preempt an active ADS acquisition merely because the screen
  center has entered the body box or bodylock activation region.
- ADS acquisition completes only after the selected target error remains
  inside a configurable radius for a configurable number of consecutive fresh
  vision frames.
- A configurable hard timeout ends acquisition even when the centered gate is
  not satisfied. This prevents a bad target, moving target, or noisy detector
  from holding ADS authority indefinitely.
- Target loss, loss of aim authority, target identity replacement, or ADS
  release ends the acquisition immediately. Projected-only samples cannot
  advance the stable-frame counter.
- After completion or timeout, ordinary bodylock eligibility applies. Bodylock
  retains its no-downstream-brake contract and bounded-overshoot behavior.
- Auto-fire readiness remains independent and may use stricter requirements.
  ADS acquisition being active never means that firing is ready.

## Configuration

Expose all three completion parameters under `[gamepad.ads]`:

```toml
completion_radius_px = 8
completion_fresh_frames = 3
max_acquisition_ms = 220
```

Runtime validation clamps unsafe inputs:

- `completion_radius_px`: `1..64` pixels;
- `completion_fresh_frames`: `1..20` fresh frames;
- `max_acquisition_ms`: `50..1000` milliseconds.

The compact config keeps the keys visible because they directly control ADS
feel. Existing configs that omit them receive the defaults above.

## State and data flow

`AimActivationTracker` remains responsible for ADS press/release timing.
Add a focused ADS completion state object owned by the native controller. It
tracks acquisition start time, consecutive centered fresh-frame identities,
the active production target identity, and a completion reason.

On each controller tick:

1. Start or reset acquisition on a new ADS activation.
2. Read the frame's production target and freshness state.
3. Count at most once per fresh vision frame when target error is inside the
   configured radius; reset the counter when a fresh frame is outside it.
4. Complete when the counter reaches the configured frame count.
5. Complete with timeout when elapsed time reaches `max_acquisition_ms`.
6. Pass `ads_acquisition_active` into AI mode selection. While true, ADS snap
   wins over bodylock. Once false, existing bodylock selection resumes.

Repeated 1kHz controller ticks over the same 160Hz vision frame must not count
as multiple stable frames.

## Observability and failure handling

Controller telemetry records:

- whether the ADS completion gate is active;
- current centered fresh-frame count;
- configured radius, required frames, and timeout;
- completion reason: `centered`, `timeout`, `target_lost`, `target_changed`,
  `authority_lost`, or `ads_released`.

Invalid or missing configuration falls back to bounded defaults and adds a
runtime diagnostic. No profile file or telemetry setting may silently change
these three values.

## Acceptance

- A body box entering bodylock range before the selected target is centered
  does not switch the mode away from `ads_snap`.
- Three distinct fresh frames inside `8px` complete ADS acquisition with the
  default configuration; repeated controller ticks do not.
- A target outside the radius remains in ADS acquisition until the default
  `220ms` timeout, then becomes eligible for bodylock.
- Target loss or identity replacement cannot cause continued pull on the old
  target.
- Existing ADS near-target and crossing brakes remain active during extended
  acquisition.
- Existing bodylock no-brake, takeover, recoil boundary, and pipeline contract
  tests remain green.
- Deterministic benchmark must report lower initial ADS placement overshoot
  without reintroducing bodylock downstream brake frames.

## Scope boundary

This slice does not implement user-profile runtime adaptation or ADS geometry
calibration. It fixes the prerequisite control-stage ordering first so later
personalization does not learn around a premature ADS-to-bodylock transition.
