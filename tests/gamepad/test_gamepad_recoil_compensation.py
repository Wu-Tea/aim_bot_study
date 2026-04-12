import unittest

from controllers.gamepad.recoil_compensation import (
    RecoilCompensationConfig,
    RecoilCompensationPlugin,
)
from controllers.gamepad.state import GamepadFrame, GamepadOutput


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
