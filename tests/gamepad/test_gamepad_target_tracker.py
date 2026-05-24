import unittest

from controllers.base_controller import ControllerTarget
from controllers.gamepad.state import GamepadOutput
from controllers.gamepad.target_tracker import (
    GamepadTargetTracker,
    GamepadTargetTrackerConfig,
)


def _target(dx: float = 30.0, dy: float = -12.0) -> ControllerTarget:
    return ControllerTarget(
        aim_point_x=320.0 + dx,
        aim_point_y=256.0 + dy,
        screen_center_x=320.0,
        screen_center_y=256.0,
        body_box=(300.0 + dx, 210.0 + dy, 360.0 + dx, 330.0 + dy),
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
