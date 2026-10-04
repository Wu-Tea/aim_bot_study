import unittest
from collections import Counter

from tools.training.bf6_specialist import group_records, group_split, schedule
from tools.training.compare_bf6_specialists import gates


class Bf6IsolationTests(unittest.TestCase):
    def test_recall_gain_cannot_waive_precision_or_roi_regression(self):
        baseline = {f"{domain}@{conf}": dict(precision=.8, recall=.3)
                    for domain in ["bf6_full", "bf6_center"] for conf in [.4, .65]}
        candidate = {key:dict(precision=.85, recall=.7) for key in baseline}
        self.assertTrue(all(gates(baseline, candidate).values()))
        candidate["bf6_center@0.65"]["precision"] = .79
        self.assertFalse(all(gates(baseline, candidate).values()))
        candidate["bf6_center@0.65"]["precision"] = .85
        candidate["bf6_full@0.4"]["recall"] = .29
        self.assertFalse(all(gates(baseline, candidate).values()))

    def test_transitive_similarity_and_source_test_precedence(self):
        records = [
            dict(original_id="a", sha256="a", phash=0, split="train"),
            dict(original_id="b", sha256="b", phash=0b111111, split="valid"),
            dict(original_id="c", sha256="c", phash=0b111111111111, split="test"),
            dict(original_id="d", sha256="d", phash=(1 << 64)-1, split="train"),
        ]
        groups = group_records(records)
        self.assertEqual(sorted(map(len, groups)), [1, 3])
        heldout = next(g for g in groups if len(g) == 3)
        self.assertEqual(group_split(heldout), "test")
        self.assertNotIn("a", [r["original_id"] for g in groups
                               if group_split(g) == "train" for r in g])

    def test_same_original_image_cannot_leak_across_exports(self):
        records = [
            dict(original_id="same", sha256="different_jpeg_a", phash=0, split="train"),
            dict(original_id="same", sha256="different_jpeg_b", phash=(1 << 64)-1, split="test"),
        ]
        groups = group_records(records)
        self.assertEqual(len(groups), 1)
        self.assertEqual(group_split(groups[0]), "test")

    def test_weight_changes_exposure_with_identical_step_budget_and_full_pools(self):
        balanced = schedule(100, 100, 1000, .5, 17)
        weighted = schedule(100, 100, 1000, .8, 17)
        self.assertEqual(len(balanced), len(weighted))
        self.assertEqual(Counter(domain for domain, _ in balanced), {"bf6": 500, "replay": 500})
        self.assertEqual(Counter(domain for domain, _ in weighted), {"bf6": 800, "replay": 200})
        for entries in [balanced, weighted]:
            for domain in ["bf6", "replay"]:
                self.assertEqual({i for d, i in entries if d == domain}, set(range(100)))
        self.assertEqual(weighted, schedule(100, 100, 1000, .8, 17))


if __name__ == "__main__":
    unittest.main()
