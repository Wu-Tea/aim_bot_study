import unittest

from vision.perf import PerformanceTracker


class FakeClock:
    def __init__(self, start=0.0):
        self.now = start

    def __call__(self):
        return self.now


class PerformanceTrackerTests(unittest.TestCase):
    def test_reset_window_discards_idle_time_before_next_log(self):
        clock = FakeClock()
        lines = []
        tracker = PerformanceTracker(
            enabled=True,
            log_interval=1.0,
            clock=clock,
            printer=lines.append,
        )

        clock.now = 12.0
        tracker.reset_window()
        tracker.update(
            wait_ms=1.0,
            preprocess_ms=0.5,
            color_copy_ms=0.25,
            infer_ms=2.0,
            post_ms=3.0,
            boxes_seen=0,
            age_ms=4.0,
            tracking_active=False,
        )

        self.assertEqual(lines, [])

    def test_log_outputs_ads_and_tracking_windows_with_age(self):
        clock = FakeClock()
        lines = []
        tracker = PerformanceTracker(
            enabled=True,
            log_interval=1.0,
            clock=clock,
            printer=lines.append,
        )

        clock.now = 0.4
        tracker.update(
            wait_ms=1.0,
            preprocess_ms=0.5,
            color_copy_ms=0.25,
            infer_ms=2.0,
            post_ms=3.0,
            boxes_seen=0,
            age_ms=4.0,
            tracking_active=False,
        )
        clock.now = 1.2
        tracker.update(
            wait_ms=4.0,
            preprocess_ms=1.5,
            color_copy_ms=0.75,
            infer_ms=5.0,
            post_ms=6.0,
            boxes_seen=2,
            age_ms=8.0,
            tracking_active=True,
        )

        self.assertEqual(len(lines), 2)
        self.assertIn("[Perf][ADS]", lines[0])
        self.assertIn("wait=2.5ms", lines[0])
        self.assertIn("pre=1.0ms", lines[0])
        self.assertIn("copy=0.5ms", lines[0])
        self.assertIn("age=6.0ms", lines[0])
        self.assertIn("boxes=1.0", lines[0])
        self.assertIn("[Perf][TRACK]", lines[1])
        self.assertIn("pre=1.5ms", lines[1])
        self.assertIn("copy=0.8ms", lines[1])
        self.assertIn("age=8.0ms", lines[1])
        self.assertIn("boxes=2.0", lines[1])

    def test_log_outputs_end_to_end_timing_metrics(self):
        clock = FakeClock()
        lines = []
        tracker = PerformanceTracker(
            enabled=True,
            log_interval=1.0,
            clock=clock,
            printer=lines.append,
        )

        clock.now = 1.1
        tracker.update(
            wait_ms=1.0,
            preprocess_ms=0.5,
            color_copy_ms=0.25,
            infer_ms=2.0,
            post_ms=3.0,
            boxes_seen=1,
            age_ms=4.0,
            tracking_active=True,
            source_age_ms=12.0,
            native_pipeline_ms=8.0,
            python_handoff_ms=1.5,
            controller_consume_age_ms=2.0,
            output_age_ms=15.0,
        )

        self.assertEqual(len(lines), 2)
        self.assertIn("src_age=12.0/12.0/12.0ms", lines[0])
        self.assertIn("native=8.0/8.0/8.0ms", lines[0])
        self.assertIn("handoff=1.5/1.5/1.5ms", lines[0])
        self.assertIn("consume=2.0/2.0/2.0ms", lines[0])
        self.assertIn("out_age=15.0/15.0/15.0ms", lines[0])
        self.assertIn("out_age=15.0/15.0/15.0ms", lines[1])

    def test_log_outputs_native_detail_timing_metrics(self):
        clock = FakeClock()
        lines = []
        tracker = PerformanceTracker(
            enabled=True,
            log_interval=1.0,
            clock=clock,
            printer=lines.append,
        )

        clock.now = 1.1
        tracker.update(
            wait_ms=1.0,
            preprocess_ms=0.5,
            color_copy_ms=0.25,
            infer_ms=2.0,
            post_ms=3.0,
            boxes_seen=1,
            age_ms=4.0,
            tracking_active=True,
            native_capture_acquire_ms=0.4,
            native_capture_copy_ms=0.2,
            native_cuda_map_ms=0.3,
            native_output_copy_sync_ms=0.9,
            native_gpu_total_ms=2.4,
            native_output_copy_ms=0.2,
            native_output_wait_ms=0.7,
            native_decode_ms=0.1,
            native_selector_ms=0.6,
            native_cuda_unmap_ms=0.1,
            external_cue_ms=0.7,
        )

        self.assertEqual(len(lines), 2)
        self.assertIn("detail cap_acq=0.4/0.4/0.4ms", lines[0])
        self.assertIn("cap_copy=0.2/0.2/0.2ms", lines[0])
        self.assertIn("cuda_map=0.3/0.3/0.3ms", lines[0])
        self.assertIn("out_sync=0.9/0.9/0.9ms", lines[0])
        self.assertIn("gpu_total=2.4/2.4/2.4ms", lines[0])
        self.assertIn("d2h_copy=0.2/0.2/0.2ms", lines[0])
        self.assertIn("sync_wait=0.7/0.7/0.7ms", lines[0])
        self.assertIn("decode=0.1/0.1/0.1ms", lines[0])
        self.assertIn("selector=0.6/0.6/0.6ms", lines[0])
        self.assertIn("cuda_unmap=0.1/0.1/0.1ms", lines[0])
        self.assertIn("cue_ms=0.7/0.7/0.7ms", lines[0])
        self.assertIn("detail cap_acq=0.4/0.4/0.4ms", lines[1])

    def test_log_outputs_target_authority_mix(self):
        clock = FakeClock()
        lines = []
        tracker = PerformanceTracker(
            enabled=True,
            log_interval=1.0,
            clock=clock,
            printer=lines.append,
        )

        samples = (
            {
                "target_source": "observed",
                "target_tier": "observed_strong",
                "aim_authority": True,
                "fire_authority": True,
                "native_auto_fire_requested": True,
                "auto_fire_active": True,
                "has_external_cue": True,
                "tracking_active": True,
            },
            {
                "target_source": "associated_weak",
                "target_tier": "associated_weak",
                "aim_authority": True,
                "fire_authority": False,
                "native_auto_fire_requested": True,
                "auto_fire_active": False,
                "tracking_active": True,
            },
            {
                "target_source": "cue_hold",
                "target_tier": "cue_hold",
                "aim_authority": True,
                "fire_authority": False,
                "native_auto_fire_requested": False,
                "auto_fire_active": False,
                "has_external_cue": True,
                "tracking_active": True,
            },
            {
                "target_source": "",
                "target_tier": "none",
                "aim_authority": False,
                "fire_authority": False,
                "native_auto_fire_requested": False,
                "auto_fire_active": False,
                "tracking_active": False,
            },
        )

        for index, sample in enumerate(samples):
            clock.now = 0.2 * index
            tracker.update(
                wait_ms=1.0,
                preprocess_ms=0.5,
                color_copy_ms=0.0,
                infer_ms=2.0,
                post_ms=0.1,
                boxes_seen=1,
                age_ms=4.0,
                **sample,
            )

        clock.now = 1.1
        tracker.update(
            wait_ms=1.0,
            preprocess_ms=0.5,
            color_copy_ms=0.0,
            infer_ms=2.0,
            post_ms=0.1,
            boxes_seen=0,
            age_ms=4.0,
            tracking_active=False,
            target_tier="none",
            aim_authority=False,
            fire_authority=False,
            native_auto_fire_requested=False,
            auto_fire_active=False,
        )

        self.assertEqual(len(lines), 2)
        self.assertIn("tier obs=1 weak=1 cue=1 none=2 unk=0", lines[0])
        self.assertIn("auth aim=3 fire=1", lines[0])
        self.assertIn("cue=2", lines[0])
        self.assertIn("fire req=2 ok=1 block=1", lines[0])
        self.assertIn("tier obs=1 weak=1 cue=1 none=0 unk=0", lines[1])
        self.assertIn("fire req=2 ok=1 block=1", lines[1])

    def test_log_outputs_target_box_crop_risk_metrics(self):
        clock = FakeClock()
        lines = []
        tracker = PerformanceTracker(
            enabled=True,
            log_interval=1.0,
            clock=clock,
            printer=lines.append,
        )

        samples = (
            {
                "target_box_width_px": 120.0,
                "target_box_height_px": 180.0,
                "target_edge_margin_px": 80.0,
                "capture_width_px": 640.0,
                "capture_height_px": 512.0,
            },
            {
                "target_box_width_px": 260.0,
                "target_box_height_px": 310.0,
                "target_edge_margin_px": 20.0,
                "capture_width_px": 640.0,
                "capture_height_px": 512.0,
            },
            {
                "target_box_width_px": 96.0,
                "target_box_height_px": 150.0,
                "target_edge_margin_px": 10.0,
                "capture_width_px": 640.0,
                "capture_height_px": 512.0,
            },
        )

        for index, sample in enumerate(samples):
            clock.now = 0.2 * index
            tracker.update(
                wait_ms=1.0,
                preprocess_ms=0.5,
                color_copy_ms=0.0,
                infer_ms=2.0,
                post_ms=0.1,
                boxes_seen=1,
                age_ms=4.0,
                tracking_active=True,
                **sample,
            )

        clock.now = 1.1
        tracker.update(
            wait_ms=1.0,
            preprocess_ms=0.5,
            color_copy_ms=0.0,
            infer_ms=2.0,
            post_ms=0.1,
            boxes_seen=0,
            age_ms=4.0,
            tracking_active=False,
        )

        self.assertEqual(len(lines), 2)
        self.assertIn("box samples=3 near=1 edge=2 near_edge=1", lines[0])
        self.assertIn("box samples=3 near=1 edge=2 near_edge=1", lines[1])


if __name__ == "__main__":
    unittest.main()
