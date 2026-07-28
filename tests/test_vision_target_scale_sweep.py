from __future__ import annotations

import sys
import unittest
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
TOOLS_DIR = PROJECT_ROOT / "tools"
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import benchmark_vision_dataset as bench
import benchmark_vision_target_scale_sweep as sweep


class TargetScaleSweepTests(unittest.TestCase):
    def test_target_centered_crop_is_clamped_without_changing_size(self) -> None:
        target = bench.Box(0, 10, 40, 90)
        crop = sweep.target_centered_crop(640, 640, target, 480, 416)
        self.assertEqual(
            crop,
            bench.CropWindow(0, 0, 480, 416, 640, 640),
        )

    def test_target_centered_crop_rejects_oversized_context(self) -> None:
        target = bench.Box(100, 100, 200, 300)
        self.assertIsNone(sweep.target_centered_crop(320, 320, target, 480, 416))

    def test_summary_reports_paired_gain_and_loss(self) -> None:
        records = [
            {
                "scales": {
                    "1": {"matched": True, "tensor_height": 80},
                    "2": {"matched": False, "tensor_height": 160},
                }
            },
            {
                "scales": {
                    "1": {"matched": False, "tensor_height": 100},
                    "2": {"matched": True, "tensor_height": 200},
                }
            },
        ]
        metrics = sweep.summarize(records, [1.0, 2.0])
        self.assertEqual(metrics["scales"]["1"]["matched"], 1)
        self.assertEqual(metrics["scales"]["2"]["matched"], 1)
        self.assertEqual(metrics["adjacent_transitions"]["1->2"]["gained"], 1)
        self.assertEqual(metrics["adjacent_transitions"]["1->2"]["lost"], 1)
        self.assertEqual(metrics["fully_paired_scales"]["1"]["targets"], 2)
        self.assertEqual(metrics["fully_paired_scales"]["2"]["targets"], 2)
        self.assertEqual(metrics["tensor_height_buckets"]["lt_96"]["targets"], 1)
        self.assertEqual(metrics["tensor_height_buckets"]["160_240"]["targets"], 2)


if __name__ == "__main__":
    unittest.main()
