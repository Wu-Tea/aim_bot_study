import unittest

from tools.analyze_fusion_video_replay import (
    center_crop_bounds,
    parse_window,
    summarize_window,
)


def _record(frame_index, marker, detector=True, fps=120.0):
    return {
        "frame_index": frame_index,
        "time_s": frame_index / fps,
        "frame_period_ms": 1000.0 / fps,
        "detector_positive": detector,
        "fusion_marker_current_policy": marker,
        "has_target": marker,
        "enemy_identity_confirmed": marker,
        "max_detection_confidence": 0.8 if detector else 0.0,
    }


class AnalyzeFusionVideoReplayTests(unittest.TestCase):
    def test_center_crop_matches_production_roi_for_1080p(self):
        self.assertEqual(center_crop_bounds(1920, 1080, 640, 512), (640, 284, 1280, 796))

    def test_parse_window(self):
        self.assertEqual(parse_window("early=0.5:2.25"), ("early", 0.5, 2.25))

    def test_summary_reports_internal_marker_gap(self):
        records = [
            _record(0, False),
            _record(1, True),
            _record(2, True),
            _record(3, False),
            _record(4, False),
            _record(5, True),
        ]

        summary = summarize_window(records, "sample", 0.0, 1.0)

        self.assertEqual(summary["direct_observation_frames"], 3)
        self.assertEqual(len(summary["direct_observation_runs"]), 2)
        self.assertEqual(len(summary["internal_direct_observation_gaps"]), 1)
        self.assertAlmostEqual(summary["max_internal_gap_ms"], 16.667, places=3)


if __name__ == "__main__":
    unittest.main()
