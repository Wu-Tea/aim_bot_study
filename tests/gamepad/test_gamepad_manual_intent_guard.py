import unittest

from controllers.gamepad.manual_intent_guard import (
    ManualIntentAdjustment,
    ManualIntentGuard,
    ManualIntentGuardConfig,
)


def _observe_sequence(guard, errors, *, start_t=0.0, step=0.02):
    for i, err in enumerate(errors):
        guard.observe_target(
            target_dx=err,
            is_aiming=True,
            timestamp=start_t + (float(i) * step),
        )


class ManualIntentGuardTests(unittest.TestCase):
    def make_guard(self):
        return ManualIntentGuard(
            ManualIntentGuardConfig(
                min_error_px=8.0,
                stable_history=3,
                opposing_input_threshold=4000,
                opposed_output_scale=0.4,
                opposed_ai_fade_scale=0.0,
            )
        )

    def test_opposed_manual_input_is_softened_after_stable_history(self):
        guard = self.make_guard()
        _observe_sequence(guard, [14.0, 18.0, 22.0])

        adjustment = guard.compute_adjustment(manual_rx=-8000)

        self.assertEqual(
            adjustment,
            ManualIntentAdjustment(output_manual_rx=-3200.0, ai_fade_manual_rx=-0.0),
        )

    def test_aligned_manual_input_is_left_unchanged(self):
        guard = self.make_guard()
        _observe_sequence(guard, [14.0, 18.0, 22.0])

        adjustment = guard.compute_adjustment(manual_rx=8000)

        self.assertEqual(
            adjustment,
            ManualIntentAdjustment(output_manual_rx=8000.0, ai_fade_manual_rx=8000.0),
        )

    def test_unstable_target_history_does_not_attenuate_manual_input(self):
        guard = self.make_guard()
        _observe_sequence(guard, [14.0, -18.0, 22.0])

        adjustment = guard.compute_adjustment(manual_rx=-8000)

        self.assertEqual(
            adjustment,
            ManualIntentAdjustment(output_manual_rx=-8000.0, ai_fade_manual_rx=-8000.0),
        )


if __name__ == "__main__":
    unittest.main()
