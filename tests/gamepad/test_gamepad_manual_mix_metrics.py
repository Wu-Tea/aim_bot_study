import unittest

from tests.gamepad.benchmark_scenarios import generate_phase1_manifests
from tests.gamepad.manual_mix_inputs import ManualMixInputConfig
from tests.gamepad.manual_mix_metrics import (
    ManualMixAggregateMetrics,
    ManualMixFrameRecord,
    ManualMixMetricsConfig,
    evaluate_manual_mix_run,
    _conflict_frames_ratio,
    _manual_yield_score,
    _wrong_input_recovery_frames,
)


class ManualMixMetricsTests(unittest.TestCase):
    def test_conflict_ratio_counts_only_frames_with_meaningful_opposed_input(self):
        frames = (
            ManualMixFrameRecord(
                frame=0,
                scenario_key="s00",
                manual_seed=1,
                kind="steady_turns",
                error_x=0.0,
                error_y=0.0,
                radial_error_px=12.0,
                manual_x=6000,
                manual_y=0,
                output_x=3000,
                output_y=0,
                ai_x=-3000,
                ai_y=0,
                mode="opposing_burst",
                in_opposing_burst=True,
                measured=True,
            ),
            ManualMixFrameRecord(
                frame=1,
                scenario_key="s00",
                manual_seed=1,
                kind="steady_turns",
                error_x=0.0,
                error_y=0.0,
                radial_error_px=10.0,
                manual_x=0,
                manual_y=0,
                output_x=-3000,
                output_y=0,
                ai_x=-3000,
                ai_y=0,
                mode="aligned_follow",
                in_opposing_burst=False,
                measured=True,
            ),
            ManualMixFrameRecord(
                frame=2,
                scenario_key="s00",
                manual_seed=1,
                kind="steady_turns",
                error_x=0.0,
                error_y=0.0,
                radial_error_px=8.0,
                manual_x=6000,
                manual_y=0,
                output_x=9000,
                output_y=0,
                ai_x=3000,
                ai_y=0,
                mode="aligned_follow",
                in_opposing_burst=False,
                measured=True,
            ),
            ManualMixFrameRecord(
                frame=3,
                scenario_key="s00",
                manual_seed=1,
                kind="steady_turns",
                error_x=0.0,
                error_y=0.0,
                radial_error_px=6.0,
                manual_x=7000,
                manual_y=0,
                output_x=6000,
                output_y=0,
                ai_x=-1000,
                ai_y=0,
                mode="aligned_follow",
                in_opposing_burst=False,
                measured=False,
            ),
        )

        ratio = _conflict_frames_ratio(frames, min_manual=2000, min_ai=2000)
        self.assertAlmostEqual(ratio, 1.0 / 3.0)

    def test_manual_yield_score_is_higher_when_ai_yields_during_opposing_bursts(self):
        frames = (
            ManualMixFrameRecord(
                frame=0,
                scenario_key="s00",
                manual_seed=1,
                kind="steady_turns",
                error_x=0.0,
                error_y=0.0,
                radial_error_px=8.0,
                manual_x=-6000,
                manual_y=0,
                output_x=-5000,
                output_y=0,
                ai_x=1000,
                ai_y=0,
                mode="opposing_burst",
                in_opposing_burst=True,
                measured=True,
            ),
            ManualMixFrameRecord(
                frame=1,
                scenario_key="s00",
                manual_seed=1,
                kind="steady_turns",
                error_x=0.0,
                error_y=0.0,
                radial_error_px=7.5,
                manual_x=-6000,
                manual_y=0,
                output_x=-5500,
                output_y=0,
                ai_x=500,
                ai_y=0,
                mode="opposing_burst",
                in_opposing_burst=True,
                measured=True,
            ),
        )

        score = _manual_yield_score(frames)
        self.assertGreater(score, 0.7)

    def test_wrong_input_recovery_frames_uses_burst_start_and_error_recovery(self):
        frames = (
            ManualMixFrameRecord(0, "s00", 1, "turn_then_decel", 0.0, 0.0, 15.0, -6000, 0, -5000, 0, 1000, 0, "opposing_burst", True, True),
            ManualMixFrameRecord(1, "s00", 1, "turn_then_decel", 0.0, 0.0, 11.0, -6000, 0, -5200, 0, 800, 0, "opposing_burst", True, True),
            ManualMixFrameRecord(2, "s00", 1, "turn_then_decel", 0.0, 0.0, 7.0, 2500, 0, 3200, 0, 700, 0, "overshoot_recover", False, True),
            ManualMixFrameRecord(3, "s00", 1, "turn_then_decel", 0.0, 0.0, 6.0, 2500, 0, 3000, 0, 500, 0, "overshoot_recover", False, True),
            ManualMixFrameRecord(4, "s00", 1, "turn_then_decel", 0.0, 0.0, 5.0, 2500, 0, 2800, 0, 300, 0, "aligned_follow", False, True),
        )

        recovery = _wrong_input_recovery_frames(
            frames,
            threshold_px=8.0,
            consecutive_frames=3,
        )
        self.assertEqual(recovery, 2.0)

    def test_evaluate_manual_mix_run_reuses_phase1_manifests_and_manual_seeds(self):
        manifests = generate_phase1_manifests("mix-suite", 12345)[:2]
        summary = evaluate_manual_mix_run(
            run_key="mix-suite",
            manifests=manifests,
            manual_seeds=(1, 2),
            config=ManualMixMetricsConfig(),
            input_config=ManualMixInputConfig(),
        )

        self.assertEqual(len(summary.scenario_metrics), 4)
        self.assertIsInstance(summary.aggregate, ManualMixAggregateMetrics)
        self.assertEqual(
            {metric.manual_seed for metric in summary.scenario_metrics},
            {1, 2},
        )


if __name__ == "__main__":
    unittest.main()
