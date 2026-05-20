import json
import tempfile
import unittest
from pathlib import Path

from vision.recoil_collection.audit import audit_recoil_profile_directory
from vision.recoil_collection.models import RecoilProfileRecord


def _profile(
    *,
    profile_id="profile-cod22-m4-ads-standing-current",
    samples_y=(0.0, -4.0, -8.0, 10.0),
):
    return RecoilProfileRecord(
        profile_id=profile_id,
        canonical_weapon_id="cod22-m4",
        game="cod22",
        stance="standing",
        aim_mode="ads",
        sample_interval_ms=10,
        duration_ms=len(samples_y) * 10,
        initial_delay_ms=0,
        samples_x=tuple(0.0 for _ in samples_y),
        samples_y=tuple(float(value) for value in samples_y),
        sample_count=len(samples_y),
        burst_count=1,
        variance_summary={"horizontal_stddev": 0.0, "vertical_stddev": 0.0},
        confidence=0.8,
        capture_resolution="640x640",
        capture_fps=100.0,
        collector_version="test",
        created_at="2026-05-20T00:00:00Z",
        profile_type="magazine_curve_v1",
        support_counts=tuple(1 for _ in samples_y),
        fit_summary={"episode_count": 2.0, "accepted_episode_count": 1.0},
    )


class RecoilAuditTests(unittest.TestCase):
    def test_audit_flags_single_supported_sign_reversing_profile(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            profile = _profile()
            (root / f"{profile.profile_id}.json").write_text(
                json.dumps(profile.to_dict(), ensure_ascii=False),
                encoding="utf-8",
            )

            report = audit_recoil_profile_directory(root)

        self.assertEqual(len(report.profiles), 1)
        findings = report.profiles[0].findings
        self.assertIn("accepted_episodes_below_min", findings)
        self.assertIn("support_below_min", findings)
        self.assertIn("vertical_direction_reversal", findings)
        self.assertFalse(report.profiles[0].runtime_ready)

    def test_audit_does_not_block_supported_magazine_profile_on_confidence_only(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            profile = _profile(
                profile_id="profile-cod22-m4-ads-standing-current",
                samples_y=(0.0, 8.0, 16.0, 24.0),
            )
            profile = RecoilProfileRecord(
                **{
                    **profile.to_dict(),
                    "confidence": 0.24,
                    "burst_count": 3,
                    "support_counts": [3, 3, 3, 3],
                    "fit_summary": {
                        "episode_count": 4.0,
                        "accepted_episode_count": 3.0,
                        "vertical_direction_reversal": 0.0,
                    },
                }
            )
            (root / f"{profile.profile_id}.json").write_text(
                __import__("json").dumps(profile.to_dict(), ensure_ascii=False),
                encoding="utf-8",
            )

            report = audit_recoil_profile_directory(root)

        self.assertEqual(report.profiles[0].findings, ())
        self.assertTrue(report.profiles[0].runtime_ready)


if __name__ == "__main__":
    unittest.main()
