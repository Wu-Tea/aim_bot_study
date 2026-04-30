import unittest

from tests.gamepad.ads_benchmark_scenarios import generate_ads_manifests
from tests.mouse.ads_benchmark_metrics import (
    MouseAdsBenchmarkAggregateMetrics,
    MouseAdsBenchmarkConfig,
    MouseAdsFrameRecord,
    _post_under_20_axis_flip_count,
    _recovery_frame_after,
    _settle_frame_after,
    _under_20_escape_count_after,
    compare_against_gamepad_ads,
    evaluate_mouse_ads_scenario,
)


class MouseAdsBenchmarkMetricsTests(unittest.TestCase):
    def _record(
        self,
        *,
        frame: int,
        engagement_error_px_after: float | None,
        engagement_error_x_after: float | None = None,
        engagement_error_y_after: float | None = None,
    ) -> MouseAdsFrameRecord:
        return MouseAdsFrameRecord(
            frame=frame,
            scenario_key="synthetic",
            family="single_static_offset",
            localized_target_id="engagement",
            controller_mode="acquire_mid",
            engagement_error_px_before=engagement_error_px_after,
            engagement_error_px_after=engagement_error_px_after,
            engagement_error_x_after=engagement_error_x_after,
            engagement_error_y_after=engagement_error_y_after,
            localized_error_px_before=engagement_error_px_after,
            localized_error_px_after=engagement_error_px_after,
            distractor_error_px_before=None,
            distractor_error_px_after=None,
            reticle_delta_px=1.0,
        )

    def test_evaluate_mouse_ads_scenario_reports_mouse_specific_metrics(self):
        manifest = generate_ads_manifests("mouse-metrics", 12345)[0]

        metrics = evaluate_mouse_ads_scenario(
            manifest,
            config=MouseAdsBenchmarkConfig(),
        )

        self.assertEqual(metrics.scenario_key, manifest.scenario_key)
        self.assertEqual(metrics.family, manifest.family)
        self.assertGreater(metrics.max_single_frame_camera_delta, 0.0)
        self.assertIsNotNone(metrics.time_to_stabilize_ms)
        self.assertIsNotNone(metrics.settle_time_after_under_10px_ms)
        self.assertIsNotNone(metrics.under_20_escape_count)
        self.assertIsNotNone(metrics.post_under_20_axis_flip_count)

    def test_under_20_smoothness_helpers_measure_escape_flips_and_settle(self):
        config = MouseAdsBenchmarkConfig()
        records = [
            self._record(frame=0, engagement_error_px_after=30.0, engagement_error_x_after=30.0),
            self._record(frame=1, engagement_error_px_after=18.0, engagement_error_x_after=18.0),
            self._record(frame=2, engagement_error_px_after=16.0, engagement_error_x_after=16.0),
            self._record(frame=3, engagement_error_px_after=24.0, engagement_error_x_after=-24.0),
            self._record(frame=4, engagement_error_px_after=14.0, engagement_error_x_after=14.0),
            self._record(frame=5, engagement_error_px_after=9.0, engagement_error_x_after=-9.0),
            self._record(frame=6, engagement_error_px_after=8.0, engagement_error_x_after=8.0),
            self._record(frame=7, engagement_error_px_after=7.0, engagement_error_x_after=7.0),
        ]

        first_under_20 = _recovery_frame_after(records, start_frame=0, config=config)
        self.assertEqual(first_under_20, 2)
        self.assertEqual(
            _under_20_escape_count_after(records, start_frame=first_under_20, config=config),
            1,
        )
        self.assertEqual(
            _post_under_20_axis_flip_count(records, start_frame=first_under_20),
            4,
        )
        self.assertEqual(
            _settle_frame_after(records, start_frame=first_under_20, config=config),
            7,
        )

    def test_compare_against_gamepad_ads_only_uses_shared_metrics(self):
        mouse = MouseAdsBenchmarkAggregateMetrics(
            wrong_target_snap_rate=0.5,
            max_single_frame_camera_delta=7.5,
            target_localization_latency_ms=0.0,
            time_to_under_20px=320.0,
            time_to_stabilize_ms=280.0,
            reacquire_time_after_occlusion=240.0,
            settle_time_after_under_10px_ms=120.0,
            under_20_escape_count=1.0,
            post_under_20_axis_flip_count=2.0,
        )
        gamepad = {
            "wrong_target_snap_rate": 0.25,
            "max_single_frame_camera_delta": 20.0,
            "target_localization_latency_ms": 0.0,
            "time_to_under_20px": 80.0,
            "time_to_body_lock": 60.0,
            "reacquire_time_after_occlusion": 30.0,
        }

        comparison = compare_against_gamepad_ads(mouse, gamepad)

        self.assertEqual(
            comparison["shared_metrics"],
            [
                "wrong_target_snap_rate",
                "max_single_frame_camera_delta",
                "target_localization_latency_ms",
                "time_to_under_20px",
                "reacquire_time_after_occlusion",
            ],
        )
        self.assertNotIn("time_to_stabilize_ms", comparison["metric_deltas"])
        self.assertIn("settle_time_after_under_10px_ms", comparison["mouse_only_metrics"])
        self.assertIn("under_20_escape_count", comparison["mouse_only_metrics"])
        self.assertIn("post_under_20_axis_flip_count", comparison["mouse_only_metrics"])
        self.assertAlmostEqual(
            comparison["metric_deltas"]["time_to_under_20px"],
            3.0,
        )


if __name__ == "__main__":
    unittest.main()
