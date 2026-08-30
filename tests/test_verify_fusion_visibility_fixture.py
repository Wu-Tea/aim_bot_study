import copy
import json
import unittest
from pathlib import Path

from tools.verify_fusion_visibility_fixture import evaluate_fixture


FIXTURE = (
    Path(__file__).parent
    / "fixtures"
    / "fusion_visibility"
    / "mw4_replay_20260830_frames_1333_1342.json"
)


class VerifyFusionVisibilityFixtureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture = json.loads(FIXTURE.read_text(encoding="utf-8"))

    def test_legacy_read_on_render_policy_reproduces_incident(self):
        report = evaluate_fixture(self.fixture, "legacy")

        self.assertEqual(report["status"], "FAIL")
        self.assertEqual(report["metrics"]["confirmed_gap_hidden_frames"], 2)
        self.assertEqual(report["metrics"]["visible_render_frames"], 0)

    def test_event_ingest_and_confirmed_latch_pass_oracles(self):
        report = evaluate_fixture(self.fixture, "candidate")

        self.assertEqual(report["status"], "PASS")
        self.assertEqual(report["metrics"]["confirmed_gap_hidden_frames"], 0)
        self.assertEqual(report["metrics"]["visible_render_frames"], 2)

    def test_generation_change_invalidates_latch(self):
        fixture = copy.deepcopy(self.fixture)
        fixture["frames"][4]["generation"] = 13

        report = evaluate_fixture(fixture, "candidate")

        self.assertEqual(report["status"], "FAIL")
        self.assertFalse(report["metrics"]["render_visibility"]["1337"])

    def test_expired_latch_does_not_bridge_gap(self):
        report = evaluate_fixture(
            self.fixture,
            "candidate",
            hold_ms_override=5.0,
        )

        self.assertEqual(report["status"], "FAIL")
        self.assertEqual(report["metrics"]["confirmed_gap_hidden_frames"], 2)


if __name__ == "__main__":
    unittest.main()
