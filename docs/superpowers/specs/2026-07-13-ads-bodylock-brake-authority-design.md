# ADS and Bodylock Brake Authority Design

## Problem

The native controller currently lets bodylock output pass through three later
correction mechanisms: the short-plan manual cross brake, output validation,
and ADS carry brake. Those mechanisms can cap, zero, or reverse a deliberate
manual direction for 130ms windows that may be re-armed. That contradicts the
product contract: ADS should brake overshoot, while bodylock should allow a
bounded overshoot so moving-target tracking remains continuous.

## Accepted control contract

- `ads_snap` keeps near-target brake, target-cross brake, stale/candidate
  validation, and acquisition carry limits.
- `body_lock` does not run manual cross brake, short-plan zero hold, observed
  target output validation, or ADS carry correction.
- Bodylock safety comes from its existing assist-force limits, manual takeover,
  target-authority gating, smoothing, and final `[-1, 1]` stick clamp. It does
  not reverse a user's direction merely because the current target error says
  that direction is wrong.
- A wrong user direction in bodylock is therefore allowed, because user intent
  owns continuous tracking. The AI may assist before takeover commits, but no
  downstream brake may repeatedly stop or reverse the user.
- ADS behavior is unchanged and remains responsible for preventing snap
  overshoot.

## Implementation boundary

Gate the existing policies at the controller mode boundary rather than tuning
their thresholds:

1. Skip `BodyLockShortPlanPolicy` when mode is `body_lock`; keep its existing
   target-cross behavior for ADS contexts.
2. Skip `OutputValidationPolicy` when mode is `body_lock`; keep it for ADS and
   projected/candidate validation.
3. Make `AdsCarryBrakePolicy` return the incoming output unchanged for
   `body_lock`, including the no-fresh acquisition cap.
4. Keep `apply_ads_near_target_brake` restricted to `ads_snap` as it is today.

## Acceptance

- A sustained bodylock manual direction is never zeroed, capped to a brake
  constant, or reversed by a downstream policy after takeover commits.
- Moving-target error crossings do not create 130ms output discontinuities.
- Limited overshoot is accepted; the benchmark scores continuity and reacquire
  instead of requiring zero overshoot.
- Wrong-way manual bodylock input remains in the user's direction after
  takeover instead of being corrected to `0.24` in the opposite direction.
- Existing ADS crossing and near-target brake tests continue to pass.
- Cooperative bodylock tracking, short noise, occlusion recovery, and recoil
  boundary tests remain green.
