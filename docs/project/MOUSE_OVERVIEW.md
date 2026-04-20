# Mouse Controller Overview

Last updated: 2026-04-20

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
   - `reset()`
3. `MouseController` listens to physical mouse movement through `pynput`.
4. The host builds a `MouseFrame`.
5. The host starts from an empty `MouseOutput`.
6. Mouse plugins add movement and click behavior.
7. The final output is injected through `win32api.mouse_event(...)`.

## Important Boundary

Unlike `gamepad`, the current mouse path does not consume `ControllerTarget` metadata.

Today it only uses:

- `dx`
- `dy`
- `auto_fire`
- local aiming state from right-click

So the mouse path is simpler, but it also has less controller-side context than the gamepad path.

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

`AIAimPlugin` currently does four things:

- scales vision error through `gain`
- ramps assistance between an inner and outer radial deadzone
- smooths output with carry-based EMA
- counteracts part of the user's manual movement through `manual_dampen`

Current code defaults:

- `gain = 0.06`
- `smoothing = 0.65`
- `max_correction_px = 1.5`
- `deadzone_inner_px = 3.0`
- `deadzone_outer_px = 8.0`
- `manual_dampen = 0.4`

Current config loader support is narrower than the dataclass:

- `config.toml` under `[mouse.ai_aim]` currently exposes:
  - `gain`
  - `smoothing`
  - `max_correction_px`
  - `manual_dampen`
- the deadzone fields currently stay at code defaults unless the code is changed

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
- releasing right-click triggers `reset()`
- injected mouse deltas are subtracted back out of the accumulator so synthetic movement does not loop back in as fake manual input
- the loop runs at roughly `1000 Hz` through `time.sleep(0.001)`

This path is intentionally additive. It does not try to replace or intercept the physical mouse device.

## Startup

`mouse_start.bat` currently:

- enables `VISION_PERF_LOG=1`
- launches `main.py --controller-mode mouse`

It does not provide the gamepad startup prompts.

## Relationship To `kbm_to_gamepad`

`mouse` and `kbm_to_gamepad` are not the same thing.

- `mouse`
  - outputs native mouse events
  - keeps the physical mouse semantics
- `kbm_to_gamepad`
  - outputs a virtual Xbox 360 controller
  - translates mouse movement into right-stick motion

So `mouse` is the native-output path, while `kbm_to_gamepad` is the virtual-gamepad bridge path.
