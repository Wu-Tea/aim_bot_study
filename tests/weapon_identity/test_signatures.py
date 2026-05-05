import unittest

import numpy as np

from vision.weapon_identity.models import VisualSignatureRecord
from vision.weapon_identity.signatures import CLASSICAL_SIGNATURE_FEATURE_TYPE
from vision.weapon_identity.signatures import extract_signature
from vision.weapon_identity.signatures import score_candidates


class SignatureMatchingTests(unittest.TestCase):
    def test_exact_match_ranks_identical_signature_first_with_full_component_scores(self):
        live_roi = _make_rifle_icon()
        identical = _make_signature_record("sig-rifle", "cod22-rifle", _make_rifle_icon())
        alternate = _make_signature_record("sig-lmg", "cod22-lmg", _make_lmg_icon())

        ranked = score_candidates(live_roi, (alternate, identical))

        self.assertEqual(ranked[0].signature_id, "sig-rifle")
        self.assertEqual(ranked[0].canonical_weapon_id, "cod22-rifle")
        self.assertAlmostEqual(ranked[0].score, 1.0, places=6)
        self.assertAlmostEqual(ranked[0].template_score, 1.0, places=6)
        self.assertAlmostEqual(ranked[0].edge_score, 1.0, places=6)
        self.assertAlmostEqual(ranked[0].hash_score, 1.0, places=6)
        self.assertLess(ranked[1].score, ranked[0].score)

    def test_near_match_still_prefers_closest_signature(self):
        live_roi = _make_rifle_icon(horizontal_shift=1, brightness=210)
        primary = _make_signature_record("sig-rifle", "cod22-rifle", _make_rifle_icon())
        alternate = _make_signature_record("sig-lmg", "cod22-lmg", _make_lmg_icon())

        ranked = score_candidates(live_roi, (alternate, primary))

        self.assertEqual(ranked[0].signature_id, "sig-rifle")
        self.assertGreater(ranked[0].score, 0.75)
        self.assertGreater(ranked[0].score, ranked[1].score)

    def test_mismatch_score_degrades_to_low_confidence(self):
        live_roi = _make_rifle_icon()
        mismatch = _make_signature_record("sig-lmg", "cod22-lmg", _make_lmg_icon())

        ranked = score_candidates(live_roi, (mismatch,))

        self.assertEqual(ranked[0].signature_id, "sig-lmg")
        self.assertLess(ranked[0].score, 0.45)
        self.assertLess(ranked[0].template_score, 0.6)
        self.assertLess(ranked[0].edge_score, 0.75)

    def test_tie_scores_preserve_input_order(self):
        live_roi = _make_rifle_icon()
        first = _make_signature_record("sig-first", "cod22-rifle-a", _make_rifle_icon())
        second = _make_signature_record("sig-second", "cod22-rifle-b", _make_rifle_icon())

        ranked = score_candidates(live_roi, (second, first))

        self.assertEqual([match.signature_id for match in ranked], ["sig-second", "sig-first"])


def _make_signature_record(signature_id, canonical_weapon_id, image):
    signature = extract_signature(image)
    return VisualSignatureRecord(
        signature_id=signature_id,
        canonical_weapon_id=canonical_weapon_id,
        game="cod22",
        region_type="weapon_icon",
        resolution_bucket="1080p",
        ui_scale_bucket="default",
        feature_type=CLASSICAL_SIGNATURE_FEATURE_TYPE,
        feature_payload=signature.to_feature_payload(),
        captured_from="synthetic test fixture",
        confidence=1.0,
    )


def _make_rifle_icon(horizontal_shift=0, brightness=255):
    image = np.zeros((64, 64), dtype=np.uint8)
    offset = int(horizontal_shift)
    image[28:34, 10 + offset : 42 + offset] = brightness
    image[24:28, 18 + offset : 24 + offset] = brightness
    image[34:42, 10 + offset : 18 + offset] = brightness
    image[26:30, 42 + offset : 54 + offset] = brightness
    return image


def _make_lmg_icon():
    image = np.zeros((64, 64), dtype=np.uint8)
    image[12:50, 28:34] = 255
    image[18:24, 20:42] = 255
    image[44:50, 34:48] = 255
    return image


if __name__ == "__main__":
    unittest.main()
