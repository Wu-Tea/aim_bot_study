import os
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

import numpy as np

from vision.weapon_identity.adapters import NormalizedROI
from vision.weapon_identity import text as text_module
from vision.weapon_identity.text import extract_text_candidates
from vision.weapon_identity.text import normalize_ocr_lines


class WeaponTextExtractionTests(unittest.TestCase):
    def test_normalize_ocr_lines_returns_unique_text_candidates(self):
        candidates = normalize_ocr_lines(
            [
                "  CR-56艾麦克斯  ",
                "",
                "CR-56艾麦克斯",
                "   CR-56   艾麦克斯   ",
                "Krig C",
            ]
        )

        self.assertEqual(candidates, ("CR-56艾麦克斯", "CR-56 艾麦克斯", "Krig C"))

    def test_extract_text_candidates_reads_roi_and_filters_empty_lines(self):
        frame = np.zeros((100, 200, 3), dtype=np.uint8)
        roi = NormalizedROI(left=0.50, top=0.20, width=0.25, height=0.30)
        seen_shapes = []

        def ocr_reader(cropped):
            seen_shapes.append(tuple(cropped.shape))
            return (
                [
                    (None, "  CR-56艾麦克斯  ", 0.99),
                    (None, "CR-56艾麦克斯", 0.94),
                    (None, "", 0.10),
                    (None, "Krig C", 0.88),
                ],
                None,
            )

        candidates = extract_text_candidates(frame, roi, ocr_reader=ocr_reader)

        self.assertEqual(seen_shapes, [(30, 50, 3)])
        self.assertEqual(candidates, ("CR-56艾麦克斯", "Krig C"))

    def test_extract_text_candidates_returns_empty_tuple_when_backend_is_unavailable(self):
        frame = np.zeros((20, 20, 3), dtype=np.uint8)
        roi = NormalizedROI(left=0.0, top=0.0, width=1.0, height=1.0)

        with patch("vision.weapon_identity.text._load_default_ocr_reader", return_value=None):
            candidates = extract_text_candidates(frame, roi)

        self.assertEqual(candidates, ())

    def test_extract_text_candidates_multi_pass_adds_joined_variants_for_split_weapon_names(self):
        frame = np.zeros((40, 80, 3), dtype=np.uint8)
        roi = NormalizedROI(left=0.0, top=0.0, width=1.0, height=1.0)

        def ocr_reader(_cropped):
            return (
                [
                    (None, "KT-3", 0.99),
                    (None, "勇士", 0.98),
                ],
                None,
            )

        candidates = extract_text_candidates(frame, roi, ocr_reader=ocr_reader, multi_pass=True)

        self.assertIn("KT-3", candidates)
        self.assertIn("勇士", candidates)
        self.assertIn("KT-3勇士", candidates)


    def test_normalize_ocr_lines_trims_trailing_ammo_and_ui_noise(self):
        candidates = normalize_ocr_lines(
            [
                "点22塔恩托 40 999 5097081 3509110813 00478901G 50911081 ngc 3509110813+",
                "格克霍娃",
            ]
        )

        self.assertEqual(candidates, ("点22塔恩托", "格克霍娃"))


    def test_build_rapidocr_kwargs_uses_cuda_when_provider_is_available(self):
        kwargs = text_module._build_rapidocr_kwargs(
            "cuda",
            ("CUDAExecutionProvider", "CPUExecutionProvider"),
        )

        self.assertTrue(kwargs["det_use_cuda"])
        self.assertTrue(kwargs["cls_use_cuda"])
        self.assertTrue(kwargs["rec_use_cuda"])
        self.assertNotIn("det_use_dml", kwargs)

    def test_load_default_ocr_reader_does_not_fall_back_to_cpu_for_missing_gpu_provider(self):
        calls = []

        class FakeRapidOCR:
            def __init__(self, **kwargs):
                calls.append(kwargs)

        text_module._DEFAULT_OCR_READER = None
        text_module._DEFAULT_OCR_READER_INITIALIZED = False
        try:
            with patch.dict("sys.modules", {"rapidocr_onnxruntime": SimpleNamespace(RapidOCR=FakeRapidOCR)}):
                with patch.dict("os.environ", {"RECOIL_OCR_PROVIDER": "cuda"}, clear=False):
                    with patch(
                        "vision.weapon_identity.text._available_onnxruntime_providers",
                        return_value=("CPUExecutionProvider",),
                    ):
                        reader = text_module._load_default_ocr_reader()

            self.assertIsNone(reader)
            self.assertEqual(calls, [])
        finally:
            text_module._DEFAULT_OCR_READER = None
            text_module._DEFAULT_OCR_READER_INITIALIZED = False

    def test_load_default_ocr_reader_prepares_cuda_dll_path_before_constructing_reader(self):
        calls = []

        class FakeRapidOCR:
            def __init__(self, **kwargs):
                del kwargs
                calls.append("rapidocr")

        text_module._DEFAULT_OCR_READER = None
        text_module._DEFAULT_OCR_READER_INITIALIZED = False
        try:
            with patch.dict("sys.modules", {"rapidocr_onnxruntime": SimpleNamespace(RapidOCR=FakeRapidOCR)}):
                with patch.dict("os.environ", {"RECOIL_OCR_PROVIDER": "cuda"}, clear=False):
                    with patch(
                        "vision.weapon_identity.text._available_onnxruntime_providers",
                        return_value=("CUDAExecutionProvider", "CPUExecutionProvider"),
                    ):
                        with patch(
                            "vision.weapon_identity.text._prepare_cuda_dll_search_path",
                            side_effect=lambda: calls.append("prepare_cuda_dlls"),
                        ):
                            reader = text_module._load_default_ocr_reader()

            self.assertIsNotNone(reader)
            self.assertEqual(calls, ["prepare_cuda_dlls", "rapidocr"])
        finally:
            text_module._DEFAULT_OCR_READER = None
            text_module._DEFAULT_OCR_READER_INITIALIZED = False

    def test_prepare_cuda_dll_search_path_prepends_discovered_directories(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            cuda_dir = Path(temp_dir) / "torch" / "lib"
            cuda_dir.mkdir(parents=True)
            dll_handles = []

            with patch(
                "vision.weapon_identity.text._discover_cuda_dll_dirs",
                return_value=(cuda_dir,),
            ):
                with patch("vision.weapon_identity.text.os.add_dll_directory", create=True) as mock_add:
                    mock_add.side_effect = lambda path: dll_handles.append(path) or SimpleNamespace(close=lambda: None)
                    with patch.dict("os.environ", {"PATH": f"C:\\Existing{os.pathsep}{cuda_dir}"}, clear=False):
                        text_module._CUDA_DLL_SEARCH_PATH_PREPARED = False
                        text_module._CUDA_DLL_DIRECTORY_HANDLES = []
                        try:
                            text_module._prepare_cuda_dll_search_path()

                            path_entries = os.environ["PATH"].split(os.pathsep)
                            self.assertEqual(path_entries[0], str(cuda_dir))
                            self.assertEqual(path_entries.count(str(cuda_dir)), 1)
                            self.assertEqual(dll_handles, [str(cuda_dir)])
                        finally:
                            text_module._CUDA_DLL_SEARCH_PATH_PREPARED = False
                            text_module._CUDA_DLL_DIRECTORY_HANDLES = []


if __name__ == "__main__":
    unittest.main()
