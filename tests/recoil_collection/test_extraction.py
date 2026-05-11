import importlib
import json
from pathlib import Path
import unittest

from vision.recoil_collection.models import RecoilBurstSampleSeries
from vision.recoil_collection.models import RecoilCollectionSession
from vision.recoil_collection.models import RecoilSample

_FIXTURES_ROOT = Path(__file__).resolve().parents[1] / "fixtures"


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
        fixture = _load_extraction_fixture("aligned_average_bursts")
        result = extraction.extract_recoil_profile(
            session=fixture["session"],
            bursts=fixture["bursts"],
            profile_id=fixture["profile_id"],
            created_at=fixture["created_at"],
            config=extraction.RecoilExtractionConfig(**fixture["config"]),
        )

        expected = fixture["expected"]
        self.assertEqual(result.accepted_burst_ids, tuple(expected["accepted_burst_ids"]))
        self.assertEqual(result.rejected_burst_ids, tuple(expected["rejected_burst_ids"]))
        self.assertEqual(result.profile.initial_delay_ms, expected["profile"]["initial_delay_ms"])
        self.assertEqual(result.profile.sample_interval_ms, expected["profile"]["sample_interval_ms"])
        self.assertEqual(result.profile.duration_ms, expected["profile"]["duration_ms"])
        self.assertEqual(result.profile.sample_count, expected["profile"]["sample_count"])
        self.assertEqual(result.profile.burst_count, expected["profile"]["burst_count"])
        self.assertTupleAlmostEqual(result.profile.samples_x, tuple(expected["profile"]["samples_x"]))
        self.assertTupleAlmostEqual(result.profile.samples_y, tuple(expected["profile"]["samples_y"]))
        self.assertAlmostEqual(
            result.profile.variance_summary["horizontal_stddev"],
            expected["profile"]["horizontal_stddev"],
            places=6,
        )
        self.assertAlmostEqual(
            result.profile.variance_summary["vertical_stddev"],
            expected["profile"]["vertical_stddev"],
            places=6,
        )
        self.assertGreater(result.profile.confidence, expected["profile"]["minimum_confidence"])

    def test_extraction_fixtures_use_serialized_burst_series_payloads(self):
        for fixture_name in ("aligned_average_bursts", "outlier_rejection_bursts"):
            with self.subTest(fixture=fixture_name):
                fixture_payload = _load_raw_extraction_fixture(fixture_name)
                session_id = fixture_payload["session"]["session_id"]
                for burst_payload in fixture_payload["bursts"]:
                    self.assertIn("samples", burst_payload)
                    self.assertIn("sample_count", burst_payload)
                    self.assertEqual(burst_payload["session_id"], session_id)
                    self.assertNotIn("anchor_x", burst_payload)
                    self.assertNotIn("anchor_y", burst_payload)
                    self.assertNotIn("deltas_x", burst_payload)
                    self.assertNotIn("deltas_y", burst_payload)

    def test_rejects_outlier_burst_before_averaging(self):
        extraction = _load_extraction_module()
        fixture = _load_extraction_fixture("outlier_rejection_bursts")
        result = extraction.extract_recoil_profile(
            session=fixture["session"],
            bursts=fixture["bursts"],
            profile_id=fixture["profile_id"],
            created_at=fixture["created_at"],
            config=extraction.RecoilExtractionConfig(**fixture["config"]),
        )

        expected = fixture["expected"]
        self.assertEqual(result.accepted_burst_ids, tuple(expected["accepted_burst_ids"]))
        self.assertEqual(result.rejected_burst_ids, tuple(expected["rejected_burst_ids"]))
        self.assertEqual(result.profile.burst_count, expected["profile"]["burst_count"])
        self.assertTupleAlmostEqual(result.profile.samples_x, tuple(expected["profile"]["samples_x"]))
        self.assertTupleAlmostEqual(result.profile.samples_y, tuple(expected["profile"]["samples_y"]))

    def test_vertical_recovery_tail_is_trimmed_from_time_curve(self):
        extraction = _load_extraction_module()
        result = extraction.extract_recoil_profile(
            session=_session(),
            bursts=(
                _series(
                    burst_id="burst-a",
                    start_offset_ms=0,
                    sample_interval_ms=10,
                    anchor_x=0.0,
                    anchor_y=0.0,
                    deltas_x=(0.0, 0.0, 0.0, 0.0, 0.0),
                    deltas_y=(0.0, -1.0, -2.0, -1.2, -0.4),
                ),
                _series(
                    burst_id="burst-b",
                    start_offset_ms=2,
                    sample_interval_ms=10,
                    anchor_x=10.0,
                    anchor_y=5.0,
                    deltas_x=(0.0, 0.0, 0.0, 0.0, 0.0),
                    deltas_y=(0.0, -1.1, -2.2, -1.1, -0.2),
                ),
                _series(
                    burst_id="burst-c",
                    start_offset_ms=4,
                    sample_interval_ms=10,
                    anchor_x=-3.0,
                    anchor_y=8.0,
                    deltas_x=(0.0, 0.0, 0.0, 0.0, 0.0),
                    deltas_y=(0.0, -0.9, -1.8, -1.0, -0.3),
                ),
            ),
            profile_id="profile-trim-recovery-tail",
            created_at="2026-05-06T12:30:00Z",
            config=extraction.RecoilExtractionConfig(sample_interval_ms=10),
        )

        self.assertTupleAlmostEqual(result.profile.samples_y, (0.0, -1.0, -2.0))
        self.assertEqual(result.profile.sample_count, 3)
        self.assertEqual(result.profile.duration_ms, 30)

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

    def test_rejects_truncated_burst_before_it_can_collapse_profile_tail(self):
        extraction = _load_extraction_module()
        result = extraction.extract_recoil_profile(
            session=_session(),
            bursts=(
                _series(
                    burst_id="good-a",
                    start_offset_ms=0,
                    sample_interval_ms=10,
                    anchor_x=0.0,
                    anchor_y=10.0,
                    deltas_x=(0.0, 1.0, 2.0, 3.0, 4.0, 5.0),
                    deltas_y=(0.0, -1.0, -2.0, -3.0, -4.0, -5.0),
                ),
                _series(
                    burst_id="good-b",
                    start_offset_ms=5,
                    sample_interval_ms=10,
                    anchor_x=30.0,
                    anchor_y=-12.0,
                    deltas_x=(0.0, 1.0, 2.0, 3.0, 4.0, 5.0),
                    deltas_y=(0.0, -1.0, -2.0, -3.0, -4.0, -5.0),
                ),
                _series(
                    burst_id="short",
                    start_offset_ms=2,
                    sample_interval_ms=10,
                    anchor_x=-8.0,
                    anchor_y=3.0,
                    deltas_x=(0.0, 1.0, 2.0),
                    deltas_y=(0.0, -1.0, -2.0),
                ),
            ),
            profile_id="profile-truncated-burst",
            created_at="2026-05-06T13:00:00Z",
            config=extraction.RecoilExtractionConfig(sample_interval_ms=10),
        )

        self.assertEqual(result.accepted_burst_ids, ("good-a", "good-b"))
        self.assertEqual(result.rejected_burst_ids, ("short",))
        self.assertEqual(result.profile.duration_ms, 60)
        self.assertEqual(result.profile.sample_count, 6)
        self.assertTupleAlmostEqual(result.profile.samples_x, (0.0, 1.0, 2.0, 3.0, 4.0, 5.0))
        self.assertTupleAlmostEqual(result.profile.samples_y, (0.0, -1.0, -2.0, -3.0, -4.0, -5.0))

    def test_rejects_degenerate_single_point_profile_extraction(self):
        extraction = _load_extraction_module()

        with self.assertRaisesRegex(ValueError, "at least 2 resampled samples"):
            extraction.extract_recoil_profile(
                session=_session(),
                bursts=(
                    _series(
                        burst_id="one-a",
                        start_offset_ms=0,
                        sample_interval_ms=10,
                        anchor_x=4.0,
                        anchor_y=8.0,
                        deltas_x=(0.0,),
                        deltas_y=(0.0,),
                    ),
                    _series(
                        burst_id="one-b",
                        start_offset_ms=3,
                        sample_interval_ms=10,
                        anchor_x=-4.0,
                        anchor_y=2.0,
                        deltas_x=(0.0,),
                        deltas_y=(0.0,),
                    ),
                    _series(
                        burst_id="one-c",
                        start_offset_ms=7,
                        sample_interval_ms=10,
                        anchor_x=12.0,
                        anchor_y=-6.0,
                        deltas_x=(0.0,),
                        deltas_y=(0.0,),
                    ),
                ),
                profile_id="profile-degenerate",
                created_at="2026-05-06T13:00:00Z",
                config=extraction.RecoilExtractionConfig(sample_interval_ms=10),
            )


def _load_extraction_fixture(name: str):
    fixture_payload = _load_raw_extraction_fixture(name)
    session = RecoilCollectionSession.from_dict(fixture_payload["session"])
    return {
        "session": session,
        "bursts": tuple(
            RecoilBurstSampleSeries.from_dict(burst_payload)
            for burst_payload in fixture_payload["bursts"]
        ),
        "profile_id": fixture_payload["profile_id"],
        "created_at": fixture_payload["created_at"],
        "config": fixture_payload["config"],
        "expected": fixture_payload["expected"],
    }


def _load_raw_extraction_fixture(name: str):
    fixture_path = _FIXTURES_ROOT / "recoil_collection" / "extraction" / f"{name}.json"
    return json.loads(fixture_path.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
