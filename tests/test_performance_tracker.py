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


if __name__ == "__main__":
    unittest.main()
