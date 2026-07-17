# AutoFire Pulse Cadence and Physical-Fire Passthrough Design

Date: 2026-07-17
Status: approved for implementation planning
Scope: native C++ gamepad runtime

## Problem

The native runtime consumes Vision at roughly 100-160 Hz while the controller
runs at 1000 Hz. After the TargetCoordinator cutover, a controller tick without
a newly published Vision frame is treated like a missing observation. The plan
enters `Coasting`, loses fire authority, and clears synthetic fire on the next
1 ms tick. In live telemetry, 1,155 sampled AutoFire requests produced only 8
active samples.

The current output-release path can also clear RB and RT after physical input
has already been copied into the virtual output. AutoFire therefore has the
ability to suppress the user's physical fire input, which violates the input
ownership contract.

TargetCoordinator additionally applies `reliability >= 0.8` after Vision has
already granted strong observed fire authority. Because reliability includes a
target-size multiplier, this silently rejects many otherwise valid fire-zone
observations.

## Goals

- Trigger the first synthetic shot immediately when AutoFire becomes ready.
- While authorization remains valid, start one shot every 100 ms: 10 starts per
  continuous second.
- Hold each synthetic press for at least 30 ms, followed by approximately 70 ms
  released before the next pulse.
- Preserve a valid strong-observed decision between Vision publications without
  counting repeated 1000 Hz controller ticks as new readiness evidence.
- Ensure physical RB and RT always pass through unchanged by AutoFire state,
  release, cooldown, or manual-takeover handling.
- Keep weak, cue-only, predicted, stale, no-target, and non-ADS states unable to
  start or sustain synthetic fire.
- Restore the existing selector's ordinary short target-hold path without
  granting fire authority to yellow-cue hold.

## Non-goals

- Weapon-specific cadence, sniper timing, magazine state, or reload detection.
- Recording weapon data or adding another Vision pass.
- Restoring the pre-refactor tracker/controller pipeline.
- Changing ADS, BodyLock, recoil strength, or manual-axis arbitration.
- Guaranteeing that every weapon emits ten bullets per second; the runtime
  guarantees ten input pulse starts per second, while a game may ignore pulses
  during bolt cycling, reloads, or other weapon lockouts.

## Considered Approaches

### A. Native pulse scheduler plus observation-event repair (selected)

Keep cadence inside `AutoFireGate`, repair TargetCoordinator's distinction
between "no new frame" and "processed miss", and combine physical and synthetic
fire with logical OR at the final output boundary.

This addresses the root cause, keeps device timing in the 1000 Hz controller,
and preserves a single fire-policy component.

### B. Extend any allowed fire tick to 30 ms at the output layer

This would make pulses visible to the game but would hide the incorrect
Observed-to-Coasting transition and retain the duplicate authority gate. It is
rejected as a symptom patch.

### C. Generate cadence in VisionTargetSelector

This would couple device output timing to a variable 100-160 Hz inference loop
and make 30 ms pulse widths imprecise. It is rejected.

## State and Timing Contract

AutoFireGate owns a synthetic pulse scheduler with these defaults:

- pulse width: 30 ms
- pulse period: 100 ms
- first pulse: immediate when the gate becomes ready and authorized
- repeated pulses: start at 100 ms intervals while authorization remains valid

Timing is based on the controller's monotonic `now_seconds`, not tick counts.
Therefore scheduler behavior remains correct if the controller briefly misses a
1 ms deadline.

The scheduler has three output states:

1. `Idle`: no valid synthetic authorization.
2. `Pressed`: the current pulse is within its 30 ms press window.
3. `ReleasedBetweenPulses`: authorization remains valid, but the gate is waiting
   for the next 100 ms boundary.

If evaluation crosses one or more period boundaries, the next pulse starts from
the current evaluation time. The scheduler does not emit catch-up bursts.

Readiness continues to advance only on distinct nonzero Vision sequences. A
repeated controller tick from the same Vision frame may preserve readiness and
synthetic output but may not count as another ready frame.

## Observation Semantics

TargetCoordinator must distinguish:

- **No new Vision publication:** `capture_fresh=false`, no candidates. Age and
  motion prediction may advance, but the last processed observation lifecycle,
  request, and fire eligibility remain valid until their normal source-age TTL.
- **Processed Vision miss:** `capture_fresh=true`, no accepted candidate. The
  plan enters continuity/coasting and immediately clears the latched fire request
  and observed fire eligibility.
- **Processed strong observation:** updates identity, geometry, request, and
  observed fire eligibility.
- **Processed weak/cue/predicted result:** may retain aim continuity according to
  existing policy but cannot retain synthetic fire authorization.

An anonymous fallback candidate from the selector's established target hold may
continue the current target only when it remains within the existing association
radius. A zero source ID must not replace the established nonzero source ID.

The first strong frame after a processed miss is `Reacquiring` but counts as live
observed evidence for readiness. It cannot fire until the configured number of
unique strong Vision sequences has settled again.

## Fire Authority

Vision's explicit strong-observed `fire_authority` remains the authoritative
eligibility decision. TargetCoordinator may revoke it for lifecycle, staleness,
or evidence-class reasons, but it must not reclassify it through the hidden
`reliability >= 0.8` size-weighted threshold.

Target size may continue to scale aiming authority. Any future minimum target
size for AutoFire must be an explicit named configuration and benchmarked policy,
not an incidental consequence of aim reliability.

## Physical Input Ownership

Physical fire and synthetic fire are independent channels:

```text
final_rb = physical_rb OR synthetic_rb
final_rt = physical_rt OR synthetic_rt
```

AutoFire may stop or release only its synthetic channel. It must never write a
physical RB/RT contribution to false or zero.

When physical fire starts while synthetic fire is active:

- the physical input is visible in the same controller tick;
- the synthetic scheduler pauses and follows the existing manual takeover delay;
- the 35 ms release and 85 ms resume delay constrain only synthetic output;
- after the guard expires, a still-authorized AutoFire scheduler begins with an
  immediate new pulse;
- recoil continues to treat either physical or synthetic fire as active.

## Hold Boundaries

- Ordinary selector target hold may preserve its existing short AutoFire request
  when it represents the established observed target.
- Yellow cue hold remains aim-continuity only and must set synthetic fire request
  and authority false.
- Weak association and prediction remain unable to fire.
- A truly processed no-target frame releases synthetic fire immediately, even if
  the current 30 ms pulse has not completed. Safety revocation takes precedence
  over minimum pulse width.
- ADS release and manual takeover also release only synthetic fire immediately.

"At least 30 ms" therefore applies while authorization remains valid; it does not
override a new safety-revocation event.

## Configuration

Add explicit AutoFire settings with these defaults and synchronize them in
`config.toml` and `config.native.example.toml`:

```toml
[gamepad.auto_fire]
pulse_width_ms = 30
pulse_period_ms = 100
```

Validation must require positive values and `pulse_width_ms <= pulse_period_ms`.
Unknown or invalid values fail through the existing configuration diagnostics;
they must not silently produce continuous hold.

## Telemetry

Retain the existing block reason and expose enough state to distinguish:

- not authorized / not ready;
- pulse pressed;
- released between authorized pulses;
- manual takeover guard.

Counters must report pulse starts separately from controller ticks where
synthetic fire is held. Existing requested/allowed/blocked counters may remain
for compatibility but cannot be used as shot counts.

## Test Design

### AutoFireGate unit tests

- Readiness starts a pulse immediately.
- The press remains active from 0 ms through at least 30 ms.
- It is released after the press window and before 100 ms.
- A new pulse starts at 100 ms.
- A one-second authorized interval produces ten pulse starts and no catch-up
  burst.
- Loss of authority, processed miss, ADS release, and manual takeover terminate
  only synthetic output.
- Physical RB and RT are unchanged for every synthetic state.

### TargetCoordinator tests

- Empty non-fresh 1000 Hz ticks preserve a strong observed plan and fire
  eligibility within TTL.
- A fresh processed miss enters coasting and clears fire request/authority.
- An anonymous in-radius ordinary hold continues the established target without
  replacing identity.
- Cue/weak continuation retains no fire authority.
- An observed strong result is not rejected by a second size-weighted 0.8 gate.

### Production-chain integration test

Drive the real native controller for one second with:

- Vision at 100 Hz;
- controller at 1000 Hz;
- stable strong target inside the fire zone;
- no manual fire.

Assert ten pulse starts, every press at least 30 ms, periods of 100 ms within one
controller tick, no frame-gap release, and no output spikes outside the fire
button.

Then inject, separately, processed no-target, cue hold, weak target, ADS release,
physical RB, and physical RT. Assert immediate synthetic revocation where
required and exact physical passthrough in every case.

## Acceptance Criteria

- Stable authorization produces ten synthetic pulse starts over a continuous
  one-second interval.
- Every uninterrupted authorized pulse is at least 30 ms wide.
- No new Vision frame between controller ticks does not release an active pulse.
- Processed no-target, weak, cue-only, predicted, stale, or ADS-release evidence
  cannot sustain synthetic fire.
- Physical RB and RT are never suppressed or delayed by AutoFire.
- Existing ADS, BodyLock, recoil, axis arbitration, partial-occlusion, and
  left-stick benchmarks do not regress.
- The actual launch-path `native/vision_native/build/Release/cod_native_runtime.exe`
  is rebuilt after verification.
