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


if __name__ == "__main__":
    unittest.main()
