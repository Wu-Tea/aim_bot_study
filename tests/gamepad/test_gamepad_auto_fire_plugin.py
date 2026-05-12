import unittest

from controllers.gamepad.auto_fire import AutoFireConfig, AutoFirePlugin
from controllers.gamepad.state import GamepadFrame, GamepadOutput


def _frame(*, aiming=True, auto_fire=False, manual_rb=False, manual_rt=0, timestamp=1.0):
    return GamepadFrame(
        timestamp=timestamp,
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

    def test_manual_rb_takeover_holds_release_window_before_passthrough(self):
        plugin = AutoFirePlugin(
            AutoFireConfig(
                fire_output="RB",
                manual_takeover_release_seconds=0.035,
                manual_takeover_resume_delay_seconds=0.085,
            )
        )
        first_manual = _frame(aiming=True, auto_fire=True, manual_rb=True, manual_rt=0, timestamp=1.000)
        first_output = _output(first_manual)

        plugin.apply(first_manual, first_output)

        self.assertFalse(first_output.buttons["rb"])
        self.assertFalse(first_output.auto_fire_active)

        still_releasing = _frame(aiming=True, auto_fire=True, manual_rb=True, manual_rt=0, timestamp=1.020)
        still_releasing_output = _output(still_releasing)
        plugin.apply(still_releasing, still_releasing_output)

        self.assertFalse(still_releasing_output.buttons["rb"])
        self.assertFalse(still_releasing_output.auto_fire_active)

        held_after_release = _frame(aiming=True, auto_fire=True, manual_rb=True, manual_rt=0, timestamp=1.036)
        held_output = _output(held_after_release)
        plugin.apply(held_after_release, held_output)

        self.assertTrue(held_output.buttons["rb"])
        self.assertFalse(held_output.auto_fire_active)

    def test_manual_rt_takeover_holds_release_window_before_passthrough(self):
        plugin = AutoFirePlugin(
            AutoFireConfig(
                fire_output="RT",
                manual_takeover_release_seconds=0.035,
                manual_takeover_resume_delay_seconds=0.085,
            )
        )
        first_manual = _frame(aiming=True, auto_fire=True, manual_rb=False, manual_rt=180, timestamp=2.000)
        first_output = _output(first_manual)

        plugin.apply(first_manual, first_output)

        self.assertEqual(first_output.right_trigger, 0)
        self.assertFalse(first_output.auto_fire_active)

        still_releasing = _frame(aiming=True, auto_fire=True, manual_rb=False, manual_rt=180, timestamp=2.020)
        still_releasing_output = _output(still_releasing)
        plugin.apply(still_releasing, still_releasing_output)

        self.assertEqual(still_releasing_output.right_trigger, 0)
        self.assertFalse(still_releasing_output.auto_fire_active)

        held_after_release = _frame(aiming=True, auto_fire=True, manual_rb=False, manual_rt=180, timestamp=2.036)
        held_output = _output(held_after_release)
        plugin.apply(held_after_release, held_output)

        self.assertEqual(held_output.right_trigger, 180)
        self.assertFalse(held_output.auto_fire_active)

    def test_manual_takeover_suppresses_auto_fire_until_release_and_delay_total_120ms(self):
        plugin = AutoFirePlugin(
            AutoFireConfig(
                fire_output="RB",
                manual_takeover_release_seconds=0.035,
                manual_takeover_resume_delay_seconds=0.085,
            )
        )
        automatic = _frame(aiming=True, auto_fire=True, manual_rb=False, manual_rt=0, timestamp=3.000)
        automatic_output = _output(automatic)
        plugin.apply(automatic, automatic_output)
        self.assertTrue(automatic_output.buttons["rb"])

        manual_after_auto = _frame(aiming=True, auto_fire=True, manual_rb=True, manual_rt=0, timestamp=3.010)
        manual_output = _output(manual_after_auto)
        plugin.apply(manual_after_auto, manual_output)
        self.assertFalse(manual_output.buttons["rb"])
        self.assertFalse(manual_output.auto_fire_active)

        released_before_total = _frame(aiming=True, auto_fire=True, manual_rb=False, manual_rt=0, timestamp=3.120)
        before_total_output = _output(released_before_total)
        plugin.apply(released_before_total, before_total_output)

        self.assertFalse(before_total_output.buttons["rb"])
        self.assertFalse(before_total_output.auto_fire_active)

        released_after_total = _frame(aiming=True, auto_fire=True, manual_rb=False, manual_rt=0, timestamp=3.131)
        after_total_output = _output(released_after_total)
        plugin.apply(released_after_total, after_total_output)

        self.assertTrue(after_total_output.buttons["rb"])
        self.assertTrue(after_total_output.auto_fire_active)


if __name__ == "__main__":
    unittest.main()
