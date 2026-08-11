# Controller Cooperative Control V2

Status: implementation authorized; live acceptance pending
Date: 2026-08-11
Scope: native C++ right-stick intent, D correction and final T ownership

## Product model

The player is continuously active, including during target acquisition. Manual
input is the native camera baseline; AI supplies only target-directed work the
player has not already supplied. The system must never reinterpret one held
gesture as acquisition on one owner and D correction on another.

ADS and BodyLock keep their existing proposal producers. ADS remains the
strong acquisition proposal and BodyLock remains the smaller tracking
proposal. The final solver has no mode-specific patch: mode behavior enters
through the one shaped AI proposal A.

## One tick, one input, one purpose, one output

```text
physical gamepad M
  -> IntentFilter once -> filtered intent F + gesture phase
  -> GesturePurpose once -> AcquireTarget | CorrectCurrentTarget | HandoverTarget
      -> Vision consumes the same purpose for I
      -> TargetCoordinator permits or rejects D movement
  -> one shaped AI total-output proposal A
  -> one 2-D cooperative residual solve
  -> one final pre-recoil T
```

Runtime samples the physical controller before Vision, then finishes the
controller tick after consuming the latest Vision result. Vision and
TargetCoordinator therefore see the same filtered gesture and purpose.

## Gesture purpose

A right-stick gesture begins at `Onset`, remains the same gesture through
`Sustained`, and ends at `Release`. A direction reversal beyond 90 degrees is a
new gesture boundary.

- An onset/reversal without an owned target is `AcquireTarget`.
- An onset/reversal with an owned target is `CorrectCurrentTarget`.
- A gesture that began as acquisition remains acquisition when I becomes owned;
  it does not mutate D merely because ownership changed mid-gesture.
- Correction purpose is scoped to the owned I. Once that target is actually
  lost (not same-generation cue continuation), a held gesture returns to
  `AcquireTarget` and cannot rewrite the next person's D.
- An explicit boundary handover request changes the purpose to
  `HandoverTarget`.

The existing IntentFilter remains the only manual deadzone/noise owner. Gesture
purpose adds no strength threshold and no time window.

## D ownership

Vision publishes source point and R. TargetCoordinator owns D.

- `AcquireTarget`: manual input may control the camera and guide I, but cannot
  mutate D.
- `CorrectCurrentTarget`: filtered manual input moves D inside R using the
  existing traversal mapping.
- `HandoverTarget`: D no longer moves; selector handover owns the gesture.

The physical manual output path and permission to modify D are separate facts.
`manual_correction_x/y` means an axis actually modified D; it no longer means
merely that a physical axis was non-zero.

## Final 2-D residual solve

Definitions:

- `M`: bounded physical right-stick vector. It is always retained as the
  native baseline.
- `F`: filtered manual intent vector, used only to establish direction and
  remove stick noise.
- `A`: shaped AI proposal, interpreted as desired total target-directed stick,
  not an additive actuator force.
- `T`: final pre-recoil right-stick output.

Rules:

1. No authoritative/material AI: `T = M`.
2. No material filtered manual intent: `T = A`.
3. Otherwise compute two-dimensional cooperation:

```text
u = A / |A|
cooperation = clamp(dot(F, A) / (|F| |A|), 0, 1)
manual_along_ai = max(dot(M, u), 0)
missing = max(|A| - manual_along_ai, 0)
assist = cooperation * missing
T = M + bounded_to_unit_circle(assist * u)
```

Consequences:

- Exactly aligned, weaker manual becomes A: AI fills only the missing amount.
- Exactly aligned, stronger manual remains M: AI does not reduce it.
- Diagonal cooperation preserves the complete manual vector and adds only
  target-directed residual work; one opposite X/Y component cannot unload an
  otherwise cooperative 2-D gesture.
- Orthogonal or opposing manual receives native M without a sign threshold,
  delay, latch, handover ramp or competing axis owner.
- The AI contribution is capped by remaining unit-circle headroom; saturation
  never scales down M.

## Acceptance gates

1. A material gesture begun before target ownership leaves first-frame D at
   the Vision default and sets zero D-correction axes.
2. The same gesture begun after ownership moves D on both exercised axes.
3. Neutral manual produces the unchanged AI proposal.
4. Exactly aligned weak manual retains AI fill; exactly opposing manual returns
   native M.
5. A diagonal manual vector whose full 2-D relationship with AI is positive
   must not lose an axis merely because one component has the opposite sign.
6. Existing cue, handover, auto-fire, recoil and target identity regressions
   remain green.
7. Live acceptance separately checks ADS placement, native controllability,
   ADS authority and BodyLock correction rates. Gain tuning is deferred until
   these ownership gates pass.

## Explicit non-goals

- No new smoother, deadzone, hysteresis, elapsed-time takeover window or
  per-game controller branch.
- No additive `M + A` force stacking.
- No return of retired VectorIntentFuser, pending-work, projection or motion
  compensation owners.
- No simultaneous ADS gain change in the ownership rewrite.
