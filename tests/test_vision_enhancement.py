import unittest

from vision.enhancement import (
    AimEnhancementPipeline,
    CatchupBoost,
    CatchupBoostConfig,
    LeadPredictor,
    LeadPredictorConfig,
    NearTargetDamping,
    NearTargetDampingConfig,
)
from vision.targeting import SelectedTarget


def _target(dx: float, dy: float):
    return SelectedTarget(
        target_x=320.0 + dx,
        target_y=320.0 + dy,
        screen_center_x=320.0,
        screen_center_y=320.0,
        score=100.0,
    )


def _target_with_slow_zone(
    dx: float,
    dy: float,
    *,
    slow_zone: tuple[float, float, float, float],
):
    return SelectedTarget(
        target_x=320.0 + dx,
        target_y=320.0 + dy,
        screen_center_x=320.0,
        screen_center_y=320.0,
        score=100.0,
        slow_zone=slow_zone,
    )


def _pipeline(*, lead=None, catchup=None, damping=None):
    return AimEnhancementPipeline(
        lead_predictor=lead or LeadPredictor(LeadPredictorConfig(lead_seconds=0.0, gain=0.0, max_lead_px=0.0)),
        catchup_boost=catchup or CatchupBoost(
            CatchupBoostConfig(trigger_frames=999, gain_per_frame=0.0, max_bonus=0.0, decay=1.0, convergence_epsilon_px=0.1)
        ),
        near_target_damping=damping or NearTargetDamping(
            NearTargetDampingConfig(inner_radius=0.0, outer_radius=0.0, min_scale=1.0)
        ),
        velocity_filter_alpha=1.0,
    )


class VisionEnhancementTests(unittest.TestCase):
    def test_lead_predictor_pushes_output_in_motion_direction(self):
        pipeline = _pipeline(
            lead=LeadPredictor(LeadPredictorConfig(lead_seconds=0.10, gain=1.0, max_lead_px=12.0)),
        )

        pipeline.process(_target(10.0, 0.0), timestamp=1.0)
        enhanced_dx, enhanced_dy = pipeline.process(_target(16.0, 0.0), timestamp=1.1)

        self.assertGreater(enhanced_dx, 16.0)
        self.assertEqual(enhanced_dy, 0.0)

    def test_catchup_boost_increases_output_after_consecutive_error_growth(self):
        pipeline = _pipeline(
            catchup=CatchupBoost(
                CatchupBoostConfig(
                    trigger_frames=2,
                    gain_per_frame=0.25,
                    max_bonus=0.60,
                    decay=0.20,
                    convergence_epsilon_px=0.1,
                )
            ),
        )

        pipeline.process(_target(8.0, 0.0), timestamp=1.0)
        pipeline.process(_target(10.0, 0.0), timestamp=1.1)
        enhanced_dx, _ = pipeline.process(_target(12.0, 0.0), timestamp=1.2)

        self.assertGreater(enhanced_dx, 12.0)

    def test_near_target_damping_preserves_non_zero_floor_after_convergence(self):
        pipeline = _pipeline(
            damping=NearTargetDamping(
                NearTargetDampingConfig(inner_radius=4.0, outer_radius=20.0, min_scale=0.60)
            ),
        )

        pipeline.process(_target(4.0, 0.0), timestamp=1.0)
        enhanced_dx, enhanced_dy = pipeline.process(_target(2.0, 0.0), timestamp=1.1)

        self.assertGreater(enhanced_dx, 0.0)
        self.assertLess(enhanced_dx, 2.0)
        self.assertEqual(enhanced_dy, 0.0)

    def test_near_target_damping_waits_for_slow_zone_entry(self):
        pipeline = _pipeline(
            damping=NearTargetDamping(
                NearTargetDampingConfig(inner_radius=4.0, outer_radius=20.0, min_scale=0.60)
            ),
        )

        enhanced_dx, enhanced_dy = pipeline.process(
            _target_with_slow_zone(
                2.0,
                0.0,
                slow_zone=(340.0, 300.0, 370.0, 340.0),
            ),
            timestamp=1.0,
        )

        self.assertEqual(enhanced_dx, 2.0)
        self.assertEqual(enhanced_dy, 0.0)

    def test_near_target_damping_does_not_apply_on_first_slow_zone_entry(self):
        pipeline = _pipeline(
            damping=NearTargetDamping(
                NearTargetDampingConfig(inner_radius=4.0, outer_radius=20.0, min_scale=0.60)
            ),
        )

        enhanced_dx, enhanced_dy = pipeline.process(
            _target_with_slow_zone(
                2.0,
                0.0,
                slow_zone=(300.0, 300.0, 340.0, 340.0),
            ),
            timestamp=1.0,
        )

        self.assertEqual(enhanced_dx, 2.0)
        self.assertEqual(enhanced_dy, 0.0)

    def test_near_target_damping_applies_inside_slow_zone_after_convergence(self):
        pipeline = _pipeline(
            damping=NearTargetDamping(
                NearTargetDampingConfig(inner_radius=4.0, outer_radius=20.0, min_scale=0.60)
            ),
        )

        pipeline.process(
            _target_with_slow_zone(
                4.0,
                0.0,
                slow_zone=(300.0, 300.0, 340.0, 340.0),
            ),
            timestamp=1.0,
        )
        enhanced_dx, enhanced_dy = pipeline.process(
            _target_with_slow_zone(
                2.0,
                0.0,
                slow_zone=(300.0, 300.0, 340.0, 340.0),
            ),
            timestamp=1.1,
        )

        self.assertGreater(enhanced_dx, 0.0)
        self.assertLess(enhanced_dx, 2.0)
        self.assertEqual(enhanced_dy, 0.0)

    def test_reset_clears_velocity_and_boost_state(self):
        pipeline = _pipeline(
            lead=LeadPredictor(LeadPredictorConfig(lead_seconds=0.10, gain=1.0, max_lead_px=12.0)),
            catchup=CatchupBoost(
                CatchupBoostConfig(
                    trigger_frames=1,
                    gain_per_frame=0.25,
                    max_bonus=0.60,
                    decay=0.20,
                    convergence_epsilon_px=0.1,
                )
            ),
        )

        pipeline.process(_target(10.0, 0.0), timestamp=1.0)
        boosted_dx, _ = pipeline.process(_target(16.0, 0.0), timestamp=1.1)
        self.assertGreater(boosted_dx, 16.0)

        pipeline.reset()
        reset_dx, reset_dy = pipeline.process(_target(16.0, 0.0), timestamp=2.0)

        self.assertEqual(reset_dx, 16.0)
        self.assertEqual(reset_dy, 0.0)


if __name__ == "__main__":
    unittest.main()
