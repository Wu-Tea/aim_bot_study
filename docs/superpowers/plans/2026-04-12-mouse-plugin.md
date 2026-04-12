# Mouse Plugin Architecture Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give `MouseController` the same plugin-based enhancement architecture that `GamepadController` has -- AI aim assist, auto-fire (left click), and recoil compensation -- operating in pixel space.

**Architecture:** Mirror `controllers/gamepad/` with a parallel `controllers/mouse/` package. `MouseFrame` (immutable) and `MouseOutput` (mutable) flow through a `MousePlugin` chain. The host (`MouseController`) reads physical mouse input, builds a frame, runs plugins, and writes the final output via `win32api.mouse_event`. Physical mouse movement is captured for AI fade calculation but not re-injected (the physical device already moves the cursor).

**Tech Stack:** Python 3.11, `win32api`, `pynput`, `unittest`

---

## File Map

| Action | Path | Responsibility |
|--------|------|----------------|
| Create | `controllers/mouse/__init__.py` | Public exports |
| Create | `controllers/mouse/state.py` | `MouseFrame`, `MouseOutput` dataclasses |
| Create | `controllers/mouse/plugin.py` | `MousePlugin` protocol, `apply_plugins`, `reset_plugins` |
| Create | `controllers/mouse/ai_aim.py` | `AIAimConfig`, `AIAimPlugin` |
| Create | `controllers/mouse/auto_fire.py` | `AutoFireConfig`, `AutoFirePlugin` |
| Create | `controllers/mouse/recoil_compensation.py` | `RecoilCompensationConfig`, `RecoilCompensationPlugin` |
| Modify | `controllers/mouse_controller.py` | Rewrite to plugin-host architecture |
| Create | `tests/mouse/__init__.py` | Test package marker |
| Create | `tests/mouse/test_mouse_state.py` | Tests for MouseFrame/MouseOutput |
| Create | `tests/mouse/test_mouse_plugin_chain.py` | Tests for plugin protocol helpers |
| Create | `tests/mouse/test_mouse_ai_aim.py` | Tests for AI aim plugin |
| Create | `tests/mouse/test_mouse_auto_fire.py` | Tests for auto-fire plugin |
| Create | `tests/mouse/test_mouse_recoil.py` | Tests for recoil compensation plugin |
| Create | `tests/mouse/test_mouse_controller_host.py` | Tests for host frame-build and output-apply |

---

### Task 1: MouseFrame and MouseOutput data structures

**Files:**
- Create: `controllers/mouse/state.py`
- Create: `tests/mouse/__init__.py`
- Create: `tests/mouse/test_mouse_state.py`

- [ ] **Step 1: Write the failing test**

Create `tests/mouse/__init__.py` (empty file) and `tests/mouse/test_mouse_state.py`:

```python
import unittest

from controllers.mouse.state import MouseFrame, MouseOutput


class MouseFrameTests(unittest.TestCase):
    def test_frame_is_frozen(self):
        frame = MouseFrame(
            timestamp=1.0,
            manual_dx=5.0,
            manual_dy=-3.0,
            is_aiming=True,
            target_dx=10.0,
            target_dy=-4.0,
            auto_fire_requested=False,
        )
        with self.assertRaises(AttributeError):
            frame.target_dx = 99.0

    def test_frame_stores_all_fields(self):
        frame = MouseFrame(
            timestamp=2.0,
            manual_dx=1.0,
            manual_dy=2.0,
            is_aiming=False,
            target_dx=3.0,
            target_dy=4.0,
            auto_fire_requested=True,
            target_revision=7,
            target_timestamp=1.5,
        )
        self.assertEqual(frame.timestamp, 2.0)
        self.assertEqual(frame.manual_dx, 1.0)
        self.assertEqual(frame.manual_dy, 2.0)
        self.assertFalse(frame.is_aiming)
        self.assertEqual(frame.target_dx, 3.0)
        self.assertEqual(frame.target_dy, 4.0)
        self.assertTrue(frame.auto_fire_requested)
        self.assertEqual(frame.target_revision, 7)
        self.assertEqual(frame.target_timestamp, 1.5)


class MouseOutputTests(unittest.TestCase):
    def test_output_is_mutable(self):
        output = MouseOutput()
        output.move_dx = 5.0
        output.move_dy = -3.0
        output.left_click = True
        output.auto_fire_active = True
        self.assertEqual(output.move_dx, 5.0)
        self.assertEqual(output.move_dy, -3.0)
        self.assertTrue(output.left_click)
        self.assertTrue(output.auto_fire_active)

    def test_output_defaults(self):
        output = MouseOutput()
        self.assertEqual(output.move_dx, 0.0)
        self.assertEqual(output.move_dy, 0.0)
        self.assertFalse(output.left_click)
        self.assertFalse(output.auto_fire_active)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python -m unittest tests.mouse.test_mouse_state -v`
Expected: `ModuleNotFoundError: No module named 'controllers.mouse'`

- [ ] **Step 3: Write minimal implementation**

Create `controllers/mouse/__init__.py` (empty for now) and `controllers/mouse/state.py`:

```python
from dataclasses import dataclass


@dataclass(slots=True, frozen=True)
class MouseFrame:
    timestamp: float
    manual_dx: float
    manual_dy: float
    is_aiming: bool
    target_dx: float
    target_dy: float
    auto_fire_requested: bool
    target_revision: int = 0
    target_timestamp: float | None = None


@dataclass(slots=True)
class MouseOutput:
    move_dx: float = 0.0
    move_dy: float = 0.0
    left_click: bool = False
    auto_fire_active: bool = False
```

- [ ] **Step 4: Run test to verify it passes**

Run: `python -m unittest tests.mouse.test_mouse_state -v`
Expected: all 4 tests PASS

- [ ] **Step 5: Commit**

```bash
git add controllers/mouse/__init__.py controllers/mouse/state.py tests/mouse/__init__.py tests/mouse/test_mouse_state.py
git commit -m "feat(mouse): add MouseFrame and MouseOutput data structures"
```

---

### Task 2: MousePlugin protocol and chain helpers

**Files:**
- Create: `controllers/mouse/plugin.py`
- Create: `tests/mouse/test_mouse_plugin_chain.py`

- [ ] **Step 1: Write the failing test**

Create `tests/mouse/test_mouse_plugin_chain.py`:

```python
import unittest

from controllers.mouse.plugin import apply_plugins, reset_plugins
from controllers.mouse.state import MouseFrame, MouseOutput


def _frame():
    return MouseFrame(
        timestamp=1.0,
        manual_dx=0.0,
        manual_dy=0.0,
        is_aiming=True,
        target_dx=0.0,
        target_dy=0.0,
        auto_fire_requested=False,
    )


class _RecordingPlugin:
    def __init__(self, name, calls):
        self.name = name
        self.calls = calls

    def reset(self):
        self.calls.append(f"reset:{self.name}")

    def apply(self, frame, output):
        self.calls.append(f"apply:{self.name}")


class MousePluginChainTests(unittest.TestCase):
    def test_apply_plugins_runs_in_declared_order(self):
        calls = []
        plugins = [
            _RecordingPlugin("aim", calls),
            _RecordingPlugin("fire", calls),
            _RecordingPlugin("recoil", calls),
        ]
        output = MouseOutput()
        apply_plugins(plugins, _frame(), output)
        self.assertEqual(calls, ["apply:aim", "apply:fire", "apply:recoil"])

    def test_reset_plugins_broadcasts_to_every_plugin(self):
        calls = []
        plugins = [_RecordingPlugin("aim", calls), _RecordingPlugin("fire", calls)]
        reset_plugins(plugins)
        self.assertEqual(calls, ["reset:aim", "reset:fire"])

    def test_apply_with_empty_list_is_noop(self):
        output = MouseOutput()
        apply_plugins([], _frame(), output)
        self.assertEqual(output.move_dx, 0.0)
        self.assertEqual(output.move_dy, 0.0)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python -m unittest tests.mouse.test_mouse_plugin_chain -v`
Expected: `ImportError: cannot import name 'apply_plugins' from 'controllers.mouse.plugin'`

- [ ] **Step 3: Write minimal implementation**

Create `controllers/mouse/plugin.py`:

```python
from typing import Iterable, Protocol

from .state import MouseFrame, MouseOutput


class MousePlugin(Protocol):
    def reset(self) -> None: ...
    def apply(self, frame: MouseFrame, output: MouseOutput) -> None: ...


def apply_plugins(
    plugins: Iterable[MousePlugin],
    frame: MouseFrame,
    output: MouseOutput,
) -> None:
    for plugin in plugins:
        plugin.apply(frame, output)


def reset_plugins(plugins: Iterable[MousePlugin]) -> None:
    for plugin in plugins:
        plugin.reset()
```

- [ ] **Step 4: Run test to verify it passes**

Run: `python -m unittest tests.mouse.test_mouse_plugin_chain -v`
Expected: all 3 tests PASS

- [ ] **Step 5: Commit**

```bash
git add controllers/mouse/plugin.py tests/mouse/test_mouse_plugin_chain.py
git commit -m "feat(mouse): add MousePlugin protocol and chain helpers"
```

---

### Task 3: AutoFirePlugin

**Files:**
- Create: `controllers/mouse/auto_fire.py`
- Create: `tests/mouse/test_mouse_auto_fire.py`

- [ ] **Step 1: Write the failing test**

Create `tests/mouse/test_mouse_auto_fire.py`:

```python
import unittest

from controllers.mouse.auto_fire import AutoFireConfig, AutoFirePlugin
from controllers.mouse.state import MouseFrame, MouseOutput


def _frame(*, aiming=True, auto_fire=False):
    return MouseFrame(
        timestamp=1.0,
        manual_dx=0.0,
        manual_dy=0.0,
        is_aiming=aiming,
        target_dx=0.0,
        target_dy=0.0,
        auto_fire_requested=auto_fire,
    )


class AutoFirePluginTests(unittest.TestCase):
    def test_fires_left_click_when_aiming_and_requested(self):
        plugin = AutoFirePlugin()
        output = MouseOutput()
        plugin.apply(_frame(aiming=True, auto_fire=True), output)
        self.assertTrue(output.left_click)
        self.assertTrue(output.auto_fire_active)

    def test_no_fire_when_not_aiming(self):
        plugin = AutoFirePlugin()
        output = MouseOutput()
        plugin.apply(_frame(aiming=False, auto_fire=True), output)
        self.assertFalse(output.left_click)
        self.assertFalse(output.auto_fire_active)

    def test_no_fire_when_not_requested(self):
        plugin = AutoFirePlugin()
        output = MouseOutput()
        plugin.apply(_frame(aiming=True, auto_fire=False), output)
        self.assertFalse(output.left_click)
        self.assertFalse(output.auto_fire_active)

    def test_aim_only_false_fires_without_aiming(self):
        plugin = AutoFirePlugin(AutoFireConfig(aim_only=False))
        output = MouseOutput()
        plugin.apply(_frame(aiming=False, auto_fire=True), output)
        self.assertTrue(output.left_click)
        self.assertTrue(output.auto_fire_active)

    def test_reset_is_noop(self):
        plugin = AutoFirePlugin()
        plugin.reset()  # should not raise


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python -m unittest tests.mouse.test_mouse_auto_fire -v`
Expected: `ModuleNotFoundError: No module named 'controllers.mouse.auto_fire'`

- [ ] **Step 3: Write minimal implementation**

Create `controllers/mouse/auto_fire.py`:

```python
from dataclasses import dataclass

from .state import MouseFrame, MouseOutput


@dataclass(slots=True, frozen=True)
class AutoFireConfig:
    aim_only: bool = True


class AutoFirePlugin:
    def __init__(self, config: AutoFireConfig | None = None):
        self.config = config or AutoFireConfig()

    def reset(self) -> None:
        return None

    def apply(self, frame: MouseFrame, output: MouseOutput) -> None:
        should_fire = frame.auto_fire_requested
        if self.config.aim_only:
            should_fire = should_fire and frame.is_aiming
        output.left_click = should_fire
        output.auto_fire_active = should_fire
```

- [ ] **Step 4: Run test to verify it passes**

Run: `python -m unittest tests.mouse.test_mouse_auto_fire -v`
Expected: all 5 tests PASS

- [ ] **Step 5: Commit**

```bash
git add controllers/mouse/auto_fire.py tests/mouse/test_mouse_auto_fire.py
git commit -m "feat(mouse): add AutoFirePlugin with left-click output"
```

---

### Task 4: RecoilCompensationPlugin

**Files:**
- Create: `controllers/mouse/recoil_compensation.py`
- Create: `tests/mouse/test_mouse_recoil.py`

- [ ] **Step 1: Write the failing test**

Create `tests/mouse/test_mouse_recoil.py`:

```python
import unittest

from controllers.mouse.recoil_compensation import (
    RecoilCompensationConfig,
    RecoilCompensationPlugin,
)
from controllers.mouse.state import MouseFrame, MouseOutput


def _frame():
    return MouseFrame(
        timestamp=1.0,
        manual_dx=0.0,
        manual_dy=0.0,
        is_aiming=True,
        target_dx=0.0,
        target_dy=0.0,
        auto_fire_requested=True,
    )


class RecoilCompensationPluginTests(unittest.TestCase):
    def test_adds_downward_pixels_when_firing(self):
        plugin = RecoilCompensationPlugin(RecoilCompensationConfig(amount_px=5.0))
        output = MouseOutput()
        output.auto_fire_active = True
        plugin.apply(_frame(), output)
        self.assertAlmostEqual(output.move_dy, 5.0)

    def test_no_pull_when_not_firing(self):
        plugin = RecoilCompensationPlugin(RecoilCompensationConfig(amount_px=5.0))
        output = MouseOutput()
        output.auto_fire_active = False
        plugin.apply(_frame(), output)
        self.assertAlmostEqual(output.move_dy, 0.0)

    def test_zero_amount_is_noop(self):
        plugin = RecoilCompensationPlugin(RecoilCompensationConfig(amount_px=0.0))
        output = MouseOutput()
        output.auto_fire_active = True
        plugin.apply(_frame(), output)
        self.assertAlmostEqual(output.move_dy, 0.0)

    def test_stacks_with_existing_move_dy(self):
        plugin = RecoilCompensationPlugin(RecoilCompensationConfig(amount_px=3.0))
        output = MouseOutput()
        output.move_dy = 2.0
        output.auto_fire_active = True
        plugin.apply(_frame(), output)
        self.assertAlmostEqual(output.move_dy, 5.0)

    def test_reset_is_noop(self):
        plugin = RecoilCompensationPlugin()
        plugin.reset()  # should not raise


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python -m unittest tests.mouse.test_mouse_recoil -v`
Expected: `ModuleNotFoundError: No module named 'controllers.mouse.recoil_compensation'`

- [ ] **Step 3: Write minimal implementation**

Create `controllers/mouse/recoil_compensation.py`:

```python
from dataclasses import dataclass

from .state import MouseFrame, MouseOutput


@dataclass(slots=True, frozen=True)
class RecoilCompensationConfig:
    amount_px: float = 3.0


class RecoilCompensationPlugin:
    def __init__(self, config: RecoilCompensationConfig | None = None):
        self.config = config or RecoilCompensationConfig()

    def reset(self) -> None:
        return None

    def apply(self, frame: MouseFrame, output: MouseOutput) -> None:
        if output.auto_fire_active and self.config.amount_px != 0.0:
            output.move_dy += self.config.amount_px
```

- [ ] **Step 4: Run test to verify it passes**

Run: `python -m unittest tests.mouse.test_mouse_recoil -v`
Expected: all 5 tests PASS

- [ ] **Step 5: Commit**

```bash
git add controllers/mouse/recoil_compensation.py tests/mouse/test_mouse_recoil.py
git commit -m "feat(mouse): add RecoilCompensationPlugin for downward mouse pull"
```

---

### Task 5: AIAimPlugin

**Files:**
- Create: `controllers/mouse/ai_aim.py`
- Create: `tests/mouse/test_mouse_ai_aim.py`

- [ ] **Step 1: Write the failing test**

Create `tests/mouse/test_mouse_ai_aim.py`:

```python
import unittest

from controllers.mouse.ai_aim import AIAimConfig, AIAimPlugin
from controllers.mouse.state import MouseFrame, MouseOutput


def _frame(*, target_dx=0.0, target_dy=0.0, aiming=True, manual_dx=0.0, manual_dy=0.0):
    return MouseFrame(
        timestamp=1.0,
        manual_dx=manual_dx,
        manual_dy=manual_dy,
        is_aiming=aiming,
        target_dx=target_dx,
        target_dy=target_dy,
        auto_fire_requested=False,
        target_revision=1,
        target_timestamp=1.0,
    )


class AIAimPluginTests(unittest.TestCase):
    def test_no_correction_when_not_aiming(self):
        plugin = AIAimPlugin()
        output = MouseOutput()
        plugin.apply(_frame(target_dx=50.0, target_dy=30.0, aiming=False), output)
        self.assertAlmostEqual(output.move_dx, 0.0)
        self.assertAlmostEqual(output.move_dy, 0.0)

    def test_no_correction_inside_inner_deadzone(self):
        config = AIAimConfig(deadzone_inner_px=2.0, deadzone_outer_px=5.0)
        plugin = AIAimPlugin(config)
        output = MouseOutput()
        # target offset of 1px radial distance is inside the 2px inner deadzone
        plugin.apply(_frame(target_dx=0.5, target_dy=0.5), output)
        self.assertAlmostEqual(output.move_dx, 0.0)
        self.assertAlmostEqual(output.move_dy, 0.0)

    def test_correction_applied_outside_deadzone(self):
        config = AIAimConfig(
            gain=1.0,
            smoothing=0.0,
            max_correction_px=50.0,
            deadzone_inner_px=1.0,
            deadzone_outer_px=2.0,
            fade_speed_px=1000.0,
        )
        plugin = AIAimPlugin(config)
        output = MouseOutput()
        plugin.apply(_frame(target_dx=20.0, target_dy=10.0), output)
        # With gain=1.0, smoothing=0.0, high fade threshold, correction should be close to target
        self.assertGreater(output.move_dx, 0.0)
        self.assertGreater(output.move_dy, 0.0)

    def test_correction_clamped_to_max(self):
        config = AIAimConfig(
            gain=1.0,
            smoothing=0.0,
            max_correction_px=5.0,
            deadzone_inner_px=0.0,
            deadzone_outer_px=0.0,
            fade_speed_px=1000.0,
        )
        plugin = AIAimPlugin(config)
        output = MouseOutput()
        plugin.apply(_frame(target_dx=100.0, target_dy=100.0), output)
        self.assertLessEqual(abs(output.move_dx), 5.0 + 0.01)
        self.assertLessEqual(abs(output.move_dy), 5.0 + 0.01)

    def test_ai_fades_with_fast_manual_movement(self):
        config = AIAimConfig(
            gain=1.0,
            smoothing=0.0,
            max_correction_px=50.0,
            deadzone_inner_px=0.0,
            deadzone_outer_px=0.0,
            fade_speed_px=10.0,
        )
        plugin = AIAimPlugin(config)

        # Slow manual movement -> full AI
        output_slow = MouseOutput()
        plugin.apply(_frame(target_dx=20.0, manual_dx=0.0), output_slow)

        # Reset carry
        plugin.reset()

        # Fast manual movement -> faded AI
        output_fast = MouseOutput()
        plugin.apply(_frame(target_dx=20.0, manual_dx=50.0), output_fast)

        self.assertGreater(abs(output_slow.move_dx), abs(output_fast.move_dx))

    def test_smoothing_carries_between_frames(self):
        config = AIAimConfig(
            gain=1.0,
            smoothing=0.5,
            max_correction_px=50.0,
            deadzone_inner_px=0.0,
            deadzone_outer_px=0.0,
            fade_speed_px=1000.0,
        )
        plugin = AIAimPlugin(config)

        # First frame with target
        out1 = MouseOutput()
        plugin.apply(_frame(target_dx=20.0, target_dy=0.0), out1)

        # Second frame with zero target: carry should produce non-zero output
        out2 = MouseOutput()
        plugin.apply(
            _frame(target_dx=0.0, target_dy=0.0, aiming=True),
            out2,
        )
        # Carry from previous frame should bleed through
        self.assertNotAlmostEqual(out2.move_dx, 0.0)

    def test_reset_clears_carry(self):
        config = AIAimConfig(gain=1.0, smoothing=0.8, deadzone_inner_px=0.0,
                             deadzone_outer_px=0.0, fade_speed_px=1000.0)
        plugin = AIAimPlugin(config)
        out = MouseOutput()
        plugin.apply(_frame(target_dx=30.0), out)
        plugin.reset()
        out2 = MouseOutput()
        plugin.apply(_frame(target_dx=0.0), out2)
        self.assertAlmostEqual(out2.move_dx, 0.0)
        self.assertAlmostEqual(out2.move_dy, 0.0)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python -m unittest tests.mouse.test_mouse_ai_aim -v`
Expected: `ModuleNotFoundError: No module named 'controllers.mouse.ai_aim'`

- [ ] **Step 3: Write minimal implementation**

Create `controllers/mouse/ai_aim.py`:

```python
from dataclasses import dataclass

from .state import MouseFrame, MouseOutput


def _soft_ramp(magnitude: float, inner: float, outer: float) -> float:
    if magnitude <= inner:
        return 0.0
    if outer <= inner or magnitude >= outer:
        return 1.0
    return (magnitude - inner) / (outer - inner)


@dataclass(slots=True, frozen=True)
class AIAimConfig:
    gain: float = 0.5
    smoothing: float = 0.6
    max_correction_px: float = 15.0
    deadzone_inner_px: float = 2.0
    deadzone_outer_px: float = 5.0
    fade_speed_px: float = 50.0


class AIAimPlugin:
    def __init__(self, config: AIAimConfig | None = None):
        self.config = config or AIAimConfig()
        self.carry_x = 0.0
        self.carry_y = 0.0

    def reset(self) -> None:
        self.carry_x = 0.0
        self.carry_y = 0.0

    def apply(self, frame: MouseFrame, output: MouseOutput) -> None:
        cfg = self.config
        desired_x = 0.0
        desired_y = 0.0

        if frame.is_aiming:
            raw_x = frame.target_dx * cfg.gain
            raw_y = frame.target_dy * cfg.gain

            raw_x = max(-cfg.max_correction_px, min(cfg.max_correction_px, raw_x))
            raw_y = max(-cfg.max_correction_px, min(cfg.max_correction_px, raw_y))

            radial = (raw_x * raw_x + raw_y * raw_y) ** 0.5
            strength = _soft_ramp(radial, cfg.deadzone_inner_px, cfg.deadzone_outer_px)

            desired_x = raw_x * strength
            desired_y = raw_y * strength

            manual_speed = (frame.manual_dx ** 2 + frame.manual_dy ** 2) ** 0.5
            fade = max(0.0, 1.0 - manual_speed / cfg.fade_speed_px) if cfg.fade_speed_px > 0 else 1.0
            desired_x *= fade
            desired_y *= fade

        s = cfg.smoothing
        self.carry_x = self.carry_x * s + desired_x * (1.0 - s)
        self.carry_y = self.carry_y * s + desired_y * (1.0 - s)

        output.move_dx += self.carry_x
        output.move_dy += self.carry_y
```

- [ ] **Step 4: Run test to verify it passes**

Run: `python -m unittest tests.mouse.test_mouse_ai_aim -v`
Expected: all 7 tests PASS

- [ ] **Step 5: Commit**

```bash
git add controllers/mouse/ai_aim.py tests/mouse/test_mouse_ai_aim.py
git commit -m "feat(mouse): add AIAimPlugin with pixel-space correction"
```

---

### Task 6: Package exports in `__init__.py`

**Files:**
- Modify: `controllers/mouse/__init__.py`

- [ ] **Step 1: Write the `__init__.py` exports**

Update `controllers/mouse/__init__.py`:

```python
from .ai_aim import AIAimConfig, AIAimPlugin
from .auto_fire import AutoFireConfig, AutoFirePlugin
from .plugin import MousePlugin, apply_plugins, reset_plugins
from .recoil_compensation import RecoilCompensationConfig, RecoilCompensationPlugin
from .state import MouseFrame, MouseOutput

__all__ = [
    "AIAimConfig",
    "AIAimPlugin",
    "AutoFireConfig",
    "AutoFirePlugin",
    "MousePlugin",
    "apply_plugins",
    "reset_plugins",
    "RecoilCompensationConfig",
    "RecoilCompensationPlugin",
    "MouseFrame",
    "MouseOutput",
]
```

- [ ] **Step 2: Verify imports work**

Run: `python -c "from controllers.mouse import AIAimPlugin, AutoFirePlugin, RecoilCompensationPlugin, MouseFrame, MouseOutput; print('imports ok')"`
Expected: `imports ok`

- [ ] **Step 3: Run all mouse tests**

Run: `python -m unittest tests.mouse.test_mouse_state tests.mouse.test_mouse_plugin_chain tests.mouse.test_mouse_auto_fire tests.mouse.test_mouse_recoil tests.mouse.test_mouse_ai_aim -v`
Expected: all tests PASS

- [ ] **Step 4: Commit**

```bash
git add controllers/mouse/__init__.py
git commit -m "feat(mouse): wire up package exports"
```

---

### Task 7: Rewrite MouseController as plugin host

**Files:**
- Modify: `controllers/mouse_controller.py`
- Create: `tests/mouse/test_mouse_controller_host.py`

- [ ] **Step 1: Write the failing test**

Create `tests/mouse/test_mouse_controller_host.py`:

```python
import threading
import unittest

from controllers.mouse_controller import MouseController
from controllers.mouse.state import MouseOutput


class _FakePlugin:
    def __init__(self):
        self.reset_calls = 0
        self.apply_calls = 0
        self.last_frame = None

    def reset(self):
        self.reset_calls += 1

    def apply(self, frame, output):
        self.apply_calls += 1
        self.last_frame = frame


class MouseControllerHostTests(unittest.TestCase):
    def _make_controller(self, plugins):
        """Create a MouseController without starting the thread or listeners."""
        ctrl = MouseController.__new__(MouseController)
        ctrl.lock = threading.Lock()
        ctrl.running = False
        ctrl.ready = True
        ctrl.target_dx = 0.0
        ctrl.target_dy = 0.0
        ctrl.target_revision = 0
        ctrl.target_timestamp = None
        ctrl._is_aiming = False
        ctrl._auto_fire_requested = False
        ctrl._acc_dx = 0.0
        ctrl._acc_dy = 0.0
        ctrl._left_click_held = False
        ctrl.plugins = list(plugins)
        return ctrl

    def test_update_stores_vision_signals(self):
        ctrl = self._make_controller([])
        ctrl.update(12.5, -8.0)
        self.assertAlmostEqual(ctrl.target_dx, 12.5)
        self.assertAlmostEqual(ctrl.target_dy, -8.0)

    def test_reset_clears_target_and_resets_plugins(self):
        p = _FakePlugin()
        ctrl = self._make_controller([p])
        ctrl.target_dx = 10.0
        ctrl.target_dy = 5.0
        ctrl.reset()
        self.assertAlmostEqual(ctrl.target_dx, 0.0)
        self.assertAlmostEqual(ctrl.target_dy, 0.0)
        self.assertEqual(p.reset_calls, 1)

    def test_set_auto_fire_stores_flag(self):
        ctrl = self._make_controller([])
        ctrl.set_auto_fire(True)
        self.assertTrue(ctrl._auto_fire_requested)
        ctrl.set_auto_fire(False)
        self.assertFalse(ctrl._auto_fire_requested)

    def test_set_auto_rb_is_alias(self):
        ctrl = self._make_controller([])
        ctrl.set_auto_rb(True)
        self.assertTrue(ctrl._auto_fire_requested)

    def test_build_frame_captures_state(self):
        p = _FakePlugin()
        ctrl = self._make_controller([p])
        ctrl._is_aiming = True
        ctrl.target_dx = 7.0
        ctrl.target_dy = -3.0
        ctrl._auto_fire_requested = True
        ctrl._acc_dx = 2.0
        ctrl._acc_dy = 1.0
        ctrl.target_revision = 5
        ctrl.target_timestamp = 99.0

        frame = ctrl._build_frame(timestamp=100.0)
        self.assertTrue(frame.is_aiming)
        self.assertAlmostEqual(frame.target_dx, 7.0)
        self.assertAlmostEqual(frame.target_dy, -3.0)
        self.assertTrue(frame.auto_fire_requested)
        self.assertAlmostEqual(frame.manual_dx, 2.0)
        self.assertAlmostEqual(frame.manual_dy, 1.0)
        self.assertEqual(frame.target_revision, 5)
        self.assertEqual(frame.target_timestamp, 99.0)
        # Accumulators should be consumed
        self.assertAlmostEqual(ctrl._acc_dx, 0.0)
        self.assertAlmostEqual(ctrl._acc_dy, 0.0)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python -m unittest tests.mouse.test_mouse_controller_host -v`
Expected: failures because MouseController does not have the new attributes/methods

- [ ] **Step 3: Rewrite MouseController**

Replace the contents of `controllers/mouse_controller.py`:

```python
import threading
import time

import win32api
from pynput import mouse as pynput_mouse

from .base_controller import BaseController
from .mouse import (
    AIAimConfig,
    AIAimPlugin,
    AutoFireConfig,
    AutoFirePlugin,
    MouseFrame,
    MouseOutput,
    RecoilCompensationConfig,
    RecoilCompensationPlugin,
    apply_plugins,
    reset_plugins,
)

MOUSEEVENTF_MOVE = 0x0001
MOUSEEVENTF_LEFTDOWN = 0x0002
MOUSEEVENTF_LEFTUP = 0x0004


class MouseController(BaseController, threading.Thread):
    """
    Native mouse controller with plugin-based enhancements.
    AI corrections are injected as additional mouse_event deltas
    on top of the physical mouse movement.
    """

    def __init__(self, plugins=None):
        super().__init__()
        self.daemon = True
        self.running = True
        self.ready = True
        self.lock = threading.Lock()

        self.target_dx = 0.0
        self.target_dy = 0.0
        self.target_revision = 0
        self.target_timestamp = None
        self._is_aiming = False
        self._auto_fire_requested = False
        self._acc_dx = 0.0
        self._acc_dy = 0.0
        self._left_click_held = False

        self.plugins = list(plugins) if plugins is not None else [
            AIAimPlugin(AIAimConfig()),
            AutoFirePlugin(AutoFireConfig()),
            RecoilCompensationPlugin(RecoilCompensationConfig()),
        ]

        self._last_mouse_x, self._last_mouse_y = win32api.GetCursorPos()
        self._mouse_listener = pynput_mouse.Listener(
            on_move=self._on_mouse_move,
            on_click=self._on_mouse_click,
        )
        self._mouse_listener.start()
        self.start()

    def _on_mouse_move(self, x, y):
        dx = x - self._last_mouse_x
        dy = y - self._last_mouse_y
        self._last_mouse_x, self._last_mouse_y = x, y
        with self.lock:
            self._acc_dx += dx
            self._acc_dy += dy

    def _on_mouse_click(self, x, y, button, pressed):
        if button == pynput_mouse.Button.right:
            self._is_aiming = pressed
            if not pressed:
                self.reset()

    def update(self, dx, dy):
        with self.lock:
            self.target_dx = dx
            self.target_dy = dy
            self.target_revision += 1
            self.target_timestamp = time.perf_counter()

    def reset(self):
        with self.lock:
            self.target_dx = 0.0
            self.target_dy = 0.0
            self.target_revision += 1
            self.target_timestamp = time.perf_counter()
        reset_plugins(self.plugins)

    def is_aiming(self):
        return self._is_aiming

    def set_auto_fire(self, pressed: bool):
        with self.lock:
            self._auto_fire_requested = bool(pressed)

    def set_auto_rb(self, pressed: bool):
        self.set_auto_fire(pressed)

    def stop(self):
        self.running = False
        self._mouse_listener.stop()

    def _build_frame(self, *, timestamp):
        with self.lock:
            manual_dx = self._acc_dx
            manual_dy = self._acc_dy
            self._acc_dx = 0.0
            self._acc_dy = 0.0
            target_dx = self.target_dx
            target_dy = self.target_dy
            auto_fire_requested = self._auto_fire_requested
            target_revision = self.target_revision
            target_timestamp = self.target_timestamp

        return MouseFrame(
            timestamp=timestamp,
            manual_dx=manual_dx,
            manual_dy=manual_dy,
            is_aiming=self._is_aiming,
            target_dx=target_dx,
            target_dy=target_dy,
            auto_fire_requested=auto_fire_requested,
            target_revision=target_revision,
            target_timestamp=target_timestamp,
        )

    def _apply_output(self, output: MouseOutput):
        move_x = int(output.move_dx)
        move_y = int(output.move_dy)
        if move_x != 0 or move_y != 0:
            win32api.mouse_event(MOUSEEVENTF_MOVE, move_x, move_y, 0, 0)

        if output.left_click and not self._left_click_held:
            win32api.mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
            self._left_click_held = True
        elif not output.left_click and self._left_click_held:
            win32api.mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
            self._left_click_held = False

    def run(self):
        while self.running:
            frame = self._build_frame(timestamp=time.perf_counter())
            output = MouseOutput()
            apply_plugins(self.plugins, frame, output)
            self._apply_output(output)
            time.sleep(0.001)
```

- [ ] **Step 4: Run host tests**

Run: `python -m unittest tests.mouse.test_mouse_controller_host -v`
Expected: all 5 tests PASS

- [ ] **Step 5: Run all mouse tests together**

Run: `python -m unittest discover -s tests/mouse -p "test_*.py" -v`
Expected: all tests PASS

- [ ] **Step 6: Verify compile and import**

Run: `python -m py_compile controllers/mouse_controller.py && python -c "from controllers.mouse_controller import MouseController; print('ok')"`
Expected: `ok`

- [ ] **Step 7: Commit**

```bash
git add controllers/mouse_controller.py tests/mouse/test_mouse_controller_host.py
git commit -m "feat(mouse): rewrite MouseController as plugin host"
```

---

### Task 8: Verify full test suite and existing gamepad tests still pass

**Files:** none (verification only)

- [ ] **Step 1: Run all mouse tests**

Run: `python -m unittest discover -s tests/mouse -p "test_*.py" -v`
Expected: all tests PASS

- [ ] **Step 2: Run all gamepad tests (regression check)**

Run: `python -m unittest discover -s tests/gamepad -p "test_*.py" -v`
Expected: all tests PASS

- [ ] **Step 3: Run full test suite**

Run: `python -m unittest discover -s tests -p "test_*.py" -v`
Expected: all tests PASS

- [ ] **Step 4: Verify mouse_start.bat entry point compiles**

Run: `python -m py_compile main.py && python -c "from controller import ControllerFactory; print('factory ok')"`
Expected: `factory ok`

- [ ] **Step 5: Commit (if any fixups were needed)**

Only commit if there were fixes. Otherwise this step is a no-op.
