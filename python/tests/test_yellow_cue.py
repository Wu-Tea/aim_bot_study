import unittest

import numpy as np

from vision.yellow_cue import ScreenCaptureCueProvider, detect_yellow_cue


class YellowCueDetectionTests(unittest.TestCase):
    def test_detect_yellow_cue_returns_centroid_for_compact_marker(self):
        frame = np.zeros((64, 64, 3), dtype=np.uint8)
        frame[12:16, 20:24] = (255, 255, 0)

        cue = detect_yellow_cue(frame)

        self.assertIsNotNone(cue)
        self.assertTrue(cue["found"])
        self.assertAlmostEqual(cue["x"], 21.5, delta=1.0)
        self.assertAlmostEqual(cue["y"], 13.5, delta=1.0)
        self.assertGreater(cue["score"], 0.0)

    def test_detect_yellow_cue_returns_none_when_frame_has_no_marker(self):
        frame = np.zeros((64, 64, 3), dtype=np.uint8)

        self.assertIsNone(detect_yellow_cue(frame))


class ScreenCaptureCueProviderTests(unittest.TestCase):
    class _FakeFrame:
        def __init__(self, frame):
            self.frame = frame

    class _FakeCaptureThread:
        def __init__(self, frame):
            self.frame = frame
            self.started = False
            self.stopped = False
            self.joined = False
            self._frame_id = 1

        def start(self):
            self.started = True

        def get_latest_frame(self, last_seen_id=0, timeout=0.0):
            if last_seen_id >= self._frame_id:
                return None, last_seen_id
            return ScreenCaptureCueProviderTests._FakeFrame(self.frame), self._frame_id

        def stop(self):
            self.stopped = True

        def join(self, timeout=None):
            self.joined = True

    def test_provider_reads_frame_and_returns_detected_cue(self):
        frame = np.zeros((64, 64, 3), dtype=np.uint8)
        frame[10:14, 30:34] = (255, 255, 0)
        capture_thread = self._FakeCaptureThread(frame)
        provider = ScreenCaptureCueProvider(
            capture_thread=capture_thread,
            detector=detect_yellow_cue,
        )

        cue = provider()

        self.assertTrue(capture_thread.started)
        self.assertIsNotNone(cue)
        self.assertAlmostEqual(cue["x"], 31.5, delta=1.0)
        self.assertAlmostEqual(cue["y"], 11.5, delta=1.0)

        provider.close()
        self.assertTrue(capture_thread.stopped)
        self.assertTrue(capture_thread.joined)


if __name__ == "__main__":
    unittest.main()
