# Mouse Plugin Architecture Design

## Goal

Bring the same plugin-based enhancement architecture from `controllers/gamepad/` to the mouse controller. The mouse path should support AI aim assist, auto-fire (left click), and recoil compensation as independent plugins, mirroring the gamepad design but operating in pixel space.

## Directory Layout

```
controllers/mouse/
  __init__.py                 # Public exports
  state.py                    # MouseFrame + MouseOutput
  plugin.py                   # MousePlugin protocol + apply/reset helpers
  ai_aim.py                   # AI aim correction plugin
  auto_fire.py                # Auto left-click plugin
  recoil_compensation.py      # Downward mouse pull during fire
```

Tests go under `tests/mouse/`.

## Data Structures

### MouseFrame (frozen dataclass)

Immutable snapshot of one frame's input plus vision signals.

| Field                | Type            | Description                                   |
|----------------------|-----------------|-----------------------------------------------|
| `timestamp`          | `float`         | `time.perf_counter()` at frame start          |
| `manual_dx`          | `float`         | User's physical mouse X delta this frame (px) |
| `manual_dy`          | `float`         | User's physical mouse Y delta this frame (px) |
| `is_aiming`          | `bool`          | Right mouse button held                       |
| `target_dx`          | `float`         | Vision target X offset (px)                   |
| `target_dy`          | `float`         | Vision target Y offset (px)                   |
| `auto_fire_requested`| `bool`          | Vision layer requests fire                    |
| `target_revision`    | `int`           | Monotonic vision update counter               |
| `target_timestamp`   | `float \| None` | When the vision update was produced            |

### MouseOutput (mutable dataclass)

Mutable output buffer that plugins modify before writeback.

| Field              | Type    | Default | Description                              |
|--------------------|---------|---------|------------------------------------------|
| `move_dx`          | `float` | `0.0`   | Pixel X delta to inject via mouse_event  |
| `move_dy`          | `float` | `0.0`   | Pixel Y delta to inject via mouse_event  |
| `left_click`       | `bool`  | `False` | Whether to hold left mouse button        |
| `auto_fire_active` | `bool`  | `False` | Flag for recoil plugin to read           |

Initial `move_dx/dy` is **0**, not the manual mouse delta. The physical mouse already moves the cursor; the program only injects additional corrections on top.

## Plugin Protocol

```python
class MousePlugin(Protocol):
    def reset(self) -> None: ...
    def apply(self, frame: MouseFrame, output: MouseOutput) -> None: ...
```

Helper functions: `apply_plugins(plugins, frame, output)` and `reset_plugins(plugins)`, identical in shape to the gamepad versions.

## Plugins

### AIAimPlugin

Converts vision-space target delta into pixel corrections added to `output.move_dx/dy`.

**Parameters:**
- `gain: float` (default 0.5) -- scales `target_dx/dy` into correction pixels
- `smoothing: float` (default 0.6) -- EMA carry factor
- `max_correction_px: float` (default 15.0) -- single-frame AI move cap
- `deadzone_inner_px: float` (default 2.0) -- below this, no correction
- `deadzone_outer_px: float` (default 5.0) -- soft ramp between inner and outer
- `fade_speed_px: float` (default 50.0) -- manual mouse speed (px/frame) at which AI fully fades out

**Logic:**
1. `desired = target_d * gain`, clamped to `max_correction_px`
2. Apply soft deadzone ramp based on radial distance
3. EMA: `carry = carry * smoothing + desired * (1 - smoothing)`
4. Compute manual mouse speed magnitude; scale AI output by `max(0, 1 - speed / fade_speed_px)`
5. Add result to `output.move_dx/dy`

Sub-plugins (HorizontalAssist, OvershootGuard) are **not** included in the initial version. They can be ported later once the base AI aim is validated.

### AutoFirePlugin

Simulates left mouse button press/release.

**Parameters:**
- `aim_only: bool` (default True) -- only fire while aiming

**Logic:**
1. `should_fire = frame.auto_fire_requested and (frame.is_aiming or not aim_only)`
2. Set `output.left_click = should_fire`
3. Set `output.auto_fire_active = should_fire`

The host tracks edge transitions (press/release) and calls `win32api.mouse_event` with `MOUSEEVENTF_LEFTDOWN` / `MOUSEEVENTF_LEFTUP`.

### RecoilCompensationPlugin

Adds downward mouse movement while auto-fire is active.

**Parameters:**
- `amount_px: float` (default 3.0) -- pixels to pull down per frame

**Logic:**
1. If `output.auto_fire_active` and `amount_px != 0`: `output.move_dy += amount_px`

## MouseController Host

Rewrites the existing `controllers/mouse_controller.py` to use the plugin architecture.

### Input Reading

Use `pynput.mouse.Listener` for:
- `on_move`: accumulate `manual_dx/dy` (delta from last position)
- `on_click`: track right-button state for `is_aiming`

Manual mouse deltas are captured for the AI fade calculation but are **not** forwarded through `output.move_dx/dy` -- the physical mouse already moves the cursor.

### Main Loop (Thread.run)

```
while running:
    1. Consume accumulated manual_dx/dy under lock
    2. Build MouseFrame with manual deltas + vision state (target_dx/dy, auto_fire_requested)
    3. Create MouseOutput (move_dx=0, move_dy=0, left_click=False)
    4. apply_plugins(plugins, frame, output)
    5. Execute output:
       - mouse_event(MOUSEEVENTF_MOVE, int(output.move_dx), int(output.move_dy))
       - Track left_click edge: press on rising, release on falling
    6. sleep(0.001)  # ~1000Hz
```

### Plugin Default Chain

```python
plugins = [
    AIAimPlugin(AIAimConfig()),
    AutoFirePlugin(AutoFireConfig()),
    RecoilCompensationPlugin(RecoilCompensationConfig()),
]
```

## Integration with ControllerFactory

`controller.py` already routes `--controller-mode mouse` to `MouseController`. No change needed to the factory or CLI args. The existing `mouse_start.bat` will continue to work.

## Output Backend Abstraction

The host writes output via `win32api.mouse_event`. If a future need arises (e.g., Interception driver for raw-input games), the output calls can be extracted into a small interface:

```python
class MouseBackend(Protocol):
    def move(self, dx: int, dy: int) -> None: ...
    def left_down(self) -> None: ...
    def left_up(self) -> None: ...
```

This is **not** built now -- just noted as a future extension point.

## What Is NOT in Scope

- HorizontalAssist / OvershootGuard sub-plugins (port later after base validation)
- Interception driver backend (add if `mouse_event` proves insufficient)
- KBMController changes (separate concern, remains as-is)
- Vision-layer changes (the controller interface `update/reset/set_auto_fire/is_aiming` is unchanged)

## Testing Plan

- Unit tests for each plugin in `tests/mouse/`
- Unit test for the host's frame building and output application
- Integration: manual test with `mouse_start.bat`, verify AI aim correction and auto-fire in a game or aim trainer
