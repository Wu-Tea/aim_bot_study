# Gamepad Overview

Last updated: 2026-04-20

## Goal

The current gamepad path is a plugin-driven controller host that:

- reads a physical gamepad
- mirrors it to a virtual Xbox 360 controller
- mixes controller-local AI assistance into the output
- keeps the vision boundary compact

This is the most mature controller mode in the repository today.

## Main Files

- `controllers/gamepad_controller.py`
- `controllers/gamepad/state.py`
- `controllers/gamepad/plugin.py`
- `controllers/gamepad/ai_aim.py`
- `controllers/gamepad/auto_fire.py`
- `controllers/gamepad/recoil_compensation.py`
- `controllers/gamepad/diagnostics.py`

Support and legacy modules still present in the folder:

- `adaptive_delta_gain.py`
- `horizontal_assist.py`
- `manual_intent_guard.py`
- `overshoot_guard.py`
- `legacy_ai_aim.py`

Those support files still matter for experiments, old benchmark context, and implementation history, but they are not the default host pipeline created by `GamepadController`.

## Runtime Flow

1. `main.py` asks `ControllerFactory` for `gamepad`.
2. `GamepadController` starts a thread, opens the physical pad through `pygame`, and opens a virtual Xbox 360 pad through `vgamepad`.
3. Vision sends only:
   - `update(dx, dy, target=ControllerTarget | None)`
   - `set_auto_fire(bool)`
   - `reset()`
4. The host reads physical input and builds a `GamepadFrame`.
5. The host seeds a mutable `GamepadOutput` from physical passthrough values.
6. The plugin chain mutates `GamepadOutput`.
7. The final output is written to the virtual pad.

## Host Responsibilities

`GamepadController` is the host, not the place for feature sprawl.

Current host responsibilities are:

- initialize `pygame`
- initialize `vgamepad`
- read sticks, triggers, buttons, and D-pad
- track aiming state from the left trigger
- store the latest vision delta and target metadata
- build `GamepadFrame`
- seed `GamepadOutput` with passthrough values
- run plugins
- write the final virtual-controller state

The host intentionally preserves manual stick values before plugins run. It does not silently deadzone away small right-stick inputs at the host layer.

## Shared Data Structures

`controllers/gamepad/state.py` defines:

- `GamepadFrame`
  - immutable snapshot of one controller frame
  - includes manual input, aiming state, latest vision signal, auto-fire request, and optional `ControllerTarget`
- `GamepadOutput`
  - mutable output buffer written by plugins before final device output

`ControllerTarget` currently carries:

- aim point
- screen center
- optional `body_box`

This extra target metadata is what allows controller-side `Body Lock` behavior without moving final actuation logic into vision.

## Current Plugin Chain

The default plugin chain created in `GamepadController.__init__` is:

1. `AIAimPlugin`
2. `AutoFirePlugin`
3. `RecoilCompensationPlugin`

Optional diagnostics:

- `DownwardPullDiagnostics` can record plugin traces when enabled through env vars

## Current AI Aim Behavior

`AIAimPlugin` is currently a state-machine-style controller plugin.

Primary modes:

- `manual`
  - passthrough baseline when there is no active assistance window
- `ads_snap`
  - short first-ADS reposition window
- `body_lock`
  - sticky upper-body correction when the crosshair is already close and the target body box supports a lock

Important details:

- `ads_snap_window_ms` defaults to `100`
- body lock only engages when the crosshair is already inside the target body box plus tolerance
- body lock uses upper-body aim instead of generic box center
- body lock has controller-side motion compensation after continuity is established
- manual input is not simply overwritten
  - same-direction help can be preserved
  - harmful opposing input can be suppressed
  - orthogonal wobble can be damped near lock
- near zero-crossing, axis guards intentionally hold correction briefly to reduce oscillation

Current tuning for `AIAimPlugin` comes from:

- code defaults in `controllers/gamepad/ai_aim.py`
- optional overrides from `config.toml` under `[gamepad.ai_aim]`

## AutoFire

`AutoFirePlugin` converts vision fire intent into actual controller output.

Current behavior:

- `aim_only = True` by default
- final output can be either:
  - `RB`
  - `RT`

Current source of that choice:

- CLI: `--auto-fire-output`
- `gamepad_start.bat` prompt

This is not currently driven by `config.toml`.

## Recoil Compensation

`RecoilCompensationPlugin` adds downward right-stick pull while `auto_fire_active` is true.

Important current nuance:

- the plugin class default is `amount = 0.30`
- the host currently instantiates it with `RecoilCompensationConfig(amount=0.20)`

So the live default path is the host-chosen `0.20`, not the dataclass default.

## Startup And Scripts

`gamepad_start.bat` currently:

- enables `VISION_PERF_LOG=1`
- enables the vision fast path unless the env already overrides it
- defaults `VISION_CAPTURE_FPS=80`
- defaults `VISION_IDLE_CAPTURE_FPS=10`
- prompts for:
  - auto-fire output
  - vision preprocessor

If `native` is selected for the preprocessor, the script prints a reminder that it still falls back to CPU when `vision_native` is unavailable.

## Legacy And Support Notes

The repository still contains older gamepad-side support modules for:

- adaptive gain
- horizontal assist
- manual-intent arbitration helpers
- overshoot control
- older legacy AI-aim structure

These are still useful reference material, benchmark context, or experimental tooling. They should not be described as the default runtime path unless the controller host is explicitly changed to instantiate them.

## Current Boundary With Vision

The gamepad controller expects vision to decide:

- what target to trust
- what target delta to send
- when auto-fire should be requested

The gamepad layer decides:

- how to mix AI with live manual stick input
- how to map fire output
- how to shape stick actuation on the virtual pad

That split is intentional and should stay stable unless the project deliberately moves execution logic out of Python controller code.
