import unittest

from controllers.gamepad_horizontal_assist import compute_axis_soft_strengths


class GamepadAimMathTests(unittest.TestCase):
    def test_x_axis_strength_decays_less_than_radial_strength_near_target(self):
        x_strength, y_strength = compute_axis_soft_strengths(
            dx=3.0,
            dy=0.0,
            inner=1.5,
            radial_outer=5.0,
            x_outer=3.0,
        )

        self.assertGreater(x_strength, y_strength)
        self.assertEqual(x_strength, 1.0)

    def test_both_axes_are_off_inside_inner_deadzone(self):
        x_strength, y_strength = compute_axis_soft_strengths(
            dx=1.0,
            dy=0.5,
            inner=1.5,
            radial_outer=5.0,
            x_outer=3.0,
        )

        self.assertEqual(x_strength, 0.0)
        self.assertEqual(y_strength, 0.0)


if __name__ == "__main__":
    unittest.main()
