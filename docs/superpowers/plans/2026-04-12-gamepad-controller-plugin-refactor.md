# Gamepad Controller Plugin Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor `GamepadController` into a plugin-hosted controller loop that keeps base physical-to-virtual passthrough in the host while moving AI aim, automatic fire, and recoil compensation into controller-level plugins.

**Architecture:** Add shared frame/output dataclasses plus a small controller-plugin runtime, implement `AutoFirePlugin`, `AIAimPlugin`, and `RecoilCompensationPlugin`, then refactor `GamepadController` to build a frame snapshot, initialize passthrough output, invoke plugins in fixed order, and send the final virtual gamepad state. Keep `HorizontalAimAssist` and `OvershootGuard` as focused logic modules, but make them internal sub-plugins of `AIAimPlugin` so future motion-prediction and overshoot-limit features have a clear extension point.

**Tech Stack:** Python 3, `unittest`, `pygame`, `vgamepad`

---

### Task 1: Add failing tests for plugin primitives and automatic fire

**Files:**
- Create: `D:\work\AI\yolo-study-001\tests\test_gamepad_plugin_chain.py`
- Create: `D:\work\AI\yolo-study-001\tests\test_gamepad_auto_fire_plugin.py`

- [ ] **Step 1: Write the failing plugin-chain tests**

```python
import unittest

from controllers.gamepad_plugin import apply_plugins, reset_plugins
from controllers.gamepad_state import GamepadFrame, GamepadOutput


def _frame():
    return GamepadFrame(
        timestamp=1.0,
        left_x=0,
        left_y=0,
        manual_right_x=0,
        manual_right_y=0,
        left_trigger=255,
        right_trigger=0,
        buttons={"rb": False},
        is_aiming=True,
        target_dx=0.0,
        target_dy=0.0,
        auto_fire_requested=False,
    )


def _output():
    return GamepadOutput(
        left_x=0,
        left_y=0,
        right_x=0,
        right_y=0,
        left_trigger=255,
        right_trigger=0,
        buttons={"rb": False},
    )


class _RecordingPlugin:
    def __init__(self, name, calls):
        self.name = name
        self.calls = calls

    def reset(self):
        self.calls.append(f"reset:{self.name}")

    def apply(self, frame, output):
        self.calls.append(f"apply:{self.name}")
        output.buttons[self.name] = True


class GamepadPluginChainTests(unittest.TestCase):
    def test_apply_plugins_runs_in_declared_order(self):
        calls = []
        plugins = [_RecordingPlugin("aim", calls), _RecordingPlugin("fire", calls), _RecordingPlugin("recoil", calls)]

        output = _output()
        apply_plugins(plugins, _frame(), output)

        self.assertEqual(calls, ["apply:aim", "apply:fire", "apply:recoil"])
        self.assertTrue(output.buttons["aim"])
        self.assertTrue(output.buttons["fire"])
        self.assertTrue(output.buttons["recoil"])

    def test_reset_plugins_broadcasts_to_every_plugin(self):
        calls = []
        plugins = [_RecordingPlugin("aim", calls), _RecordingPlugin("fire", calls)]

        reset_plugins(plugins)

        self.assertEqual(calls, ["reset:aim", "reset:fire"])


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Write the failing automatic-fire tests**

```python
import unittest

from controllers.gamepad_auto_fire import AutoFireConfig, AutoFirePlugin
from controllers.gamepad_state import GamepadFrame, GamepadOutput


def _frame(*, aiming=True, auto_fire=False, manual_rb=False, manual_rt=0):
    return GamepadFrame(
        timestamp=1.0,
        left_x=0,
        left_y=0,
        manual_right_x=0,
        manual_right_y=0,
        left_trigger=255 if aiming else 0,
        right_trigger=manual_rt,
        buttons={"rb": manual_rb},
        is_aiming=aiming,
        target_dx=0.0,
        target_dy=0.0,
        auto_fire_requested=auto_fire,
    )


def _output(frame):
    return GamepadOutput(
        left_x=frame.left_x,
        left_y=frame.left_y,
        right_x=frame.manual_right_x,
        right_y=frame.manual_right_y,
        left_trigger=frame.left_trigger,
        right_trigger=frame.right_trigger,
        buttons=dict(frame.buttons),
    )


class AutoFirePluginTests(unittest.TestCase):
    def test_rb_mode_or_combines_manual_rb_and_auto_fire(self):
        plugin = AutoFirePlugin(AutoFireConfig(fire_output="RB"))
        frame = _frame(aiming=True, auto_fire=True, manual_rb=False, manual_rt=0)
        output = _output(frame)

        plugin.apply(frame, output)

        self.assertTrue(output.buttons["rb"])
        self.assertTrue(output.auto_fire_active)
        self.assertEqual(output.right_trigger, 0)

    def test_rt_mode_drives_trigger_without_touching_rb(self):
        plugin = AutoFirePlugin(AutoFireConfig(fire_output="RT"))
        frame = _frame(aiming=True, auto_fire=True, manual_rb=False, manual_rt=0)
        output = _output(frame)

        plugin.apply(frame, output)

        self.assertFalse(output.buttons["rb"])
        self.assertEqual(output.right_trigger, 255)
        self.assertTrue(output.auto_fire_active)

    def test_auto_fire_is_suppressed_when_not_aiming(self):
        plugin = AutoFirePlugin(AutoFireConfig(fire_output="RB"))
        frame = _frame(aiming=False, auto_fire=True, manual_rb=False, manual_rt=0)
        output = _output(frame)

        plugin.apply(frame, output)

        self.assertFalse(output.buttons["rb"])
        self.assertFalse(output.auto_fire_active)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 3: Run the new tests and confirm they fail because the plugin support modules do not exist yet**

Run:

```powershell
python -m unittest tests.test_gamepad_plugin_chain tests.test_gamepad_auto_fire_plugin -v
```

Expected:

```text
ERROR: test_gamepad_plugin_chain
ModuleNotFoundError: No module named 'controllers.gamepad_plugin'
```

- [ ] **Step 4: Implement the shared plugin primitives and `AutoFirePlugin`**

Create `D:\work\AI\yolo-study-001\controllers\gamepad_state.py`:

```python
from dataclasses import dataclass, field
from typing import Mapping


@dataclass(slots=True, frozen=True)
class GamepadFrame:
    timestamp: float
    left_x: int
    left_y: int
    manual_right_x: int
    manual_right_y: int
    left_trigger: int
    right_trigger: int
    buttons: Mapping[str, bool]
    is_aiming: bool
    target_dx: float
    target_dy: float
    auto_fire_requested: bool


@dataclass(slots=True)
class GamepadOutput:
    left_x: int = 0
    left_y: int = 0
    right_x: int = 0
    right_y: int = 0
    left_trigger: int = 0
    right_trigger: int = 0
    buttons: dict[str, bool] = field(default_factory=dict)
    auto_fire_active: bool = False
```

Create `D:\work\AI\yolo-study-001\controllers\gamepad_plugin.py`:

```python
from typing import Iterable, Protocol

from .gamepad_state import GamepadFrame, GamepadOutput


class GamepadPlugin(Protocol):
    def reset(self) -> None:
        ...

    def apply(self, frame: GamepadFrame, output: GamepadOutput) -> None:
        ...


def apply_plugins(plugins: Iterable[GamepadPlugin], frame: GamepadFrame, output: GamepadOutput) -> None:
    for plugin in plugins:
        plugin.apply(frame, output)


def reset_plugins(plugins: Iterable[GamepadPlugin]) -> None:
    for plugin in plugins:
        plugin.reset()
```

Create `D:\work\AI\yolo-study-001\controllers\gamepad_auto_fire.py`:

```python
from dataclasses import dataclass
from typing import Literal

from .gamepad_state import GamepadFrame, GamepadOutput


@dataclass(slots=True, frozen=True)
class AutoFireConfig:
    fire_output: Literal["RB", "RT"] = "RB"
    aim_only: bool = True


class AutoFirePlugin:
    def __init__(self, config: AutoFireConfig | None = None):
        self.config = config or AutoFireConfig()

    def reset(self) -> None:
        return None

    def apply(self, frame: GamepadFrame, output: GamepadOutput) -> None:
        should_fire = frame.auto_fire_requested
        if self.config.aim_only:
            should_fire = should_fire and frame.is_aiming

        output.auto_fire_active = should_fire
        if self.config.fire_output == "RB":
            output.buttons["rb"] = bool(output.buttons.get("rb", False) or should_fire)
            return

        if should_fire:
            output.right_trigger = 255
```

- [ ] **Step 5: Re-run the tests and confirm they pass**

Run:

```powershell
python -m unittest tests.test_gamepad_plugin_chain tests.test_gamepad_auto_fire_plugin -v
```

Expected:

```text
test_apply_plugins_runs_in_declared_order ... ok
test_reset_plugins_broadcasts_to_every_plugin ... ok
test_rb_mode_or_combines_manual_rb_and_auto_fire ... ok
test_rt_mode_drives_trigger_without_touching_rb ... ok
test_auto_fire_is_suppressed_when_not_aiming ... ok
```

- [ ] **Step 6: Commit the passing plugin primitive and automatic-fire baseline**

```powershell
git add tests/test_gamepad_plugin_chain.py tests/test_gamepad_auto_fire_plugin.py controllers/gamepad_state.py controllers/gamepad_plugin.py controllers/gamepad_auto_fire.py
git commit -m "refactor: add gamepad plugin primitives"
```

### Task 2: Add failing tests for AI-aim and recoil plugins

**Files:**
- Create: `D:\work\AI\yolo-study-001\tests\test_gamepad_ai_aim_plugin.py`
- Create: `D:\work\AI\yolo-study-001\tests\test_gamepad_recoil_compensation.py`

- [ ] **Step 1: Write the failing AI-aim plugin tests**

```python
import unittest

from controllers.gamepad_ai_aim import AIAimConfig, AIAimPlugin
from controllers.gamepad_state import GamepadFrame, GamepadOutput


def _frame(*, aiming=True, target_dx=0.0, target_dy=0.0, manual_rx=0, manual_ry=0):
    return GamepadFrame(
        timestamp=1.0,
        left_x=0,
        left_y=0,
        manual_right_x=manual_rx,
        manual_right_y=manual_ry,
        left_trigger=255 if aiming else 0,
        right_trigger=0,
        buttons={"rb": False},
        is_aiming=aiming,
        target_dx=target_dx,
        target_dy=target_dy,
        auto_fire_requested=False,
    )


def _output(frame):
    return GamepadOutput(
        left_x=frame.left_x,
        left_y=frame.left_y,
        right_x=frame.manual_right_x,
        right_y=frame.manual_right_y,
        left_trigger=frame.left_trigger,
        right_trigger=frame.right_trigger,
        buttons=dict(frame.buttons),
    )


class AIAimPluginTests(unittest.TestCase):
    def test_ai_aim_adds_right_stick_correction_when_aiming(self):
        plugin = AIAimPlugin(
            AIAimConfig(
                smoothing=0.0,
                max_pixels=150,
                deadzone_inner=0.0,
                deadzone_outer=1.0,
                x_deadzone_outer=1.0,
                ai_delta_gain=1.0,
            ),
            sub_plugins=(),
        )
        frame = _frame(aiming=True, target_dx=30.0, target_dy=-15.0, manual_rx=0, manual_ry=0)
        output = _output(frame)

        plugin.apply(frame, output)

        self.assertGreater(output.right_x, 0)
        self.assertGreater(output.right_y, 0)

    def test_ai_aim_keeps_manual_passthrough_when_not_aiming(self):
        plugin = AIAimPlugin(AIAimConfig(smoothing=0.0), sub_plugins=())
        frame = _frame(aiming=False, target_dx=50.0, target_dy=20.0, manual_rx=4000, manual_ry=-3000)
        output = _output(frame)

        plugin.apply(frame, output)

        self.assertEqual(output.right_x, 4000)
        self.assertEqual(output.right_y, -3000)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Write the failing recoil-compensation tests**

```python
import unittest

from controllers.gamepad_recoil_compensation import RecoilCompensationConfig, RecoilCompensationPlugin
from controllers.gamepad_state import GamepadFrame, GamepadOutput


def _frame():
    return GamepadFrame(
        timestamp=1.0,
        left_x=0,
        left_y=0,
        manual_right_x=0,
        manual_right_y=0,
        left_trigger=255,
        right_trigger=0,
        buttons={"rb": False},
        is_aiming=True,
        target_dx=0.0,
        target_dy=0.0,
        auto_fire_requested=False,
    )


class RecoilCompensationPluginTests(unittest.TestCase):
    def test_recoil_is_applied_only_when_auto_fire_is_active(self):
        plugin = RecoilCompensationPlugin(RecoilCompensationConfig(amount=0.30))
        frame = _frame()
        output = GamepadOutput(right_y=0, auto_fire_active=True)

        plugin.apply(frame, output)

        self.assertLess(output.right_y, 0)

    def test_recoil_is_skipped_when_auto_fire_is_inactive(self):
        plugin = RecoilCompensationPlugin(RecoilCompensationConfig(amount=0.30))
        frame = _frame()
        output = GamepadOutput(right_y=0, auto_fire_active=False)

        plugin.apply(frame, output)

        self.assertEqual(output.right_y, 0)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 3: Run the new tests and confirm they fail because the AI/recoil plugins do not exist yet**

Run:

```powershell
python -m unittest tests.test_gamepad_ai_aim_plugin tests.test_gamepad_recoil_compensation -v
```

Expected:

```text
ERROR: test_gamepad_ai_aim_plugin
ModuleNotFoundError: No module named 'controllers.gamepad_ai_aim'
```

- [ ] **Step 4: Implement `AIAimPlugin` and `RecoilCompensationPlugin`**

Create `D:\work\AI\yolo-study-001\controllers\gamepad_ai_aim.py`:

```python
from dataclasses import dataclass
from typing import Iterable, Protocol

from .gamepad_horizontal_assist import HorizontalAimAssist, HorizontalAimAssistConfig, compute_axis_soft_strengths
from .gamepad_overshoot_guard import OvershootGuard, OvershootGuardConfig
from .gamepad_state import GamepadFrame, GamepadOutput


@dataclass(slots=True, frozen=True)
class AIAimConfig:
    smoothing: float = 0.65
    max_pixels: int = 150
    invert_x: bool = False
    invert_y: bool = False
    max_ai_force: float = 0.6
    deadzone_inner: float = 1.5
    deadzone_outer: float = 5.0
    x_deadzone_outer: float = 3.0
    phys_stick_deadzone: int = 2500
    ai_fade_full: int = 8000
    ai_delta_gain: float = 0.7


@dataclass(slots=True)
class AIAimContext:
    manual_rx: int
    manual_ry: int
    target_dx: float
    target_dy: float
    timestamp: float
    assist_dx: float
    x_force_bonus: float = 0.0
    x_desired_scale: float = 1.0
    y_desired_scale: float = 1.0
    x_carry_scale: float = 1.0
    y_carry_scale: float = 1.0


class AIAimSubPlugin(Protocol):
    def reset(self) -> None:
        ...

    def observe_target(self, *, target_dx: float, target_dy: float, is_aiming: bool, timestamp: float) -> None:
        ...

    def apply(self, context: AIAimContext) -> None:
        ...


class HorizontalAssistSubPlugin:
    def __init__(self):
        self.assist = HorizontalAimAssist(
            HorizontalAimAssistConfig(
                min_error_px=4.0,
                min_velocity_px_per_sec=60.0,
                velocity_filter_alpha=0.45,
                feedforward_lead_seconds=0.02,
                feedforward_gain=0.65,
                max_feedforward_px=6.0,
                catchup_trigger_frames=3,
                catchup_gain_per_update=0.02,
                catchup_max_bonus=0.10,
                catchup_decay=0.04,
                opposing_input_threshold=5000,
                convergence_epsilon_px=0.25,
            )
        )

    def reset(self) -> None:
        self.assist.reset()

    def observe_target(self, *, target_dx: float, target_dy: float, is_aiming: bool, timestamp: float) -> None:
        self.assist.observe_target(target_dx=target_dx, is_aiming=is_aiming, timestamp=timestamp)

    def apply(self, context: AIAimContext) -> None:
        feedforward_dx, x_force_bonus = self.assist.compute_adjustment(context.manual_rx)
        context.assist_dx += feedforward_dx
        context.x_force_bonus += x_force_bonus


class OvershootGuardSubPlugin:
    def __init__(self):
        self.guard = OvershootGuard(
            OvershootGuardConfig(
                manual_input_threshold=3500,
                near_error_px=8.0,
                release_error_px=22.0,
                convergence_epsilon_px=0.25,
                convergence_trigger_frames=2,
                convergence_build_per_update=0.22,
                convergence_max_guard=0.50,
                convergence_decay=0.18,
                zero_cross_arm_px=6.0,
                zero_cross_hold_seconds=0.04,
                zero_cross_guard=0.85,
                carry_damp_gain=1.0,
            )
        )

    def reset(self) -> None:
        self.guard.reset()

    def observe_target(self, *, target_dx: float, target_dy: float, is_aiming: bool, timestamp: float) -> None:
        self.guard.observe_target(target_dx=target_dx, target_dy=target_dy, is_aiming=is_aiming, timestamp=timestamp)

    def apply(self, context: AIAimContext) -> None:
        adjustment = self.guard.compute_adjustment(
            manual_rx=context.manual_rx,
            manual_ry=context.manual_ry,
            timestamp=context.timestamp,
        )
        context.x_desired_scale *= adjustment.x_desired_scale
        context.y_desired_scale *= adjustment.y_desired_scale
        context.x_carry_scale *= adjustment.x_carry_scale
        context.y_carry_scale *= adjustment.y_carry_scale


class AIAimPlugin:
    def __init__(self, config: AIAimConfig | None = None, sub_plugins: Iterable[AIAimSubPlugin] | None = None):
        self.config = config or AIAimConfig()
        self.sub_plugins = tuple(sub_plugins) if sub_plugins is not None else (
            HorizontalAssistSubPlugin(),
            OvershootGuardSubPlugin(),
        )
        self.ai_stick_x = 0.0
        self.ai_stick_y = 0.0

    def reset(self) -> None:
        for plugin in self.sub_plugins:
            plugin.reset()

    def apply(self, frame: GamepadFrame, output: GamepadOutput) -> None:
        target_dx = frame.target_dx * self.config.ai_delta_gain
        target_dy = frame.target_dy * self.config.ai_delta_gain
        for plugin in self.sub_plugins:
            plugin.observe_target(
                target_dx=target_dx,
                target_dy=target_dy,
                is_aiming=frame.is_aiming,
                timestamp=frame.timestamp,
            )

        context = AIAimContext(
            manual_rx=frame.manual_right_x,
            manual_ry=frame.manual_right_y,
            target_dx=target_dx,
            target_dy=target_dy,
            timestamp=frame.timestamp,
            assist_dx=target_dx,
        )
        for plugin in self.sub_plugins:
            plugin.apply(context)

        desired_ai_x = 0.0
        desired_ai_y = 0.0
        if frame.is_aiming:
            x_strength, y_strength = compute_axis_soft_strengths(
                dx=context.assist_dx,
                dy=context.target_dy,
                inner=self.config.deadzone_inner,
                radial_outer=self.config.deadzone_outer,
                x_outer=self.config.x_deadzone_outer,
            )
            if x_strength > 0.0 or y_strength > 0.0:
                desired_ai_x = self._map_pixel_to_stick(context.assist_dx) * x_strength
                desired_ai_y = self._map_pixel_to_stick(-context.target_dy) * y_strength
                if self.config.invert_x:
                    desired_ai_x = -desired_ai_x
                if self.config.invert_y:
                    desired_ai_y = -desired_ai_y
                x_limit = 32767 * min(1.0, self.config.max_ai_force + context.x_force_bonus)
                y_limit = 32767 * self.config.max_ai_force
                desired_ai_x = max(-x_limit, min(x_limit, desired_ai_x)) * context.x_desired_scale
                desired_ai_y = max(-y_limit, min(y_limit, desired_ai_y)) * context.y_desired_scale

        self.ai_stick_x = (self.ai_stick_x * self.config.smoothing * context.x_carry_scale) + (
            desired_ai_x * (1.0 - self.config.smoothing)
        )
        self.ai_stick_y = (self.ai_stick_y * self.config.smoothing * context.y_carry_scale) + (
            desired_ai_y * (1.0 - self.config.smoothing)
        )

        scale_x = self._ai_scale_factor(frame.manual_right_x)
        scale_y = self._ai_scale_factor(frame.manual_right_y)
        output.right_x = int(frame.manual_right_x + self.ai_stick_x * scale_x)
        output.right_y = int(frame.manual_right_y + self.ai_stick_y * scale_y)

    def _map_pixel_to_stick(self, delta: float) -> float:
        clamped = max(-self.config.max_pixels, min(self.config.max_pixels, delta))
        return (clamped / self.config.max_pixels) * 32767

    def _ai_scale_factor(self, user_val: int) -> float:
        magnitude = abs(user_val)
        if magnitude >= self.config.ai_fade_full:
            return 0.0
        return 1.0 - (magnitude / self.config.ai_fade_full)
```

Create `D:\work\AI\yolo-study-001\controllers\gamepad_recoil_compensation.py`:

```python
from dataclasses import dataclass

from .gamepad_state import GamepadFrame, GamepadOutput


@dataclass(slots=True, frozen=True)
class RecoilCompensationConfig:
    amount: float = 0.30


class RecoilCompensationPlugin:
    def __init__(self, config: RecoilCompensationConfig | None = None):
        self.config = config or RecoilCompensationConfig()

    def reset(self) -> None:
        return None

    def apply(self, frame: GamepadFrame, output: GamepadOutput) -> None:
        if output.auto_fire_active and self.config.amount != 0.0:
            output.right_y -= int(self.config.amount * 32767)
```

- [ ] **Step 5: Re-run the AI/recoil tests and confirm they pass**

Run:

```powershell
python -m unittest tests.test_gamepad_ai_aim_plugin tests.test_gamepad_recoil_compensation -v
```

Expected:

```text
test_ai_aim_adds_right_stick_correction_when_aiming ... ok
test_ai_aim_keeps_manual_passthrough_when_not_aiming ... ok
test_recoil_is_applied_only_when_auto_fire_is_active ... ok
test_recoil_is_skipped_when_auto_fire_is_inactive ... ok
```

- [ ] **Step 6: Commit the AI and recoil plugins**

```powershell
git add tests/test_gamepad_ai_aim_plugin.py tests/test_gamepad_recoil_compensation.py controllers/gamepad_ai_aim.py controllers/gamepad_recoil_compensation.py
git commit -m "refactor: extract gamepad aim and recoil plugins"
```

### Task 3: Add failing tests for host reset, compatibility aliases, and plugin-backed controller behavior

**Files:**
- Create: `D:\work\AI\yolo-study-001\tests\test_gamepad_controller_host.py`
- Modify: `D:\work\AI\yolo-study-001\tests\test_vision_runner.py`

- [ ] **Step 1: Write the failing host tests**

```python
import threading
import unittest

from controllers.gamepad_controller import GamepadController


class _FakePlugin:
    def __init__(self):
        self.reset_calls = 0

    def reset(self):
        self.reset_calls += 1

    def apply(self, frame, output):
        return None


class GamepadControllerHostTests(unittest.TestCase):
    def test_reset_clears_shared_target_signals_and_resets_plugins(self):
        controller = GamepadController.__new__(GamepadController)
        controller.lock = threading.Lock()
        controller.target_dx = 12.0
        controller.target_dy = -8.0
        controller.plugins = [_FakePlugin(), _FakePlugin()]

        GamepadController.reset(controller)

        self.assertEqual(controller.target_dx, 0.0)
        self.assertEqual(controller.target_dy, 0.0)
        self.assertEqual(controller.plugins[0].reset_calls, 1)
        self.assertEqual(controller.plugins[1].reset_calls, 1)

    def test_set_auto_rb_is_a_compatibility_alias_for_set_auto_fire(self):
        controller = GamepadController.__new__(GamepadController)
        controller.lock = threading.Lock()
        controller._auto_fire_requested = False

        GamepadController.set_auto_rb(controller, True)

        self.assertTrue(controller._auto_fire_requested)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Extend the vision-runner tests to lock the new base-controller alias**

```python
import unittest

from controllers.base_controller import BaseController


class _AliasController(BaseController):
    def __init__(self):
        self.auto_fire_values = []

    def update(self, dx, dy):
        return None

    def reset(self):
        return None

    def is_aiming(self):
        return False

    def set_auto_fire(self, pressed: bool):
        self.auto_fire_values.append(bool(pressed))


class BaseControllerAliasTests(unittest.TestCase):
    def test_set_auto_rb_forwards_to_set_auto_fire(self):
        controller = _AliasController()

        controller.set_auto_rb(True)

        self.assertEqual(controller.auto_fire_values, [True])
```

- [ ] **Step 3: Run the host and alias tests and confirm they fail against the current controller code**

Run:

```powershell
python -m unittest tests.test_gamepad_controller_host tests.test_vision_runner -v
```

Expected:

```text
FAIL: test_set_auto_rb_is_a_compatibility_alias_for_set_auto_fire
AssertionError: False is not true
```

- [ ] **Step 4: Refactor `GamepadController` into a plugin host and migrate the controller API**

Modify `D:\work\AI\yolo-study-001\controllers\base_controller.py`:

```python
from abc import ABC, abstractmethod


class BaseController(ABC):
    @abstractmethod
    def update(self, dx: float, dy: float):
        pass

    @abstractmethod
    def reset(self):
        pass

    @abstractmethod
    def is_aiming(self) -> bool:
        pass

    def set_auto_fire(self, pressed: bool):
        pass

    def set_auto_rb(self, pressed: bool):
        self.set_auto_fire(pressed)

    def stop(self):
        pass
```

Modify `D:\work\AI\yolo-study-001\controllers\kbm_controller.py`:

```python
    def set_auto_fire(self, pressed: bool):
        with self.lock:
            self._auto_rb_pressed = bool(pressed)
            self._sync_rb_state()

    def set_auto_rb(self, pressed: bool):
        self.set_auto_fire(pressed)
```

Modify `D:\work\AI\yolo-study-001\controllers\gamepad_controller.py` to use plugin-backed output construction:

```python
import threading
import time
import os

import pygame
import vgamepad as vg

from .base_controller import BaseController
from .gamepad_ai_aim import AIAimConfig, AIAimPlugin
from .gamepad_auto_fire import AutoFireConfig, AutoFirePlugin
from .gamepad_plugin import apply_plugins, reset_plugins
from .gamepad_recoil_compensation import RecoilCompensationConfig, RecoilCompensationPlugin
from .gamepad_state import GamepadFrame, GamepadOutput

os.environ["PYGAME_HIDE_SUPPORT_PROMPT"] = "hide"


class GamepadController(BaseController, threading.Thread):
    def __init__(self, smoothing=0.65, max_pixels=150, auto_fire_output="RB", plugins=None):
        super().__init__()
        self.daemon = True
        self.running = True
        self.ready = False
        self.lock = threading.Lock()
        self._is_aiming = False
        self._auto_fire_requested = False
        self.target_dx = 0.0
        self.target_dy = 0.0
        self.plugins = list(plugins) if plugins is not None else [
            AIAimPlugin(AIAimConfig(smoothing=smoothing, max_pixels=max_pixels)),
            AutoFirePlugin(AutoFireConfig(fire_output=auto_fire_output)),
            RecoilCompensationPlugin(RecoilCompensationConfig(amount=0.30)),
        ]

    def update(self, dx, dy):
        with self.lock:
            self.target_dx = dx
            self.target_dy = dy

    def reset(self):
        with self.lock:
            self.target_dx = 0.0
            self.target_dy = 0.0
        reset_plugins(self.plugins)

    def set_auto_fire(self, pressed: bool):
        with self.lock:
            self._auto_fire_requested = bool(pressed)

    def set_auto_rb(self, pressed: bool):
        self.set_auto_fire(pressed)

    def _build_frame(self, timestamp: float, phys_rx: int, phys_ry: int, l2_val: int, r2_val: int, buttons: dict[str, bool]):
        with self.lock:
            target_dx = self.target_dx
            target_dy = self.target_dy
            auto_fire_requested = self._auto_fire_requested
        return GamepadFrame(
            timestamp=timestamp,
            left_x=self._axis_to_xbox(self.joystick.get_axis(0)),
            left_y=self._axis_to_xbox(-self.joystick.get_axis(1)),
            manual_right_x=phys_rx,
            manual_right_y=phys_ry,
            left_trigger=l2_val,
            right_trigger=r2_val,
            buttons=buttons,
            is_aiming=self._is_aiming,
            target_dx=target_dx,
            target_dy=target_dy,
            auto_fire_requested=auto_fire_requested,
        )

    def _build_output(self, frame: GamepadFrame) -> GamepadOutput:
        return GamepadOutput(
            left_x=frame.left_x,
            left_y=frame.left_y,
            right_x=frame.manual_right_x,
            right_y=frame.manual_right_y,
            left_trigger=frame.left_trigger,
            right_trigger=frame.right_trigger,
            buttons=dict(frame.buttons),
        )

    def _apply_output(self, output: GamepadOutput) -> None:
        self.virtual_gamepad.left_joystick(x_value=output.left_x, y_value=output.left_y)
        self.virtual_gamepad.right_joystick(
            x_value=max(-32768, min(32767, output.right_x)),
            y_value=max(-32768, min(32767, output.right_y)),
        )
        self.virtual_gamepad.left_trigger(value=output.left_trigger)
        self.virtual_gamepad.right_trigger(value=output.right_trigger)
        if output.buttons.get("rb", False):
            self.virtual_gamepad.press_button(button=vg.XUSB_BUTTON.XUSB_GAMEPAD_RIGHT_SHOULDER)
        else:
            self.virtual_gamepad.release_button(button=vg.XUSB_BUTTON.XUSB_GAMEPAD_RIGHT_SHOULDER)
```

After inserting the fields above, keep the existing `vg.VX360Gamepad()`, `pygame.init()`, joystick detection, error handling, `self.ready = True`, and `self.start()` sequence intact below them. Do not rework hardware setup in this refactor.

Modify the `run()` loop in `D:\work\AI\yolo-study-001\controllers\gamepad_controller.py` so the feature logic becomes:

```python
            buttons = {"rb": bool(self.joystick.get_button(10))}
            frame = self._build_frame(
                timestamp=time.perf_counter(),
                phys_rx=phys_rx,
                phys_ry=phys_ry,
                l2_val=l2_val,
                r2_val=r2_val,
                buttons=buttons,
            )
            output = self._build_output(frame)
            apply_plugins(self.plugins, frame, output)
            self._apply_output(output)
            self.virtual_gamepad.update()
```

Modify `D:\work\AI\yolo-study-001\vision\runner.py` to call the new API:

```python
                    if controller:
                        controller.reset()
                        controller.set_auto_fire(False)
```

```python
                if controller:
                    controller.set_auto_fire(False)
                    controller.reset()
```

```python
            if controller:
                controller.set_auto_fire(auto_rb_active)
                if best_target_delta:
                    controller.update(best_target_delta[0], best_target_delta[1])
                else:
                    controller.reset()
```

```python
        if controller:
            controller.set_auto_fire(False)
            controller.reset()
```

- [ ] **Step 5: Re-run the host, alias, and existing aim-plugin tests**

Run:

```powershell
python -m unittest tests.test_gamepad_controller_host tests.test_vision_runner tests.test_gamepad_plugin_chain tests.test_gamepad_auto_fire_plugin tests.test_gamepad_ai_aim_plugin tests.test_gamepad_recoil_compensation -v
```

Expected:

```text
test_reset_clears_shared_target_signals_and_resets_plugins ... ok
test_set_auto_rb_is_a_compatibility_alias_for_set_auto_fire ... ok
test_set_auto_rb_forwards_to_set_auto_fire ... ok
test_apply_plugins_runs_in_declared_order ... ok
test_rb_mode_or_combines_manual_rb_and_auto_fire ... ok
test_ai_aim_adds_right_stick_correction_when_aiming ... ok
test_recoil_is_applied_only_when_auto_fire_is_active ... ok
```

- [ ] **Step 6: Commit the controller-host refactor and compatibility migration**

```powershell
git add tests/test_gamepad_controller_host.py tests/test_vision_runner.py controllers/base_controller.py controllers/kbm_controller.py controllers/gamepad_controller.py vision/runner.py
git commit -m "refactor: host gamepad enhancements through plugins"
```

### Task 4: Preserve current aim behavior through targeted regression tests

**Files:**
- Modify: `D:\work\AI\yolo-study-001\tests\test_gamepad_ai_aim_plugin.py`

- [ ] **Step 1: Add an AI-aim integration test that locks in sub-plugin ordering and the right-stick shaping path**

```python
class _AddDxPlugin:
    def reset(self):
        return None

    def observe_target(self, *, target_dx: float, target_dy: float, is_aiming: bool, timestamp: float):
        return None

    def apply(self, context):
        context.assist_dx += 5.0


class _ScalePlugin:
    def reset(self):
        return None

    def observe_target(self, *, target_dx: float, target_dy: float, is_aiming: bool, timestamp: float):
        return None

    def apply(self, context):
        context.x_desired_scale *= 0.5


class AIAimPluginTests(unittest.TestCase):
    def test_ai_aim_applies_sub_plugins_in_declared_order(self):
        plugin = AIAimPlugin(
            AIAimConfig(
                smoothing=0.0,
                deadzone_inner=0.0,
                deadzone_outer=1.0,
                x_deadzone_outer=1.0,
                ai_delta_gain=1.0,
            ),
            sub_plugins=(_AddDxPlugin(), _ScalePlugin()),
        )
        frame = _frame(aiming=True, target_dx=20.0, target_dy=0.0, manual_rx=0, manual_ry=0)
        output = _output(frame)

        plugin.apply(frame, output)

        self.assertGreater(output.right_x, 0)
        self.assertLess(output.right_x, 32767)
```

- [ ] **Step 2: Run the targeted plugin and math regressions**

Run:

```powershell
python -m unittest tests.test_gamepad_ai_aim_plugin tests.test_gamepad_horizontal_assist tests.test_gamepad_overshoot_guard tests.test_gamepad_aim_math -v
```

Expected:

```text
test_ai_aim_applies_sub_plugins_in_declared_order ... ok
test_sustained_horizontal_growth_produces_positive_feedforward ... ok
test_zero_cross_near_center_triggers_brake_without_manual_input ... ok
test_x_axis_strength_decays_less_than_radial_strength_near_target ... ok
```

- [ ] **Step 3: Commit the regression lock-in**

```powershell
git add tests/test_gamepad_ai_aim_plugin.py tests/test_gamepad_horizontal_assist.py tests/test_gamepad_overshoot_guard.py tests/test_gamepad_aim_math.py
git commit -m "test: lock in gamepad plugin behavior"
```

### Task 5: Run final verification and report remaining risks

**Files:**
- Modify: `D:\work\AI\yolo-study-001\controllers\__init__.py` only if export surface changes

- [ ] **Step 1: Run the full targeted unittest set**

Run:

```powershell
python -m unittest tests.test_gamepad_plugin_chain tests.test_gamepad_auto_fire_plugin tests.test_gamepad_ai_aim_plugin tests.test_gamepad_recoil_compensation tests.test_gamepad_controller_host tests.test_gamepad_horizontal_assist tests.test_gamepad_overshoot_guard tests.test_gamepad_aim_math tests.test_vision_runner -v
```

Expected:

```text
OK
```

- [ ] **Step 2: Run syntax verification for the refactored controller stack**

Run:

```powershell
python -m py_compile controller.py controllers\base_controller.py controllers\gamepad_state.py controllers\gamepad_plugin.py controllers\gamepad_auto_fire.py controllers\gamepad_ai_aim.py controllers\gamepad_recoil_compensation.py controllers\gamepad_controller.py controllers\kbm_controller.py vision\runner.py
```

Expected:

```text
[no output]
```

- [ ] **Step 3: Check for accidental API drift**

Run:

```powershell
git diff --stat
```

Expected:

```text
Only the planned controller, vision, and test files are modified.
```

- [ ] **Step 4: Summarize the refactor boundaries and known follow-up risks**

Report:

```text
- Host loop now owns only passthrough I/O, frame construction, plugin dispatch, and virtual-pad writes.
- AI aim, automatic fire, and recoil compensation now live in isolated controller-level plugins.
- Horizontal assist and overshoot guard remain focused logic units under AIAimPlugin for future extension.
- Remaining follow-up risks: hardware-only regressions around hat/button passthrough, RT-mode auto-fire feel in live gameplay, and whether KBM mode should later adopt the same plugin host structure.
```
