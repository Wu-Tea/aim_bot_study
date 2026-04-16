from pathlib import Path
from tempfile import TemporaryDirectory
import subprocess
import sys
import unittest
from unittest.mock import patch

from tools.run_gamepad_benchmark import (
    GitMetadata,
    replay_run_key,
    replay_scenario_key,
    run_benchmark,
)


class GamepadBenchmarkRunnerTests(unittest.TestCase):
    def git_metadata(self):
        return GitMetadata(commit="abc1234", dirty=False)

    def test_run_benchmark_without_baseline_writes_artifact_and_reports_no_baseline(self):
        with TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            artifact_dir = temp_path / "artifacts" / "benchmarks" / "gamepad"
            scoreboard_path = temp_path / "docs" / "project" / "GAMEPAD_BENCHMARKS.md"

            result = run_benchmark(
                run_key="run-alpha",
                run_seed=12345,
                artifact_dir=artifact_dir,
                scoreboard_path=scoreboard_path,
                git_metadata=self.git_metadata(),
                set_baseline=False,
            )

            artifact_path = artifact_dir / "run-alpha.json"
            self.assertTrue(artifact_path.exists())
            self.assertEqual(result["run_key"], "run-alpha")
            self.assertIsNone(result["baseline_key"])
            self.assertEqual(len(result["scenarios"]), 24)
            self.assertIn("No baseline has been recorded yet.", scoreboard_path.read_text(encoding="utf-8"))
            self.assertIn("no baseline available yet", scoreboard_path.read_text(encoding="utf-8"))

    def test_set_baseline_records_the_created_run_and_updates_scoreboard(self):
        with TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            artifact_dir = temp_path / "artifacts" / "benchmarks" / "gamepad"
            scoreboard_path = temp_path / "docs" / "project" / "GAMEPAD_BENCHMARKS.md"

            result = run_benchmark(
                run_key="run-baseline",
                run_seed=12345,
                artifact_dir=artifact_dir,
                scoreboard_path=scoreboard_path,
                git_metadata=self.git_metadata(),
                set_baseline=True,
            )

            self.assertEqual(result["baseline_key"], "run-baseline")
            scoreboard_text = scoreboard_path.read_text(encoding="utf-8")
            self.assertIn("Baseline Run Key: `run-baseline`", scoreboard_text)
            self.assertIn("Artifact: `artifacts/benchmarks/gamepad/run-baseline.json`", scoreboard_text)

    def test_replay_run_key_uses_stored_manifests_instead_of_regenerating(self):
        with TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            artifact_dir = temp_path / "artifacts" / "benchmarks" / "gamepad"
            scoreboard_path = temp_path / "docs" / "project" / "GAMEPAD_BENCHMARKS.md"

            run_benchmark(
                run_key="run-baseline",
                run_seed=12345,
                artifact_dir=artifact_dir,
                scoreboard_path=scoreboard_path,
                git_metadata=self.git_metadata(),
                set_baseline=True,
            )

            with patch("tools.run_gamepad_benchmark.generate_phase1_manifests", side_effect=AssertionError("should not regenerate")):
                replay = replay_run_key(
                    run_key="run-baseline",
                    artifact_dir=artifact_dir,
                )

            self.assertEqual(replay["run_key"], "run-baseline")
            self.assertEqual(len(replay["scenarios"]), 24)

    def test_replay_scenario_key_replays_one_stored_scenario(self):
        with TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            artifact_dir = temp_path / "artifacts" / "benchmarks" / "gamepad"
            scoreboard_path = temp_path / "docs" / "project" / "GAMEPAD_BENCHMARKS.md"

            result = run_benchmark(
                run_key="run-baseline",
                run_seed=12345,
                artifact_dir=artifact_dir,
                scoreboard_path=scoreboard_path,
                git_metadata=self.git_metadata(),
                set_baseline=True,
            )
            scenario_key = result["scenarios"][0]["manifest"]["scenario_key"]

            replay = replay_scenario_key(
                scenario_key=scenario_key,
                artifact_dir=artifact_dir,
            )

            self.assertEqual(replay["scenario_key"], scenario_key)
            self.assertIn("metrics", replay)

    def test_missing_replay_keys_raise_clear_errors(self):
        with TemporaryDirectory() as temp_dir:
            artifact_dir = Path(temp_dir) / "artifacts" / "benchmarks" / "gamepad"

            with self.assertRaisesRegex(FileNotFoundError, "missing-run"):
                replay_run_key(
                    run_key="missing-run",
                    artifact_dir=artifact_dir,
                )

            with self.assertRaisesRegex(FileNotFoundError, "missing-scenario"):
                replay_scenario_key(
                    scenario_key="missing-scenario",
                    artifact_dir=artifact_dir,
                )

    def test_script_entrypoint_runs_from_repo_root(self):
        with TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            artifact_dir = temp_path / "artifacts" / "benchmarks" / "gamepad"
            scoreboard_path = temp_path / "docs" / "project" / "GAMEPAD_BENCHMARKS.md"
            repo_root = Path(__file__).resolve().parents[2]

            completed = subprocess.run(
                [
                    sys.executable,
                    "tools/run_gamepad_benchmark.py",
                    "--set-baseline",
                    "--run-key",
                    "script-run",
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
            self.assertTrue((artifact_dir / "script-run.json").exists())
            self.assertTrue(scoreboard_path.exists())


if __name__ == "__main__":
    unittest.main()
