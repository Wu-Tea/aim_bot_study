import time
import unittest
from unittest.mock import patch

import numpy as np

from vision.capture import CapturedFrame, ScreenCaptureThread


class _FakeCamera:
    def __init__(self, frames):
        self._frames = list(frames)
        self.started = []
        self.stopped = False

    def start(self, target_fps, video_mode=True):
        self.started.append((target_fps, video_mode))

    def get_latest_frame(self):
        if self._frames:
            return self._frames.pop(0)
        time.sleep(0.001)
        return None

    def stop(self):
        self.stopped = True


class ScreenCaptureThreadTests(unittest.TestCase):
    @patch("vision.capture.win32api.GetSystemMetrics", side_effect=[1920, 1080])
    def test_get_latest_frame_returns_captured_frame_metadata(self, _metrics):
        camera = _FakeCamera([np.zeros((4, 4, 3), dtype=np.uint8)])

        with patch("vision.capture.dxcam.create", return_value=camera):
            thread = ScreenCaptureThread(target_fps=80, crop_width=640, crop_height=512)
            thread.start()
            try:
                captured, last_seen_id = thread.get_latest_frame(timeout=0.2)
            finally:
                thread.stop()
                thread.join(timeout=1.0)

        self.assertIsInstance(captured, CapturedFrame)
        self.assertEqual(last_seen_id, 1)
        self.assertEqual(captured.frame_id, 1)
        self.assertGreater(captured.captured_at, 0.0)
        self.assertEqual(captured.frame.shape, (4, 4, 3))
        self.assertTrue(camera.stopped)

    @patch("vision.capture.win32api.GetSystemMetrics", side_effect=[1920, 1080])
    def test_get_latest_frame_returns_none_when_no_new_frame_arrives_before_timeout(self, _metrics):
        camera = _FakeCamera([np.zeros((2, 2, 3), dtype=np.uint8)])

        with patch("vision.capture.dxcam.create", return_value=camera):
            thread = ScreenCaptureThread(target_fps=80, crop_width=640, crop_height=512)
            thread.start()
            try:
                first, last_seen_id = thread.get_latest_frame(timeout=0.2)
                second, next_seen_id = thread.get_latest_frame(last_seen_id=last_seen_id, timeout=0.01)
            finally:
                thread.stop()
                thread.join(timeout=1.0)

        self.assertIsNotNone(first)
        self.assertIsNone(second)
        self.assertEqual(next_seen_id, last_seen_id)


if __name__ == "__main__":
    unittest.main()
