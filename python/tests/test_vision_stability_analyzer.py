import json
import tempfile
import unittest
from pathlib import Path

from tools import analyze_vision_stability as stability


def _write_jsonl(path: Path, rows: list[dict]) -> None:
    path.write_text(
        "\n".join(json.dumps(row, sort_keys=True) for row in rows) + "\n",
        encoding="utf-8",
    )


class VisionStabilityAnalyzerTests(unittest.TestCase):
    def test_unique_frame_intervals_ignore_repeated_frame_consumption(self):
        rows = [
            {
                "relative_ms": 0.0,
                "aiming": True,
                "frame_updated": True,
                "frame_id": 1,
                "infer_ms": 2.0,
                "gpu_total_ms": 2.5,
                "service_freshness": "fresh",
                "service_source_state": "fresh_frame",
            },
            {
                "relative_ms": 1.0,
                "aiming": True,
                "frame_updated": True,
                "frame_id": 1,
                "infer_ms": 2.0,
                "gpu_total_ms": 2.5,
                "service_freshness": "reused",
                "service_source_state": "repeat_last_frame",
            },
            {
                "relative_ms": 10.0,
                "aiming": True,
                "frame_updated": True,
                "frame_id": 2,
                "infer_ms": 3.0,
                "gpu_total_ms": 3.5,
                "service_freshness": "fresh",
                "service_source_state": "fresh_frame",
            },
        ]
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "native_aim_perf.jsonl"
            _write_jsonl(path, rows)

            summary = stability.analyze_log(path, long_gap_ms=100.0)

        self.assertEqual(summary["updated_frame_rows"], 3)
        self.assertEqual(summary["unique_frame_rows"], 2)
        self.assertEqual(summary["active_interval_ms"]["count"], 1)
        self.assertAlmostEqual(summary["active_interval_ms"]["avg"], 10.0)
        self.assertAlmostEqual(summary["metrics"]["infer_ms"]["avg"], 2.5)
        self.assertEqual(summary["service_freshness"]["fresh"], 2)
        self.assertEqual(summary["service_freshness"]["reused"], 1)
        self.assertEqual(summary["service_source_state"]["repeat_last_frame"], 1)

    def test_long_gap_creates_activation_sample_and_no_update_span(self):
        rows = [
            {"relative_ms": 0.0, "aiming": True, "frame_updated": True, "frame_id": 1, "gpu_total_ms": 2.0},
            {"relative_ms": 50.0, "aiming": True, "frame_updated": False, "frame_id": 0},
            {"relative_ms": 90.0, "aiming": True, "frame_updated": False, "frame_id": 0},
            {
                "relative_ms": 250.0,
                "aiming": True,
                "frame_updated": True,
                "frame_id": 2,
                "gpu_total_ms": 40.0,
                "infer_ms": 39.0,
                "age_ms": 42.0,
            },
        ]
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "native_aim_perf.jsonl"
            _write_jsonl(path, rows)

            summary = stability.analyze_log(path, long_gap_ms=100.0)

        self.assertEqual(summary["long_gap_ms"]["count"], 1)
        self.assertAlmostEqual(summary["long_gap_ms"]["max"], 250.0)
        self.assertEqual(summary["activation"]["count"], 2)
        self.assertAlmostEqual(summary["activation"]["metrics"]["gpu_total_ms"]["max"], 40.0)
        self.assertEqual(summary["no_update_spans"]["count"], 1)
        self.assertEqual(summary["no_update_spans"]["max_rows"], 2)
        self.assertAlmostEqual(summary["no_update_spans"]["max_duration_ms"], 40.0)


if __name__ == "__main__":
    unittest.main()
