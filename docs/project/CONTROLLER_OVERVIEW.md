# Controller Overview

Last updated: 2026-06-11

## Goal

The controller layer is responsible for:

- reading real user input
- receiving compact vision output
- applying controller-local execution logic
- writing the final output to the target device

Vision does not own final actuation. Controllers do.

For live gamepad play, the default controller layer is now native C++ under
`native/controller_native/` and is driven by `cod_native_runtime.exe`. The
Python controller layer remains available for fallback gamepad mode, mouse,
`kbm_to_gamepad`, debug tooling, and tests.

## Entry Points

- `controller.py`
- `controllers/factory.py`
- `controllers/base_controller.py`
- `controllers/gamepad_controller.py`
- `controllers/mouse_controller.py`
- `controllers/kbm_controller.py`
- `native/runtime_app/main.cpp`
- `native/runtime_app/runtime_loop.cpp`
- `native/controller_native/native_gamepad_controller.cpp`

`ControllerFactory.get_controller(...)` now lives in `controllers/factory.py`. Root
`controller.py` remains as a compatibility shim for older imports.

The factory supports:

- `gamepad`
- `mouse`
- `kbm_to_gamepad`

Unknown modes fall back to the native mouse controller.

## Shared Interface

Python controllers implement the `BaseController` contract:

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

Default native gamepad runtime:

- `native/vision_native` emits `VisionResult`
- `native/runtime_app` passes that result directly to `NativeGamepadController`
- no Python controller or pybind handoff is required during normal gamepad play

Python fallback and non-gamepad modes call the same narrow controller surface:

- `is_aiming()`
- `update(dx, dy, target=ControllerTarget | None)`
- `clear_target()`
- `set_auto_fire(bool)`
- `reset()`

That Python boundary stays intentionally small so fallback and debug modes can switch between Python and native vision without repeatedly redesigning controller code.

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

Default files:

- `native/runtime_app/runtime_loop.cpp`
- `native/controller_native/native_gamepad_controller.cpp`
- `native/controller_native/xinput_reader.cpp`
- `native/controller_native/sdl_gamepad_reader.cpp`
- `native/controller_native/virtual_gamepad.cpp`

Python fallback file:

- `controllers/gamepad_controller.py`

Purpose:

- mirror a physical gamepad into a virtual Xbox 360 pad
- run controller-local AI aim, auto-fire, aim-assist dynamics, and recoil before final writeback

Current shape:

- native C++ by default
- uses `VisionResult` authority and target metadata
- Python plugin host remains available behind `GAMEPAD_RUNTIME=python`
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

- `scripts\launch\gamepad_start.bat`
  - launches the full native C++ gamepad runtime by default
  - set `GAMEPAD_RUNTIME=python` to launch `main.py --controller-mode gamepad`
- `scripts\launch\gamepad_native_cpp_start.bat`
  - runs `native\vision_native\build\Release\cod_native_runtime.exe --config config.toml --perf-log`
  - prompts for auto-fire output: `RB` or `RT`
  - prompts for native recoil profile selection
- `scripts\launch\debug\gamepad_debug.bat`
  - launches Python-hosted gamepad mode with debug window and frame saving
  - lets you choose native vs Python backend
- `scripts\launch\debug\gamepad_native_debug.bat`
  - forces the Python-hosted native-vision debug bridge
- `scripts\launch\mouse_start.bat`
  - launches `main.py --controller-mode mouse`
  - defaults to native vision
- `scripts\launch\debug\mouse_native_debug.bat`
  - launches the native mouse path with debug window and debug-frame saving

Current gap:

- there is still no dedicated `kbm_to_gamepad` start script

## Configuration Sources

Controller-related configuration comes from two places:

1. `config.toml`
   - loaded through `config/loader.py`
   - currently exposes:
     - `runtime.vision`
     - `runtime.gamepad`
     - `gamepad.ai_aim`
     - `gamepad.adaptive_delta_gain`
     - `mouse.ai_aim`
2. startup or CLI choices
   - `GAMEPAD_RUNTIME`
   - `--controller-mode`
   - `--auto-fire-output`
   - `--vision-backend`
   - the `.bat` script prompts and existing environment overrides

Important notes:

- live gamepad runtime defaults should be checked in `native/controller_native/runtime_config.h` and `config/loader.py`
- some fallback defaults are still instantiated directly in Python controller code
- debug-specific startup behavior still lives in the `.bat` wrappers

## Current Recommendation

For current assisted play and ongoing tuning:

- use `scripts\launch\gamepad_start.bat` for the current full native C++ gamepad path
- set `GAMEPAD_RUNTIME=python` only for fallback/comparison work
- use `mouse` when you want native mouse output and a smaller feature surface
- treat `kbm_to_gamepad` as supported but less actively structured

## Related Documents

- `docs/project/GAMEPAD_OVERVIEW.md`
- `docs/project/MOUSE_OVERVIEW.md`
- `docs/project/VISION_OVERVIEW.md`
