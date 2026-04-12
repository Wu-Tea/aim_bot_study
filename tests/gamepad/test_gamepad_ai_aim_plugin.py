import unittest

from controllers.gamepad.ai_aim import AIAimConfig, AIAimPlugin
from controllers.gamepad.state import GamepadFrame, GamepadOutput


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


if __name__ == "__main__":
    unittest.main()
