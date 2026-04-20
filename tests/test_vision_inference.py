import threading
import time
import unittest
from types import SimpleNamespace
from unittest.mock import patch

import numpy as np

from vision.capture import CapturedFrame
from vision.inference import InferenceResult, InferenceThread


class _FakeCaptureThread:
    def __init__(self):
        self.running = True
        self._condition = threading.Condition()
        self._latest_frame = None

    def publish(self, captured_frame):
        with self._condition:
            self._latest_frame = captured_frame
            self._condition.notify_all()

    def get_latest_frame(self, last_seen_id: int = 0, timeout: float = 0.1):
        deadline = time.perf_counter() + timeout
        with self._condition:
            while self.running and (self._latest_frame is None or self._latest_frame.frame_id <= last_seen_id):
                remaining = deadline - time.perf_counter()
                if remaining <= 0:
                    return None, last_seen_id
                self._condition.wait(timeout=remaining)

            if self._latest_frame is None or self._latest_frame.frame_id <= last_seen_id:
                return None, last_seen_id

            return self._latest_frame, self._latest_frame.frame_id

    def stop(self):
        self.running = False
        with self._condition:
            self._condition.notify_all()


class _WaitingCaptureThread(_FakeCaptureThread):
    def __init__(self):
        super().__init__()
        self.waiting = threading.Event()

    def get_latest_frame(self, last_seen_id: int = 0, timeout: float = 0.1):
        deadline = time.perf_counter() + timeout
        with self._condition:
            while self.running and (self._latest_frame is None or self._latest_frame.frame_id <= last_seen_id):
                self.waiting.set()
                remaining = deadline - time.perf_counter()
                if remaining <= 0:
                    return None, last_seen_id
                self._condition.wait(timeout=remaining)

            if self._latest_frame is None or self._latest_frame.frame_id <= last_seen_id:
                return None, last_seen_id

            return self._latest_frame, self._latest_frame.frame_id


class InferenceThreadTests(unittest.TestCase):
    def test_fast_path_receives_captured_frame_as_prediction_source(self):
        capture = _FakeCaptureThread()
        observed_sources = []
        fast_path = object()

        def fake_fast_predict(active_fast_path, frame_source):
            observed_sources.append((active_fast_path, frame_source))
            return ["ok"]

        thread = InferenceThread(
            capture_thread=capture,
            frame_timeout=0.01,
            fast_path=fast_path,
            use_fast_path=True,
        )
        with patch("vision.fastpath._fast_predict", side_effect=fake_fast_predict):
            thread.start()
            thread.resume()
            try:
                captured = CapturedFrame(
                    frame_id=1,
                    captured_at=9.0,
                    frame=np.full((2, 2, 3), 7, dtype=np.uint8),
                )
                capture.publish(captured)
                result, _ = thread.get_latest_result(timeout=0.2)
            finally:
                thread.stop()
                capture.stop()
                thread.join(timeout=1.0)

        self.assertEqual(result.detections, ["ok"])
        self.assertEqual(len(observed_sources), 1)
        self.assertIs(observed_sources[0][0], fast_path)
        self.assertIs(observed_sources[0][1], captured)

    def test_get_latest_result_returns_newest_processed_frame(self):
        capture = _FakeCaptureThread()

        def predict_fn(frame):
            return [int(frame[0, 0, 0])]

        thread = InferenceThread(
            capture_thread=capture,
            frame_timeout=0.01,
            predict_fn=predict_fn,
        )
        thread.start()
        thread.resume()
        try:
            capture.publish(
                CapturedFrame(
                    frame_id=1,
                    captured_at=10.0,
                    frame=np.full((2, 2, 3), 1, dtype=np.uint8),
                )
            )
            result, last_seen_id = thread.get_latest_result(timeout=0.2)
        finally:
            thread.stop()
            capture.stop()
            thread.join(timeout=1.0)

        self.assertIsInstance(result, InferenceResult)
        self.assertEqual(last_seen_id, 1)
        self.assertEqual(result.frame_id, 1)
        self.assertEqual(result.captured_at, 10.0)
        self.assertEqual(result.detections, [1])
        self.assertGreaterEqual(result.infer_ms, 0.0)

    def test_pause_clears_stale_result_and_resume_requires_a_new_frame(self):
        capture = _FakeCaptureThread()

        def predict_fn(frame):
            return [int(frame[0, 0, 0])]

        thread = InferenceThread(
            capture_thread=capture,
            frame_timeout=0.01,
            predict_fn=predict_fn,
        )
        thread.start()
        thread.resume()
        try:
            capture.publish(
                CapturedFrame(
                    frame_id=1,
                    captured_at=20.0,
                    frame=np.full((2, 2, 3), 1, dtype=np.uint8),
                )
            )
            first, _ = thread.get_latest_result(timeout=0.2)

            thread.pause(clear_result=True)
            stale, stale_seen_id = thread.get_latest_result(last_seen_id=0, timeout=0.02)

            capture.publish(
                CapturedFrame(
                    frame_id=2,
                    captured_at=21.0,
                    frame=np.full((2, 2, 3), 2, dtype=np.uint8),
                )
            )
            thread.resume()
            second, next_seen_id = thread.get_latest_result(last_seen_id=0, timeout=0.2)
        finally:
            thread.stop()
            capture.stop()
            thread.join(timeout=1.0)

        self.assertEqual(first.frame_id, 1)
        self.assertIsNone(stale)
        self.assertEqual(stale_seen_id, 0)
        self.assertEqual(next_seen_id, 2)
        self.assertEqual(second.frame_id, 2)
        self.assertEqual(second.detections, [2])

    def test_pause_while_waiting_prevents_new_inference_until_resume(self):
        capture = _WaitingCaptureThread()
        predict_started = threading.Event()

        def predict_fn(frame):
            predict_started.set()
            return [int(frame[0, 0, 0])]

        thread = InferenceThread(
            capture_thread=capture,
            frame_timeout=0.05,
            predict_fn=predict_fn,
        )
        thread.start()
        thread.resume()
        try:
            self.assertTrue(capture.waiting.wait(timeout=0.2))

            thread.pause(clear_result=True)
            capture.publish(
                CapturedFrame(
                    frame_id=1,
                    captured_at=30.0,
                    frame=np.full((2, 2, 3), 1, dtype=np.uint8),
                )
            )

            time.sleep(0.03)
            self.assertFalse(predict_started.is_set())

            thread.resume()
            result, last_seen_id = thread.get_latest_result(timeout=0.2)
        finally:
            thread.stop()
            capture.stop()
            thread.join(timeout=1.0)

        self.assertEqual(last_seen_id, 1)
        self.assertEqual(result.frame_id, 1)
        self.assertEqual(result.detections, [1])

    def test_prediction_failure_surfaces_through_get_latest_result(self):
        capture = _FakeCaptureThread()

        def predict_fn(frame):
            raise RuntimeError("predict boom")

        thread = InferenceThread(
            capture_thread=capture,
            frame_timeout=0.01,
            predict_fn=predict_fn,
        )
        thread.start()
        thread.resume()
        try:
            capture.publish(
                CapturedFrame(
                    frame_id=1,
                    captured_at=40.0,
                    frame=np.full((2, 2, 3), 1, dtype=np.uint8),
                )
            )

            with self.assertRaisesRegex(RuntimeError, "InferenceThread prediction failed"):
                thread.get_latest_result(timeout=0.2)
        finally:
            thread.stop()
            capture.stop()
            thread.join(timeout=1.0)

        self.assertFalse(thread.running)

    def test_fast_path_skips_lazy_frame_materialization_for_empty_detection_results(self):
        capture = _FakeCaptureThread()
        release_calls = []
        load_calls = []
        fast_path = object()

        thread = InferenceThread(
            capture_thread=capture,
            frame_timeout=0.01,
            fast_path=fast_path,
            use_fast_path=True,
            require_result_frame=False,
        )
        with patch("vision.fastpath._fast_predict", return_value=[]):
            thread.start()
            thread.resume()
            try:
                capture.publish(
                    CapturedFrame(
                        frame_id=1,
                        captured_at=9.0,
                        frame=None,
                        native_frame={"slot_index": 2},
                        frame_loader=lambda: load_calls.append("load") or np.ones((2, 2, 3), dtype=np.uint8),
                        frame_release=lambda: release_calls.append("release"),
                    )
                )
                result, _ = thread.get_latest_result(timeout=0.2)
            finally:
                thread.stop()
                capture.stop()
                thread.join(timeout=1.0)

        self.assertEqual(result.detections, [])
        self.assertIsNone(result.frame)
        self.assertEqual(load_calls, [])
        self.assertEqual(release_calls, ["release"])

    def test_fast_path_prefetches_roi_frame_when_detections_need_post_processing(self):
        capture = _FakeCaptureThread()
        release_calls = []
        load_calls = []
        roi_calls = []
        fast_path = object()
        detections = [
            SimpleNamespace(
                boxes=np.array([[10.0, 20.0, 30.0, 40.0]], dtype=np.float32),
                confs=np.array([0.95], dtype=np.float32),
            )
        ]

        thread = InferenceThread(
            capture_thread=capture,
            frame_timeout=0.01,
            fast_path=fast_path,
            use_fast_path=True,
            require_result_frame=False,
        )
        with patch("vision.fastpath._fast_predict", return_value=detections):
            thread.start()
            thread.resume()
            try:
                capture.publish(
                    CapturedFrame(
                        frame_id=1,
                        captured_at=11.0,
                        frame=None,
                        native_frame={"slot_index": 4},
                        frame_shape=(64, 64, 3),
                        frame_loader=lambda: load_calls.append("load") or np.full((2, 2, 3), 7, dtype=np.uint8),
                        roi_loader=lambda left, top, right, bottom: roi_calls.append((left, top, right, bottom))
                        or np.full((bottom - top, right - left, 3), 5, dtype=np.uint8),
                        frame_release=lambda: release_calls.append("release"),
                    )
                )
                result, _ = thread.get_latest_result(timeout=0.2)
            finally:
                thread.stop()
                capture.stop()
                thread.join(timeout=1.0)

        self.assertEqual(result.detections, detections)
        self.assertIsNotNone(result.frame)
        self.assertEqual(result.frame.shape, (64, 64, 3))
        np.testing.assert_array_equal(
            result.frame.get_roi_rgb(8, 6, 32, 18),
            np.full((12, 24, 3), 5, dtype=np.uint8),
        )
        self.assertEqual(load_calls, [])
        self.assertEqual(roi_calls, [(8, 6, 32, 18)])
        self.assertEqual(release_calls, ["release"])


if __name__ == "__main__":
    unittest.main()
