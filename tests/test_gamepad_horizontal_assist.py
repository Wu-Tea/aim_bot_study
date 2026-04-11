import unittest

from controllers.gamepad_horizontal_assist import HorizontalAimAssist, HorizontalAimAssistConfig


class HorizontalAimAssistTests(unittest.TestCase):
    def make_assist(self):
        return HorizontalAimAssist(
            HorizontalAimAssistConfig(
                min_error_px=4.0,
                min_velocity_px_per_sec=40.0,
                velocity_filter_alpha=0.5,
                feedforward_lead_seconds=0.03,
                feedforward_gain=0.8,
                max_feedforward_px=8.0,
                catchup_trigger_frames=2,
                catchup_gain_per_update=0.05,
                catchup_max_bonus=0.15,
                catchup_decay=0.06,
                opposing_input_threshold=4000,
                convergence_epsilon_px=0.25,
            )
        )

    def test_sustained_horizontal_growth_produces_positive_feedforward(self):
        assist = self.make_assist()

        assist.observe_target(4.0, True, 0.00)
        assist.observe_target(8.0, True, 0.02)
        assist.observe_target(12.0, True, 0.04)

        feedforward_dx, x_force_bonus = assist.compute_adjustment(manual_rx=0)

        self.assertGreater(feedforward_dx, 0.0)
        self.assertGreaterEqual(x_force_bonus, 0.0)

    def test_repeated_non_converging_error_builds_catchup_bonus(self):
        assist = self.make_assist()

        assist.observe_target(5.0, True, 0.00)
        assist.observe_target(8.0, True, 0.02)
        assist.observe_target(11.0, True, 0.04)
        assist.observe_target(14.0, True, 0.06)

        _, x_force_bonus = assist.compute_adjustment(manual_rx=0)

        self.assertGreater(x_force_bonus, 0.0)

    def test_opposing_manual_input_suppresses_enhancement(self):
        assist = self.make_assist()

        assist.observe_target(5.0, True, 0.00)
        assist.observe_target(9.0, True, 0.02)
        assist.observe_target(13.0, True, 0.04)

        feedforward_dx, x_force_bonus = assist.compute_adjustment(manual_rx=-5000)

        self.assertEqual(feedforward_dx, 0.0)
        self.assertEqual(x_force_bonus, 0.0)

    def test_reset_clears_accumulated_assist_state(self):
        assist = self.make_assist()

        assist.observe_target(5.0, True, 0.00)
        assist.observe_target(10.0, True, 0.02)
        assist.observe_target(15.0, True, 0.04)
        assist.reset()

        feedforward_dx, x_force_bonus = assist.compute_adjustment(manual_rx=0)

        self.assertEqual(feedforward_dx, 0.0)
        self.assertEqual(x_force_bonus, 0.0)


if __name__ == "__main__":
    unittest.main()
