import unittest

from controllers.gamepad.adaptive_delta_gain import (
    AdaptiveDeltaGain,
    AdaptiveDeltaGainConfig,
)


def _observe_sequence(gain, errors, is_aiming=True, start_t=0.0, step=0.01):
    for i, err in enumerate(errors):
        gain.observe_target(
            target_dx=err,
            target_dy=0.0,
            is_aiming=is_aiming,
            timestamp=start_t + float(i) * step,
        )


class AdaptiveDeltaGainTests(unittest.TestCase):
    def test_first_observation_yields_no_bonus(self):
        gain = AdaptiveDeltaGain()
        gain.observe_target(target_dx=40.0, target_dy=0.0, is_aiming=True, timestamp=0.0)
        adj = gain.compute_adjustment(manual_rx=0, manual_ry=0)
        self.assertEqual(adj.target_dx_multiplier, 1.0)
        self.assertEqual(adj.target_dy_multiplier, 1.0)

    def test_bonus_accumulates_on_sustained_error(self):
        cfg = AdaptiveDeltaGainConfig(
            min_error_px=5.0,
            trigger_frames=2,
            gain_per_update=0.1,
            max_bonus=0.5,
        )
        gain = AdaptiveDeltaGain(cfg)
        _observe_sequence(gain, [40.0] * 6)
        adj = gain.compute_adjustment(manual_rx=0, manual_ry=0)
        self.assertGreater(adj.target_dx_multiplier, 1.0)
        self.assertLessEqual(adj.target_dx_multiplier, 1.0 + cfg.max_bonus)

    def test_bonus_caps_at_max_bonus(self):
        cfg = AdaptiveDeltaGainConfig(
            min_error_px=5.0,
            trigger_frames=1,
            gain_per_update=0.2,
            max_bonus=0.3,
        )
        gain = AdaptiveDeltaGain(cfg)
        _observe_sequence(gain, [40.0] * 50)
        adj = gain.compute_adjustment(manual_rx=0, manual_ry=0)
        self.assertAlmostEqual(adj.target_dx_multiplier, 1.3, places=6)

    def test_bonus_decays_on_convergence(self):
        cfg = AdaptiveDeltaGainConfig(
            min_error_px=5.0,
            trigger_frames=1,
            gain_per_update=0.15,
            decay_per_update=0.1,
            max_bonus=0.6,
        )
        gain = AdaptiveDeltaGain(cfg)
        _observe_sequence(gain, [40.0] * 10)
        boosted = gain.compute_adjustment(manual_rx=0, manual_ry=0).target_dx_multiplier
        _observe_sequence(gain, [35.0, 30.0, 25.0], start_t=0.1)
        decayed = gain.compute_adjustment(manual_rx=0, manual_ry=0).target_dx_multiplier
        self.assertLess(decayed, boosted)

    def test_close_range_decays_existing_bonus(self):
        cfg = AdaptiveDeltaGainConfig(
            min_error_px=5.0,
            trigger_frames=1,
            gain_per_update=0.2,
            decay_per_update=0.1,
            max_bonus=0.5,
        )
        gain = AdaptiveDeltaGain(cfg)
        _observe_sequence(gain, [40.0] * 10)
        before = gain.compute_adjustment(manual_rx=0, manual_ry=0).target_dx_multiplier
        gain.observe_target(target_dx=3.0, target_dy=0.0, is_aiming=True, timestamp=0.2)
        after = gain.compute_adjustment(manual_rx=0, manual_ry=0).target_dx_multiplier
        self.assertLess(after, before)

    def test_opposing_manual_input_zeroes_axis_bonus(self):
        cfg = AdaptiveDeltaGainConfig(
            min_error_px=5.0,
            trigger_frames=1,
            gain_per_update=0.2,
            max_bonus=0.4,
            opposing_input_threshold=4500,
        )
        gain = AdaptiveDeltaGain(cfg)
        _observe_sequence(gain, [40.0] * 5)
        opposed = gain.compute_adjustment(manual_rx=-10000, manual_ry=0)
        neutral = gain.compute_adjustment(manual_rx=0, manual_ry=0)
        self.assertEqual(opposed.target_dx_multiplier, 1.0)
        self.assertGreater(neutral.target_dx_multiplier, 1.0)

    def test_reset_on_aim_release_clears_bonus(self):
        cfg = AdaptiveDeltaGainConfig(
            min_error_px=5.0,
            trigger_frames=1,
            gain_per_update=0.3,
            max_bonus=0.6,
        )
        gain = AdaptiveDeltaGain(cfg)
        _observe_sequence(gain, [40.0] * 5)
        gain.observe_target(target_dx=40.0, target_dy=0.0, is_aiming=False, timestamp=0.1)
        adj = gain.compute_adjustment(manual_rx=0, manual_ry=0)
        self.assertEqual(adj.target_dx_multiplier, 1.0)

    def test_stale_gap_resets_nonconvergence_counter(self):
        cfg = AdaptiveDeltaGainConfig(
            min_error_px=5.0,
            trigger_frames=3,
            gain_per_update=0.2,
            max_bonus=0.5,
            stale_seconds=0.1,
        )
        gain = AdaptiveDeltaGain(cfg)
        gain.observe_target(target_dx=40.0, target_dy=0.0, is_aiming=True, timestamp=0.0)
        gain.observe_target(target_dx=40.0, target_dy=0.0, is_aiming=True, timestamp=0.01)
        gain.observe_target(target_dx=40.0, target_dy=0.0, is_aiming=True, timestamp=0.5)
        gain.observe_target(target_dx=40.0, target_dy=0.0, is_aiming=True, timestamp=0.51)
        adj = gain.compute_adjustment(manual_rx=0, manual_ry=0)
        self.assertEqual(adj.target_dx_multiplier, 1.0)

    def test_y_axis_tracks_independently(self):
        cfg = AdaptiveDeltaGainConfig(
            min_error_px=5.0,
            trigger_frames=1,
            gain_per_update=0.2,
            max_bonus=0.4,
        )
        gain = AdaptiveDeltaGain(cfg)
        for i in range(5):
            gain.observe_target(
                target_dx=40.0,
                target_dy=2.0,
                is_aiming=True,
                timestamp=float(i) * 0.01,
            )
        adj = gain.compute_adjustment(manual_rx=0, manual_ry=0)
        self.assertGreater(adj.target_dx_multiplier, 1.0)
        self.assertEqual(adj.target_dy_multiplier, 1.0)


if __name__ == "__main__":
    unittest.main()
