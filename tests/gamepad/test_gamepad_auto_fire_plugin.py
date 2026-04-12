import unittest

from controllers.gamepad.auto_fire import AutoFireConfig, AutoFirePlugin
from controllers.gamepad.state import GamepadFrame, GamepadOutput


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
