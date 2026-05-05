import importlib
import json
import tempfile
import unittest
from pathlib import Path

from vision.recoil_collection.models import RecoilProfileRecord
from vision.weapon_identity.models import VisualSignatureRecord
from vision.weapon_identity.models import WeaponIdentityRecord


def _load_storage_module():
    try:
        return importlib.import_module("vision.recoil_collection.storage")
    except ModuleNotFoundError as exc:
        raise AssertionError(f"Missing storage module: {exc}") from exc


class RecoilCollectionStorageTests(unittest.TestCase):
    def test_identity_save_and_load_round_trip_uses_byte_stable_utf8_json(self):
        storage = _load_storage_module()
        record = WeaponIdentityRecord(
            canonical_weapon_id="cod22-m4",
            game="cod22",
            weapon_family="assault_rifle",
            display_name="M4",
            alias_names=("M4A1",),
            blueprint_names=("Caf\u00e9 Noir",),
            signature_refs=("sig-primary",),
            notes="se\u00f1al recoil baseline",
            created_at="2026-05-05T09:00:00Z",
            updated_at="2026-05-05T10:15:00Z",
        )

        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "identity.json"

            storage.save_identity_record(path, record)

            self.assertEqual(storage.load_identity_record(path), record)

            expected_json = json.dumps(record.to_dict(), ensure_ascii=False, indent=2, sort_keys=True) + "\n"
            expected_bytes = expected_json.encode("utf-8")

            self.assertEqual(path.read_bytes(), expected_bytes)
            self.assertNotIn(b"\r\n", path.read_bytes())

    def test_signature_save_and_load_round_trip(self):
        storage = _load_storage_module()
        record = VisualSignatureRecord(
            signature_id="sig-primary",
            canonical_weapon_id="cod22-m4",
            game="cod22",
            region_type="weapon_icon",
            resolution_bucket="1080p",
            ui_scale_bucket="default",
            feature_type="perceptual_hash",
            feature_payload={
                "hash": "aa11bb22",
                "metadata": {"variant": "blackcell", "weights": [1, 2, 3]},
            },
            captured_from="hud crop",
            confidence=0.91,
        )

        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "signature.json"

            storage.save_signature_record(path, record)

            self.assertEqual(storage.load_signature_record(path), record)

    def test_profile_save_and_load_round_trip(self):
        storage = _load_storage_module()
        profile = RecoilProfileRecord(
            profile_id="profile-cod22-m4-ads-standing-v1",
            canonical_weapon_id="cod22-m4",
            game="cod22",
            stance="standing",
            aim_mode="ads",
            sample_interval_ms=16,
            duration_ms=64,
            initial_delay_ms=32,
            samples_x=(0.0, 0.8, 1.2, 1.4),
            samples_y=(0.0, -2.5, -5.1, -6.0),
            sample_count=4,
            burst_count=6,
            variance_summary={"horizontal_stddev": 0.19, "vertical_stddev": 0.42},
            confidence=0.88,
            capture_resolution="1920x1080",
            capture_fps=240.0,
            collector_version="collector-0.1.0",
            created_at="2026-05-05T11:00:00Z",
        )

        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "profile.json"

            storage.save_recoil_profile(path, profile)

            self.assertEqual(storage.load_recoil_profile(path), profile)

    def test_load_recoil_profile_rejects_corrupted_json(self):
        storage = _load_storage_module()

        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "profile.json"
            path.write_text('{"profile_id": "broken",', encoding="utf-8")

            with self.assertRaisesRegex(storage.StorageError, r"Invalid JSON.*line 1 column 25 char 24"):
                storage.load_recoil_profile(path)

    def test_load_identity_record_rejects_missing_required_field(self):
        storage = _load_storage_module()
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

        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "identity.json"
            path.write_text(
                json.dumps(payload, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )

            with self.assertRaisesRegex(storage.StorageError, "canonical_weapon_id"):
                storage.load_identity_record(path)

    def test_load_signature_record_rejects_invalid_utf8_with_storage_error(self):
        storage = _load_storage_module()

        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "signature.json"
            path.write_bytes(b'{"signature_id": "\xff"}')

            with self.assertRaisesRegex(storage.StorageError, r"UTF-8.*signature\.json"):
                storage.load_signature_record(path)

    def test_save_identity_record_wraps_filesystem_failures_in_storage_error(self):
        storage = _load_storage_module()
        record = WeaponIdentityRecord(
            canonical_weapon_id="cod22-m4",
            game="cod22",
            weapon_family="assault_rifle",
            display_name="M4",
            alias_names=(),
            blueprint_names=(),
            signature_refs=(),
            notes="",
            created_at="2026-05-05T09:00:00Z",
            updated_at="2026-05-05T10:15:00Z",
        )

        with tempfile.TemporaryDirectory() as temp_dir:
            directory_path = Path(temp_dir)

            with self.assertRaisesRegex(storage.StorageError, r"Unable to write JSON.*Is a directory|Unable to write JSON.*Permission denied|Unable to write JSON.*Access is denied"):
                storage.save_identity_record(directory_path, record)


if __name__ == "__main__":
    unittest.main()
