import unittest

from vision.recoil_collection.models import RecoilProfileRecord
from vision.recoil_collection.models import RecoilProfileSummary
from vision.recoil_collection.models import RecoilSample


class RecoilSampleTests(unittest.TestCase):
    def test_round_trip_serialization(self):
        sample = RecoilSample(offset_ms=120, x=1.5, y=-4.25)

        self.assertEqual(RecoilSample.from_dict(sample.to_dict()), sample)

    def test_constructor_rejects_negative_offset(self):
        with self.assertRaisesRegex(ValueError, "offset_ms"):
            RecoilSample(offset_ms=-1, x=0.0, y=0.0)


class RecoilProfileRecordTests(unittest.TestCase):
    def test_round_trip_serialization_and_samples_property(self):
        profile = RecoilProfileRecord(
            profile_id="profile-cod22-m4-ads-standing-v1",
            canonical_weapon_id="cod22-m4",
            game="cod22",
            stance="standing",
            aim_mode="ads",
            sample_interval_ms=16,
            duration_ms=64,
            initial_delay_ms=32,
            samples_x=(0.0, 0.8, 1.2, 1.4),
            samples_y=(0.0, -2.5, -5.1, -6.0),
            sample_count=4,
            burst_count=6,
            variance_summary={"horizontal_stddev": 0.19, "vertical_stddev": 0.42},
            confidence=0.88,
            capture_resolution="1920x1080",
            capture_fps=240.0,
            collector_version="collector-0.1.0",
            created_at="2026-05-05T11:00:00Z",
        )

        round_tripped = RecoilProfileRecord.from_dict(profile.to_dict())

        self.assertEqual(round_tripped, profile)
        self.assertEqual(
            round_tripped.samples,
            (
                RecoilSample(offset_ms=32, x=0.0, y=0.0),
                RecoilSample(offset_ms=48, x=0.8, y=-2.5),
                RecoilSample(offset_ms=64, x=1.2, y=-5.1),
                RecoilSample(offset_ms=80, x=1.4, y=-6.0),
            ),
        )
        self.assertNotIn("blueprint_names", round_tripped.to_dict())

    def test_from_dict_rejects_missing_profile_id(self):
        payload = {
            "canonical_weapon_id": "cod22-m4",
            "game": "cod22",
            "stance": "standing",
            "aim_mode": "ads",
            "sample_interval_ms": 16,
            "duration_ms": 64,
            "initial_delay_ms": 32,
            "samples_x": [0.0, 0.8],
            "samples_y": [0.0, -2.5],
            "sample_count": 2,
            "burst_count": 6,
            "variance_summary": {"horizontal_stddev": 0.19, "vertical_stddev": 0.42},
            "confidence": 0.88,
            "capture_resolution": "1920x1080",
            "capture_fps": 240.0,
            "collector_version": "collector-0.1.0",
            "created_at": "2026-05-05T11:00:00Z",
        }

        with self.assertRaisesRegex(ValueError, "profile_id"):
            RecoilProfileRecord.from_dict(payload)

    def test_constructor_rejects_mismatched_sample_lengths(self):
        with self.assertRaisesRegex(ValueError, "samples_y"):
            RecoilProfileRecord(
                profile_id="profile-cod22-m4-ads-standing-v1",
                canonical_weapon_id="cod22-m4",
                game="cod22",
                stance="standing",
                aim_mode="ads",
                sample_interval_ms=16,
                duration_ms=64,
                initial_delay_ms=32,
                samples_x=(0.0, 0.8),
                samples_y=(0.0,),
                sample_count=2,
                burst_count=6,
                variance_summary={"horizontal_stddev": 0.19, "vertical_stddev": 0.42},
                confidence=0.88,
                capture_resolution="1920x1080",
                capture_fps=240.0,
                collector_version="collector-0.1.0",
                created_at="2026-05-05T11:00:00Z",
            )


class RecoilProfileSummaryTests(unittest.TestCase):
    def test_round_trip_serialization(self):
        summary = RecoilProfileSummary(
            profile_id="profile-cod22-m4-ads-standing-v1",
            canonical_weapon_id="cod22-m4",
            game="cod22",
            stance="standing",
            aim_mode="ads",
            sample_count=4,
            burst_count=6,
            confidence=0.88,
            peak_abs_x=1.4,
            peak_abs_y=6.0,
            created_at="2026-05-05T11:00:00Z",
        )

        self.assertEqual(RecoilProfileSummary.from_dict(summary.to_dict()), summary)

    def test_constructor_rejects_blank_profile_id(self):
        with self.assertRaisesRegex(ValueError, "profile_id"):
            RecoilProfileSummary(
                profile_id="",
                canonical_weapon_id="cod22-m4",
                game="cod22",
                stance="standing",
                aim_mode="ads",
                sample_count=4,
                burst_count=6,
                confidence=0.88,
                peak_abs_x=1.4,
                peak_abs_y=6.0,
                created_at="2026-05-05T11:00:00Z",
            )


if __name__ == "__main__":
    unittest.main()
