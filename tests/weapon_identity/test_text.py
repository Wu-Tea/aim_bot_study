import unittest
from unittest.mock import patch

import numpy as np

from vision.weapon_identity.adapters import NormalizedROI
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


if __name__ == "__main__":
    unittest.main()
