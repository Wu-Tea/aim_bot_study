import unittest

import numpy as np

from tests.test_native_vision_targeting_bridge import _load_native_module


class NativeVisionImageOpsBridgeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.module = _load_native_module()

    def test_native_gray_helpers_are_exposed(self):
        self.assertTrue(hasattr(self.module, "grayscale_rgb"))
        self.assertTrue(hasattr(self.module, "downsample_grayscale"))

    def test_grayscale_rgb_returns_expected_shape_and_luma(self):
        if not hasattr(self.module, "grayscale_rgb"):
            self.fail("grayscale_rgb is missing")

        frame = np.array(
            [
                [[255, 0, 0], [0, 255, 0]],
                [[0, 0, 255], [255, 255, 255]],
            ],
            dtype=np.uint8,
        )

        gray = self.module.grayscale_rgb(frame)

        self.assertEqual(gray.shape, (2, 2))
        self.assertEqual(gray.dtype, np.uint8)
        self.assertEqual(int(gray[0, 0]), (255 * 77) >> 8)
        self.assertEqual(int(gray[0, 1]), (255 * 150) >> 8)
        self.assertEqual(int(gray[1, 0]), (255 * 29) >> 8)
        self.assertEqual(int(gray[1, 1]), ((255 * 77) + (255 * 150) + (255 * 29)) >> 8)

    def test_downsample_grayscale_keeps_top_left_samples(self):
        if not hasattr(self.module, "downsample_grayscale"):
            self.fail("downsample_grayscale is missing")

        frame = np.arange(16, dtype=np.uint8).reshape((4, 4))

        downsampled = self.module.downsample_grayscale(frame, 2)

        self.assertEqual(downsampled.shape, (2, 2))
        self.assertEqual(downsampled.dtype, np.uint8)
        self.assertEqual(int(downsampled[0, 0]), 0)
        self.assertEqual(int(downsampled[0, 1]), 2)
        self.assertEqual(int(downsampled[1, 0]), 8)
        self.assertEqual(int(downsampled[1, 1]), 10)


if __name__ == "__main__":
    unittest.main()
