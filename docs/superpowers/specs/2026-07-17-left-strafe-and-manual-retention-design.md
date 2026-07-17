# Left-Strafe Prediction and Per-Axis Manual Retention Design

> Implementation evidence update: the proposed extra pixel projection was
> rejected after same-timing A/B runs. The existing learned response already
> enters `error_rate_px_per_sec` and the plan horizon on every controller tick;
> adding the same left transition to `error_px` double-counted it, worsening
> 100 Hz fast P95 by 1.33%. The accepted design keeps that single existing
> left-intent path and adds asynchronous benchmark coverage only. Manual
> retention starts only after a wrong-axis input reaches 0.25, then reuses the
> existing 12 ms confirmation hold while the wrong-way evidence decays.

## Goal

Improve aim response in two related cases without weapon tables or additional
vision work:

1. predict the short screen-space effect of the player's horizontal movement
   before the next vision observation reaches the tracker;
2. attenuate only the confirmed-wrong right-stick axis while preserving user
   escape authority and the unaffected axis.

The change must preserve the current ADS/BodyLock feel, damping, zero-overshoot
behavior, AutoFire, recoil, and the single TargetPlan controller pipeline.

## Existing System

`IntentFilter` already exposes independent left/right axis state. The
`ControlResponseEstimator` already learns a signed in-memory relationship
between left-stick excitation and isolated screen-space response when an
existing vision/tracker hint supplies a clean sample. `TargetCoordinator`
publishes this estimate through `TargetPlan.response_scale` and
`response_confidence`. `TargetCoordinator` also already applies it to
`error_rate_px_per_sec` and the short plan horizon, including input changes
between vision observations.

The current accepted `AxisIntentArbiter` is deliberately narrow. On stable
Observed evidence it can stop one wrong-way right-stick axis from suppressing
AI, but it does not attenuate that physical input. It also does not use left
stick input to project the interval before the next vision observation.

## Chosen Architecture

Use one prediction stage and one arbitration stage:

```text
Vision observation + left-axis intent
              |
              v
TargetCoordinator
  tracked motion + unobserved left-strafe projection
              |
              v
          TargetPlan
              |
              v
AxisIntentArbiter (X and Y independently)
  AI-yield confidence + physical-manual retention
              |
              v
ADS / BodyLock -> one dynamics shaper -> per-axis manual mix -> Recoil
```

No parallel brake, takeover state machine, weapon profile, optical flow, or
additional detector is added.

## Left-Stick Short-Horizon Prediction

### Source signal

Reuse `ControlResponseEstimator`. Accepted samples remain restricted to clean,
unambiguous windows with sufficient left-stick excitation. The estimate is
signed and process-memory-only. It is not keyed by weapon and is never written
to disk.

### Projection

`TargetCoordinator` computes an X-only `intent_projection_px` from:

- filtered `left_x`;
- learned signed response scale;
- response confidence;
- time since the most recent observed vision sample;
- a bounded inter-frame horizon.

The projection represents motion that the tracker has not observed yet. It is
zero on the fresh Observed tick, grows only across the normal controller ticks
before the next vision frame, and is bounded to one ordinary vision interval.
The next Observed sample resets it, preventing double-counting with measured
tracker velocity.

The projection is added to the controller-facing horizontal error/plan demand,
not directly to final stick output. ADS and BodyLock therefore retain their
existing force limits, stopping logic, authority, and dynamics shaping.

### Safety boundaries

Projection is zero when any of these is true:

- response confidence is insufficient;
- left X is within the learned deadzone;
- no target or Manual mode;
- target identity changed;
- lifecycle is Reacquiring;
- geometry size changed beyond the existing stability threshold;
- observation age exceeds the short inter-frame horizon.

Coasting needs two interpretations. Ordinary controller ticks between 80/100Hz
vision frames may use the bounded projection. A longer observation gap cannot
renew it and naturally reaches zero at the horizon.

Y is unchanged.

## Per-Axis Wrong-Way Manual Retention

`AxisDecision` gains a `manual_retention` value. It defaults to `1.0`.

The existing confirmed-wrong conditions remain mandatory:

- stable Observed evidence;
- same target identity;
- sufficient reliability;
- no geometry cooldown;
- manual direction opposes the required correction;
- error is worsening by rate or recent history;
- manual magnitude is below the explicit escape threshold.

When confirmation starts, retention begins at `0.85`. Continued confirmation
or its existing 12ms inter-frame bridge moves it smoothly toward the configured
floor. It never drops below that floor. When confirmation ends, retention
smoothly returns to `1.0`.

The physical stick is scaled immediately before the AI/manual mix:

```text
mixed_x = physical_right_x * retention_x + shaped_ai_x
mixed_y = physical_right_y * retention_y + shaped_ai_y
```

Only the confirmed axis changes. The other axis remains exactly `1.0`. Recoil
continues after the mix and is not shaped or attenuated.

At or above `manual_escape_threshold`, retention immediately becomes `1.0` and
the intervention hold clears. Target change, None, Reacquiring, low reliability,
or geometry instability also clears intervention and restores full manual
authority.

## Configuration

Add one canonical key:

```toml
[gamepad.intent]
wrong_way_manual_preservation_floor = 0.65
```

Validation clamps the value to `[0.50, 1.00]`. The default is `0.65`. No aliases
or weapon-specific variants are introduced.

Attack/release timing, the initial `0.85` retention, projection confidence
threshold, and inter-frame horizon remain internal constants with focused tests.
They are not additional user-facing tuning knobs.

## Diagnostics

`NativeControllerOutputComponents` records:

- per-axis intervention state;
- per-axis wrong-way/stable/worsening evidence;
- per-axis manual retention;
- left-strafe intent projection in pixels.

The normal runtime log remains optional. The partial-occlusion benchmark records
intervention frames, retention exposure, and left-projection activity so a
score change can be attributed to the intended path.

## Benchmark Design

Use seed `1337` and the current profile-faithful configuration.

### Scenario 1: normal combat

Retain diagonal target motion, left strafing, partial-body geometry changes,
observation gaps, and reacquisition. It must show no wrong-way manual retention
and no new overshoot or output spikes.

### Scenario 2: classic user errors

Retain stable-observation `wrong_x`/`wrong_y` below escape authority plus
occlusion-bound stale/crossing cases above escape authority. Verify only the
wrong axis is attenuated.

### Left-strafe coverage

Add clean learned-response cases for:

- player and target moving in the same horizontal direction;
- player and target moving in opposite horizontal directions;
- left-stick reversal between vision frames;
- low-confidence response estimation;
- long observation gap after a valid estimate.

The benchmark must compare prediction disabled and enabled under identical
observations, manual inputs, seed, and configuration.

## Acceptance Criteria

- Normal combat aggregate has no material score regression, no new overshoot,
  and no manual retention below `1.0`.
- `wrong_x` and `wrong_y` mean error improve beyond the current intervention-only
  implementation; the unaffected axis remains numerically unchanged.
- Same-direction and opposite-direction left-strafe cases both improve mean or
  response latency without increasing P95 output delta beyond the current
  smoothness envelope.
- Strong input at or above `0.45`, stale/crossing occlusion cases, target change,
  Reacquiring, and long gaps retain full physical input.
- 80Hz and 100Hz vision with 1000Hz controller output has no periodic 10ms pulse,
  large sign flip, or additional output spike.
- ADS, BodyLock, dynamics, AutoFire, recoil, runtime-config, arbiter, integration,
  and benchmark tests pass; Release runtime builds.

## Non-Goals

- No weapon database or persistent learned calibration.
- No additional detector, optical flow, segmentation, or visual effect.
- No Y-axis compensation from left-stick input.
- No final-output clamp after recoil.
- No change to AutoFire or recoil behavior.
- No full Kalman/state-space rewrite.
