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
        # target offset of 1px radial distance is inside the 2px inner deadzone
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
            fade_speed_px=1000.0,
        )
        plugin = AIAimPlugin(config)
        output = MouseOutput()
        plugin.apply(_frame(target_dx=20.0, target_dy=10.0), output)
        # With gain=1.0, smoothing=0.0, high fade threshold, correction should be close to target
        self.assertGreater(output.move_dx, 0.0)
        self.assertGreater(output.move_dy, 0.0)

    def test_correction_clamped_to_max(self):
        config = AIAimConfig(
            gain=1.0,
            smoothing=0.0,
            max_correction_px=5.0,
            deadzone_inner_px=0.0,
            deadzone_outer_px=0.0,
            fade_speed_px=1000.0,
        )
        plugin = AIAimPlugin(config)
        output = MouseOutput()
        plugin.apply(_frame(target_dx=100.0, target_dy=100.0), output)
        self.assertLessEqual(abs(output.move_dx), 5.0 + 0.01)
        self.assertLessEqual(abs(output.move_dy), 5.0 + 0.01)

    def test_ai_fades_with_fast_manual_movement(self):
        config = AIAimConfig(
            gain=1.0,
            smoothing=0.0,
            max_correction_px=50.0,
            deadzone_inner_px=0.0,
            deadzone_outer_px=0.0,
            fade_speed_px=10.0,
        )
        plugin = AIAimPlugin(config)

        # Slow manual movement -> full AI
        output_slow = MouseOutput()
        plugin.apply(_frame(target_dx=20.0, manual_dx=0.0), output_slow)

        # Reset carry
        plugin.reset()

        # Fast manual movement -> faded AI
        output_fast = MouseOutput()
        plugin.apply(_frame(target_dx=20.0, manual_dx=50.0), output_fast)

        self.assertGreater(abs(output_slow.move_dx), abs(output_fast.move_dx))

    def test_smoothing_carries_between_frames(self):
        config = AIAimConfig(
            gain=1.0,
            smoothing=0.5,
            max_correction_px=50.0,
            deadzone_inner_px=0.0,
            deadzone_outer_px=0.0,
            fade_speed_px=1000.0,
        )
        plugin = AIAimPlugin(config)

        # First frame with target
        out1 = MouseOutput()
        plugin.apply(_frame(target_dx=20.0, target_dy=0.0), out1)

        # Second frame with zero target: carry should produce non-zero output
        out2 = MouseOutput()
        plugin.apply(
            _frame(target_dx=0.0, target_dy=0.0, aiming=True),
            out2,
        )
        # Carry from previous frame should bleed through
        self.assertNotAlmostEqual(out2.move_dx, 0.0)

    def test_reset_clears_carry(self):
        config = AIAimConfig(gain=1.0, smoothing=0.8, deadzone_inner_px=0.0,
                             deadzone_outer_px=0.0, fade_speed_px=1000.0)
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
