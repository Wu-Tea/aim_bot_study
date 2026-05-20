import contextlib
import io
import json
import tempfile
import unittest
from pathlib import Path

from controllers.gamepad.recoil_compensation import RecoilCompensationConfig
from vision.recoil_collection.models import RecoilProfileRecord


def _profile(
    *,
    samples_x: tuple[float, ...] = (0.0, -1.0, -2.0),
    samples_y: tuple[float, ...] = (0.0, 1.8, 3.6),
) -> RecoilProfileRecord:
    return RecoilProfileRecord(
        profile_id="profile-cod22-m4-ads-standing-current",
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
        burst_count=3,
        variance_summary={"horizontal_stddev": 0.1, "vertical_stddev": 0.2},
        confidence=0.9,
        capture_resolution="2560x1440",
        capture_fps=144.0,
        collector_version="test",
        created_at="2026-05-20T00:00:00Z",
        profile_type="magazine_curve_v1",
        support_counts=tuple(3 for _ in samples_y),
        fit_summary={"accepted_episode_count": 3.0, "episode_count": 3.0},
    )


class RecoilPlaybackDryRunTests(unittest.TestCase):
    def test_simulation_reports_stick_curve_and_profile_amount(self):
        from tools.dry_run_recoil_playback import simulate_profile_playback

        report = simulate_profile_playback(
            _profile(),
            config=RecoilCompensationConfig(
                profile_amount=1.0,
                profile_x_amount=2.0,
                feedback_amount=0.0,
            ),
            frame_count=3,
        )

        self.assertEqual(report["profile_id"], "profile-cod22-m4-ads-standing-current")
        self.assertEqual(report["profile_amount"], 1.0)
        self.assertEqual(report["profile_x_amount"], 2.0)
        self.assertEqual(report["feedback_amount"], 0.0)
        self.assertFalse(report["calibrated"])
        self.assertEqual(report["peak_abs_right_x"], 947)
        self.assertEqual(report["peak_abs_right_y"], 1704)
        self.assertEqual(
            report["frames"],
            [
                {"elapsed_ms": 0, "profile_x": 0.0, "profile_y": 0.0, "right_x": 0, "right_y": 0},
                {"elapsed_ms": 10, "profile_x": -1.0, "profile_y": 1.8, "right_x": 947, "right_y": -852},
                {"elapsed_ms": 20, "profile_x": -2.0, "profile_y": 3.6, "right_x": 947, "right_y": -1704},
            ],
        )

    def test_cli_prints_json_report_for_profile_file(self):
        from tools.dry_run_recoil_playback import main

        profile = _profile()
        with tempfile.TemporaryDirectory() as temp_dir:
            profile_path = Path(temp_dir) / "profile.json"
            profile_path.write_text(json.dumps(profile.to_dict(), ensure_ascii=False), encoding="utf-8")

            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                exit_code = main(
                    [
                        "--profile",
                        str(profile_path),
                        "--profile-amount",
                        "0.8",
                        "--profile-x-amount",
                        "1.4",
                        "--feedback-amount",
                        "0.5",
                        "--frame-count",
                        "3",
                    ]
                )

        self.assertEqual(exit_code, 0)
        payload = json.loads(stdout.getvalue())
        self.assertEqual(payload["profile_id"], profile.profile_id)
        self.assertEqual(payload["profile_amount"], 0.8)
        self.assertEqual(payload["profile_x_amount"], 1.4)
        self.assertEqual(payload["feedback_amount"], 0.5)
        self.assertEqual(payload["frames"][-1]["right_x"], 530)
        self.assertEqual(payload["frames"][-1]["right_y"], -1363)


if __name__ == "__main__":
    unittest.main()
