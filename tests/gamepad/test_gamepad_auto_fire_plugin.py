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

    def test_manual_rb_press_forces_one_release_tick_before_passthrough(self):
        plugin = AutoFirePlugin(AutoFireConfig(fire_output="RB"))
        first_manual = _frame(aiming=True, auto_fire=True, manual_rb=True, manual_rt=0)
        first_output = _output(first_manual)

        plugin.apply(first_manual, first_output)

        self.assertFalse(first_output.buttons["rb"])
        self.assertFalse(first_output.auto_fire_active)

        held_output = _output(first_manual)
        plugin.apply(first_manual, held_output)

        self.assertTrue(held_output.buttons["rb"])
        self.assertFalse(held_output.auto_fire_active)

    def test_manual_rt_press_forces_one_release_tick_before_passthrough(self):
        plugin = AutoFirePlugin(AutoFireConfig(fire_output="RT"))
        first_manual = _frame(aiming=True, auto_fire=True, manual_rb=False, manual_rt=180)
        first_output = _output(first_manual)

        plugin.apply(first_manual, first_output)

        self.assertEqual(first_output.right_trigger, 0)
        self.assertFalse(first_output.auto_fire_active)

        held_output = _output(first_manual)
        plugin.apply(first_manual, held_output)

        self.assertEqual(held_output.right_trigger, 180)
        self.assertFalse(held_output.auto_fire_active)

    def test_manual_press_releases_previous_auto_fire_even_after_request_stops(self):
        plugin = AutoFirePlugin(AutoFireConfig(fire_output="RB"))
        automatic = _frame(aiming=True, auto_fire=True, manual_rb=False, manual_rt=0)
        automatic_output = _output(automatic)
        plugin.apply(automatic, automatic_output)
        self.assertTrue(automatic_output.buttons["rb"])

        manual_after_auto = _frame(aiming=True, auto_fire=False, manual_rb=True, manual_rt=0)
        manual_output = _output(manual_after_auto)

        plugin.apply(manual_after_auto, manual_output)

        self.assertFalse(manual_output.buttons["rb"])
        self.assertFalse(manual_output.auto_fire_active)


if __name__ == "__main__":
    unittest.main()
