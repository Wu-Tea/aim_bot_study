from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

from tests.gamepad.benchmark_scoreboard import (
    ScoreboardRunEntry,
    extract_baseline_key,
    render_scoreboard,
    update_scoreboard,
)


class BenchmarkScoreboardTests(unittest.TestCase):
    def make_entry(
        self,
        *,
        run_key,
        artifact_path,
        timestamp="2026-04-16T12:00:00Z",
        dirty=False,
        delta_metrics=None,
    ):
        return ScoreboardRunEntry(
            run_key=run_key,
            timestamp=timestamp,
            artifact_path=artifact_path,
            git_commit="abc1234",
            dirty=dirty,
            aggregate_metrics={
                "mean_error_px": 10.0,
                "p95_error_px": 15.0,
                "p99_error_px": 20.0,
                "overshoot_events": 3,
                "max_overshoot_px": 6.5,
                "mean_recovery_frames_after_turn": 8.0,
                "mean_settle_frames_after_decel": 11.0,
            },
            delta_metrics=delta_metrics,
        )

    def benchmark_parameters(self):
        return {
            "frame_dt": 1.0 / 60.0,
            "sim_frames": 180,
            "measure_from_frame": 60,
            "max_reticle_speed_pps": 1500.0,
            "stick_max": 32767,
            "overshoot_threshold_px": 2.0,
            "turn_recovery_threshold_px": 6.0,
            "settle_threshold_px": 5.0,
            "settle_consecutive_frames": 4,
        }

    def scenario_logic(self):
        return [
            "steady_turns: 8 scenarios with one or more heading changes and no hard stop",
            "turn_then_decel: 8 scenarios with a turn followed by a deceleration event",
            "decel_resume: 8 scenarios with a deceleration event and optional resume",
        ]

    def test_update_scoreboard_creates_missing_file_with_required_sections(self):
        with TemporaryDirectory() as temp_dir:
            scoreboard_path = Path(temp_dir) / "GAMEPAD_BENCHMARKS.md"
            baseline = self.make_entry(
                run_key="run-baseline",
                artifact_path="artifacts/benchmarks/gamepad/run-baseline.json",
            )

            content = update_scoreboard(
                scoreboard_path=scoreboard_path,
                latest_run=baseline,
                all_runs=(baseline,),
                baseline_key="run-baseline",
                benchmark_parameters=self.benchmark_parameters(),
                scenario_logic=self.scenario_logic(),
            )

            self.assertTrue(scoreboard_path.exists())
            self.assertEqual(content, scoreboard_path.read_text(encoding="utf-8"))
            self.assertIn("# Gamepad Benchmarks", content)
            self.assertIn("## Baseline Definition", content)
            self.assertIn("## Benchmark Parameters", content)
            self.assertIn("## Scenario Logic", content)
            self.assertIn("## Latest Run", content)
            self.assertIn("## History vs Baseline", content)

    def test_baseline_update_records_run_key_and_parameter_snapshot(self):
        with TemporaryDirectory() as temp_dir:
            scoreboard_path = Path(temp_dir) / "GAMEPAD_BENCHMARKS.md"
            baseline = self.make_entry(
                run_key="run-baseline",
                artifact_path="artifacts/benchmarks/gamepad/run-baseline.json",
            )

            content = update_scoreboard(
                scoreboard_path=scoreboard_path,
                latest_run=baseline,
                all_runs=(baseline,),
                baseline_key="run-baseline",
                benchmark_parameters=self.benchmark_parameters(),
                scenario_logic=self.scenario_logic(),
            )

            self.assertEqual(extract_baseline_key(scoreboard_path), "run-baseline")
            self.assertIn("Baseline Run Key: `run-baseline`", content)
            self.assertIn("Artifact: `artifacts/benchmarks/gamepad/run-baseline.json`", content)
            self.assertIn("`frame_dt`: `0.016666666666666666`", content)
            self.assertIn("`settle_consecutive_frames`: `4`", content)

    def test_non_baseline_run_replaces_latest_section_and_appends_history(self):
        with TemporaryDirectory() as temp_dir:
            scoreboard_path = Path(temp_dir) / "GAMEPAD_BENCHMARKS.md"
            baseline = self.make_entry(
                run_key="run-baseline",
                artifact_path="artifacts/benchmarks/gamepad/run-baseline.json",
            )
            current = self.make_entry(
                run_key="run-current",
                artifact_path="artifacts/benchmarks/gamepad/run-current.json",
                timestamp="2026-04-16T13:00:00Z",
                dirty=True,
                delta_metrics={
                    "mean_error_px": 0.15,
                    "p95_error_px": 0.20,
                    "p99_error_px": 0.22,
                    "overshoot_events": -0.33,
                    "max_overshoot_px": 0.05,
                    "mean_recovery_frames_after_turn": -0.25,
                    "mean_settle_frames_after_decel": -0.10,
                },
            )

            update_scoreboard(
                scoreboard_path=scoreboard_path,
                latest_run=baseline,
                all_runs=(baseline,),
                baseline_key="run-baseline",
                benchmark_parameters=self.benchmark_parameters(),
                scenario_logic=self.scenario_logic(),
            )
            content = update_scoreboard(
                scoreboard_path=scoreboard_path,
                latest_run=current,
                all_runs=(baseline, current),
                baseline_key="run-baseline",
                benchmark_parameters=self.benchmark_parameters(),
                scenario_logic=self.scenario_logic(),
            )

            self.assertEqual(extract_baseline_key(scoreboard_path), "run-baseline")
            self.assertIn("Baseline Run Key: `run-baseline`", content)
            self.assertIn("### Latest Run Summary", content)
            self.assertIn("Run Key: `run-current`", content)
            self.assertIn("Artifact: `artifacts/benchmarks/gamepad/run-current.json`", content)
            self.assertIn("| `run-current` | 2026-04-16T13:00:00Z | `artifacts/benchmarks/gamepad/run-current.json` | dirty |", content)
            self.assertNotIn("| `run-baseline` | 2026-04-16T12:00:00Z | `artifacts/benchmarks/gamepad/run-baseline.json` | clean |", content)

    def test_markdown_references_artifact_paths_without_embedding_manifest_payloads(self):
        with TemporaryDirectory() as temp_dir:
            scoreboard_path = Path(temp_dir) / "GAMEPAD_BENCHMARKS.md"
            current = self.make_entry(
                run_key="run-current",
                artifact_path="artifacts/benchmarks/gamepad/run-current.json",
            )

            content = update_scoreboard(
                scoreboard_path=scoreboard_path,
                latest_run=current,
                all_runs=(current,),
                baseline_key=None,
                benchmark_parameters=self.benchmark_parameters(),
                scenario_logic=self.scenario_logic(),
            )

            self.assertIn("Artifact: `artifacts/benchmarks/gamepad/run-current.json`", content)
            self.assertNotIn("scenario_key", content)
            self.assertNotIn("initial_dx", content)
            self.assertNotIn("turn_events", content)

    def test_render_scoreboard_supports_manual_mix_title(self):
        current = self.make_entry(
            run_key="run-current",
            artifact_path="artifacts/benchmarks/gamepad_manual_mix/run-current.json",
        )

        content = render_scoreboard(
            title="Gamepad Manual-Mix Benchmarks",
            latest_run=current,
            all_runs=(current,),
            baseline_key=None,
            benchmark_parameters=self.benchmark_parameters(),
            scenario_logic=self.scenario_logic(),
        )

        self.assertIn("# Gamepad Manual-Mix Benchmarks", content)
        self.assertIn("Artifact: `artifacts/benchmarks/gamepad_manual_mix/run-current.json`", content)


if __name__ == "__main__":
    unittest.main()
