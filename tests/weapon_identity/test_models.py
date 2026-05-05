import unittest

from vision.weapon_identity.models import RecognitionEvent
from vision.weapon_identity.models import VisualSignatureRecord
from vision.weapon_identity.models import WeaponIdentityRecord


class WeaponIdentityRecordTests(unittest.TestCase):
    def test_round_trip_serialization_and_blueprint_alias_resolution(self):
        record = WeaponIdentityRecord(
            canonical_weapon_id="cod22-m4",
            game="cod22",
            weapon_family="assault_rifle",
            display_name="M4",
            alias_names=("M4A1", "M4 Platform"),
            blueprint_names=("Blackcell Ember", "Vault Issue"),
            signature_refs=("sig-primary", "sig-secondary"),
            notes="standing-only collector baseline",
            created_at="2026-05-05T09:00:00Z",
            updated_at="2026-05-05T10:15:00Z",
        )

        round_tripped = WeaponIdentityRecord.from_dict(record.to_dict())

        self.assertEqual(round_tripped, record)
        self.assertEqual(round_tripped.resolve_name("cod22-m4"), "cod22-m4")
        self.assertEqual(round_tripped.resolve_name("M4A1"), "cod22-m4")
        self.assertEqual(round_tripped.resolve_name("Blackcell Ember"), "cod22-m4")
        self.assertIsNone(round_tripped.resolve_name("Unrelated Weapon"))

    def test_from_dict_rejects_missing_required_field(self):
        payload = {
            "game": "cod22",
            "weapon_family": "assault_rifle",
            "display_name": "M4",
            "alias_names": [],
            "blueprint_names": ["Blackcell Ember"],
            "signature_refs": ["sig-primary"],
            "notes": "",
            "created_at": "2026-05-05T09:00:00Z",
            "updated_at": "2026-05-05T10:15:00Z",
        }

        with self.assertRaisesRegex(ValueError, "canonical_weapon_id"):
            WeaponIdentityRecord.from_dict(payload)

    def test_constructor_rejects_blank_display_name(self):
        with self.assertRaisesRegex(ValueError, "display_name"):
            WeaponIdentityRecord(
                canonical_weapon_id="cod22-m4",
                game="cod22",
                weapon_family="assault_rifle",
                display_name="",
                alias_names=(),
                blueprint_names=(),
                signature_refs=(),
                notes="",
                created_at="2026-05-05T09:00:00Z",
                updated_at="2026-05-05T10:15:00Z",
            )


class VisualSignatureRecordTests(unittest.TestCase):
    def test_round_trip_serialization(self):
        record = VisualSignatureRecord(
            signature_id="sig-primary",
            canonical_weapon_id="cod22-m4",
            game="cod22",
            region_type="weapon_icon",
            resolution_bucket="1080p",
            ui_scale_bucket="default",
            feature_type="perceptual_hash",
            feature_payload={"hash": "aa11bb22", "bits": 64},
            captured_from="hud crop",
            confidence=0.91,
        )

        self.assertEqual(VisualSignatureRecord.from_dict(record.to_dict()), record)

    def test_constructor_rejects_non_mapping_feature_payload(self):
        with self.assertRaisesRegex(ValueError, "feature_payload"):
            VisualSignatureRecord(
                signature_id="sig-primary",
                canonical_weapon_id="cod22-m4",
                game="cod22",
                region_type="weapon_icon",
                resolution_bucket="1080p",
                ui_scale_bucket="default",
                feature_type="perceptual_hash",
                feature_payload="aa11bb22",
                captured_from="hud crop",
                confidence=0.91,
            )


class RecognitionEventTests(unittest.TestCase):
    def test_round_trip_serialization(self):
        event = RecognitionEvent(
            game="cod22",
            canonical_weapon_id="cod22-m4",
            confidence=0.84,
            source="fused",
            timestamp="2026-05-05T10:16:00Z",
            degraded=False,
            matched_name="Blackcell Ember",
        )

        self.assertEqual(RecognitionEvent.from_dict(event.to_dict()), event)

    def test_from_dict_ignores_unknown_extra_fields(self):
        payload = {
            "game": "cod22",
            "canonical_weapon_id": "cod22-m4",
            "confidence": 0.84,
            "source": "fused",
            "timestamp": "2026-05-05T10:16:00Z",
            "degraded": False,
            "matched_name": "Blackcell Ember",
            "future_field": {"schema_version": 2},
        }

        event = RecognitionEvent.from_dict(payload)

        self.assertEqual(event.canonical_weapon_id, "cod22-m4")
        self.assertEqual(event.matched_name, "Blackcell Ember")

    def test_from_dict_rejects_missing_source(self):
        payload = {
            "game": "cod22",
            "canonical_weapon_id": "cod22-m4",
            "confidence": 0.84,
            "timestamp": "2026-05-05T10:16:00Z",
            "degraded": False,
            "matched_name": "Blackcell Ember",
        }

        with self.assertRaisesRegex(ValueError, "source"):
            RecognitionEvent.from_dict(payload)


if __name__ == "__main__":
    unittest.main()
