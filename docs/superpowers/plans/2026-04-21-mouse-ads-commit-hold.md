# Mouse ADS Commit-Hold Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the current native mouse assist with the new ADS-entry / commit-hold / reacquire-bridge state machine while expanding the controller target contract to preserve `target_source`.

**Architecture:** Keep `vision` responsible for target choice, body-box geometry, and auto-fire recommendation, but expand the controller-facing metadata so the mouse controller can tell `observed` from `reconstructed` and `predicted`. Keep mouse `auto_fire` and `recoil_compensation` as independent plugins, and rewrite only the mouse AI plugin plus host state so mouse actuation becomes phase-aware instead of one flat correction law.

**Tech Stack:** Python 3.11, unittest, dataclasses, pynput, win32api, existing `vision.runner` / `vision.native_runner`, existing config loader

---

## File Map

- Modify: `controllers/base_controller.py`
- Modify: `vision/runner.py`
- Modify: `vision/native_runner.py`
- Modify: `controllers/mouse/state.py`
- Modify: `controllers/mouse/ai_aim.py`
- Modify: `controllers/mouse/__init__.py`
- Modify: `controllers/mouse_controller.py`
- Modify: `config/loader.py`
- Modify: `tests/test_vision_runner.py`
- Modify: `tests/test_native_vision_runner.py`
- Modify: `tests/test_config_loader.py`
- Modify: `tests/mouse/test_mouse_state.py`
- Modify: `tests/mouse/test_mouse_ai_aim.py`
- Modify: `tests/mouse/test_mouse_controller_host.py`
- Modify: `tests/mouse/test_mouse_plugin_chain.py`
- Create: `tests/mouse/test_mouse_ai_aim_sequences.py`

## Implementation Notes

- Keep `ControllerTarget` backward-compatible for gamepad by adding `target_source` as an optional final field with default `None`.
- Keep `MouseOutput` shape stable so `AutoFirePlugin` and `RecoilCompensationPlugin` remain reusable.
- Replace old mouse `AIAimConfig` fields instead of trying to preserve `gain/smoothing/max_correction_px/manual_dampen`.
- Do not teach the new mouse controller about `slow_zone` or `fire_zone` in V1. Derive commit bands from `target_dx/dy`, `body_box`, and `target_source`.
- Prefer a direct rewrite of `tests/mouse/test_mouse_ai_aim.py` over trying to preserve deadzone-era assertions that no longer describe the product behavior.

### Task 1: Expand the target metadata contract end-to-end

**Files:**
- Modify: `controllers/base_controller.py`
- Modify: `vision/runner.py`
- Modify: `vision/native_runner.py`
- Modify: `tests/test_vision_runner.py`
- Modify: `tests/test_native_vision_runner.py`

- [ ] **Step 1: Write the failing tests**

Add contract assertions that require `target_source` to survive both Python and native vision handoff:

```python
class BaseControllerAliasTests(unittest.TestCase):
    def test_update_can_receive_controller_target_source(self):
        controller = _AliasController()
        target = ControllerTarget(
            aim_point_x=320.0,
            aim_point_y=210.0,
            screen_center_x=320.0,
            screen_center_y=256.0,
            body_box=(282.0, 128.0, 358.0, 316.0),
            target_source="observed",
        )

        controller.update(8.0, -4.0, target=target)

        self.assertEqual(controller.updates[0][2].target_source, "observed")


class NativeVisionRunnerMappingTests(unittest.TestCase):
    def test_controller_target_maps_native_target_source(self):
        target = _controller_target_from_native_result(
            {
                "target_x": 331.5,
                "target_y": 201.25,
                "screen_center_x": 320.0,
                "screen_center_y": 256.0,
                "has_body_box": True,
                "body_x1": 280.0,
                "body_y1": 120.0,
                "body_x2": 360.0,
                "body_y2": 320.0,
                "target_source": "predicted",
            }
        )

        self.assertEqual(target.target_source, "predicted")
```

Add one Python-runner mapping assertion in `tests/test_vision_runner.py`:

```python
class VisionRunnerControllerTargetTests(unittest.TestCase):
    def test_controller_target_preserves_selected_target_source(self):
        selected = SelectedTarget(
            target_x=84.0,
            target_y=42.0,
            screen_center_x=80.0,
            screen_center_y=48.0,
            score=330.0,
            selected_box=(60.0, 20.0, 104.0, 86.0),
            source="reconstructed",
        )

        target = _controller_target(selected)

        self.assertEqual(target.target_source, "reconstructed")
```

- [ ] **Step 2: Run tests to verify they fail**

Run:

```bash
py -3 -m unittest tests.test_vision_runner tests.test_native_vision_runner -v
```

Expected:
- FAIL because `ControllerTarget` does not accept `target_source`
- FAIL because `_controller_target(...)` and `_controller_target_from_native_result(...)` drop source metadata

- [ ] **Step 3: Write minimal implementation**

Update the controller target dataclass:

```python
@dataclass(slots=True, frozen=True)
class ControllerTarget:
    aim_point_x: float
    aim_point_y: float
    screen_center_x: float
    screen_center_y: float
    body_box: tuple[float, float, float, float] | None = None
    target_source: str | None = None
```

Update the Python runner mapping:

```python
def _controller_target(selected_target) -> ControllerTarget | None:
    if selected_target is None:
        return None
    return ControllerTarget(
        aim_point_x=selected_target.target_x,
        aim_point_y=selected_target.target_y,
        screen_center_x=selected_target.screen_center_x,
        screen_center_y=selected_target.screen_center_y,
        body_box=selected_target.selected_box,
        target_source=getattr(selected_target, "source", None),
    )
```

Update the native runner mapping:

```python
def _controller_target_from_native_result(result: dict) -> ControllerTarget | None:
    body_box = None
    if result.get("has_body_box"):
        body_box = (
            float(result.get("body_x1", 0.0)),
            float(result.get("body_y1", 0.0)),
            float(result.get("body_x2", 0.0)),
            float(result.get("body_y2", 0.0)),
        )
    return ControllerTarget(
        aim_point_x=float(result.get("target_x", 0.0)),
        aim_point_y=float(result.get("target_y", 0.0)),
        screen_center_x=float(result.get("screen_center_x", 0.0)),
        screen_center_y=float(result.get("screen_center_y", 0.0)),
        body_box=body_box,
        target_source=result.get("target_source"),
    )
```

- [ ] **Step 4: Run tests to verify they pass**

Run:

```bash
py -3 -m unittest tests.test_vision_runner tests.test_native_vision_runner -v
```

Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add controllers/base_controller.py vision/runner.py vision/native_runner.py tests/test_vision_runner.py tests/test_native_vision_runner.py
git commit -m "Preserve target source in controller handoff"
```

### Task 2: Replace the mouse data model and AI plugin with the new state machine

**Files:**
- Modify: `controllers/mouse/state.py`
- Modify: `controllers/mouse/ai_aim.py`
- Modify: `controllers/mouse/__init__.py`
- Modify: `tests/mouse/test_mouse_state.py`
- Modify: `tests/mouse/test_mouse_ai_aim.py`

- [ ] **Step 1: Write the failing tests**

Replace deadzone-era assertions with state-machine-oriented tests:

```python
def _target(*, source="observed"):
    return ControllerTarget(
        aim_point_x=332.0,
        aim_point_y=228.0,
        screen_center_x=320.0,
        screen_center_y=256.0,
        body_box=(288.0, 140.0, 368.0, 340.0),
        target_source=source,
    )


def _frame(
    *,
    timestamp=1.0,
    aiming=True,
    target_dx=12.0,
    target_dy=-6.0,
    manual_dx=0.0,
    manual_dy=0.0,
    target=None,
):
    return MouseFrame(
        timestamp=timestamp,
        manual_dx=manual_dx,
        manual_dy=manual_dy,
        is_aiming=aiming,
        target_dx=target_dx,
        target_dy=target_dy,
        auto_fire_requested=False,
        target=target or _target(),
        target_revision=1,
        target_timestamp=timestamp,
    )


class AIAimPluginTests(unittest.TestCase):
    def test_frame_stores_target_metadata(self):
        frame = _frame()
        self.assertEqual(frame.target.target_source, "observed")

    def test_observed_target_enters_ads_entry_assist(self):
        plugin = AIAimPlugin(AIAimConfig(entry_window_ms=100, arming_band_px=30.0))
        output = MouseOutput()
        plugin.apply(_frame(target_dx=16.0, target_dy=-8.0), output)
        self.assertEqual(plugin._mode, "ads_entry_assist")
        self.assertNotEqual((output.move_dx, output.move_dy), (0.0, 0.0))

    def test_predicted_target_does_not_start_entry_assist(self):
        plugin = AIAimPlugin(AIAimConfig(entry_window_ms=100, arming_band_px=30.0))
        output = MouseOutput()
        plugin.apply(_frame(target_dx=16.0, target=_target(source="predicted")), output)
        self.assertEqual(plugin._mode, "manual")

    def test_commit_hold_activates_inside_hold_band(self):
        plugin = AIAimPlugin(AIAimConfig(arming_band_px=30.0, hold_band_px=10.0))
        output = MouseOutput()
        plugin.apply(_frame(target_dx=6.0, target_dy=-4.0), output)
        self.assertEqual(plugin._mode, "commit_hold")

    def test_strong_breakaway_returns_to_manual(self):
        plugin = AIAimPlugin(AIAimConfig(breakaway_speed_px=18.0))
        plugin.apply(_frame(target_dx=6.0, target_dy=-4.0), MouseOutput())
        output = MouseOutput()
        plugin.apply(_frame(target_dx=5.0, manual_dx=-30.0, manual_dy=0.0), output)
        self.assertEqual(plugin._mode, "manual")
        self.assertEqual((output.move_dx, output.move_dy), (0.0, 0.0))
```

- [ ] **Step 2: Run tests to verify they fail**

Run:

```bash
py -3 -m unittest tests.mouse.test_mouse_state tests.mouse.test_mouse_ai_aim -v
```

Expected:
- FAIL because `MouseFrame` has no `target`
- FAIL because the old `AIAimConfig` fields and old `AIAimPlugin` behavior do not match the new tests

- [ ] **Step 3: Write minimal implementation**

Redesign `MouseFrame` to carry both numeric error and target metadata:

```python
@dataclass(slots=True, frozen=True)
class MouseFrame:
    timestamp: float
    manual_dx: float
    manual_dy: float
    is_aiming: bool
    target_dx: float
    target_dy: float
    auto_fire_requested: bool
    target: ControllerTarget | None = None
    target_revision: int = 0
    target_timestamp: float | None = None
```

Replace the old mouse config with state-machine knobs:

```python
@dataclass(slots=True, frozen=True)
class AIAimConfig:
    entry_window_ms: int = 90
    arming_band_px: float = 30.0
    hold_band_px: float = 10.0
    inner_release_band_px: float = 3.0
    entry_gain: float = 0.18
    hold_gain: float = 0.10
    bridge_gain: float = 0.06
    entry_max_move_px: float = 2.5
    hold_max_move_px: float = 1.4
    bridge_max_move_px: float = 0.8
    bridge_window_ms: int = 100
    breakaway_speed_px: float = 18.0
    opposing_suppression: float = 0.75
    orthogonal_suppression: float = 0.50
```

Implement the new AI plugin as a state machine:

```python
class AIAimPlugin:
    def __init__(self, config: AIAimConfig | None = None):
        self.config = config or AIAimConfig()
        self.reset()

    def reset(self) -> None:
        self._mode = "manual"
        self._ads_started_at = None
        self._bridge_until = None
        self._last_target = None

    def apply(self, frame: MouseFrame, output: MouseOutput) -> None:
        if not frame.is_aiming or frame.target is None:
            self.reset()
            return
        if self._should_release(frame):
            self.reset()
            return
        self._begin_ads_session(frame)
        self._mode = self._choose_mode(frame)
        move_dx, move_dy = self._compute_move(frame, self._mode)
        output.move_dx += move_dx
        output.move_dy += move_dy
        self._remember_target(frame)
```

Key helpers to implement in this task:

```python
def _choose_mode(self, frame: MouseFrame) -> str:
    if self._can_commit_hold(frame):
        return "commit_hold"
    if self._can_bridge(frame):
        return "reacquire_bridge"
    if self._can_entry_assist(frame):
        return "ads_entry_assist"
    return "manual"
```

- [ ] **Step 4: Run tests to verify they pass**

Run:

```bash
py -3 -m unittest tests.mouse.test_mouse_state tests.mouse.test_mouse_ai_aim -v
```

Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add controllers/mouse/state.py controllers/mouse/ai_aim.py controllers/mouse/__init__.py tests/mouse/test_mouse_state.py tests/mouse/test_mouse_ai_aim.py
git commit -m "Replace mouse ai aim with commit-hold state machine"
```

### Task 3: Integrate the new target-aware mouse state into the host and config loader

**Files:**
- Modify: `controllers/mouse_controller.py`
- Modify: `config/loader.py`
- Modify: `tests/mouse/test_mouse_controller_host.py`
- Modify: `tests/test_config_loader.py`
- Modify: `tests/mouse/test_mouse_plugin_chain.py`

- [ ] **Step 1: Write the failing tests**

Add host assertions that require the full target object to survive into `MouseFrame`:

```python
class MouseControllerHostTests(unittest.TestCase):
    def test_update_stores_target_metadata(self):
        ctrl = self._make_controller([])
        target = ControllerTarget(
            aim_point_x=332.0,
            aim_point_y=228.0,
            screen_center_x=320.0,
            screen_center_y=256.0,
            body_box=(288.0, 140.0, 368.0, 340.0),
            target_source="observed",
        )

        ctrl.update(12.5, -8.0, target=target)

        self.assertEqual(ctrl.target_info, target)

    def test_build_frame_captures_target_object(self):
        ctrl = self._make_controller([])
        target = ControllerTarget(
            aim_point_x=332.0,
            aim_point_y=228.0,
            screen_center_x=320.0,
            screen_center_y=256.0,
            body_box=(288.0, 140.0, 368.0, 340.0),
            target_source="reconstructed",
        )
        ctrl.update(7.0, -3.0, target=target)

        frame = ctrl._build_frame(timestamp=100.0)

        self.assertEqual(frame.target, target)
        self.assertEqual(frame.target.target_source, "reconstructed")
```

Replace the mouse loader test with new config keys:

```python
def test_mouse_state_machine_knobs_load_from_config(self):
    toml = textwrap.dedent(
        '''
        [mouse.ai_aim]
        entry_window_ms = 110
        arming_band_px = 32.0
        hold_band_px = 11.0
        bridge_window_ms = 120
        breakaway_speed_px = 20.0
        opposing_suppression = 0.80
        '''
    ).strip()
```

- [ ] **Step 2: Run tests to verify they fail**

Run:

```bash
py -3 -m unittest tests.mouse.test_mouse_controller_host tests.test_config_loader tests.mouse.test_mouse_plugin_chain -v
```

Expected:
- FAIL because `MouseController` does not keep `target_info`
- FAIL because `MouseFrame` build path does not include `target`
- FAIL because `config/loader.py` does not accept the new mouse keys

- [ ] **Step 3: Write minimal implementation**

Update the mouse host to store and forward full target metadata:

```python
class MouseController(BaseController, threading.Thread):
    def __init__(self, plugins=None):
        ...
        self.target_info = None
        ...

    def update(self, dx, dy, target=None):
        with self.lock:
            self.target_dx = dx
            self.target_dy = dy
            self.target_info = target
            self.target_revision += 1
            self.target_timestamp = time.perf_counter()

    def reset(self):
        with self.lock:
            self.target_dx = 0.0
            self.target_dy = 0.0
            self.target_info = None
            self.target_revision += 1
            self.target_timestamp = time.perf_counter()
        reset_plugins(self.plugins)
```

Build the new frame shape:

```python
return MouseFrame(
    timestamp=timestamp,
    manual_dx=manual_dx,
    manual_dy=manual_dy,
    is_aiming=self._is_aiming,
    target_dx=target_dx,
    target_dy=target_dy,
    auto_fire_requested=auto_fire_requested,
    target=target_info,
    target_revision=target_revision,
    target_timestamp=target_timestamp,
)
```

Update the loader to use the new knob set:

```python
MOUSE_AI_AIM_KEYS = frozenset(
    {
        "entry_window_ms",
        "arming_band_px",
        "hold_band_px",
        "inner_release_band_px",
        "entry_gain",
        "hold_gain",
        "bridge_gain",
        "entry_max_move_px",
        "hold_max_move_px",
        "bridge_max_move_px",
        "bridge_window_ms",
        "breakaway_speed_px",
        "opposing_suppression",
        "orthogonal_suppression",
    }
)
```

- [ ] **Step 4: Run tests to verify they pass**

Run:

```bash
py -3 -m unittest tests.mouse.test_mouse_controller_host tests.test_config_loader tests.mouse.test_mouse_plugin_chain tests.mouse.test_mouse_auto_fire tests.mouse.test_mouse_recoil -v
```

Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add controllers/mouse_controller.py config/loader.py tests/mouse/test_mouse_controller_host.py tests/test_config_loader.py tests/mouse/test_mouse_plugin_chain.py tests/mouse/test_mouse_auto_fire.py tests/mouse/test_mouse_recoil.py
git commit -m "Wire target-aware mouse state machine into host and config"
```

### Task 4: Add combat-sequence coverage for commit, bridge, and switch release

**Files:**
- Create: `tests/mouse/test_mouse_ai_aim_sequences.py`
- Modify: `controllers/mouse/ai_aim.py`

- [ ] **Step 1: Write the failing tests**

Create explicit scenario tests for the three behaviors the spec cares most about:

```python
class MouseAIAimSequenceTests(unittest.TestCase):
    def test_manual_entry_can_go_directly_to_commit_hold(self):
        plugin = AIAimPlugin(AIAimConfig(arming_band_px=30.0, hold_band_px=10.0))

        output = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.0,
                target_dx=5.0,
                target_dy=-4.0,
                manual_dx=4.0,
                manual_dy=-2.0,
                target=_target(source="observed"),
            ),
            output,
        )

        self.assertEqual(plugin._mode, "commit_hold")

    def test_commit_hold_uses_bridge_for_short_predicted_gap(self):
        plugin = AIAimPlugin(AIAimConfig(bridge_window_ms=120))
        plugin.apply(_frame(timestamp=1.0, target_dx=6.0, target=_target(source="observed")), MouseOutput())

        output = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.05,
                target_dx=7.0,
                target=_target(source="predicted"),
            ),
            output,
        )

        self.assertEqual(plugin._mode, "reacquire_bridge")
        self.assertNotEqual((output.move_dx, output.move_dy), (0.0, 0.0))

    def test_fast_breakaway_flick_cancels_bridge_or_hold(self):
        plugin = AIAimPlugin(AIAimConfig(breakaway_speed_px=18.0))
        plugin.apply(_frame(timestamp=1.0, target_dx=6.0, target=_target(source="observed")), MouseOutput())

        output = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.06,
                target_dx=8.0,
                manual_dx=-40.0,
                manual_dy=0.0,
                target=_target(source="reconstructed"),
            ),
            output,
        )

        self.assertEqual(plugin._mode, "manual")
        self.assertEqual((output.move_dx, output.move_dy), (0.0, 0.0))
```

- [ ] **Step 2: Run tests to verify they fail**

Run:

```bash
py -3 -m unittest tests.mouse.test_mouse_ai_aim_sequences -v
```

Expected: FAIL because the initial rewrite will not yet fully implement bridge memory, direct manual-to-hold promotion, or breakaway release semantics.

- [ ] **Step 3: Write minimal implementation**

Finish the missing state transitions inside `controllers/mouse/ai_aim.py`:

```python
def _can_bridge(self, frame: MouseFrame) -> bool:
    if self._last_target is None:
        return False
    if frame.target is None or frame.target.target_source != "predicted":
        return False
    if self._bridge_until is None or frame.timestamp > self._bridge_until:
        return False
    return self._same_target_family(frame.target, self._last_target)


def _remember_target(self, frame: MouseFrame) -> None:
    if frame.target is None:
        return
    self._last_target = frame.target
    if self._mode == "commit_hold":
        self._bridge_until = frame.timestamp + (self.config.bridge_window_ms / 1000.0)


def _should_release(self, frame: MouseFrame) -> bool:
    if frame.manual_dx == 0.0 and frame.manual_dy == 0.0:
        return False
    speed = math.hypot(frame.manual_dx, frame.manual_dy)
    if speed < self.config.breakaway_speed_px:
        return False
    return (frame.manual_dx * frame.target_dx) < 0.0 or (frame.manual_dy * frame.target_dy) < 0.0
```

- [ ] **Step 4: Run tests to verify they pass**

Run:

```bash
py -3 -m unittest tests.mouse.test_mouse_ai_aim tests.mouse.test_mouse_ai_aim_sequences -v
```

Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add controllers/mouse/ai_aim.py tests/mouse/test_mouse_ai_aim.py tests/mouse/test_mouse_ai_aim_sequences.py
git commit -m "Add mouse commit hold and bridge sequence coverage"
```

### Task 5: Run the final regression slice for the mouse rewrite

**Files:**
- Verify: `controllers/base_controller.py`
- Verify: `vision/runner.py`
- Verify: `vision/native_runner.py`
- Verify: `controllers/mouse/*.py`
- Verify: `tests/mouse/*.py`
- Verify: `tests/test_vision_runner.py`
- Verify: `tests/test_native_vision_runner.py`
- Verify: `tests/test_config_loader.py`

- [ ] **Step 1: Run the focused mouse and runner suite**

Run:

```bash
py -3 -m unittest tests.test_vision_runner tests.test_native_vision_runner tests.test_config_loader tests.mouse.test_mouse_state tests.mouse.test_mouse_ai_aim tests.mouse.test_mouse_ai_aim_sequences tests.mouse.test_mouse_auto_fire tests.mouse.test_mouse_recoil tests.mouse.test_mouse_plugin_chain tests.mouse.test_mouse_controller_host -v
```

Expected: PASS

- [ ] **Step 2: Run the CLI safety slice**

Run:

```bash
py -3 -m unittest tests.test_main_cli -v
```

Expected: PASS with no controller-mode regression

- [ ] **Step 3: Inspect the final state manually**

Manual checks:

- `ControllerTarget` now carries `target_source`
- Python and native vision both populate it
- `MouseController._build_frame(...)` forwards full target metadata
- `AIAimPlugin` no longer references `manual_dampen`, `deadzone_inner_px`, or `deadzone_outer_px`
- `AutoFirePlugin` and `RecoilCompensationPlugin` still operate on `MouseOutput` only

- [ ] **Step 4: Commit the final integration**

```bash
git add controllers/base_controller.py vision/runner.py vision/native_runner.py controllers/mouse/state.py controllers/mouse/ai_aim.py controllers/mouse/__init__.py controllers/mouse_controller.py config/loader.py tests/test_vision_runner.py tests/test_native_vision_runner.py tests/test_config_loader.py tests/mouse/test_mouse_state.py tests/mouse/test_mouse_ai_aim.py tests/mouse/test_mouse_ai_aim_sequences.py tests/mouse/test_mouse_controller_host.py tests/mouse/test_mouse_plugin_chain.py
git commit -m "Finish mouse ADS commit-hold rewrite"
```
