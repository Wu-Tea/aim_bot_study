import tempfile
import unittest
from pathlib import Path

from tools import analyze_vision_stability as stability
from tools import benchmark_vision_gpu_service as bench


class VisionGpuServiceBenchmarkTests(unittest.TestCase):
    def test_worker_keepwarm_improves_activation_and_gap_stability(self):
        result = bench.run_benchmark(
            duration_ms=5000.0,
            controller_hz=100.0,
            active_windows=((1000.0, 2200.0), (3200.0, 4400.0)),
            no_update_windows=((1700.0, 2000.0),),
            strategies=("current_sync_poll", "worker_keepwarm"),
        )

        current = result["strategies"]["current_sync_poll"]["summary"]
        worker = result["strategies"]["worker_keepwarm"]["summary"]

        self.assertGreater(current["activation_gpu_total_ms"]["max"], 40.0)
        self.assertLess(worker["activation_gpu_total_ms"]["max"], current["activation_gpu_total_ms"]["max"])
        self.assertGreater(current["long_gap_ms"]["max"], 500.0)
        self.assertLess(worker["long_gap_ms"]["max"], current["long_gap_ms"]["max"])
        self.assertGreater(worker["active_snapshot_fps"], current["active_snapshot_fps"])
        self.assertGreater(worker["active_reused_source_rows"], 0)
        self.assertLessEqual(worker["active_fresh_source_fps"], worker["active_snapshot_fps"])

    def test_gpu_occupancy_metrics_compare_cost_and_stability(self):
        result = bench.run_benchmark(
            duration_ms=5000.0,
            controller_hz=100.0,
            active_windows=((1000.0, 2200.0), (3200.0, 4400.0)),
            no_update_windows=((1700.0, 2000.0),),
            strategies=("current_sync_poll", "worker_keepwarm", "always_full_rate"),
        )

        current = result["strategies"]["current_sync_poll"]["summary"]
        worker = result["strategies"]["worker_keepwarm"]["summary"]
        full_rate = result["strategies"]["always_full_rate"]["summary"]

        self.assertEqual(current["estimated_gpu_occupancy_pct"]["idle"], 0.0)
        self.assertGreater(worker["estimated_gpu_occupancy_pct"]["idle"], 0.0)
        self.assertGreater(
            full_rate["estimated_gpu_occupancy_pct"]["idle"],
            worker["estimated_gpu_occupancy_pct"]["idle"],
        )
        self.assertGreater(
            worker["estimated_gpu_occupancy_pct"]["active"],
            current["estimated_gpu_occupancy_pct"]["active"],
        )
        self.assertIn("gpu_occupancy_stability", worker)
        self.assertGreater(
            worker["gpu_occupancy_stability"]["active"]["bucket_count"],
            0,
        )
        self.assertGreaterEqual(
            worker["gpu_occupancy_stability"]["active"]["stdev_pct_points"],
            0.0,
        )
        self.assertGreater(
            worker["gpu_efficiency"]["active_snapshot_fps_per_overall_gpu_pct"],
            0.0,
        )
        self.assertGreater(
            worker["gpu_efficiency"]["active_snapshot_fps_per_overall_gpu_pct"],
            full_rate["gpu_efficiency"]["active_snapshot_fps_per_overall_gpu_pct"],
        )

    def test_generated_jsonl_is_compatible_with_stability_analyzer(self):
        result = bench.run_benchmark(
            duration_ms=2500.0,
            controller_hz=100.0,
            active_windows=((500.0, 1500.0),),
            no_update_windows=((900.0, 1100.0),),
            strategies=("worker_keepwarm",),
        )

        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "worker_keepwarm.jsonl"
            bench.write_strategy_jsonl(
                result["strategies"]["worker_keepwarm"]["rows"],
                output,
            )

            summary = stability.analyze_log(output, long_gap_ms=100.0)

        self.assertGreater(summary["rows"], 0)
        self.assertGreater(summary["unique_frame_rows"], 0)
        self.assertGreater(summary["activation"]["count"], 0)
        self.assertIn("old_bgra_copy", summary["preprocess_modes"])


if __name__ == "__main__":
    unittest.main()
