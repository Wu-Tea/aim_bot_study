from pathlib import Path
from tempfile import TemporaryDirectory
import json
import subprocess
import sys
import unittest
from unittest.mock import patch

from tools.run_mouse_benchmark import (
    GitMetadata,
    replay_run_key,
    replay_scenario_key,
    run_benchmark,
)


class MouseBenchmarkRunnerTests(unittest.TestCase):
    def git_metadata(self):
        return GitMetadata(commit="abc1234", dirty=False)

    def test_run_benchmark_writes_isolated_mouse_ads_artifact_and_scoreboard(self):
        with TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            artifact_dir = temp_path / "artifacts" / "benchmarks" / "mouse_ads"
            scoreboard_path = temp_path / "docs" / "project" / "MOUSE_ADS_BENCHMARKS.md"

            result = run_benchmark(
                run_key="mouse-run",
                run_seed=12345,
                artifact_dir=artifact_dir,
                scoreboard_path=scoreboard_path,
                git_metadata=self.git_metadata(),
            )

            self.assertTrue((artifact_dir / "mouse-run.json").exists())
            self.assertEqual(result["run_key"], "mouse-run")
            self.assertEqual(result["suite"], "mouse-ads")
            self.assertEqual(
                result["artifact_path"],
                "artifacts/benchmarks/mouse_ads/mouse-run.json",
            )
            self.assertEqual(len(result["scenarios"]), 36)
            self.assertIn("time_to_stabilize_ms", result["aggregate_metrics"])
            self.assertIn(
                "settle_time_after_under_10px_ms",
                result["aggregate_metrics"],
            )
            self.assertIn("under_20_escape_count", result["aggregate_metrics"])
            self.assertIn(
                "post_under_20_axis_flip_count",
                result["aggregate_metrics"],
            )
            self.assertIn("# Mouse ADS Benchmarks", scoreboard_path.read_text(encoding="utf-8"))

    def test_run_benchmark_with_gamepad_reference_reports_shared_metric_comparison(self):
        with TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            artifact_dir = temp_path / "artifacts" / "benchmarks" / "mouse_ads"
            scoreboard_path = temp_path / "docs" / "project" / "MOUSE_ADS_BENCHMARKS.md"
            gamepad_artifact_dir = temp_path / "artifacts" / "benchmarks" / "gamepad_ads"
            gamepad_artifact_dir.mkdir(parents=True, exist_ok=True)
            (gamepad_artifact_dir / "ads-ref.json").write_text(
                json.dumps(
                    {
                        "run_key": "ads-ref",
                        "aggregate_metrics": {
                            "wrong_target_snap_rate": 0.25,
                            "max_single_frame_camera_delta": 20.0,
                            "target_localization_latency_ms": 0.0,
                            "time_to_under_20px": 80.0,
                            "time_to_body_lock": 60.0,
                            "reacquire_time_after_occlusion": 30.0,
                        },
                    }
                ),
                encoding="utf-8",
            )

            result = run_benchmark(
                run_key="mouse-run",
                run_seed=12345,
                artifact_dir=artifact_dir,
                scoreboard_path=scoreboard_path,
                git_metadata=self.git_metadata(),
                reference_gamepad_run_key="ads-ref",
                reference_gamepad_artifact_dir=gamepad_artifact_dir,
            )

            comparison = result["gamepad_comparison"]
            self.assertEqual(comparison["reference_run_key"], "ads-ref")
            self.assertIn("time_to_under_20px", comparison["metric_deltas"])
            self.assertNotIn("time_to_body_lock", comparison["metric_deltas"])

    def test_replay_run_key_uses_stored_manifests_instead_of_regenerating(self):
        with TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            artifact_dir = temp_path / "artifacts" / "benchmarks" / "mouse_ads"
            scoreboard_path = temp_path / "docs" / "project" / "MOUSE_ADS_BENCHMARKS.md"

            run_benchmark(
                run_key="mouse-run",
                run_seed=12345,
                artifact_dir=artifact_dir,
                scoreboard_path=scoreboard_path,
                git_metadata=self.git_metadata(),
            )

            with patch(
                "tools.run_mouse_benchmark.generate_ads_manifests",
                side_effect=AssertionError("should not regenerate"),
            ):
                replay = replay_run_key(
                    run_key="mouse-run",
                    artifact_dir=artifact_dir,
                )

            self.assertEqual(replay["run_key"], "mouse-run")
            self.assertEqual(len(replay["scenarios"]), 36)

    def test_replay_scenario_key_replays_one_stored_scenario(self):
        with TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            artifact_dir = temp_path / "artifacts" / "benchmarks" / "mouse_ads"
            scoreboard_path = temp_path / "docs" / "project" / "MOUSE_ADS_BENCHMARKS.md"

            result = run_benchmark(
                run_key="mouse-run",
                run_seed=12345,
                artifact_dir=artifact_dir,
                scoreboard_path=scoreboard_path,
                git_metadata=self.git_metadata(),
            )
            scenario_key = result["scenarios"][0]["manifest"]["scenario_key"]

            replay = replay_scenario_key(
                scenario_key=scenario_key,
                artifact_dir=artifact_dir,
            )

            self.assertEqual(replay["scenario_key"], scenario_key)
            self.assertIn("metrics", replay)

    def test_script_entrypoint_runs_from_repo_root(self):
        with TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            artifact_dir = temp_path / "artifacts" / "benchmarks" / "mouse_ads"
            scoreboard_path = temp_path / "docs" / "project" / "MOUSE_ADS_BENCHMARKS.md"
            repo_root = Path(__file__).resolve().parents[2]

            completed = subprocess.run(
                [
                    sys.executable,
                    "tools/run_mouse_benchmark.py",
                    "--run-key",
                    "mouse-script-run",
                    "--run-seed",
                    "12345",
                    "--artifact-dir",
                    str(artifact_dir),
                    "--scoreboard-path",
                    str(scoreboard_path),
                ],
                cwd=repo_root,
                check=False,
                capture_output=True,
                text=True,
            )

            self.assertEqual(completed.returncode, 0, msg=completed.stderr)
            self.assertTrue((artifact_dir / "mouse-script-run.json").exists())
            self.assertTrue(scoreboard_path.exists())


if __name__ == "__main__":
    unittest.main()
