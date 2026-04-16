# Gamepad Controller Execution Requirements

## Purpose

This document describes the desired controller behavior for gamepad aiming.
It is intentionally requirement-focused only.
It does not prescribe implementation details, architecture, or rollout steps.

## Scope

- Only the `controller` layer is in scope.
- `vision` does not need a behavior redesign in this phase.
- The goal is to define how the controller should behave when consuming target information.

## Core Problem

The current controller behavior is not acceptable in live play.

Observed problems:

- AI aim can pull the reticle in unwanted directions.
- AI assist can swallow or distort the player's fine manual adjustment.
- Continuous mixed-control behavior feels worse than fully manual aim.

The next version should stop behaving like a generic always-on blended assist.
Instead, it should behave like two clearly separated controller actions:

1. `ADS Snap`: a brief, aggressive first-time ADS reposition.
2. `Body Lock`: a close-range / close-crosshair tracking correction mode.

## High-Level Behavior

The controller should behave as a small state machine with three player-facing modes:

1. `Manual`
2. `ADS Snap`
3. `Body Lock`

Outside the explicitly allowed windows for `ADS Snap` and `Body Lock`, control should remain primarily manual.

## Requirements

### 1. Manual mode

- Default behavior should preserve player control.
- The controller should not continuously apply strong AI correction during normal ADS tracking.
- Outside of the two special modes below, the player should be responsible for tracking and micro-adjustment.

### 2. ADS Snap

- `ADS Snap` should trigger only once per ADS session.
- An ADS session starts when the player newly enters ADS.
- `ADS Snap` should not retrigger for newly seen targets while the player remains in the same ADS session.
- The snap window should be uniform across weapons in `v1`.
- The target snap window should be approximately `100ms`.
- If no stable valid target exists at the first ADS frame, but a valid target appears later inside the same `100ms` window, `ADS Snap` may still trigger.
- `ADS Snap` is allowed to be aggressive or "violent" in `v1`.
- The purpose of `ADS Snap` is to quickly bring the crosshair close to the target, not to fully replace tracking for the entire ADS duration.

### 3. Body Lock activation

- `Body Lock` should only activate after the crosshair has already entered a near-target region.
- The near-target region should be defined as:
  - the target body box
  - plus an additional configurable pixel tolerance around that box
- `Body Lock` should not be the default mode immediately after ADS unless the near-target condition is satisfied.

### 4. Body Lock target point

- Once `Body Lock` is active, the desired lock point should be the `upper-body center`.
- It should not lock to the full-body center by default.
- It should not blindly preserve the previous generic aim point if that conflicts with upper-body lock behavior.

### 5. Body Lock behavior

- `Body Lock` should function as input correction, not as a generic free-running aim takeover.
- Intended use cases include:
  - standing left-right gunfights
  - close-range mutual strafe fights
  - targets that jump while trading shots
  - tracking sliding targets
- In these cases, the controller should help correct and stabilize the player's aim around the upper body.
- `Body Lock` may continue to actively hold the target even if the player briefly releases the right stick back toward neutral.

### 6. Body Lock exit rules

- `Body Lock` should remain active until the target leaves the near-target region.
- Manual input should not cancel `Body Lock`.
- Strong manual input should not be used as the primary exit condition.
- Releasing ADS should immediately exit `Body Lock`.
- Losing ADS should clear all lock state without carryover into hip-fire.

### 7. ADS boundary rules

- Both `ADS Snap` and `Body Lock` should only exist while ADS is held.
- Releasing ADS should immediately terminate any controller-driven lock or snap state.
- The next ADS press should start a fresh ADS session and allow one new `ADS Snap`.

### 8. Auto fire

- `Auto fire` is out of scope for this phase.
- The player should keep full firing responsibility.
- The controller requirements here cover aim execution only.

### 9. Weapon handling

- `v1` should not differentiate behavior by weapon-specific ADS timing.
- All weapons may use the same ADS snap timing budget in the first version.
- Weapon-specific timing can be revisited later if needed.

## Behavioral Intent

The desired feel is:

- manual aim remains the default
- first ADS engagement gets a strong initial assist
- close-to-target fighting gets a sticky upper-body correction mode
- the controller stops trying to continuously outsmart the player in all other situations

This is intentionally different from the current blended-assist direction.

## Non-Goals

- No redesign of the `vision` decision logic in this document
- No weapon-specific tuning matrix in `v1`
- No ADS-session retrigger on newly discovered targets
- No autofire behavior
- No implementation prescription for plugins, classes, or file layout
- No benchmark or rollout plan in this document

## Notes For Handoff

- This file is a behavior contract only.
- If later implementation needs additional target metadata beyond simple `dx/dy`, that should be treated as an execution concern, not a change to the requirements above.
- The key product goal is to replace "always-on blended assist" with "one-time ADS snap plus conditional body-lock."
