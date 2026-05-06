import importlib
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


if __name__ == "__main__":
    unittest.main()
