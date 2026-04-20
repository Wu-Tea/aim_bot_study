import unittest
from types import SimpleNamespace

import numpy as np
import torch

from vision.fastpath import (
    CpuFastPathPreprocessor,
    FastPath,
    _build_fast_path_preprocessor,
    _decode_nms_in_engine,
    _detect_output_kind,
    _fast_path_input_dtype,
    _fast_predict,
)


class DummyBackend:
    def __init__(self, fp16):
        self.fp16 = fp16


class RecordingBackend:
    def __init__(self, raw):
        self.raw = raw
        self.calls = []

    def __call__(self, tensor):
        self.calls.append(tensor.detach().clone())
        return self.raw


class RecordingPreprocessor:
    def __init__(self):
        self.calls = []

    def prepare(self, fast_path: FastPath, frame_source):
        self.calls.append(frame_source)
        fast_path.gpu_input.zero_()


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

    def test_cpu_fast_path_preprocessor_writes_normalized_chw_tensor(self):
        gpu_input = torch.empty((1, 3, 1, 2), dtype=torch.float32)
        raw = torch.zeros((1, 1, 6), dtype=torch.float32)
        raw[0, 0, :4] = torch.tensor([1.0, 2.0, 3.0, 4.0])
        raw[0, 0, 4] = 0.95
        backend = RecordingBackend(raw)
        fast_path = FastPath(
            backend=backend,
            gpu_input=gpu_input,
            conf_thr=0.5,
            max_det=10,
            output_kind="nms_in_engine",
            preprocessor=CpuFastPathPreprocessor(),
        )
        frame_rgb = np.array([[[255, 0, 0], [0, 128, 255]]], dtype=np.uint8)

        detections = _fast_predict(fast_path, frame_rgb)

        self.assertEqual(len(detections), 1)
        self.assertEqual(len(backend.calls), 1)
        np.testing.assert_allclose(
            backend.calls[0].numpy(),
            np.array(
                [[[[1.0, 0.0]], [[0.0, 128.0 / 255.0]], [[0.0, 1.0]]]],
                dtype=np.float32,
            ),
            rtol=1e-6,
            atol=1e-6,
        )

    def test_fast_predict_passes_frame_source_through_to_preprocessor(self):
        backend = RecordingBackend(torch.zeros((1, 0, 6), dtype=torch.float32))
        preprocessor = RecordingPreprocessor()
        fast_path = FastPath(
            backend=backend,
            gpu_input=torch.ones((1, 3, 2, 2), dtype=torch.float32),
            conf_thr=0.5,
            max_det=10,
            output_kind="nms_in_engine",
            preprocessor=preprocessor,
        )
        frame_source = SimpleNamespace(frame=np.zeros((2, 2, 3), dtype=np.uint8), native_token="roi-17")

        _fast_predict(fast_path, frame_source)

        self.assertEqual(preprocessor.calls, [frame_source])

    def test_build_fast_path_preprocessor_falls_back_to_cpu_when_native_loader_is_missing(self):
        preprocessor = _build_fast_path_preprocessor(
            requested="native",
            native_loader=lambda: None,
        )

        self.assertIsInstance(preprocessor, CpuFastPathPreprocessor)


if __name__ == "__main__":
    unittest.main()
