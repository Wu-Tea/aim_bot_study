# Controller Overview

Last updated: 2026-04-30

## Goal

The controller layer is responsible for:

- reading real user input
- receiving compact vision output
- applying controller-local execution logic
- writing the final output to the target device

Vision does not own final actuation. Controllers do.

## Entry Points

- `controller.py`
- `controllers/base_controller.py`
- `controllers/gamepad_controller.py`
- `controllers/mouse_controller.py`
- `controllers/kbm_controller.py`

`ControllerFactory.get_controller(...)` supports:

- `gamepad`
- `mouse`
- `kbm_to_gamepad`

Unknown modes fall back to the native mouse controller.

## Shared Interface

All controllers implement the `BaseController` contract:

- `update(dx, dy, target=None)`
- `reset()`
- `clear_target()`
- `is_aiming()`
- `set_auto_fire(pressed)`
- `stop()`

Compatibility note:

- `set_auto_rb(...)` still exists as an alias, but the main vision paths now use `set_auto_fire(...)`

The shared metadata type is `ControllerTarget`:

- `aim_point_x`
- `aim_point_y`
- `screen_center_x`
- `screen_center_y`
- `body_box`
- `target_source`

Current usage:

- `gamepad` consumes `ControllerTarget`
- `mouse` consumes `ControllerTarget` metadata for continuity-aware behavior
- `kbm_to_gamepad` remains the simpler path and does not currently use the richer target metadata

## Vision To Controller Boundary

Both vision backends call the same narrow controller surface:

- `is_aiming()`
- `update(dx, dy, target=ControllerTarget | None)`
- `clear_target()`
- `set_auto_fire(bool)`
- `reset()`

That boundary is intentionally small so the project can switch between Python and native vision without repeatedly redesigning controller code.

Vision decides:

- target selection
- target delta
- target continuity metadata
- auto-fire recommendation

Controller decides:

- how user input is read
- how AI output mixes with local input
- which physical or virtual device receives the final output

## Current Modes

### `gamepad`

File:

- `controllers/gamepad_controller.py`

Purpose:

- mirror a physical gamepad into a virtual Xbox 360 pad
- run controller-local plugins before final writeback

Current shape:

- plugin-based
- uses `ControllerTarget` metadata
- current primary assisted mode

### `mouse`

File:

- `controllers/mouse_controller.py`

Purpose:

- keep the physical mouse active
- inject additional `mouse_event` deltas and auto-fire clicks

Current shape:

- plugin-based
- narrower than the gamepad path
- additive native mouse output instead of virtual-stick output
- includes target-continuity behavior for ADS entry / commit-hold / bridge behavior

### `kbm_to_gamepad`

File:

- `controllers/kbm_controller.py`

Purpose:

- translate keyboard and mouse input into a virtual Xbox 360 controller
- layer simple AI aim on top of that virtual stick output

Current shape:

- older, simpler host
- not plugin-based like `gamepad` and `mouse`
- supported, but not the most actively iterated path

## Startup Paths

Current scripts:

- `gamepad_start.bat`
  - launches `main.py --controller-mode gamepad`
  - prompts for auto-fire output: `RB` or `RT`
  - defaults to `VISION_BACKEND=native`
  - enables perf logging
- `gamepad_debug.bat`
  - launches gamepad mode with debug window and frame saving
  - lets you choose native vs Python backend
- `gamepad_native_debug.bat`
  - forces native gamepad debug
- `mouse_start.bat`
  - launches `main.py --controller-mode mouse`
  - defaults to native vision
- `mouse_native_debug.bat`
  - launches the native mouse path with debug window and debug-frame saving

Current gap:

- there is still no dedicated `kbm_to_gamepad` start script

## Configuration Sources

Controller-related configuration comes from two places:

1. `config.toml`
   - loaded through `config/loader.py`
   - currently exposes:
     - `gamepad.ai_aim`
     - `gamepad.adaptive_delta_gain`
     - `mouse.ai_aim`
2. startup or CLI choices
   - `--controller-mode`
   - `--auto-fire-output`
   - `--vision-backend`
   - the `.bat` script prompts and env defaults

Important limitation:

- not every runtime default is exposed through the config loader today
- some defaults are still instantiated directly in controller code
- some startup behavior still lives in the `.bat` wrappers rather than in `config.toml`

## Current Recommendation

For current assisted play and ongoing tuning:

- use `gamepad` when you want the most mature controller path
- use `mouse` when you want native mouse output and a smaller feature surface
- treat `kbm_to_gamepad` as supported but less actively structured

## Related Documents

- `docs/project/GAMEPAD_OVERVIEW.md`
- `docs/project/MOUSE_OVERVIEW.md`
- `docs/project/VISION_OVERVIEW.md`
