# Controller Overview

Last updated: 2026-04-21

## Goal

The controller layer is responsible for:

- reading real user input
- receiving compact vision output
- applying controller-local execution logic
- writing the final output to the target device

Vision does not own final button mapping or output actuation. Controllers do.

## Entry Points

- `controller.py`
- `controllers/base_controller.py`
- `controllers/gamepad_controller.py`
- `controllers/mouse_controller.py`
- `controllers/kbm_controller.py`

`ControllerFactory.get_controller(...)` currently supports:

- `gamepad`
- `mouse`
- `kbm_to_gamepad`

Unknown modes fall back to the native mouse controller.

## Shared Interface

All controllers implement the `BaseController` contract:

- `update(dx, dy, target=None)`
- `reset()`
- `is_aiming()`
- `set_auto_fire(pressed)`
- `stop()`

Compatibility note:

- `set_auto_rb(...)` still exists as a compatibility alias, but vision now calls the generic `set_auto_fire(...)` path

The richest shared metadata type is `ControllerTarget`:

- `aim_point_x`
- `aim_point_y`
- `screen_center_x`
- `screen_center_y`
- `body_box`

Current usage:

- `gamepad` consumes `ControllerTarget`
- `mouse` currently ignores `target`
- `kbm_to_gamepad` currently ignores `target`

## Vision To Controller Boundary

The vision runner currently calls only these controller hooks:

- `is_aiming()`
- `update(dx, dy, target=ControllerTarget | None)`
- `set_auto_fire(bool)`
- `reset()`

That boundary is intentionally narrow.

Vision decides:

- target selection
- target delta
- auto-fire recommendation

Controller decides:

- how user input is read
- how AI output mixes with local input
- what physical or virtual device receives the final output

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
- currently consumes `dx/dy + auto_fire` only

### `kbm_to_gamepad`

File:

- `controllers/kbm_controller.py`

Purpose:

- translate keyboard and mouse input into a virtual Xbox 360 controller
- layer simple AI aim on top of that virtual stick output

Current shape:

- older, simpler host
- not plugin-based like `gamepad` and `mouse`
- currently consumes `dx/dy + auto_fire` only

## Startup Paths

Current scripts:

- `gamepad_start.bat`
  - launches `main.py --controller-mode gamepad`
  - prompts for auto-fire output: `RB` or `RT`
  - sets vision perf logging, fast-path, and capture FPS env defaults
- `mouse_start.bat`
  - launches `main.py --controller-mode mouse`
  - does not offer the gamepad startup prompts

Current gap:

- there is no dedicated `kbm_to_gamepad` start script

## Configuration Sources

Current controller-related configuration comes from two places:

1. `config.toml`
   - loaded through `config/loader.py`
   - currently exposes:
     - `gamepad.ai_aim`
     - `gamepad.adaptive_delta_gain`
     - `mouse.ai_aim`
2. startup or CLI choices
   - `--controller-mode`
   - `--auto-fire-output`
   - `gamepad_start.bat` prompts

Important limitation:

- not every runtime default is exposed through the config loader today
- some defaults are still instantiated directly in controller code
- `gamepad.adaptive_delta_gain` is still present as a config surface, but it is not part of the default `GamepadController` plugin chain today

## Current Recommendation

For current assisted play and ongoing tuning:

- use `gamepad` when you want the most mature controller path
- use `mouse` when you want native mouse output and a smaller feature surface
- treat `kbm_to_gamepad` as a supported but less structured path

## Related Documents

- `docs/project/GAMEPAD_OVERVIEW.md`
- `docs/project/MOUSE_OVERVIEW.md`
- `docs/project/VISION_OVERVIEW.md`
