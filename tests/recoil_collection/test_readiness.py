import unittest

from vision.recoil_collection.models import RecoilProfileRecord
from vision.recoil_collection.readiness import is_profile_ready_for_compensation
from vision.recoil_collection.readiness import profile_readiness_reason


class RecoilProfileReadinessTests(unittest.TestCase):
    def test_legacy_profile_is_ready_when_confidence_is_high_enough(self):
        self.assertTrue(is_profile_ready_for_compensation(_profile(confidence=0.78)))

    def test_low_confidence_profile_is_not_ready(self):
        self.assertFalse(is_profile_ready_for_compensation(_profile(confidence=0.62)))
        self.assertEqual(profile_readiness_reason(_profile(confidence=0.62)), "confidence_below_min")

    def test_magazine_profile_requires_multiple_supported_recordings(self):
        self.assertFalse(
            is_profile_ready_for_compensation(
                _profile(
                    confidence=0.92,
                    profile_type="magazine_curve_v1",
                    burst_count=1,
                    support_counts=(1, 1, 1),
                    fit_summary={"accepted_episode_count": 1.0},
                )
            )
        )

    def test_magazine_profile_is_ready_when_each_point_has_enough_support(self):
        self.assertTrue(
            is_profile_ready_for_compensation(
                _profile(
                    confidence=0.82,
                    profile_type="magazine_curve_v1",
                    burst_count=2,
                    support_counts=(2, 2, 2),
                    fit_summary={"accepted_episode_count": 2.0},
                )
            )
        )

    def test_magazine_profile_ignores_low_confidence_when_structure_is_supported(self):
        profile = _profile(
            confidence=0.24,
            profile_type="magazine_curve_v1",
            burst_count=3,
            support_counts=(3, 3, 3),
            fit_summary={"accepted_episode_count": 3.0},
        )

        self.assertTrue(is_profile_ready_for_compensation(profile))
        self.assertIsNone(profile_readiness_reason(profile))

    def test_magazine_profile_with_vertical_reversal_is_not_ready(self):
        profile = _profile(
            confidence=0.95,
            profile_type="magazine_curve_v1",
            burst_count=2,
            support_counts=(2, 2, 2),
            fit_summary={
                "accepted_episode_count": 2.0,
                "vertical_direction_reversal": 1.0,
            },
        )

        self.assertEqual(profile_readiness_reason(profile), "vertical_direction_reversal")

    def test_magazine_profile_with_large_vertical_recovery_tail_is_ready_for_trial_playback(self):
        profile = _profile(
            confidence=0.95,
            profile_type="magazine_curve_v1",
            burst_count=3,
            samples_y=(0.0, 120.0, 300.0, 110.0),
            support_counts=(3, 3, 3, 3),
            fit_summary={"accepted_episode_count": 3.0},
        )

        self.assertTrue(is_profile_ready_for_compensation(profile))
        self.assertIsNone(profile_readiness_reason(profile))

    def test_magazine_profile_with_horizontal_episode_disagreement_is_not_ready(self):
        profile = _profile(
            confidence=0.95,
            profile_type="magazine_curve_v1",
            burst_count=3,
            support_counts=(3, 3, 3),
            fit_summary={
                "accepted_episode_count": 3.0,
                "horizontal_final_range": 220.0,
            },
        )

        self.assertEqual(profile_readiness_reason(profile), "horizontal_episode_disagreement")


def _profile(
    *,
    confidence: float,
    profile_type: str = "burst_average_v1",
    burst_count: int = 4,
    samples_y: tuple[float, ...] = (0.0, -1.5, -3.0),
    support_counts: tuple[int, ...] = (),
    fit_summary: dict[str, float] | None = None,
) -> RecoilProfileRecord:
    samples_x = tuple(0.0 for _ in samples_y)
    return RecoilProfileRecord(
        profile_id="profile-cod22-m4-ads-standing-v1",
        canonical_weapon_id="cod22-m4",
        game="cod22",
        stance="standing",
        aim_mode="ads",
        sample_interval_ms=10,
        duration_ms=len(samples_y) * 10,
        initial_delay_ms=0,
        samples_x=samples_x,
        samples_y=samples_y,
        sample_count=len(samples_y),
        burst_count=burst_count,
        variance_summary={"horizontal_stddev": 0.1, "vertical_stddev": 0.2},
        confidence=confidence,
        capture_resolution="2560x1440",
        capture_fps=144.0,
        collector_version="test",
        created_at="2026-05-06T12:00:00Z",
        profile_type=profile_type,
        support_counts=support_counts,
        fit_summary=fit_summary or {},
    )


if __name__ == "__main__":
    unittest.main()
