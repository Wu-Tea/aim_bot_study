import unittest

from controllers.base_controller import ControllerTarget
from controllers.gamepad.state import GamepadOutput
from controllers.gamepad.target_tracker import (
    GamepadTargetTracker,
    GamepadTargetTrackerConfig,
)


def _target(
    dx: float = 30.0,
    dy: float = -12.0,
    *,
    target_source: str | None = None,
    target_tier: str | None = None,
) -> ControllerTarget:
    return ControllerTarget(
        aim_point_x=320.0 + dx,
        aim_point_y=256.0 + dy,
        screen_center_x=320.0,
        screen_center_y=256.0,
        body_box=(300.0 + dx, 210.0 + dy, 360.0 + dx, 330.0 + dy),
        target_source=target_source,
        target_tier=target_tier,
        observed_at=1.0,
    )


class GamepadTargetTrackerTests(unittest.TestCase):
    def test_repeated_observation_offsets_error_by_recorded_camera_motion(self):
        tracker = GamepadTargetTracker(
            GamepadTargetTrackerConfig(
                reticle_speed_px_per_sec=1000.0,
                stick_max=10000,
                max_projection_age_ms=80.0,
            )
        )
        target = _target(dx=30.0, dy=-12.0)
        tracker.update_observation(
            dx=30.0,
            dy=-12.0,
            target=target,
            revision=1,
            observed_at=1.000,
        )

        tracker.record_output(
            GamepadOutput(right_x=5000, right_y=0),
            dt=0.020,
        )
        projection = tracker.project(timestamp=1.020)

        self.assertIsNotNone(projection)
        self.assertAlmostEqual(projection.dx, 20.0, places=5)
        self.assertAlmostEqual(projection.dy, -12.0, places=5)
        self.assertEqual(projection.revision, 1)
        self.assertEqual(projection.observed_at, 1.000)
        self.assertAlmostEqual(projection.target.aim_point_x, 340.0, places=5)
        self.assertAlmostEqual(projection.target.aim_point_y, 244.0, places=5)
        self.assertEqual(projection.target.body_box, (320.0, 198.0, 380.0, 318.0))

    def test_record_output_clamps_stick_motion_to_configured_range(self):
        tracker = GamepadTargetTracker(
            GamepadTargetTrackerConfig(
                reticle_speed_px_per_sec=1000.0,
                stick_max=10000,
                max_projection_age_ms=100.0,
            )
        )
        tracker.update_observation(
            dx=40.0,
            dy=0.0,
            target=_target(dx=40.0, dy=0.0),
            revision=1,
            observed_at=1.000,
        )

        tracker.record_output(GamepadOutput(right_x=20000, right_y=0), dt=0.010)
        projection = tracker.project(timestamp=1.010)

        self.assertIsNotNone(projection)
        self.assertAlmostEqual(projection.dx, 30.0, places=5)

    def test_new_observations_estimate_target_velocity_after_camera_motion(self):
        tracker = GamepadTargetTracker(
            GamepadTargetTrackerConfig(
                reticle_speed_px_per_sec=1000.0,
                stick_max=10000,
                velocity_lowpass_alpha=0.0,
                max_target_velocity_px_per_sec=2000.0,
                max_projection_age_ms=80.0,
            )
        )
        tracker.update_observation(
            dx=30.0,
            dy=0.0,
            target=_target(dx=30.0, dy=0.0),
            revision=1,
            observed_at=1.000,
        )
        tracker.record_output(GamepadOutput(right_x=5000), dt=0.020)

        tracker.update_observation(
            dx=50.0,
            dy=0.0,
            target=_target(dx=50.0, dy=0.0),
            revision=2,
            observed_at=1.020,
        )
        projection = tracker.project(timestamp=1.030)

        self.assertIsNotNone(projection)
        self.assertAlmostEqual(projection.dx, 65.0, places=5)
        self.assertAlmostEqual(projection.dy, 0.0, places=5)

    def test_weak_association_updates_target_but_does_not_refresh_velocity(self):
        tracker = GamepadTargetTracker(
            GamepadTargetTrackerConfig(
                reticle_speed_px_per_sec=1000.0,
                stick_max=10000,
                velocity_lowpass_alpha=0.0,
                max_target_velocity_px_per_sec=2000.0,
                max_projection_age_ms=80.0,
            )
        )
        tracker.update_observation(
            dx=30.0,
            dy=0.0,
            target=_target(dx=30.0, dy=0.0, target_source="observed", target_tier="observed_strong"),
            revision=1,
            observed_at=1.000,
        )
        tracker.record_output(GamepadOutput(right_x=5000), dt=0.020)

        tracker.update_observation(
            dx=50.0,
            dy=0.0,
            target=_target(
                dx=50.0,
                dy=0.0,
                target_source="associated_weak",
                target_tier="associated_weak",
            ),
            revision=2,
            observed_at=1.020,
        )
        projection = tracker.project(timestamp=1.030)

        self.assertIsNotNone(projection)
        self.assertAlmostEqual(projection.dx, 50.0, places=5)
        self.assertAlmostEqual(projection.dy, 0.0, places=5)
        self.assertEqual(projection.target.target_source, "associated_weak")

    def test_weak_association_preserves_existing_velocity_with_decay(self):
        tracker = GamepadTargetTracker(
            GamepadTargetTrackerConfig(
                reticle_speed_px_per_sec=1000.0,
                stick_max=10000,
                velocity_lowpass_alpha=0.0,
                max_target_velocity_px_per_sec=2000.0,
                max_projection_age_ms=80.0,
                weak_observation_velocity_decay=0.5,
            )
        )
        tracker.update_observation(
            dx=0.0,
            dy=0.0,
            target=_target(dx=0.0, dy=0.0, target_source="observed", target_tier="observed_strong"),
            revision=1,
            observed_at=1.000,
        )
        tracker.update_observation(
            dx=20.0,
            dy=0.0,
            target=_target(dx=20.0, dy=0.0, target_source="observed", target_tier="observed_strong"),
            revision=2,
            observed_at=1.020,
        )

        tracker.update_observation(
            dx=40.0,
            dy=0.0,
            target=_target(
                dx=40.0,
                dy=0.0,
                target_source="associated_weak",
                target_tier="associated_weak",
            ),
            revision=3,
            observed_at=1.040,
        )
        projection = tracker.project(timestamp=1.050)

        self.assertIsNotNone(projection)
        self.assertAlmostEqual(projection.dx, 45.0, places=5)
        self.assertAlmostEqual(projection.dy, 0.0, places=5)
        self.assertEqual(projection.target.target_source, "associated_weak")

    def test_cue_hold_preserves_existing_velocity_with_decay(self):
        tracker = GamepadTargetTracker(
            GamepadTargetTrackerConfig(
                velocity_lowpass_alpha=0.0,
                max_target_velocity_px_per_sec=2000.0,
                max_projection_age_ms=80.0,
                weak_observation_velocity_decay=0.25,
            )
        )
        tracker.update_observation(
            dx=0.0,
            dy=0.0,
            target=_target(dx=0.0, dy=0.0, target_source="observed", target_tier="observed_strong"),
            revision=1,
            observed_at=1.000,
        )
        tracker.update_observation(
            dx=20.0,
            dy=0.0,
            target=_target(dx=20.0, dy=0.0, target_source="observed", target_tier="observed_strong"),
            revision=2,
            observed_at=1.020,
        )

        tracker.update_observation(
            dx=38.0,
            dy=0.0,
            target=_target(dx=38.0, dy=0.0, target_source="cue_hold", target_tier="cue_hold"),
            revision=3,
            observed_at=1.040,
        )
        projection = tracker.project(timestamp=1.050)

        self.assertIsNotNone(projection)
        self.assertAlmostEqual(projection.dx, 40.5, places=5)
        self.assertEqual(projection.target.target_source, "cue_hold")

    def test_low_score_source_without_tier_does_not_refresh_velocity(self):
        tracker = GamepadTargetTracker(
            GamepadTargetTrackerConfig(
                reticle_speed_px_per_sec=1000.0,
                stick_max=10000,
                velocity_lowpass_alpha=0.0,
                max_target_velocity_px_per_sec=2000.0,
                max_projection_age_ms=80.0,
            )
        )
        tracker.update_observation(
            dx=30.0,
            dy=0.0,
            target=_target(dx=30.0, dy=0.0, target_source="observed"),
            revision=1,
            observed_at=1.000,
        )
        tracker.record_output(GamepadOutput(right_x=5000), dt=0.020)

        tracker.update_observation(
            dx=50.0,
            dy=0.0,
            target=_target(dx=50.0, dy=0.0, target_source="low_score"),
            revision=2,
            observed_at=1.020,
        )
        projection = tracker.project(timestamp=1.030)

        self.assertIsNotNone(projection)
        self.assertAlmostEqual(projection.dx, 50.0, places=5)

    def test_projection_expires_after_max_age(self):
        tracker = GamepadTargetTracker(
            GamepadTargetTrackerConfig(max_projection_age_ms=20.0)
        )
        tracker.update_observation(
            dx=30.0,
            dy=0.0,
            target=_target(dx=30.0, dy=0.0),
            revision=1,
            observed_at=1.000,
        )

        self.assertIsNone(tracker.project(timestamp=1.050))


if __name__ == "__main__":
    unittest.main()
