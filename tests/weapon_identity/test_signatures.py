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
        self.assertAlmostEqual(ranked[0].template_score, 1.0, places=6)
        self.assertAlmostEqual(ranked[0].edge_score, 1.0, places=6)
        self.assertAlmostEqual(ranked[0].hash_score, 1.0, places=6)
        self.assertGreater(ranked[0].structure_score, 0.0)
        self.assertAlmostEqual(
            ranked[0].score,
            (ranked[0].template_score * 0.45)
            + (ranked[0].edge_score * 0.20)
            + (ranked[0].hash_score * 0.10)
            + (ranked[0].structure_score * 0.25),
            places=6,
        )
        self.assertLess(ranked[1].score, ranked[0].score)

    def test_near_match_still_prefers_closest_signature(self):
        live_roi = _make_rifle_icon(horizontal_shift=1, brightness=210)
        primary = _make_signature_record("sig-rifle", "cod22-rifle", _make_rifle_icon())
        alternate = _make_signature_record("sig-lmg", "cod22-lmg", _make_lmg_icon())

        ranked = score_candidates(live_roi, (alternate, primary))

        self.assertEqual(ranked[0].signature_id, "sig-rifle")
        self.assertGreater(ranked[0].score, 0.75)
        self.assertGreater(ranked[0].score, ranked[1].score)

    def test_shifted_dimmed_near_match_prefers_full_rifle_over_shortened_stock_variant(self):
        live_roi = _make_rifle_icon(horizontal_shift=1, brightness=230)
        full_weapon = _make_signature_record("sig-rifle", "cod22-rifle", _make_rifle_icon())
        shortened_stock = _make_signature_record(
            "sig-rifle-short-stock",
            "cod22-rifle-short-stock",
            _make_rifle_icon_with_shortened_stock(),
        )

        ranked = score_candidates(live_roi, (shortened_stock, full_weapon))

        self.assertEqual(ranked[0].signature_id, "sig-rifle")
        self.assertGreater(ranked[0].score, ranked[1].score)

    def test_same_family_partial_candidate_does_not_outrank_full_weapon_under_occlusion(self):
        live_roi = _make_rifle_icon(brightness=230)
        live_roi[20:32, 42:55] = 0

        full_weapon = _make_signature_record("sig-rifle", "cod22-rifle", _make_rifle_icon())
        partial_same_family = _make_signature_record(
            "sig-rifle-partial",
            "cod22-rifle-partial",
            _make_rifle_icon_without_barrel(),
        )

        ranked = score_candidates(live_roi, (partial_same_family, full_weapon))

        self.assertEqual(ranked[0].signature_id, "sig-rifle")
        self.assertGreater(ranked[0].score, ranked[1].score)

    def test_candidate_score_is_stable_when_other_candidates_change(self):
        live_roi = _make_rifle_icon(horizontal_shift=1, brightness=230)
        shortened_stock = _make_signature_record(
            "sig-rifle-short-stock",
            "cod22-rifle-short-stock",
            _make_rifle_icon_with_shortened_stock(),
        )
        full_weapon = _make_signature_record("sig-rifle", "cod22-rifle", _make_rifle_icon())

        isolated = score_candidates(live_roi, (shortened_stock,))[0]
        paired = score_candidates(live_roi, (shortened_stock, full_weapon))[1]

        self.assertAlmostEqual(isolated.score, paired.score, places=6)

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

    def test_exact_match_exposes_structure_score_when_it_affects_ranking(self):
        live_roi = _make_rifle_icon()
        identical = _make_signature_record("sig-rifle", "cod22-rifle", _make_rifle_icon())

        ranked = score_candidates(live_roi, (identical,))

        self.assertTrue(hasattr(ranked[0], "structure_score"))
        self.assertGreater(ranked[0].structure_score, 0.0)
        self.assertAlmostEqual(
            ranked[0].score,
            (ranked[0].template_score * 0.45)
            + (ranked[0].edge_score * 0.20)
            + (ranked[0].hash_score * 0.10)
            + (ranked[0].structure_score * 0.25),
            places=6,
        )

    def test_ignores_candidates_with_unsupported_feature_type(self):
        live_roi = _make_rifle_icon()
        incompatible = _make_signature_record(
            "sig-legacy",
            "cod22-legacy",
            _make_lmg_icon(),
            feature_type="legacy_template_v0",
            feature_payload={"hash": "aa11bb22"},
        )
        valid = _make_signature_record("sig-rifle", "cod22-rifle", _make_rifle_icon())

        ranked = score_candidates(live_roi, (incompatible, valid))

        self.assertEqual([match.signature_id for match in ranked], ["sig-rifle"])

    def test_rejects_malformed_template_shape_before_comparison(self):
        malformed = _make_signature_record("sig-bad-template", "cod22-bad-template", _make_rifle_icon())
        payload = malformed.to_dict()
        payload["feature_payload"]["template"] = payload["feature_payload"]["template"][:-1]
        malformed = VisualSignatureRecord.from_dict(payload)

        with self.assertRaisesRegex(ValueError, "feature_payload.template must have shape 32x32"):
            score_candidates(_make_rifle_icon(), (malformed,))

    def test_rejects_malformed_edge_map_shape_before_comparison(self):
        malformed = _make_signature_record("sig-bad-edges", "cod22-bad-edges", _make_rifle_icon())
        payload = malformed.to_dict()
        payload["feature_payload"]["edge_map"] = [row[:-1] for row in payload["feature_payload"]["edge_map"]]
        malformed = VisualSignatureRecord.from_dict(payload)

        with self.assertRaisesRegex(ValueError, "feature_payload.edge_map must have shape 32x32"):
            score_candidates(_make_rifle_icon(), (malformed,))

    def test_rejects_malformed_perceptual_hash_length_before_comparison(self):
        malformed = _make_signature_record("sig-bad-hash", "cod22-bad-hash", _make_rifle_icon())
        payload = malformed.to_dict()
        payload["feature_payload"]["perceptual_hash"] = payload["feature_payload"]["perceptual_hash"][:-1]
        malformed = VisualSignatureRecord.from_dict(payload)

        with self.assertRaisesRegex(ValueError, "feature_payload.perceptual_hash must be 64 bits"):
            score_candidates(_make_rifle_icon(), (malformed,))


def _make_signature_record(
    signature_id,
    canonical_weapon_id,
    image,
    *,
    feature_type=CLASSICAL_SIGNATURE_FEATURE_TYPE,
    feature_payload=None,
):
    signature = extract_signature(image)
    return VisualSignatureRecord(
        signature_id=signature_id,
        canonical_weapon_id=canonical_weapon_id,
        game="cod22",
        region_type="weapon_icon",
        resolution_bucket="1080p",
        ui_scale_bucket="default",
        feature_type=feature_type,
        feature_payload=signature.to_feature_payload() if feature_payload is None else feature_payload,
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


def _make_rifle_icon_without_barrel():
    image = _make_rifle_icon()
    image[26:30, 42:54] = 0
    return image


def _make_rifle_icon_with_shortened_stock():
    image = _make_rifle_icon()
    image[34:42, 10:12] = 0
    return image


if __name__ == "__main__":
    unittest.main()
