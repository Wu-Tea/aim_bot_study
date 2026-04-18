import unittest

from controllers.gamepad.ai_aim import AIAimConfig

from tests.gamepad.ads_benchmark_metrics import (
    AdsBenchmarkAggregateMetrics,
    AdsBenchmarkConfig,
    AdsBenchmarkMetricDeltas,
    AdsBenchmarkRunSummary,
    AdsBenchmarkScenarioMetrics,
    DEFAULT_ADS_INPUT_PROFILES,
    evaluate_ads_run,
    evaluate_ads_scenario,
)
from tests.gamepad.ads_benchmark_scenarios import (
    AdsLocalizationEvent,
    AdsScenarioManifest,
    AdsTargetSpec,
    AdsVisibilityGap,
)


class _LocalizedResponsePlugin:
    def __init__(self):
        self.config = AIAimConfig()
        self.reset()

    def reset(self):
        self._mode = "manual"
        self.ai_stick_x = 0.0
        self.ai_stick_y = 0.0
        self._last_lock_confidence = 0.0
        self._last_sanitized_manual_x = 0.0
        self._last_sanitized_manual_y = 0.0
        self._last_helpful_preserved_ratio = 1.0
        self._last_harmful_suppressed_ratio = 0.0
        self._last_orthogonal_suppressed_ratio = 0.0

    def apply(self, frame, output):
        self._last_sanitized_manual_x = float(frame.manual_right_x)
        self._last_sanitized_manual_y = float(frame.manual_right_y)
        if not frame.is_aiming or frame.target is None:
            self._mode = "manual"
            self.ai_stick_x = 0.0
            self.ai_stick_y = 0.0
            output.right_x = frame.manual_right_x
            output.right_y = frame.manual_right_y
            return

        desired_x = max(-32767, min(32767, round((frame.target_dx / 90.0) * 32767 * 0.65)))
        desired_y = max(-32767, min(32767, round((-frame.target_dy / 80.0) * 32767 * 0.65)))
        self.ai_stick_x = float(desired_x)
        self.ai_stick_y = float(desired_y)
        output.right_x = frame.manual_right_x + desired_x
        output.right_y = frame.manual_right_y + desired_y
        if abs(frame.target_dx) <= 18.0 and abs(frame.target_dy) <= 18.0:
            self._mode = "body_lock"
            self._last_lock_confidence = 0.9
            self._last_harmful_suppressed_ratio = 0.6
        else:
            self._mode = "ads_snap"
            self._last_lock_confidence = 0.1


def _plugin_factory():
    return _LocalizedResponsePlugin()


class AdsBenchmarkMetricsContractTests(unittest.TestCase):
    def test_metric_dataclasses_expose_expected_fields(self):
        self.assertEqual(
            list(AdsBenchmarkConfig.__dataclass_fields__),
            [
                "frame_dt",
                "target_sample_hz",
                "sim_frames",
                "max_reticle_speed_pps",
                "stick_max",
                "response_delta_threshold_px",
                "response_improvement_threshold_px",
                "under_target_threshold_px",
                "under_target_consecutive_frames",
                "lock_loss_window_frames",
                "lock_loss_grace_frames",
                "wrong_target_margin_px",
            ],
        )
        self.assertIn("wrong_target_snap_rate", AdsBenchmarkScenarioMetrics.__dataclass_fields__)
        self.assertIn("max_single_frame_camera_delta", AdsBenchmarkAggregateMetrics.__dataclass_fields__)
        self.assertIn("wrong_input_recovery_after_ads_frames", AdsBenchmarkMetricDeltas.__dataclass_fields__)
        self.assertEqual(DEFAULT_ADS_INPUT_PROFILES, ("none", "aligned_follow", "opposing_burst", "overshoot_recover"))

    def test_default_config_matches_ads_spec(self):
        config = AdsBenchmarkConfig()

        self.assertEqual(config.frame_dt, 1.0 / 60.0)
        self.assertIsNone(config.target_sample_hz)
        self.assertEqual(config.sim_frames, 90)
        self.assertEqual(config.max_reticle_speed_pps, 1500.0)
        self.assertEqual(config.stick_max, 32767)
        self.assertEqual(config.response_delta_threshold_px, 1.0)
        self.assertEqual(config.response_improvement_threshold_px, 0.5)
        self.assertEqual(config.under_target_threshold_px, 20.0)
        self.assertEqual(config.under_target_consecutive_frames, 2)
        self.assertEqual(config.lock_loss_window_frames, 12)
        self.assertEqual(config.lock_loss_grace_frames, 2)
        self.assertEqual(config.wrong_target_margin_px, 2.0)


class AdsBenchmarkScenarioEvaluationTests(unittest.TestCase):
    def test_evaluate_static_scenario_reports_core_metrics(self):
        manifest = AdsScenarioManifest(
            scenario_key="ads-run-s00",
            family="single_static_offset",
            engagement_target_id="engagement",
            targets=(
                AdsTargetSpec(
                    target_id="engagement",
                    initial_dx=80.0,
                    initial_dy=-24.0,
                    velocity_x=0.0,
                    velocity_y=0.0,
                ),
            ),
        )

        metrics = evaluate_ads_scenario(
            manifest,
            input_profile="aligned_follow",
            plugin_factory=_plugin_factory,
            config=AdsBenchmarkConfig(sim_frames=24),
        )

        self.assertEqual(metrics.scenario_key, "ads-run-s00")
        self.assertEqual(metrics.input_profile, "aligned_follow")
        self.assertEqual(metrics.family, "single_static_offset")
        self.assertIsNone(metrics.wrong_target_snap_rate)
        self.assertGreater(metrics.max_single_frame_camera_delta, 0.0)
        self.assertIsNotNone(metrics.target_localization_latency_ms)
        self.assertIsNotNone(metrics.time_to_under_20px)
        self.assertIsNotNone(metrics.time_to_body_lock)
        self.assertIsNone(metrics.reacquire_time_after_occlusion)

    def test_dual_target_scenario_can_detect_wrong_target_snap(self):
        manifest = AdsScenarioManifest(
            scenario_key="ads-run-s01",
            family="dual_target_disambiguation",
            engagement_target_id="engagement",
            targets=(
                AdsTargetSpec(
                    target_id="engagement",
                    initial_dx=70.0,
                    initial_dy=12.0,
                    velocity_x=0.0,
                    velocity_y=0.0,
                ),
                AdsTargetSpec(
                    target_id="distractor",
                    initial_dx=102.0,
                    initial_dy=12.0,
                    velocity_x=0.0,
                    velocity_y=0.0,
                ),
            ),
            localization_schedule=(
                AdsLocalizationEvent(frame=0, target_id="distractor"),
                AdsLocalizationEvent(frame=4, target_id="engagement"),
            ),
        )

        metrics = evaluate_ads_scenario(
            manifest,
            input_profile="none",
            plugin_factory=_plugin_factory,
            config=AdsBenchmarkConfig(sim_frames=20),
        )

        self.assertEqual(metrics.wrong_target_snap_rate, 1.0)

    def test_reacquire_scenario_reports_reacquire_time(self):
        manifest = AdsScenarioManifest(
            scenario_key="ads-run-s02",
            family="reacquire_after_gap",
            engagement_target_id="engagement",
            targets=(
                AdsTargetSpec(
                    target_id="engagement",
                    initial_dx=60.0,
                    initial_dy=-16.0,
                    velocity_x=180.0,
                    velocity_y=0.0,
                    gap_windows=(AdsVisibilityGap(start_frame=4, duration_frames=4),),
                ),
            ),
        )

        metrics = evaluate_ads_scenario(
            manifest,
            input_profile="none",
            plugin_factory=_plugin_factory,
            config=AdsBenchmarkConfig(sim_frames=24),
        )

        self.assertIsNotNone(metrics.reacquire_time_after_occlusion)

    def test_evaluate_run_aggregates_non_applicable_metrics_over_non_null_values_only(self):
        manifests = (
            AdsScenarioManifest(
                scenario_key="ads-run-s10",
                family="single_static_offset",
                engagement_target_id="engagement",
                targets=(
                    AdsTargetSpec(
                        target_id="engagement",
                        initial_dx=80.0,
                        initial_dy=0.0,
                        velocity_x=0.0,
                        velocity_y=0.0,
                    ),
                ),
            ),
            AdsScenarioManifest(
                scenario_key="ads-run-s11",
                family="dual_target_disambiguation",
                engagement_target_id="engagement",
                targets=(
                    AdsTargetSpec(
                        target_id="engagement",
                        initial_dx=60.0,
                        initial_dy=0.0,
                        velocity_x=0.0,
                        velocity_y=0.0,
                    ),
                    AdsTargetSpec(
                        target_id="distractor",
                        initial_dx=96.0,
                        initial_dy=0.0,
                        velocity_x=0.0,
                        velocity_y=0.0,
                    ),
                ),
                localization_schedule=(AdsLocalizationEvent(frame=0, target_id="distractor"),),
            ),
        )

        summary = evaluate_ads_run(
            "ads-run",
            manifests,
            input_profiles=("none",),
            plugin_factory=_plugin_factory,
            config=AdsBenchmarkConfig(sim_frames=18),
        )

        self.assertIsInstance(summary, AdsBenchmarkRunSummary)
        self.assertEqual(len(summary.scenario_metrics), 2)
        self.assertEqual(summary.aggregate.wrong_target_snap_rate, 1.0)
        self.assertIsNone(summary.scenario_metrics[0].wrong_target_snap_rate)
        self.assertEqual(summary.scenario_metrics[1].wrong_target_snap_rate, 1.0)


if __name__ == "__main__":
    unittest.main()
