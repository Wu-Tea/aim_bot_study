import unittest

from tests.gamepad.benchmark_scenarios import (
    DecelEvent,
    InitialState,
    ScenarioManifest,
    generate_phase1_manifests,
)
from tests.gamepad.manual_mix_inputs import ManualMixInputConfig
from tests.gamepad.manual_mix_metrics import (
    ManualMixAggregateMetrics,
    ManualMixFrameRecord,
    ManualMixMetricsConfig,
    ManualMixScenarioMetrics,
    evaluate_manual_mix_run,
    _aggregate_manual_mix_metrics,
    _aligned_input_preservation_ratio,
    _conflict_frames_ratio,
    _harmful_input_suppression_ratio,
    _lock_survival_rate,
    _manual_yield_score,
    _opposing_burst_hold_error_px,
    _wrong_input_recovery_frames,
)


class ManualMixMetricsTests(unittest.TestCase):
    def test_hard_stop_strafe_stabilizes_quickly_without_reintroducing_overshoot(self):
        manifest = ScenarioManifest(
            scenario_key="strafe-hard-stop",
            kind="decel_resume",
            initial_state=InitialState(
                initial_dx=-26.0,
                initial_dy=5.0,
                initial_speed_px_per_sec=180.0,
                initial_heading_deg=0.0,
            ),
            decel_events=(
                DecelEvent(
                    frame=46,
                    duration_frames=16,
                    target_speed_scale=0.0,
                    hard_stop=True,
                ),
            ),
            resume_events=(),
        )

        summary = evaluate_manual_mix_run(
            run_key="strafe-hard-stop",
            manifests=(manifest,),
            manual_seeds=(1, 2, 3),
            config=ManualMixMetricsConfig(),
            input_config=ManualMixInputConfig(),
        )

        self.assertEqual(summary.aggregate.overshoot_events, 0)
        self.assertIsNotNone(summary.aggregate.mean_settle_frames_after_decel)
        self.assertLessEqual(summary.aggregate.mean_settle_frames_after_decel, 14.0)
        self.assertLessEqual(summary.aggregate.mean_error_px, 1.4)

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
                sanitized_manual_x=6000,
                sanitized_manual_y=0,
                output_x=3000,
                output_y=0,
                ai_x=-3000,
                ai_y=0,
                manual_mode="opposing_burst",
                controller_mode="body_lock",
                in_opposing_burst=True,
                measured=True,
                lock_confidence=0.9,
                helpful_preserved_ratio=1.0,
                harmful_suppressed_ratio=0.5,
                orthogonal_suppressed_ratio=0.0,
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
                sanitized_manual_x=0,
                sanitized_manual_y=0,
                output_x=-3000,
                output_y=0,
                ai_x=-3000,
                ai_y=0,
                manual_mode="aligned_follow",
                controller_mode="ads_snap",
                in_opposing_burst=False,
                measured=True,
                lock_confidence=0.0,
                helpful_preserved_ratio=1.0,
                harmful_suppressed_ratio=0.0,
                orthogonal_suppressed_ratio=0.0,
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
                sanitized_manual_x=6000,
                sanitized_manual_y=0,
                output_x=9000,
                output_y=0,
                ai_x=3000,
                ai_y=0,
                manual_mode="aligned_follow",
                controller_mode="body_lock",
                in_opposing_burst=False,
                measured=True,
                lock_confidence=0.8,
                helpful_preserved_ratio=1.0,
                harmful_suppressed_ratio=0.0,
                orthogonal_suppressed_ratio=0.0,
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
                sanitized_manual_x=7000,
                sanitized_manual_y=0,
                output_x=6000,
                output_y=0,
                ai_x=-1000,
                ai_y=0,
                manual_mode="aligned_follow",
                controller_mode="body_lock",
                in_opposing_burst=False,
                measured=False,
                lock_confidence=0.8,
                helpful_preserved_ratio=1.0,
                harmful_suppressed_ratio=0.0,
                orthogonal_suppressed_ratio=0.0,
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
                sanitized_manual_x=-5000,
                sanitized_manual_y=0,
                output_x=-5000,
                output_y=0,
                ai_x=1000,
                ai_y=0,
                manual_mode="opposing_burst",
                controller_mode="body_lock",
                in_opposing_burst=True,
                measured=True,
                lock_confidence=0.85,
                helpful_preserved_ratio=1.0,
                harmful_suppressed_ratio=0.2,
                orthogonal_suppressed_ratio=0.0,
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
                sanitized_manual_x=-5500,
                sanitized_manual_y=0,
                output_x=-5500,
                output_y=0,
                ai_x=500,
                ai_y=0,
                manual_mode="opposing_burst",
                controller_mode="body_lock",
                in_opposing_burst=True,
                measured=True,
                lock_confidence=0.9,
                helpful_preserved_ratio=1.0,
                harmful_suppressed_ratio=0.1,
                orthogonal_suppressed_ratio=0.0,
            ),
        )

        score = _manual_yield_score(frames)
        self.assertGreater(score, 0.7)

    def test_wrong_input_recovery_frames_uses_burst_start_and_error_recovery(self):
        frames = (
            ManualMixFrameRecord(0, "s00", 1, "turn_then_decel", 0.0, 0.0, 15.0, -6000, 0, -5000, 0, -5000, 0, 1000, 0, "opposing_burst", "body_lock", True, True, 0.9, 1.0, 0.2, 0.0),
            ManualMixFrameRecord(1, "s00", 1, "turn_then_decel", 0.0, 0.0, 11.0, -6000, 0, -5200, 0, -5200, 0, 800, 0, "opposing_burst", "body_lock", True, True, 0.9, 1.0, 0.15, 0.0),
            ManualMixFrameRecord(2, "s00", 1, "turn_then_decel", 0.0, 0.0, 7.0, 2500, 0, 2500, 0, 3200, 0, 700, 0, "overshoot_recover", "body_lock", False, True, 0.85, 1.0, 0.0, 0.0),
            ManualMixFrameRecord(3, "s00", 1, "turn_then_decel", 0.0, 0.0, 6.0, 2500, 0, 2500, 0, 3000, 0, 500, 0, "overshoot_recover", "body_lock", False, True, 0.8, 1.0, 0.0, 0.0),
            ManualMixFrameRecord(4, "s00", 1, "turn_then_decel", 0.0, 0.0, 5.0, 2500, 0, 2500, 0, 2800, 0, 300, 0, "aligned_follow", "body_lock", False, True, 0.8, 1.0, 0.0, 0.0),
        )

        recovery = _wrong_input_recovery_frames(
            frames,
            threshold_px=8.0,
            consecutive_frames=3,
        )
        self.assertEqual(recovery, 2.0)

    def test_harmful_input_suppression_ratio_reflects_removed_opposing_manual_input(self):
        frames = (
            ManualMixFrameRecord(
                frame=0,
                scenario_key="s00",
                manual_seed=1,
                kind="steady_turns",
                error_x=0.0,
                error_y=0.0,
                radial_error_px=9.0,
                manual_x=-6000,
                manual_y=0,
                sanitized_manual_x=-1200,
                sanitized_manual_y=0,
                output_x=4200,
                output_y=0,
                ai_x=5400,
                ai_y=0,
                manual_mode="opposing_burst",
                controller_mode="body_lock",
                in_opposing_burst=True,
                measured=True,
                lock_confidence=0.9,
                helpful_preserved_ratio=1.0,
                harmful_suppressed_ratio=0.8,
                orthogonal_suppressed_ratio=0.0,
            ),
        )

        self.assertAlmostEqual(_harmful_input_suppression_ratio(frames), 0.8)

    def test_aligned_input_preservation_ratio_reflects_surviving_helpful_input(self):
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
                sanitized_manual_x=5400,
                sanitized_manual_y=0,
                output_x=11400,
                output_y=0,
                ai_x=6000,
                ai_y=0,
                manual_mode="aligned_follow",
                controller_mode="body_lock",
                in_opposing_burst=False,
                measured=True,
                lock_confidence=0.9,
                helpful_preserved_ratio=0.9,
                harmful_suppressed_ratio=0.0,
                orthogonal_suppressed_ratio=0.0,
            ),
        )

        self.assertAlmostEqual(_aligned_input_preservation_ratio(frames), 0.9)

    def test_opposing_burst_hold_error_averages_only_measured_burst_frames(self):
        frames = (
            ManualMixFrameRecord(
                frame=0,
                scenario_key="s00",
                manual_seed=1,
                kind="steady_turns",
                error_x=0.0,
                error_y=0.0,
                radial_error_px=9.0,
                manual_x=-6000,
                manual_y=0,
                sanitized_manual_x=-1200,
                sanitized_manual_y=0,
                output_x=4200,
                output_y=0,
                ai_x=5400,
                ai_y=0,
                manual_mode="opposing_burst",
                controller_mode="body_lock",
                in_opposing_burst=True,
                measured=True,
                lock_confidence=0.9,
                helpful_preserved_ratio=1.0,
                harmful_suppressed_ratio=0.8,
                orthogonal_suppressed_ratio=0.0,
            ),
            ManualMixFrameRecord(
                frame=1,
                scenario_key="s00",
                manual_seed=1,
                kind="steady_turns",
                error_x=0.0,
                error_y=0.0,
                radial_error_px=12.0,
                manual_x=-4000,
                manual_y=0,
                sanitized_manual_x=-1000,
                sanitized_manual_y=0,
                output_x=3000,
                output_y=0,
                ai_x=4000,
                ai_y=0,
                manual_mode="opposing_burst",
                controller_mode="body_lock",
                in_opposing_burst=True,
                measured=True,
                lock_confidence=0.8,
                helpful_preserved_ratio=1.0,
                harmful_suppressed_ratio=0.75,
                orthogonal_suppressed_ratio=0.0,
            ),
        )

        self.assertAlmostEqual(_opposing_burst_hold_error_px(frames), 10.5)

    def test_lock_survival_rate_counts_bursts_that_remain_in_body_lock_and_recover(self):
        frames = (
            ManualMixFrameRecord(0, "s00", 1, "turn_then_decel", 0.0, 0.0, 10.0, -5000, 0, -1000, 0, 4000, 0, 5000, 0, "opposing_burst", "body_lock", True, True, 0.9, 1.0, 0.8, 0.0),
            ManualMixFrameRecord(1, "s00", 1, "turn_then_decel", 0.0, 0.0, 9.0, -5000, 0, -800, 0, 4200, 0, 5000, 0, "opposing_burst", "body_lock", True, True, 0.9, 1.0, 0.8, 0.0),
            ManualMixFrameRecord(2, "s00", 1, "turn_then_decel", 0.0, 0.0, 7.0, 2000, 0, 4500, 0, 2500, 0, 500, 0, "overshoot_recover", "body_lock", False, True, 0.8, 0.9, 0.0, 0.0),
        )

        self.assertAlmostEqual(_lock_survival_rate(frames, recovery_threshold_px=8.0), 1.0)

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
        self.assertTrue(hasattr(summary.aggregate, "harmful_input_suppression_ratio"))
        self.assertTrue(hasattr(summary.aggregate, "aligned_input_preservation_ratio"))
        self.assertTrue(hasattr(summary.aggregate, "opposing_burst_hold_error_px"))
        self.assertTrue(hasattr(summary.aggregate, "lock_survival_rate"))
        self.assertTrue(hasattr(summary.aggregate, "turn_recovery_coverage_ratio"))
        self.assertTrue(hasattr(summary.aggregate, "decel_settle_coverage_ratio"))
        self.assertTrue(hasattr(summary.aggregate, "wrong_input_recovery_coverage_ratio"))
        self.assertEqual(
            {metric.manual_seed for metric in summary.scenario_metrics},
            {1, 2},
        )

    def test_manual_mix_aggregate_reports_recovery_coverage_separately_from_mean_frames(self):
        aggregate = _aggregate_manual_mix_metrics(
            (
                ManualMixScenarioMetrics(
                    "s00",
                    1,
                    "steady_turns",
                    1.0,
                    2.0,
                    3.0,
                    0,
                    0.0,
                    5.0,
                    None,
                    0.0,
                    None,
                    None,
                    None,
                    None,
                    None,
                    None,
                ),
                ManualMixScenarioMetrics(
                    "s01",
                    1,
                    "turn_then_decel",
                    1.0,
                    2.0,
                    3.0,
                    0,
                    0.0,
                    None,
                    None,
                    0.0,
                    None,
                    None,
                    None,
                    None,
                    14.0,
                    0.0,
                ),
                ManualMixScenarioMetrics(
                    "s02",
                    1,
                    "turn_then_decel",
                    1.0,
                    2.0,
                    3.0,
                    0,
                    0.0,
                    7.0,
                    12.0,
                    0.0,
                    4.0,
                    None,
                    None,
                    None,
                    10.0,
                    1.0,
                ),
                ManualMixScenarioMetrics(
                    "s03",
                    1,
                    "decel_resume",
                    1.0,
                    2.0,
                    3.0,
                    0,
                    0.0,
                    None,
                    18.0,
                    0.0,
                    6.0,
                    None,
                    None,
                    None,
                    12.0,
                    1.0,
                ),
            )
        )

        self.assertEqual(aggregate.mean_settle_frames_after_decel, 15.0)
        self.assertEqual(aggregate.wrong_input_recovery_frames, 5.0)
        self.assertAlmostEqual(aggregate.turn_recovery_coverage_ratio, 2.0 / 3.0)
        self.assertAlmostEqual(aggregate.decel_settle_coverage_ratio, 2.0 / 3.0)
        self.assertAlmostEqual(aggregate.wrong_input_recovery_coverage_ratio, 2.0 / 3.0)


if __name__ == "__main__":
    unittest.main()
