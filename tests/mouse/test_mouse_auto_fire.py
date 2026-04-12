import unittest

from controllers.mouse.auto_fire import AutoFireConfig, AutoFirePlugin
from controllers.mouse.state import MouseFrame, MouseOutput


def _frame(*, aiming=True, auto_fire=False, timestamp=1.0):
    return MouseFrame(
        timestamp=timestamp,
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

    def test_pulse_releases_after_hold_period(self):
        config = AutoFireConfig(hold_seconds=0.140, release_seconds=0.010)
        plugin = AutoFirePlugin(config)

        # t=0.0: start firing
        out = MouseOutput()
        plugin.apply(_frame(aiming=True, auto_fire=True, timestamp=0.0), out)
        self.assertTrue(out.left_click)

        # t=0.100: still in hold phase
        out = MouseOutput()
        plugin.apply(_frame(aiming=True, auto_fire=True, timestamp=0.100), out)
        self.assertTrue(out.left_click)

        # t=0.145: in release phase (0.140 < 0.145 < 0.150)
        out = MouseOutput()
        plugin.apply(_frame(aiming=True, auto_fire=True, timestamp=0.145), out)
        self.assertFalse(out.left_click)
        self.assertFalse(out.auto_fire_active)

        # t=0.155: new cycle, firing again
        out = MouseOutput()
        plugin.apply(_frame(aiming=True, auto_fire=True, timestamp=0.155), out)
        self.assertTrue(out.left_click)

    def test_reset_restarts_cycle(self):
        plugin = AutoFirePlugin()
        out = MouseOutput()
        plugin.apply(_frame(aiming=True, auto_fire=True, timestamp=0.0), out)
        plugin.reset()
        self.assertIsNone(plugin._cycle_start)

    def test_stop_and_restart_resets_cycle(self):
        plugin = AutoFirePlugin(AutoFireConfig(hold_seconds=0.140, release_seconds=0.010))

        # Fire for a while
        out = MouseOutput()
        plugin.apply(_frame(aiming=True, auto_fire=True, timestamp=0.0), out)

        # Stop firing
        out = MouseOutput()
        plugin.apply(_frame(aiming=True, auto_fire=False, timestamp=0.100), out)
        self.assertFalse(out.left_click)

        # Restart: cycle should begin fresh from this timestamp
        out = MouseOutput()
        plugin.apply(_frame(aiming=True, auto_fire=True, timestamp=5.0), out)
        self.assertTrue(out.left_click)


if __name__ == "__main__":
    unittest.main()
