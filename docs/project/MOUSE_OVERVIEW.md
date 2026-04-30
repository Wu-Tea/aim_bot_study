# Mouse Controller Overview

Last updated: 2026-04-30

## Goal

The native mouse path keeps the physical mouse active and injects extra correction deltas on top of it.

It is designed for:

- additive AI mouse correction
- optional pulse auto-fire
- simple recoil pull-down

It is narrower than the gamepad path, but it is a real current implementation, not a placeholder.

## Main Files

- `controllers/mouse_controller.py`
- `controllers/mouse/state.py`
- `controllers/mouse/plugin.py`
- `controllers/mouse/ai_aim.py`
- `controllers/mouse/auto_fire.py`
- `controllers/mouse/recoil_compensation.py`

## Runtime Flow

1. `main.py --controller-mode mouse` creates `MouseController`.
2. Vision sends:
   - `update(dx, dy, target=ControllerTarget | None)`
   - `set_auto_fire(bool)`
   - `clear_target()` for transient aiming frames with no target
   - `reset()`
3. `MouseController` listens to physical mouse movement through `pynput`.
4. The host builds a `MouseFrame`.
5. The host starts from an empty `MouseOutput`.
6. Mouse plugins add movement and click behavior.
7. The final output is injected through `win32api.mouse_event(...)`.

## Important Boundary

The mouse host still does not choose targets on its own, but `AIAimPlugin`
does consume `ControllerTarget` metadata from vision:

- `target_source`
  - `observed` can start a fresh aggressive acquire or direct stabilize
  - `reconstructed` can also start fresh acquire/stabilize when vision has
    rebuilt the same target strongly enough
  - `predicted` never starts a fresh acquire; it only keeps stabilize alive for
    the same committed target family
- `body_box`
  - used to decide whether a reconstructed or predicted target is still the same
    target family instead of a silent switch

So the boundary is:

- vision remains responsible for selection/tracking
- mouse plugins decide whether the current target metadata is eligible for
  aggressive acquire, near-target stabilize, or manual fallback

## Data Structures

`controllers/mouse/state.py` defines:

- `MouseFrame`
  - immutable snapshot of manual mouse delta, aiming state, vision delta, and auto-fire request
- `MouseOutput`
  - mutable output buffer with:
    - `move_dx`
    - `move_dy`
    - `left_click`
    - `auto_fire_active`

Unlike gamepad output, `MouseOutput` starts at zero. The physical mouse has already moved the cursor on its own.

## Current Plugin Chain

The default plugin chain created by `MouseController` is:

1. `AIAimPlugin`
2. `AutoFirePlugin`
3. `RecoilCompensationPlugin`

## Current AI Aim Behavior

`AIAimPlugin` now uses a four-stage model instead of the retired
entry/hold/bridge path.

Current modes:

- `acquire_far`
  - the main long-range ADS pull phase
  - uses the strongest per-frame movement for `observed` and `reconstructed`
    targets outside the midrange band
- `acquire_mid`
  - takes over once the error has been reduced into the midrange band
  - still pulls decisively, but with less per-frame movement than `acquire_far`
    so the cursor does not fly past the target as often on strafe scenarios
- `reacquire`
  - only arms after a transient no-target gap while the same target family is
    still inside the short continuity window
  - gives a short, stronger-than-mid burst to reconnect to the same fight
    without turning every normal stabilize exit into a burst
- `motion lead`
  - `acquire_far`, `acquire_mid`, and `reacquire` can all add a small same-
    target motion lead based on successive aim-point movement
  - this is bounded and only applies when the current target still matches the
    last seen target family, so it helps strafe/diagonal scenarios without
    blindly chasing target switches
- `chase hold`
  - when the same target is still moving outward along the current error vector
    fast enough, the plugin can keep `acquire_far` active instead of dropping to
    `acquire_mid` or `stabilize` too early
  - this is mainly for strafe/diagonal cases where the midrange cap would
    otherwise fail to catch up before the target decelerates
- `stabilize`
  - activates once the crosshair is already near the target
  - activates later than before, so mouse assist spends more time pulling the
    crosshair in before switching to fine correction
  - uses lower per-frame movement and hysteresis so close-range correction does
    not chatter between help/no-help on every frame
  - scales down inside the inner release band instead of hard-zeroing, so tiny
    near-center corrections can still accumulate instead of pausing completely
  - if the same target starts pulling away on-screen and error grows sharply,
    it immediately falls back to `acquire_mid` instead of idling in a weak
    stabilize
  - can continue briefly through `predicted` targets, but only for the same
    target family and only inside the grace window
- `manual`
  - emits no AI movement
  - transient no-target aiming frames fall back here without wiping the current
    stabilize continuity context

Current config loader support for `[mouse.ai_aim]` is:

- `acquire_radius_px`
- `mid_acquire_enter_px`
- `mid_acquire_exit_px`
- `stabilize_enter_px`
- `stabilize_exit_px`
- `inner_release_band_px`
- `stabilize_reacquire_growth_px`
- `stabilize_reacquire_motion_px`
- `acquire_gain`
- `mid_acquire_gain`
- `reacquire_gain`
- `stabilize_gain`
- `predicted_stabilize_gain`
- `acquire_max_move_px`
- `mid_acquire_max_move_px`
- `reacquire_max_move_px`
- `stabilize_max_move_px`
- `predicted_stabilize_max_move_px`
- `acquire_lead_seconds`
- `mid_acquire_lead_seconds`
- `reacquire_lead_seconds`
- `acquire_lead_max_px`
- `same_target_grace_ms`
- `reacquire_radius_px`
- `reacquire_window_ms`
- `chase_hold_projection_px_per_sec`
- `chase_hold_min_radius_px`
- `breakaway_speed_px`

## AutoFire

`AutoFirePlugin` uses a pulse cycle rather than a continuous held click.

Current defaults:

- `aim_only = True`
- `hold_seconds = 0.120`
- `release_seconds = 0.030`

The host also forces a clean click edge by sending `LEFTUP` before a new `LEFTDOWN`.

## Recoil Compensation

`RecoilCompensationPlugin` adds downward mouse delta when `auto_fire_active` is true.

Current default:

- `amount_px = 0.80`

Unlike the gamepad path, mouse recoil and mouse auto-fire are not currently exposed through the shared config loader.

## Host Implementation Notes

Important host details in `MouseController`:

- right-click controls aiming state
- transient aiming frames with no target use `clear_target()`
  - clears target deltas/metadata without resetting plugins
- releasing right-click still triggers full `reset()`
- injected mouse deltas are subtracted back out of the accumulator so synthetic movement does not loop back in as fake manual input
- the loop runs at roughly `1000 Hz` through `time.sleep(0.001)`

This path is intentionally additive. It does not try to replace or intercept the physical mouse device.

## Startup

`mouse_start.bat` currently:

- enables `VISION_PERF_LOG=1`
- defaults `VISION_BACKEND=native`
- defaults `VISION_CAPTURE_FPS=140`
- defaults `VISION_QUIT_KEY=0`
- launches `main.py --controller-mode mouse --vision-backend native --perf-log`

`mouse_native_debug.bat` currently:

- enables the same native defaults as `mouse_start.bat`
- launches `main.py --controller-mode mouse --vision-backend native --vision-debug --vision-debug-save --perf-log`

Neither script provides the gamepad startup prompts.

## Relationship To `kbm_to_gamepad`

`mouse` and `kbm_to_gamepad` are not the same thing.

- `mouse`
  - outputs native mouse events
  - keeps the physical mouse semantics
- `kbm_to_gamepad`
  - outputs a virtual Xbox 360 controller
  - translates mouse movement into right-stick motion

So `mouse` is the native-output path, while `kbm_to_gamepad` is the virtual-gamepad bridge path.
