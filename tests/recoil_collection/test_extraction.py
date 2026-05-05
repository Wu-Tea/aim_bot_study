import importlib
import unittest

from vision.recoil_collection.models import RecoilBurstSampleSeries
from vision.recoil_collection.models import RecoilCollectionSession
from vision.recoil_collection.models import RecoilSample


def _load_extraction_module():
    try:
        return importlib.import_module("vision.recoil_collection.extraction")
    except ModuleNotFoundError as exc:
        raise AssertionError(f"Missing extraction module: {exc}") from exc


def _session() -> RecoilCollectionSession:
    return RecoilCollectionSession(
        session_id="session-cod22-m4-20260506-120000",
        canonical_weapon_id="cod22-m4",
        game="cod22",
        stance="standing",
        aim_mode="ads",
        capture_resolution="1920x1080",
        capture_fps=240.0,
        collector_version="collector-0.1.0",
        started_at="2026-05-06T12:00:00Z",
    )


def _series(
    *,
    burst_id: str,
    start_offset_ms: int,
    sample_interval_ms: int,
    anchor_x: float,
    anchor_y: float,
    deltas_x: tuple[float, ...],
    deltas_y: tuple[float, ...],
) -> RecoilBurstSampleSeries:
    if len(deltas_x) != len(deltas_y):
        raise AssertionError("Synthetic burst fixture deltas must have matching lengths")
    return RecoilBurstSampleSeries(
        burst_id=burst_id,
        session_id=_session().session_id,
        sample_interval_ms=sample_interval_ms,
        samples=tuple(
            RecoilSample(
                offset_ms=start_offset_ms + (index * sample_interval_ms),
                x=anchor_x + delta_x,
                y=anchor_y + delta_y,
            )
            for index, (delta_x, delta_y) in enumerate(zip(deltas_x, deltas_y))
        ),
        sample_count=len(deltas_x),
    )


class ExtractRecoilProfileTests(unittest.TestCase):
    def assertTupleAlmostEqual(
        self,
        actual: tuple[float, ...],
        expected: tuple[float, ...],
        *,
        places: int = 6,
    ) -> None:
        self.assertEqual(len(actual), len(expected))
        for actual_value, expected_value in zip(actual, expected):
            self.assertAlmostEqual(actual_value, expected_value, places=places)

    def test_generates_aligned_average_curve_from_repeated_synthetic_bursts(self):
        extraction = _load_extraction_module()
        result = extraction.extract_recoil_profile(
            session=_session(),
            bursts=(
                _series(
                    burst_id="burst-fine-a",
                    start_offset_ms=4,
                    sample_interval_ms=10,
                    anchor_x=10.0,
                    anchor_y=50.0,
                    deltas_x=(0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0),
                    deltas_y=(0.0, -2.0, -4.0, -6.0, -8.0, -10.0, -12.0),
                ),
                _series(
                    burst_id="burst-coarse-b",
                    start_offset_ms=6,
                    sample_interval_ms=20,
                    anchor_x=-3.0,
                    anchor_y=7.0,
                    deltas_x=(0.0, 2.0, 4.0, 6.0),
                    deltas_y=(0.0, -4.0, -8.0, -12.0),
                ),
                _series(
                    burst_id="burst-fine-c",
                    start_offset_ms=0,
                    sample_interval_ms=10,
                    anchor_x=100.0,
                    anchor_y=20.0,
                    deltas_x=(0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0),
                    deltas_y=(0.0, -2.0, -4.0, -6.0, -8.0, -10.0, -12.0),
                ),
            ),
            profile_id="profile-cod22-m4-ads-standing-v1",
            created_at="2026-05-06T12:30:00Z",
            config=extraction.RecoilExtractionConfig(sample_interval_ms=10),
        )

        self.assertEqual(result.accepted_burst_ids, ("burst-fine-a", "burst-coarse-b", "burst-fine-c"))
        self.assertEqual(result.rejected_burst_ids, ())
        self.assertEqual(result.profile.initial_delay_ms, 0)
        self.assertEqual(result.profile.sample_interval_ms, 10)
        self.assertEqual(result.profile.duration_ms, 70)
        self.assertEqual(result.profile.sample_count, 7)
        self.assertEqual(result.profile.burst_count, 3)
        self.assertTupleAlmostEqual(result.profile.samples_x, (0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0))
        self.assertTupleAlmostEqual(result.profile.samples_y, (0.0, -2.0, -4.0, -6.0, -8.0, -10.0, -12.0))
        self.assertAlmostEqual(result.profile.variance_summary["horizontal_stddev"], 0.0, places=6)
        self.assertAlmostEqual(result.profile.variance_summary["vertical_stddev"], 0.0, places=6)
        self.assertGreater(result.profile.confidence, 0.7)

    def test_rejects_outlier_burst_before_averaging(self):
        extraction = _load_extraction_module()
        result = extraction.extract_recoil_profile(
            session=_session(),
            bursts=(
                _series(
                    burst_id="burst-good-a",
                    start_offset_ms=0,
                    sample_interval_ms=10,
                    anchor_x=5.0,
                    anchor_y=30.0,
                    deltas_x=(0.0, 0.5, 1.0, 1.5, 2.0),
                    deltas_y=(0.0, -1.0, -2.0, -3.0, -4.0),
                ),
                _series(
                    burst_id="burst-good-b",
                    start_offset_ms=5,
                    sample_interval_ms=10,
                    anchor_x=-2.0,
                    anchor_y=10.0,
                    deltas_x=(0.0, 0.5, 1.0, 1.5, 2.0),
                    deltas_y=(0.0, -1.0, -2.0, -3.0, -4.0),
                ),
                _series(
                    burst_id="burst-good-c",
                    start_offset_ms=2,
                    sample_interval_ms=10,
                    anchor_x=50.0,
                    anchor_y=-10.0,
                    deltas_x=(0.0, 0.5, 1.0, 1.5, 2.0),
                    deltas_y=(0.0, -1.0, -2.0, -3.0, -4.0),
                ),
                _series(
                    burst_id="burst-outlier",
                    start_offset_ms=4,
                    sample_interval_ms=10,
                    anchor_x=9.0,
                    anchor_y=9.0,
                    deltas_x=(0.0, 5.0, 10.0, 15.0, 20.0),
                    deltas_y=(0.0, 8.0, 16.0, 24.0, 32.0),
                ),
            ),
            profile_id="profile-cod22-m4-ads-standing-v1",
            created_at="2026-05-06T12:30:00Z",
            config=extraction.RecoilExtractionConfig(sample_interval_ms=10),
        )

        self.assertEqual(result.accepted_burst_ids, ("burst-good-a", "burst-good-b", "burst-good-c"))
        self.assertEqual(result.rejected_burst_ids, ("burst-outlier",))
        self.assertEqual(result.profile.burst_count, 3)
        self.assertTupleAlmostEqual(result.profile.samples_x, (0.0, 0.5, 1.0, 1.5, 2.0))
        self.assertTupleAlmostEqual(result.profile.samples_y, (0.0, -1.0, -2.0, -3.0, -4.0))

    def test_variance_increases_when_clean_bursts_disagree(self):
        extraction = _load_extraction_module()
        agreeing = extraction.extract_recoil_profile(
            session=_session(),
            bursts=(
                _series(
                    burst_id="agree-a",
                    start_offset_ms=0,
                    sample_interval_ms=10,
                    anchor_x=0.0,
                    anchor_y=0.0,
                    deltas_x=(0.0, 1.0, 2.0, 3.0, 4.0),
                    deltas_y=(0.0, -1.0, -2.0, -3.0, -4.0),
                ),
                _series(
                    burst_id="agree-b",
                    start_offset_ms=3,
                    sample_interval_ms=10,
                    anchor_x=10.0,
                    anchor_y=20.0,
                    deltas_x=(0.0, 1.0, 2.0, 3.0, 4.0),
                    deltas_y=(0.0, -1.0, -2.0, -3.0, -4.0),
                ),
                _series(
                    burst_id="agree-c",
                    start_offset_ms=6,
                    sample_interval_ms=10,
                    anchor_x=-5.0,
                    anchor_y=-5.0,
                    deltas_x=(0.0, 1.0, 2.0, 3.0, 4.0),
                    deltas_y=(0.0, -1.0, -2.0, -3.0, -4.0),
                ),
            ),
            profile_id="profile-agreeing",
            created_at="2026-05-06T12:30:00Z",
            config=extraction.RecoilExtractionConfig(sample_interval_ms=10),
        )
        disagreeing = extraction.extract_recoil_profile(
            session=_session(),
            bursts=(
                _series(
                    burst_id="disagree-a",
                    start_offset_ms=0,
                    sample_interval_ms=10,
                    anchor_x=0.0,
                    anchor_y=0.0,
                    deltas_x=(0.0, 1.0, 2.0, 3.0, 4.0),
                    deltas_y=(0.0, -1.0, -2.0, -3.0, -4.0),
                ),
                _series(
                    burst_id="disagree-b",
                    start_offset_ms=2,
                    sample_interval_ms=10,
                    anchor_x=10.0,
                    anchor_y=5.0,
                    deltas_x=(0.0, 1.4, 2.8, 4.2, 5.6),
                    deltas_y=(0.0, -0.5, -1.5, -2.5, -3.5),
                ),
                _series(
                    burst_id="disagree-c",
                    start_offset_ms=4,
                    sample_interval_ms=10,
                    anchor_x=-10.0,
                    anchor_y=7.0,
                    deltas_x=(0.0, 0.6, 1.2, 1.8, 2.4),
                    deltas_y=(0.0, -1.5, -2.5, -3.5, -4.5),
                ),
            ),
            profile_id="profile-disagreeing",
            created_at="2026-05-06T12:30:00Z",
            config=extraction.RecoilExtractionConfig(sample_interval_ms=10),
        )

        self.assertEqual(disagreeing.rejected_burst_ids, ())
        self.assertGreater(
            disagreeing.profile.variance_summary["horizontal_stddev"],
            agreeing.profile.variance_summary["horizontal_stddev"],
        )
        self.assertGreater(
            disagreeing.profile.variance_summary["vertical_stddev"],
            agreeing.profile.variance_summary["vertical_stddev"],
        )
        self.assertLess(disagreeing.profile.confidence, agreeing.profile.confidence)

    def test_returns_low_confidence_when_too_few_clean_bursts_remain(self):
        extraction = _load_extraction_module()
        result = extraction.extract_recoil_profile(
            session=_session(),
            bursts=(
                _series(
                    burst_id="burst-good-a",
                    start_offset_ms=0,
                    sample_interval_ms=10,
                    anchor_x=0.0,
                    anchor_y=20.0,
                    deltas_x=(0.0, 0.4, 0.8, 1.2, 1.6),
                    deltas_y=(0.0, -0.8, -1.6, -2.4, -3.2),
                ),
                _series(
                    burst_id="burst-good-b",
                    start_offset_ms=3,
                    sample_interval_ms=10,
                    anchor_x=50.0,
                    anchor_y=-12.0,
                    deltas_x=(0.0, 0.4, 0.8, 1.2, 1.6),
                    deltas_y=(0.0, -0.8, -1.6, -2.4, -3.2),
                ),
                _series(
                    burst_id="burst-outlier",
                    start_offset_ms=5,
                    sample_interval_ms=10,
                    anchor_x=0.0,
                    anchor_y=0.0,
                    deltas_x=(0.0, 4.0, 8.0, 12.0, 16.0),
                    deltas_y=(0.0, 5.0, 10.0, 15.0, 20.0),
                ),
            ),
            profile_id="profile-low-confidence",
            created_at="2026-05-06T12:30:00Z",
            config=extraction.RecoilExtractionConfig(
                sample_interval_ms=10,
                min_clean_bursts=3,
                target_clean_bursts=4,
            ),
        )

        self.assertEqual(result.accepted_burst_ids, ("burst-good-a", "burst-good-b"))
        self.assertEqual(result.rejected_burst_ids, ("burst-outlier",))
        self.assertEqual(result.profile.burst_count, 2)
        self.assertLess(result.profile.confidence, 0.5)


if __name__ == "__main__":
    unittest.main()
