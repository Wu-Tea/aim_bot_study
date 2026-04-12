import unittest

from controllers.mouse.ai_aim import AIAimConfig, AIAimPlugin
from controllers.mouse.state import MouseFrame, MouseOutput


def _frame(*, target_dx=0.0, target_dy=0.0, aiming=True, manual_dx=0.0, manual_dy=0.0):
    return MouseFrame(
        timestamp=1.0,
        manual_dx=manual_dx,
        manual_dy=manual_dy,
        is_aiming=aiming,
        target_dx=target_dx,
        target_dy=target_dy,
        auto_fire_requested=False,
        target_revision=1,
        target_timestamp=1.0,
    )


class AIAimPluginTests(unittest.TestCase):
    def test_no_correction_when_not_aiming(self):
        plugin = AIAimPlugin()
        output = MouseOutput()
        plugin.apply(_frame(target_dx=50.0, target_dy=30.0, aiming=False), output)
        self.assertAlmostEqual(output.move_dx, 0.0)
        self.assertAlmostEqual(output.move_dy, 0.0)

    def test_no_correction_inside_inner_deadzone(self):
        config = AIAimConfig(deadzone_inner_px=2.0, deadzone_outer_px=5.0)
        plugin = AIAimPlugin(config)
        output = MouseOutput()
        plugin.apply(_frame(target_dx=0.5, target_dy=0.5), output)
        self.assertAlmostEqual(output.move_dx, 0.0)
        self.assertAlmostEqual(output.move_dy, 0.0)

    def test_correction_applied_outside_deadzone(self):
        config = AIAimConfig(
            gain=1.0,
            smoothing=0.0,
            max_correction_px=50.0,
            deadzone_inner_px=1.0,
            deadzone_outer_px=2.0,
            manual_dampen=0.0,
        )
        plugin = AIAimPlugin(config)
        output = MouseOutput()
        plugin.apply(_frame(target_dx=20.0, target_dy=10.0), output)
        self.assertGreater(output.move_dx, 0.0)
        self.assertGreater(output.move_dy, 0.0)

    def test_correction_clamped_to_max(self):
        config = AIAimConfig(
            gain=1.0,
            smoothing=0.0,
            max_correction_px=5.0,
            deadzone_inner_px=0.0,
            deadzone_outer_px=0.0,
            manual_dampen=0.0,
        )
        plugin = AIAimPlugin(config)
        output = MouseOutput()
        plugin.apply(_frame(target_dx=100.0, target_dy=100.0), output)
        self.assertLessEqual(abs(output.move_dx), 5.0 + 0.01)
        self.assertLessEqual(abs(output.move_dy), 5.0 + 0.01)

    def test_manual_dampen_counteracts_user_movement(self):
        config = AIAimConfig(
            gain=0.1,
            smoothing=0.0,
            max_correction_px=50.0,
            deadzone_inner_px=0.0,
            deadzone_outer_px=0.0,
            manual_dampen=0.5,
        )
        plugin = AIAimPlugin(config)

        # With target present and manual movement, output should be reduced
        output = MouseOutput()
        plugin.apply(_frame(target_dx=20.0, manual_dx=10.0), output)
        # AI adds positive correction, but dampen subtracts 50% of manual_dx (5.0)
        # Net should be less than AI-only correction
        ai_only = 20.0 * 0.1  # 2.0
        self.assertLess(output.move_dx, ai_only)

    def test_no_dampen_without_target(self):
        config = AIAimConfig(manual_dampen=0.5)
        plugin = AIAimPlugin(config)
        output = MouseOutput()
        # Target inside deadzone -> strength=0 -> dampen=0
        plugin.apply(_frame(target_dx=0.0, manual_dx=10.0, aiming=True), output)
        self.assertAlmostEqual(output.move_dx, 0.0)

    def test_smoothing_carries_between_frames(self):
        config = AIAimConfig(
            gain=1.0,
            smoothing=0.5,
            max_correction_px=50.0,
            deadzone_inner_px=0.0,
            deadzone_outer_px=0.0,
            manual_dampen=0.0,
        )
        plugin = AIAimPlugin(config)

        out1 = MouseOutput()
        plugin.apply(_frame(target_dx=20.0, target_dy=0.0), out1)

        out2 = MouseOutput()
        plugin.apply(_frame(target_dx=0.0, target_dy=0.0, aiming=True), out2)
        self.assertNotAlmostEqual(out2.move_dx, 0.0)

    def test_reset_clears_carry(self):
        config = AIAimConfig(gain=1.0, smoothing=0.8, deadzone_inner_px=0.0,
                             deadzone_outer_px=0.0, manual_dampen=0.0)
        plugin = AIAimPlugin(config)
        out = MouseOutput()
        plugin.apply(_frame(target_dx=30.0), out)
        plugin.reset()
        out2 = MouseOutput()
        plugin.apply(_frame(target_dx=0.0), out2)
        self.assertAlmostEqual(out2.move_dx, 0.0)
        self.assertAlmostEqual(out2.move_dy, 0.0)


if __name__ == "__main__":
    unittest.main()
