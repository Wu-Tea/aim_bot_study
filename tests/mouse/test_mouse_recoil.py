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
