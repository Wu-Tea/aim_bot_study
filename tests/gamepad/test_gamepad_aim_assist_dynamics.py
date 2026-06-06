import unittest

from controllers.gamepad.aim_assist_dynamics import (
    AimAssistDynamicsConfig,
    AimAssistDynamicsPlugin,
)
from controllers.gamepad.plugin import apply_plugins
from controllers.gamepad.state import GamepadFrame, GamepadOutput


def _frame(
    *,
    timestamp: float = 1.0,
    manual_right_x: int = 0,
    manual_right_y: int = 0,
    is_aiming: bool = True,
    right_trigger: int = 0,
    rb: bool = False,
) -> GamepadFrame:
    return GamepadFrame(
        timestamp=timestamp,
        left_x=0,
        left_y=0,
        manual_right_x=manual_right_x,
        manual_right_y=manual_right_y,
        left_trigger=255,
        right_trigger=right_trigger,
        buttons={"rb": rb},
        is_aiming=is_aiming,
        target_dx=0.0,
        target_dy=0.0,
        auto_fire_requested=False,
    )


class _WriteAssistPlugin:
    def __init__(self, *, assist_x: int = 0, assist_y: int = 0):
        self.assist_x = assist_x
        self.assist_y = assist_y

    def reset(self) -> None:
        pass

    def apply(self, frame: GamepadFrame, output: GamepadOutput) -> None:
        output.right_x = frame.manual_right_x + self.assist_x
        output.right_y = frame.manual_right_y + self.assist_y


class _AddRightStickPlugin:
    def __init__(self, *, x: int = 0, y: int = 0):
        self.x = x
        self.y = y

    def reset(self) -> None:
        pass

    def apply(self, frame: GamepadFrame, output: GamepadOutput) -> None:
        output.right_x += self.x
        output.right_y += self.y


class _SetAutoFirePlugin:
    def __init__(self, active: bool):
        self.active = active

    def reset(self) -> None:
        pass

    def apply(self, frame: GamepadFrame, output: GamepadOutput) -> None:
        output.auto_fire_active = self.active


class AimAssistDynamicsPluginTests(unittest.TestCase):
    def test_pure_manual_input_passes_through(self):
        plugin = AimAssistDynamicsPlugin(AimAssistDynamicsConfig())
        frame = _frame(manual_right_x=18000, manual_right_y=-7000)
        output = GamepadOutput(right_x=18000, right_y=-7000)

        plugin.apply(frame, output)

        self.assertEqual(output.right_x, 18000)
        self.assertEqual(output.right_y, -7000)

    def test_non_firing_ai_assist_output_passes_through_exactly(self):
        plugin = AimAssistDynamicsPlugin(AimAssistDynamicsConfig())
        frame = _frame(manual_right_x=2000, manual_right_y=-1000)
        output = GamepadOutput(right_x=14000, right_y=-9000, auto_fire_active=False)

        plugin.apply(frame, output)

        self.assertEqual(output.right_x, 14000)
        self.assertEqual(output.right_y, -9000)

    def test_recoil_active_suppresses_small_assist_sign_flip_on_both_axes(self):
        plugin = AimAssistDynamicsPlugin(
            AimAssistDynamicsConfig(
                recoil_jitter_assist_threshold=1200.0,
                recoil_jitter_flip_scale=0.0,
            )
        )
        first = GamepadOutput(right_x=900, right_y=900, auto_fire_active=True)
        plugin.apply(_frame(timestamp=1.0), first)

        second = GamepadOutput(right_x=-900, right_y=-900, auto_fire_active=True)
        plugin.apply(_frame(timestamp=1.0 + (1.0 / 120.0)), second)

        self.assertEqual(first.right_x, 900)
        self.assertEqual(first.right_y, 900)
        self.assertEqual(second.right_x, 0)
        self.assertEqual(second.right_y, 0)

    def test_manual_fire_activates_recoil_jitter_guard(self):
        plugin = AimAssistDynamicsPlugin(
            AimAssistDynamicsConfig(
                recoil_jitter_assist_threshold=1200.0,
                recoil_jitter_flip_scale=0.0,
            )
        )
        first = GamepadOutput(right_x=700, right_y=900, auto_fire_active=False)
        plugin.apply(_frame(timestamp=1.0, rb=True), first)

        second = GamepadOutput(right_x=-700, right_y=-900, auto_fire_active=False)
        plugin.apply(_frame(timestamp=1.0 + (1.0 / 120.0), rb=True), second)

        self.assertEqual(first.right_x, 700)
        self.assertEqual(first.right_y, 900)
        self.assertEqual(second.right_x, 0)
        self.assertEqual(second.right_y, 0)

    def test_large_aim_correction_during_recoil_passes_through_on_both_axes(self):
        plugin = AimAssistDynamicsPlugin(
            AimAssistDynamicsConfig(recoil_jitter_assist_threshold=1200.0)
        )
        first = GamepadOutput(right_x=900, right_y=900, auto_fire_active=True)
        plugin.apply(_frame(timestamp=1.0), first)

        second = GamepadOutput(right_x=-5000, right_y=-5000, auto_fire_active=True)
        plugin.apply(_frame(timestamp=1.0 + (1.0 / 120.0)), second)

        self.assertEqual(second.right_x, -5000)
        self.assertEqual(second.right_y, -5000)

    def test_plugin_order_leaves_later_recoil_delta_unshaped(self):
        dynamics = AimAssistDynamicsPlugin(
            AimAssistDynamicsConfig(
                recoil_jitter_assist_threshold=1200.0,
                recoil_jitter_flip_scale=0.0,
            )
        )
        plugins = [
            _WriteAssistPlugin(assist_x=-900, assist_y=-900),
            _SetAutoFirePlugin(True),
            dynamics,
            _AddRightStickPlugin(x=-5000, y=-5000),
        ]
        output = GamepadOutput(right_x=0, right_y=0)

        apply_plugins(plugins, _frame(manual_right_x=1000, manual_right_y=1000), output)

        self.assertEqual(output.right_x, -4900)
        self.assertEqual(output.right_y, -4900)

    def test_disabled_dynamics_preserves_ai_aim_output_exactly(self):
        plugin = AimAssistDynamicsPlugin(AimAssistDynamicsConfig(enabled=False))
        frame = _frame(manual_right_x=2000, manual_right_y=-1000)
        output = GamepadOutput(right_x=14000, right_y=-9000)

        plugin.apply(frame, output)

        self.assertEqual(output.right_x, 14000)
        self.assertEqual(output.right_y, -9000)


if __name__ == "__main__":
    unittest.main()
