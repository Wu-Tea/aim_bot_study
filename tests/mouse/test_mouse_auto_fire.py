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
