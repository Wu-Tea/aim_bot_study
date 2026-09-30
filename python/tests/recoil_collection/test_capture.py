import importlib
from types import SimpleNamespace
import unittest

import numpy as np

from vision.weapon_identity.models import RecognitionEvent


def _load_capture_module():
    try:
        return importlib.import_module("vision.recoil_collection.capture")
    except ModuleNotFoundError as exc:
        raise AssertionError(f"Missing recoil capture module: {exc}") from exc


class RecoilCapturePhaseCorrelationTests(unittest.TestCase):
    def test_default_collector_config_targets_magazine_curve_profiles(self):
        capture = _load_capture_module()

        self.assertEqual(capture.RecoilCollectorConfig().profile_type, "magazine_curve_v1")

    def test_collect_recoil_profile_defaults_to_magazine_curve_profiles(self):
        capture = _load_capture_module()
        motion_samples = (
            capture.MotionTraceSample(offset_ms=0, x=0.0, y=0.0, center_motion=0.0, manual_marker="start"),
            capture.MotionTraceSample(offset_ms=10, x=0.2, y=-1.0, center_motion=1.0),
            capture.MotionTraceSample(offset_ms=20, x=0.2, y=-1.0, center_motion=0.1),
            capture.MotionTraceSample(offset_ms=30, x=0.4, y=-2.0, center_motion=1.0),
            capture.MotionTraceSample(offset_ms=40, x=0.6, y=-3.0, center_motion=1.0),
            capture.MotionTraceSample(offset_ms=50, x=0.6, y=-3.0, center_motion=0.1),
            capture.MotionTraceSample(offset_ms=60, x=0.8, y=-4.0, center_motion=1.0, manual_marker="stop"),
        )

        def _segmenter(*, session, samples, config):
            del samples
            del config
            return (
                capture.RecoilBurstWindow(
                    burst_id=f"{session.session_id}-mag-001",
                    session_id=session.session_id,
                    start_offset_ms=0,
                    end_offset_ms=70,
                    start_reason="manual",
                    end_reason="manual",
                ),
            )

        result = capture.collect_recoil_profile(
            game="cod22",
            aim_mode="ads",
            standing_only=True,
            recognizer=None,
            weapon_frame_source=None,
            motion_sampler=lambda: motion_samples,
            recognition_event_override=RecognitionEvent(
                game="cod22",
                canonical_weapon_id="cod22-m4",
                confidence=0.95,
                source="test",
                timestamp="2026-05-06T12:00:00Z",
                degraded=False,
            ),
            config=capture.RecoilCollectorConfig(capture_fps=100, min_clean_bursts=1, target_clean_bursts=1),
            timestamp_fn=lambda: "2026-05-06T12:00:00Z",
            segmenter=_segmenter,
        )

        self.assertEqual(result.extracted_profile.profile.profile_type, "magazine_curve_v1")
        self.assertEqual(result.extracted_profile.profile.samples_y, (0.0, -1.0, -1.0, -2.0, -3.0, -3.0, -4.0))
        self.assertEqual(result.extracted_profile.profile.fit_summary["accepted_episode_count"], 1.0)

    def test_estimate_phase_shift_returns_zero_for_identical_blank_frames(self):
        capture = _load_capture_module()
        blank = np.zeros((64, 64), dtype=np.float32)

        delta_x, delta_y = capture._estimate_phase_shift(blank, blank)

        self.assertEqual((delta_x, delta_y), (0.0, 0.0))

    def test_estimate_phase_shift_returns_zero_for_identical_non_zero_flat_frames(self):
        capture = _load_capture_module()
        flat = np.full((64, 64), 17.0, dtype=np.float32)

        delta_x, delta_y = capture._estimate_phase_shift(flat, flat)

        self.assertEqual((delta_x, delta_y), (0.0, 0.0))

    def test_estimate_phase_shift_preserves_known_translation_for_textured_frame(self):
        capture = _load_capture_module()
        base = np.zeros((64, 64), dtype=np.float32)
        base[10:20, 15:25] = 1.0
        base[30:45, 40:50] = 0.6
        transform = np.float32([[1, 0, 4], [0, 1, 3]])
        translated = capture.cv2.warpAffine(
            base,
            transform,
            (64, 64),
            flags=capture.cv2.INTER_LINEAR,
            borderMode=capture.cv2.BORDER_CONSTANT,
            borderValue=0,
        )

        delta_x, delta_y = capture._estimate_phase_shift(base, translated)

        self.assertAlmostEqual(delta_x, 4.0, places=3)
        self.assertAlmostEqual(delta_y, 3.0, places=3)

    def test_static_roi_motion_estimator_ignores_bottom_weapon_animation(self):
        import cv2

        from vision.recoil_collection.motion_estimation import estimate_static_roi_motion

        base = np.zeros((120, 160, 3), dtype=np.uint8)
        cv2.rectangle(base, (40, 25), (120, 70), (255, 255, 255), -1)
        cv2.circle(base, (80, 48), 12, (0, 0, 0), -1)
        shifted = np.roll(base, shift=5, axis=0)
        shifted[90:120, :, :] = 255

        result = estimate_static_roi_motion(base, shifted)

        self.assertAlmostEqual(result.delta_y, 5.0, delta=1.0)
        self.assertGreaterEqual(result.quality, 0.5)

    def test_static_roi_motion_estimator_reports_low_quality_for_blank_frames(self):
        from vision.recoil_collection.motion_estimation import estimate_static_roi_motion

        result = estimate_static_roi_motion(
            np.zeros((120, 160, 3), dtype=np.uint8),
            np.zeros((120, 160, 3), dtype=np.uint8),
        )

        self.assertEqual(result.delta_x, 0.0)
        self.assertEqual(result.delta_y, 0.0)
        self.assertLess(result.quality, 0.5)

    def test_collect_motion_trace_marks_fire_trigger_press_and_release(self):
        capture = _load_capture_module()
        textured = np.zeros((64, 64, 3), dtype=np.uint8)
        textured[8:20, 12:24] = 255
        textured[28:44, 34:50] = 120
        frames = [
            SimpleNamespace(frame=textured.copy(), captured_at=0.00),
            SimpleNamespace(frame=textured.copy(), captured_at=0.10),
            SimpleNamespace(frame=textured.copy(), captured_at=0.20),
            SimpleNamespace(frame=textured.copy(), captured_at=0.30),
        ]

        class _StubCaptureThread:
            def __init__(self, captured_frames):
                self._captured_frames = list(captured_frames)
                self._index = 0

            def get_latest_frame(self, *, last_seen_id, timeout):
                del timeout
                if self._index >= len(self._captured_frames):
                    return None, last_seen_id
                frame = self._captured_frames[self._index]
                self._index += 1
                return frame, self._index

        class _StubFireInputSource:
            def __init__(self, states):
                self._states = list(states)
                self._index = 0

            def is_firing(self):
                if self._index >= len(self._states):
                    return self._states[-1]
                state = self._states[self._index]
                self._index += 1
                return state

        perf_counter_values = iter([0.0, 0.01, 0.02, 0.03, 0.04, 1.10])
        original_perf_counter = capture.time.perf_counter
        capture.time.perf_counter = lambda: next(perf_counter_values)
        try:
            samples = capture._collect_motion_trace_from_thread(
                capture_thread=_StubCaptureThread(frames),
                config=capture.RecoilCollectorConfig(max_capture_seconds=1.0),
                fire_input_source=_StubFireInputSource([False, True, True, False]),
            )
        finally:
            capture.time.perf_counter = original_perf_counter

        self.assertEqual(samples[0].manual_marker, None)
        self.assertEqual(samples[1].manual_marker, "start")
        self.assertEqual(samples[2].manual_marker, None)
        self.assertEqual(samples[3].manual_marker, "stop")


if __name__ == "__main__":
    unittest.main()
