# Gamepad Overview

Last updated: 2026-05-06

## Goal

The current gamepad path is a plugin-driven controller host that:

- reads a physical gamepad
- mirrors it to a virtual Xbox 360 controller
- mixes controller-local AI assistance into the output
- keeps the vision boundary compact

This is still the most mature controller mode in the repository today.

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

This is still chosen at startup time rather than through `config.toml`.

## Recoil Compensation

`RecoilCompensationPlugin` now supports two modes:

- legacy fixed-pull fallback when no recoil-profile provider is wired in
- profile-driven curve playback when the host can resolve an active recoil profile

The profile-driven path:

- reads the current active profile from the runtime recoil sidecar contract
- advances the curve only while `auto_fire_active` is true
- treats stored recoil samples as collector screen-response curves and maps them into incremental right-stick deltas
- resets playback when firing stops, the active weapon profile changes, or the sidecar falls out of `ready`

The current host keeps the integration conservative:

- if `RECOIL_PROFILE_DIR` and `RECOIL_RECOGNIZER_STATE_PATH` are both available, the host builds a `RecoilSidecarService` client and enables profile-driven recoil
- if those paths are not configured, the host keeps the old fixed fallback through `RecoilCompensationConfig(amount=0.20)`

This means the gamepad layer still does not perform weapon recognition or CV work itself. It only consumes already-resolved sidecar state.

## Direct-Use Recoil Workflow

The recoil system is now usable as a standalone sidecar flow around the existing gamepad host.

Recommended workflow:

1. Capture a weapon signature and identity record:
   - `python tools/weapon_signature_capture.py --game cod22 --canonical-weapon-id cod22-m4 --display-name M4 --weapon-family assault_rifle --signature-dir artifacts/weapon_signatures`
2. Collect a recoil profile for that weapon:
   - `python tools/recoil_collector.py --game cod22 --mode ads --standing-only --profile-dir artifacts/recoil_profiles --signature-dir artifacts/weapon_signatures --output artifacts/recoil_profiles/latest-summary.json`
3. Launch recognizer plus gamepad together:
   - `python tools/recoil_runtime_launcher.py --game cod22 --profile-dir artifacts/recoil_profiles --signature-dir artifacts/weapon_signatures --controller-mode gamepad --auto-fire-output RB`

Runtime files and directories:

- weapon signatures live under `artifacts/weapon_signatures/`
- recoil profiles live under `artifacts/recoil_profiles/`
- live recognizer state defaults to `artifacts/recoil_state/<game>-latest-state.json`

The recognizer now defaults to full-screen capture for live HUD work and uses OCR on the configured weapon-name ROI when available. The gamepad host still only reads the latest state file and matched profile data; it does not run OCR or template matching internally.

## Startup And Scripts

Current gamepad entry points:

- `gamepad_start.bat`
  - main gamepad runtime
  - prompts for auto-fire output: `RB` or `RT`
  - defaults to `VISION_BACKEND=native`
  - enables `VISION_PERF_LOG=1`
  - currently defaults `VISION_CAPTURE_FPS=140`
  - disables the old quit hotkey through `VISION_QUIT_KEY=0`
- `gamepad_debug.bat`
  - gamepad debug runtime
  - prompts for auto-fire output and backend choice
  - enables `--vision-debug --vision-debug-save`
  - defaults `VISION_CAPTURE_FPS=140`
- `gamepad_native_debug.bat`
  - native-only debug runtime
  - enables `--vision-debug`
  - defaults `VISION_CAPTURE_FPS=140`

The default production gamepad path is now the hybrid runtime:

- native C++ for the hot vision loop
- Python for the controller host, startup scripts, and debug wrapper logic

`gamepad_start.bat` now has an opt-in recoil-runtime path:

- set `ENABLE_RECOIL_RUNTIME=1`
- optionally set:
  - `RECOIL_GAME`
  - `RECOIL_PROFILE_DIR`
  - `RECOIL_SIGNATURE_DIR`
  - `RECOIL_STATE_FILE`
  - `RECOIL_RECOGNIZER_FPS`
- then launch `gamepad_start.bat` normally

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

That split is intentional. The current native migration changed the vision runtime, but it deliberately did not move final controller actuation out of Python.
