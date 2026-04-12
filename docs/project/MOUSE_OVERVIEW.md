# Mouse Controller Overview

## Goal

The mouse path mirrors the gamepad plugin architecture but operates in pixel space. AI corrections are injected as additional `mouse_event` deltas on top of the physical mouse movement. The physical mouse is never intercepted or replaced — the program acts as an additive signal.

## Directory layout

- `controllers/mouse_controller.py`
  - mouse host loop
  - pynput listener for physical mouse input
  - win32api output (mouse_event)
  - plugin invocation
- `controllers/mouse/`
  - plugin contracts and mouse-specific enhancement modules
- `tests/mouse/`
  - mouse plugin and host tests
- `mouse_start.bat`
  - start mouse mode

## Runtime flow

1. `main.py --controller-mode mouse` creates `MouseController` via `ControllerFactory`.
2. `vision/runner.py` drives detection and targeting.
3. Vision sends signals into the controller:
   - `controller.update(dx, dy)` for target offset
   - `controller.set_auto_fire(pressed)` for fire intent
   - `controller.reset()` when no valid target
4. `MouseController` reads physical mouse movement via pynput, builds a `MouseFrame`, creates a mutable `MouseOutput`, then runs the plugin chain.
5. The final `MouseOutput` is applied: `mouse_event(MOVE)` for aim correction, `mouse_event(LEFTDOWN/LEFTUP)` for auto-fire.

## Key difference from gamepad

The gamepad outputs stick values (-32768 to 32767) which are velocities. The mouse outputs pixel deltas which are position increments. This means:

- Parameters must be much smaller (the loop runs at ~1000Hz)
- The AI philosophy is inverted: gamepad fades AI when user moves the stick hard; mouse dampens user movement when AI has a target (`manual_dampen`)
- Physical mouse movement is captured for dampening calculation but not re-injected (the physical device already moves the cursor)

## Data structures

`controllers/mouse/state.py` defines:

- `MouseFrame` — immutable snapshot of one frame: manual mouse delta, vision target offset, aiming state, auto-fire request
- `MouseOutput` — mutable output buffer: `move_dx/dy` (pixel delta to inject), `left_click` (auto-fire state), `auto_fire_active` (flag for recoil plugin)

`output.move_dx/dy` starts at **0**, not at the manual mouse delta.

## Plugin contracts

`controllers/mouse/plugin.py` defines:

- `MousePlugin` protocol: `reset()` and `apply(frame, output)`
- `apply_plugins(plugins, frame, output)` and `reset_plugins(plugins)`

## Current plugins

### AIAimPlugin (`controllers/mouse/ai_aim.py`)

Converts vision target delta into pixel corrections.

- `gain` (0.06): scales target offset into correction pixels
- `smoothing` (0.65): EMA carry factor between frames
- `max_correction_px` (1.5): single-frame AI move cap
- `deadzone_inner_px` (3.0) / `deadzone_outer_px` (8.0): soft ramp based on raw target distance
- `manual_dampen` (0.4): fraction of user's physical mouse movement to counteract when AI has a target, making the cursor "stickier" near the target

Deadzone is applied to the raw target offset (pixel space) before gain scaling, not to the post-gain value.

Sub-plugins (HorizontalAssist, OvershootGuard) are not yet ported from the gamepad side.

### AutoFirePlugin (`controllers/mouse/auto_fire.py`)

Simulates left mouse button press with pulse timing.

- `hold_seconds` (0.120): how long to hold the click
- `release_seconds` (0.030): gap between pulses
- `aim_only` (True): only fire while right-click is held

The pulse cycle prevents continuous hold, which some games may not register properly. The host sends `LEFTUP` before every `LEFTDOWN` to ensure a clean press edge.

### RecoilCompensationPlugin (`controllers/mouse/recoil_compensation.py`)

Adds downward mouse delta during auto-fire.

- `amount_px` (0.80): pixels to pull down per frame (~800px/sec at 1000Hz)

## Host implementation notes

### Feedback loop prevention

`pynput.Listener.on_move` captures all cursor movement including our synthetic `mouse_event` injections. After injecting movement, the host subtracts the injected amount from the accumulator to prevent a feedback loop.

### Auto-fire click edge

The host sends `LEFTUP` before every `LEFTDOWN` transition to create a guaranteed clean press edge. This ensures the game recognizes a fresh click even if the button was already held. `pynput` cannot reliably distinguish synthetic from physical clicks, so manual-click tracking was removed in favor of this approach.

### Output backend

Currently uses `win32api.mouse_event`. If a game uses Raw Input and ignores synthetic input, an Interception driver backend could be added as an alternative.

## Tuned parameters (2026-04-12)

These defaults were tuned through playtesting:

| Parameter | Value | Rationale |
|-----------|-------|-----------|
| `gain` | 0.06 | 50px offset → 3px raw, keeps corrections subtle |
| `max_correction_px` | 1.5 | At 1000Hz, 1.5px/frame ≈ 1500px/sec max speed |
| `smoothing` | 0.65 | Balances responsiveness and jitter |
| `manual_dampen` | 0.4 | 40% user movement suppression near target |
| `recoil amount_px` | 0.80 | ~800px/sec downward pull during fire |
| `hold_seconds` | 0.120 | Pulse fire hold duration |
| `release_seconds` | 0.030 | Pulse fire gap |
