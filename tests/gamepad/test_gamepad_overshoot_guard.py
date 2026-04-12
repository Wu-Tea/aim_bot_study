import unittest

from controllers.gamepad.overshoot_guard import OvershootGuard, OvershootGuardConfig


class OvershootGuardTests(unittest.TestCase):
    def make_guard(self):
        return OvershootGuard(
            OvershootGuardConfig(
                manual_input_threshold=2500,
                near_error_px=8.0,
                release_error_px=20.0,
                convergence_epsilon_px=0.25,
                convergence_trigger_frames=2,
                convergence_build_per_update=0.25,
                convergence_max_guard=0.55,
                convergence_decay=0.20,
                zero_cross_arm_px=6.0,
                zero_cross_hold_seconds=0.05,
                zero_cross_guard=0.85,
                carry_damp_gain=1.0,
            )
        )

    def test_zero_cross_near_center_triggers_brake_without_manual_input(self):
        guard = self.make_guard()

        guard.observe_target(target_dx=4.0, target_dy=0.0, is_aiming=True, timestamp=0.00)
        guard.observe_target(target_dx=1.0, target_dy=0.0, is_aiming=True, timestamp=0.02)
        guard.observe_target(target_dx=-1.0, target_dy=0.0, is_aiming=True, timestamp=0.04)

        adjustment = guard.compute_adjustment(manual_rx=0, manual_ry=0, timestamp=0.04)

        self.assertLess(adjustment.x_desired_scale, 1.0)
        self.assertLess(adjustment.x_carry_scale, 1.0)
        self.assertEqual(adjustment.y_desired_scale, 1.0)

    def test_same_direction_manual_pull_while_converging_reduces_x_ai(self):
        guard = self.make_guard()

        guard.observe_target(target_dx=12.0, target_dy=0.0, is_aiming=True, timestamp=0.00)
        guard.observe_target(target_dx=7.0, target_dy=0.0, is_aiming=True, timestamp=0.02)
        guard.observe_target(target_dx=4.0, target_dy=0.0, is_aiming=True, timestamp=0.04)

        adjustment = guard.compute_adjustment(manual_rx=5000, manual_ry=0, timestamp=0.04)

        self.assertLess(adjustment.x_desired_scale, 1.0)
        self.assertLess(adjustment.x_carry_scale, 1.0)

    def test_converging_without_same_direction_manual_input_does_not_suppress(self):
        guard = self.make_guard()

        guard.observe_target(target_dx=12.0, target_dy=0.0, is_aiming=True, timestamp=0.00)
        guard.observe_target(target_dx=7.0, target_dy=0.0, is_aiming=True, timestamp=0.02)
        guard.observe_target(target_dx=4.0, target_dy=0.0, is_aiming=True, timestamp=0.04)

        adjustment = guard.compute_adjustment(manual_rx=-5000, manual_ry=0, timestamp=0.04)

        self.assertEqual(adjustment.x_desired_scale, 1.0)
        self.assertEqual(adjustment.x_carry_scale, 1.0)

    def test_reset_clears_short_term_memory(self):
        guard = self.make_guard()

        guard.observe_target(target_dx=5.0, target_dy=0.0, is_aiming=True, timestamp=0.00)
        guard.observe_target(target_dx=-1.0, target_dy=0.0, is_aiming=True, timestamp=0.02)
        guard.reset()

        adjustment = guard.compute_adjustment(manual_rx=0, manual_ry=0, timestamp=0.02)

        self.assertEqual(adjustment.x_desired_scale, 1.0)
        self.assertEqual(adjustment.x_carry_scale, 1.0)
        self.assertEqual(adjustment.y_desired_scale, 1.0)
        self.assertEqual(adjustment.y_carry_scale, 1.0)


if __name__ == "__main__":
    unittest.main()
