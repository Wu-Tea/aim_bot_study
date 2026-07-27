import json
import tempfile
import unittest
from pathlib import Path

from tools import compare_vision_benchmark_candidates as compare


def _frame(candidate_id, sample_key, targets, infer_ms):
    return {
        "schema_version": 1,
        "candidate_id": candidate_id,
        "sample_key": sample_key,
        "status": "evaluated",
        "targets": targets,
        "timings": {
            "infer_ms": infer_ms,
            "gpu_total_ms": infer_ms + 0.5,
            "wall_ms": infer_ms + 1.0,
        },
    }


class CompareVisionBenchmarkCandidatesTests(unittest.TestCase):
    def test_compare_pairs_common_and_newly_visible_targets(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            baseline = root / "baseline.jsonl"
            candidate = root / "candidate.jsonl"
            baseline.write_text(
                json.dumps(
                    _frame(
                        "fixed480",
                        "demo/valid/a.jpg",
                        [
                            {"target_index": 0, "matched": True},
                            {"target_index": 1, "matched": False},
                        ],
                        4.0,
                    )
                )
                + "\n",
                encoding="utf-8",
            )
            candidate.write_text(
                json.dumps(
                    _frame(
                        "wide320",
                        "demo/valid/a.jpg",
                        [
                            {"target_index": 0, "matched": True},
                            {"target_index": 2, "matched": True},
                        ],
                        2.0,
                    )
                )
                + "\n",
                encoding="utf-8",
            )

            result = compare.compare_candidates(baseline, candidate)

        self.assertEqual(result["samples"]["common_evaluated"], 1)
        self.assertEqual(result["targets"]["common_visible"]["targets"], 1)
        self.assertEqual(result["targets"]["candidate_only_visible"]["targets"], 1)
        self.assertEqual(result["targets"]["baseline_only_visible"]["targets"], 1)
        self.assertAlmostEqual(result["timings"]["infer_ms"]["p50_delta_percent"], -50.0)


if __name__ == "__main__":
    unittest.main()
