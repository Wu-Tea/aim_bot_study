import unittest
from dataclasses import dataclass

from vision.occlusion_compensation import TargetOcclusionCompensator, TargetSource


UPPER_CHEST_RATIO = 0.38


@dataclass(slots=True, frozen=True)
class StubTarget:
    target_x: float
    target_y: float
    selected_box: tuple[float, float, float, float] | None
    source: TargetSource | str = TargetSource.OBSERVED


def _target(
    box: tuple[float, float, float, float] | None,
    *,
    target_x: float | None = None,
    target_y: float | None = None,
    source: TargetSource | str = TargetSource.OBSERVED,
) -> StubTarget:
    if box is None:
        return StubTarget(
            target_x=0.0 if target_x is None else float(target_x),
            target_y=0.0 if target_y is None else float(target_y),
            selected_box=None,
            source=source,
        )

    left, top, right, bottom = (float(value) for value in box)
    height = bottom - top
    return StubTarget(
        target_x=((left + right) * 0.5) if target_x is None else float(target_x),
        target_y=(top + (height * UPPER_CHEST_RATIO))
        if target_y is None
        else float(target_y),
        selected_box=(left, top, right, bottom),
        source=source,
    )


class TargetOcclusionCompensatorTests(unittest.TestCase):
    def test_history_keeps_last_three_non_predicted_samples(self):
        compensator = TargetOcclusionCompensator()

        compensator.record_observation(
            _target((300.0, 240.0, 340.0, 360.0)),
            timestamp=1.0,
        )
        compensator.record_observation(
            _target((304.0, 242.0, 344.0, 362.0)),
            timestamp=2.0,
        )
        compensator.record_observation(
            _target((308.0, 244.0, 348.0, 364.0)),
            timestamp=3.0,
        )
        compensator.record_observation(
            _target(
                (312.0, 246.0, 352.0, 366.0),
                source=TargetSource.PREDICTED,
            ),
            timestamp=4.0,
        )
        compensator.record_observation(
            _target((316.0, 248.0, 356.0, 368.0)),
            timestamp=5.0,
        )

        self.assertEqual(len(compensator._stable_samples), 3)
        self.assertEqual(
            [sample.selected_box for sample in compensator._stable_samples],
            [
                (304.0, 242.0, 344.0, 362.0),
                (308.0, 244.0, 348.0, 364.0),
                (316.0, 248.0, 356.0, 368.0),
            ],
        )
        self.assertEqual(
            [sample.source for sample in compensator._stable_samples],
            [TargetSource.OBSERVED, TargetSource.OBSERVED, TargetSource.OBSERVED],
        )

    def test_prediction_requires_two_stable_samples(self):
        compensator = TargetOcclusionCompensator()
        compensator.record_observation(
            _target((300.0, 240.0, 340.0, 360.0)),
            timestamp=1.0,
        )

        predicted = compensator.try_predict(timestamp=1.02)

        self.assertIsNone(predicted)

    def test_reconstruction_uses_recent_height_when_bottom_is_stable(self):
        compensator = TargetOcclusionCompensator()
        compensator.record_observation(
            _target((300.0, 240.0, 340.0, 360.0)),
            timestamp=1.0,
        )
        compensator.record_observation(
            _target((304.0, 242.0, 344.0, 362.0)),
            timestamp=2.0,
        )

        reconstructed = compensator.try_reconstruct(
            _target((306.0, 286.0, 346.0, 362.0)),
            timestamp=2.02,
        )

        self.assertIsNotNone(reconstructed)
        self.assertEqual(reconstructed.source, TargetSource.RECONSTRUCTED)
        self.assertAlmostEqual(reconstructed.selected_box[1], 242.0, places=3)
        self.assertAlmostEqual(reconstructed.selected_box[3], 362.0, places=3)
        self.assertAlmostEqual(reconstructed.target_x, 326.0, places=3)
        self.assertAlmostEqual(
            reconstructed.target_y,
            242.0 + (120.0 * UPPER_CHEST_RATIO),
            places=3,
        )

    def test_reconstruction_rejects_large_center_shift(self):
        compensator = TargetOcclusionCompensator()
        compensator.record_observation(
            _target((300.0, 240.0, 340.0, 360.0)),
            timestamp=1.0,
        )
        compensator.record_observation(
            _target((304.0, 242.0, 344.0, 362.0)),
            timestamp=2.0,
        )

        reconstructed = compensator.try_reconstruct(
            _target((360.0, 286.0, 400.0, 362.0)),
            timestamp=2.02,
        )

        self.assertIsNone(reconstructed)

    def test_prediction_emits_only_two_frames_and_does_not_feed_back(self):
        compensator = TargetOcclusionCompensator()
        compensator.record_observation(
            _target((300.0, 240.0, 340.0, 360.0)),
            timestamp=1.0,
        )
        compensator.record_observation(
            _target((306.0, 244.0, 346.0, 364.0)),
            timestamp=2.0,
        )

        predicted_one = compensator.try_predict(timestamp=3.0)
        predicted_two = compensator.try_predict(timestamp=4.0)
        predicted_three = compensator.try_predict(timestamp=5.0)

        self.assertIsNotNone(predicted_one)
        self.assertIsNotNone(predicted_two)
        self.assertEqual(predicted_one.source, TargetSource.PREDICTED)
        self.assertEqual(predicted_two.source, TargetSource.PREDICTED)
        self.assertLess(predicted_one.selected_box[0], predicted_two.selected_box[0])
        self.assertIsNone(predicted_three)
        self.assertEqual(len(compensator._stable_samples), 2)
        self.assertEqual(
            compensator._stable_samples[-1].selected_box,
            (306.0, 244.0, 346.0, 364.0),
        )

    def test_prediction_clamps_per_step_movement(self):
        compensator = TargetOcclusionCompensator()
        compensator.record_observation(
            _target((300.0, 240.0, 340.0, 360.0)),
            timestamp=1.0,
        )
        compensator.record_observation(
            _target((400.0, 340.0, 440.0, 460.0)),
            timestamp=2.0,
        )

        predicted = compensator.try_predict(timestamp=3.0)

        self.assertIsNotNone(predicted)
        self.assertAlmostEqual(predicted.selected_box[0], 428.0, places=3)
        self.assertAlmostEqual(predicted.selected_box[3], 488.0, places=3)
        self.assertAlmostEqual(predicted.target_x, 448.0, places=3)
        self.assertAlmostEqual(predicted.target_y, 413.6, places=3)

    def test_real_observation_clears_prediction_budget(self):
        compensator = TargetOcclusionCompensator()
        compensator.record_observation(
            _target((300.0, 240.0, 340.0, 360.0)),
            timestamp=1.0,
        )
        compensator.record_observation(
            _target((306.0, 244.0, 346.0, 364.0)),
            timestamp=2.0,
        )

        self.assertIsNotNone(compensator.try_predict(timestamp=3.0))
        self.assertIsNotNone(compensator.try_predict(timestamp=4.0))
        self.assertIsNone(compensator.try_predict(timestamp=5.0))

        compensator.record_observation(
            _target((312.0, 248.0, 352.0, 368.0)),
            timestamp=5.0,
        )

        predicted_after_reacquire = compensator.try_predict(timestamp=6.0)

        self.assertIsNotNone(predicted_after_reacquire)
        self.assertEqual(predicted_after_reacquire.source, TargetSource.PREDICTED)


if __name__ == "__main__":
    unittest.main()
