import importlib
from types import SimpleNamespace
import unittest

import numpy as np


def _load_capture_module():
    try:
        return importlib.import_module("vision.recoil_collection.capture")
    except ModuleNotFoundError as exc:
        raise AssertionError(f"Missing recoil capture module: {exc}") from exc


class RecoilCapturePhaseCorrelationTests(unittest.TestCase):
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
