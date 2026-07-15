# Tracker Ownership and Autofire Recovery Design

## Problem

The live scope-border recording exposes two coupled failures:

1. A 10–23 ms detector gap lets a fresh strong observation on the opposite side replace the previously selected track and immediately receive ADS authority.
2. A vision observation is treated as fire-authoritative for only one 1 kHz controller tick. Autofire readiness requires two frames, so the intervening controller ticks clear readiness before the next 80–100 Hz vision frame arrives.

The solution must not add weapon tables, extra vision work, a second tracker, a second controller planner, or another brake.

## Behavioral Contract

- The selector's currently owned track remains the canonical owner while that track is still present in tracker memory.
- An empty detector frame or a competing selected observation does not immediately replace the owner.
- While ownership is held without a current observation, identity is retained but aim and fire authority are withheld. The existing assist envelope releases output smoothly.
- A current observation mapped to the owner restores observed authority.
- A competing track can replace the owner only after the owner is no longer available, or after an explicit user-yield decision already supported by the authority policy.
- Fire authority is valid for the lifetime of the current observed source, bounded by the existing source-age limit; it is not consumed after one controller tick.
- Autofire readiness counts unique vision sequences, not controller ticks.
- Projected, coasting, ambiguous, pending-switch, stale, or manually overridden targets never receive fire authority.

## Architecture

### Target ownership

`TargetSnapshotProvider` remains the single owner of selector-to-tracker binding. It records the selected tracker ref as `owned_track_`. Each vision snapshot first ingests all detections and resolves the selector's raw observation to a tracker ref.

- Same track: refresh `owned_track_` and use the current observation.
- No selected observation: keep `owned_track_` only if its estimate still exists; clear its observation id and mark the selection as continuity.
- Different track while the owner estimate exists: keep the owner as continuity and do not grant current-observation authority to the competitor.
- Different track after the owner estimate is gone: adopt the new tracker ref.

This uses the tracker's existing coast lifetime and association model instead of adding a second timeout state machine.

### Authority

Current-observation backing is based on the latest observation/track binding, not whether one controller tick has consumed it. `fresh_observation` and `vision_sequence` continue to control once-per-vision-frame updates.

An ownership hold is identity-only: the policy returns `TrackOnly`, so no new desired assist is generated and fire authority is false.

### Autofire

`AutoFireGate` stores the last vision sequence counted for readiness. A valid sequence contributes at most one ready frame. Repeated 1 kHz controller ticks preserve the readiness state without incrementing it.

The decision exposes a block reason and the controller output components expose request, readiness, permission, activity, and final fire output for telemetry.

## Verification

- Stable target at 80 Hz vision / 1 kHz controller fires within two unique vision frames.
- Readiness cannot be satisfied by repeated controller ticks from one vision frame.
- A one-frame detector gap followed by an opposite-side competing track preserves the owner, produces no opposite-side full assist, and never fires.
- The original owner can reacquire after the gap.
- A real replacement is adopted after the old tracker estimate expires.
- Existing controller, tracker, left-stick, vertical BodyLock, ADS brake, recoil, and runtime telemetry tests remain green.

