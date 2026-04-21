import unittest

import numpy as np
import torch

from vision.fastpath import _decode_nms_in_engine, _detect_output_kind, _fast_path_input_dtype


class DummyBackend:
    def __init__(self, fp16):
        self.fp16 = fp16


class VisionFastpathTests(unittest.TestCase):
    def test_fast_path_input_dtype_prefers_backend_capability_over_config_half(self):
        self.assertEqual(_fast_path_input_dtype(DummyBackend(fp16=False), config_half=True), torch.float32)
        self.assertEqual(_fast_path_input_dtype(DummyBackend(fp16=True), config_half=False), torch.float16)

    def test_detect_output_kind_recognizes_nms_in_engine_layout(self):
        raw = torch.zeros((1, 10, 57), dtype=torch.float32)

        self.assertEqual(_detect_output_kind(raw), "nms_in_engine")

    def test_detect_output_kind_recognizes_raw_layout(self):
        raw = torch.zeros((1, 56, 8400), dtype=torch.float32)

        self.assertEqual(_detect_output_kind(raw), "raw")

    def test_decode_nms_in_engine_preserves_parsed_detections_contract(self):
        raw = torch.zeros((1, 300, 57), dtype=torch.float32)

        raw[0, 0, :4] = torch.tensor([11.0, 22.0, 33.0, 44.0])
        raw[0, 0, 4] = 0.90
        raw[0, 0, 5] = 0.0
        raw[0, 0, 6:] = torch.arange(51, dtype=torch.float32)

        raw[0, 1, :4] = torch.tensor([55.0, 66.0, 77.0, 88.0])
        raw[0, 1, 4] = 0.40
        raw[0, 1, 5] = 0.0

        detections = _decode_nms_in_engine(raw, conf_thr=0.5, max_det=10)

        self.assertEqual(len(detections), 1)
        self.assertEqual(detections[0].boxes.shape, (1, 4))
        self.assertEqual(detections[0].confs.shape, (1,))
        self.assertEqual(detections[0].keypoints.shape, (1, 17, 3))
        np.testing.assert_allclose(
            detections[0].boxes[0],
            np.array([11.0, 22.0, 33.0, 44.0], dtype=np.float32),
        )
        np.testing.assert_allclose(detections[0].confs[0], np.float32(0.90))


if __name__ == "__main__":
    unittest.main()
