from __future__ import annotations

import sys
import unittest
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
TOOLS_DIR = PROJECT_ROOT / "tools"
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import benchmark_vision_viewport_sweep as sweep


class ViewportSweepTests(unittest.TestCase):
    def setUp(self) -> None:
        self.viewports = [
            sweep.ViewportSpec("precision", 360, 312),
            sweep.ViewportSpec("normal", 480, 416),
            sweep.ViewportSpec("rescue", 600, 520),
        ]

    def test_parse_and_validate_production_viewports(self) -> None:
        parsed = sweep.parse_viewport_spec("precision=360x312")
        self.assertEqual(parsed, self.viewports[0])
        sweep.validate_viewports(self.viewports, 480, 416)

    def test_validate_rejects_aspect_distortion(self) -> None:
        with self.assertRaisesRegex(ValueError, "does not match tensor aspect"):
            sweep.validate_viewports(
                [
                    sweep.ViewportSpec("bad", 360, 300),
                    sweep.ViewportSpec("rescue", 600, 520),
                ],
                480,
                416,
            )

    def test_summary_separates_pure_scale_recovery_from_clipping(self) -> None:
        def viewport(matched: bool, visible: float = 1.0) -> dict[str, object]:
            return {"matched": matched, "visible_fraction": visible}

        records = [
            {
                "cohort": "fully_visible_all",
                "viewports": {
                    "precision": viewport(False),
                    "normal": viewport(True),
                    "rescue": viewport(True),
                },
            },
            {
                "cohort": "precision_clipped",
                "viewports": {
                    "precision": viewport(False, 0.5),
                    "normal": viewport(False, 0.8),
                    "rescue": viewport(True),
                },
            },
            {
                "cohort": "fully_visible_all",
                "viewports": {
                    "precision": viewport(True),
                    "normal": viewport(True),
                    "rescue": viewport(False),
                },
            },
        ]
        metrics = sweep.summarize_records(records, self.viewports)
        rescue = metrics["transitions_from_smallest"]["rescue"]
        self.assertEqual(rescue["all_recovered"], 2)
        self.assertEqual(rescue["fully_visible_recovered"], 1)
        self.assertEqual(rescue["all_regressed"], 1)
        self.assertEqual(rescue["fully_visible_regressed"], 1)
        self.assertAlmostEqual(
            metrics["cohorts"]["fully_visible_all"]["rescue"]["recall"],
            0.5,
        )


if __name__ == "__main__":
    unittest.main()
