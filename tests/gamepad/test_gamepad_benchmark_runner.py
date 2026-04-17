from pathlib import Path
from tempfile import TemporaryDirectory
import json
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

    def test_run_benchmark_with_manual_mix_suite_writes_to_manual_mix_paths(self):
        with TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            artifact_dir = temp_path / "artifacts" / "benchmarks" / "gamepad_manual_mix"
            scoreboard_path = temp_path / "docs" / "project" / "GAMEPAD_MANUAL_MIX_BENCHMARKS.md"

            result = run_benchmark(
                run_key="mix-run",
                run_seed=12345,
                suite="manual-mix",
                artifact_dir=artifact_dir,
                scoreboard_path=scoreboard_path,
                git_metadata=self.git_metadata(),
                set_baseline=False,
            )

            artifact_path = artifact_dir / "mix-run.json"
            self.assertTrue(artifact_path.exists())
            self.assertEqual(result["run_key"], "mix-run")
            self.assertEqual(result["suite"], "manual-mix")
            self.assertEqual(result["artifact_path"], "artifacts/benchmarks/gamepad_manual_mix/mix-run.json")
            self.assertIn("# Gamepad Manual-Mix Benchmarks", scoreboard_path.read_text(encoding="utf-8"))
            self.assertIn("harmful_input_suppression_ratio", result["aggregate_metrics"])
            self.assertIn("aligned_input_preservation_ratio", result["aggregate_metrics"])
            self.assertIn("opposing_burst_hold_error_px", result["aggregate_metrics"])
            self.assertIn("lock_survival_rate", result["aggregate_metrics"])
            self.assertEqual(len(result["manual_seeds"]), 3)

    def test_manual_mix_run_keeps_loading_older_baseline_artifacts_missing_new_metrics(self):
        with TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            artifact_dir = temp_path / "artifacts" / "benchmarks" / "gamepad_manual_mix"
            scoreboard_path = temp_path / "docs" / "project" / "GAMEPAD_MANUAL_MIX_BENCHMARKS.md"
            artifact_dir.mkdir(parents=True, exist_ok=True)
            scoreboard_path.parent.mkdir(parents=True, exist_ok=True)

            baseline_artifact = {
                "suite": "manual-mix",
                "run_key": "mix-baseline",
                "run_seed": 12345,
                "baseline_key": "mix-baseline",
                "timestamp": "2026-04-17T00:00:00Z",
                "artifact_path": "artifacts/benchmarks/gamepad_manual_mix/mix-baseline.json",
                "git_metadata": {"commit": "abc1234", "dirty": False},
                "benchmark_config": {
                    "frame_dt": 1.0 / 60.0,
                    "sim_frames": 180,
                    "measure_from_frame": 60,
                    "max_reticle_speed_pps": 1500.0,
                    "stick_max": 32767,
                    "overshoot_threshold_px": 2.0,
                    "turn_recovery_threshold_px": 6.0,
                    "settle_threshold_px": 5.0,
                    "settle_consecutive_frames": 4,
                    "conflict_manual_threshold": 2000,
                    "conflict_ai_threshold": 2000,
                    "wrong_input_recovery_threshold_px": 8.0,
                    "wrong_input_recovery_consecutive_frames": 3,
                },
                "manual_input_config": {
                    "max_manual_ratio": 0.72,
                    "full_scale_x": 90.0,
                    "full_scale_y": 80.0,
                    "aligned_scale": 0.62,
                    "wobble_scale": 0.18,
                    "opposing_scale": 0.55,
                    "recover_scale": 0.48,
                    "vertical_jitter_scale": 0.12,
                    "near_target_radius_px": 18.0,
                    "wobble_period_frames": 6,
                    "event_window_frames": 16,
                    "opposing_burst_min_frames": 2,
                    "opposing_burst_max_frames": 5,
                    "overshoot_recover_frames": 3,
                    "reference_frame_dt": 1.0 / 60.0,
                },
                "manual_seeds": [1, 2, 3],
                "scenario_logic": [],
                "controller_config_snapshot": {},
                "aggregate_metrics": {
                    "mean_error_px": 11.0,
                    "p95_error_px": 16.0,
                    "p99_error_px": 18.0,
                    "overshoot_events": 20,
                    "max_overshoot_px": 4.5,
                    "mean_recovery_frames_after_turn": 12.0,
                    "mean_settle_frames_after_decel": 9.0,
                    "conflict_frames_ratio": 0.2,
                    "wrong_input_recovery_frames": 10.0,
                    "manual_yield_score": 0.1,
                },
                "relative_deltas_vs_baseline": None,
                "scenarios": [],
            }
            (artifact_dir / "mix-baseline.json").write_text(
                json.dumps(baseline_artifact),
                encoding="utf-8",
            )
            scoreboard_path.write_text(
                "# Gamepad Manual-Mix Benchmarks\n\n## Baseline Definition\n\n- Baseline Run Key: `mix-baseline`\n",
                encoding="utf-8",
            )

            result = run_benchmark(
                run_key="mix-current",
                run_seed=12345,
                suite="manual-mix",
                artifact_dir=artifact_dir,
                scoreboard_path=scoreboard_path,
                git_metadata=self.git_metadata(),
                set_baseline=False,
            )

            self.assertEqual(result["baseline_key"], "mix-baseline")
            self.assertIsNotNone(result["relative_deltas_vs_baseline"])
            self.assertIn("harmful_input_suppression_ratio", result["relative_deltas_vs_baseline"])
            self.assertIsNone(result["relative_deltas_vs_baseline"]["harmful_input_suppression_ratio"])


if __name__ == "__main__":
    unittest.main()
