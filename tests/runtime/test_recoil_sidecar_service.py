import importlib
import io
import json
import tempfile
import unittest
from pathlib import Path

from vision.recoil_collection.models import RecoilProfileRecord


def _load_service_module():
    try:
        return importlib.import_module("runtime.recoil_sidecar.service")
    except ModuleNotFoundError as exc:
        raise AssertionError(f"Missing recoil sidecar service module: {exc}") from exc


class RecoilSidecarServiceTests(unittest.TestCase):
    def test_recognized_weapon_resolves_to_stored_profile(self):
        service_module = _load_service_module()

        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            profile_dir = root / "profiles"
            profile_dir.mkdir()
            _write_profile(
                profile_dir / "profile-cod22-m4-ads-standing-v1.json",
                _profile_record(
                    profile_id="profile-cod22-m4-ads-standing-v1",
                    canonical_weapon_id="cod22-m4",
                    game="cod22",
                    aim_mode="ads",
                    confidence=0.88,
                ),
            )
            state_path = root / "latest-state.json"
            state_path.write_text(
                json.dumps(
                    _recognizer_payload(
                        canonical_weapon_id="cod22-m4",
                        confidence=0.91,
                        degraded=False,
                        profile_ids=[],
                    ),
                    indent=2,
                    sort_keys=True,
                )
                + "\n",
                encoding="utf-8",
            )

            service = service_module.RecoilSidecarService(
                profile_dir=profile_dir,
                recognizer_state_path=state_path,
            )

            state = service.read_recognizer_state()
            matches = service.load_matching_profiles(state, context={"aim_mode": "ads"})
            active_profile = service.publish_active_profile(context={"aim_mode": "ads"})

            self.assertEqual(state.canonical_weapon_id, "cod22-m4")
            self.assertEqual([profile.profile_id for profile in matches], ["profile-cod22-m4-ads-standing-v1"])
            self.assertEqual(active_profile.status, "ready")
            self.assertEqual(active_profile.canonical_weapon_id, "cod22-m4")
            self.assertEqual(active_profile.profile_id, "profile-cod22-m4-ads-standing-v1")
            self.assertEqual(active_profile.game, "cod22")
            self.assertEqual(active_profile.stance, "standing")
            self.assertEqual(active_profile.aim_mode, "ads")
            self.assertEqual(active_profile.profile_confidence, 0.88)
            self.assertEqual(active_profile.identity_confidence, 0.91)
            self.assertEqual(active_profile.updated_at, "2026-05-06T12:00:00Z")

    def test_degraded_recognition_yields_degraded_sidecar_status(self):
        service_module = _load_service_module()

        with tempfile.TemporaryDirectory() as temp_dir:
            profile_dir = Path(temp_dir) / "profiles"
            profile_dir.mkdir()
            _write_profile(
                profile_dir / "profile-cod22-m4-ads-standing-v1.json",
                _profile_record(
                    profile_id="profile-cod22-m4-ads-standing-v1",
                    canonical_weapon_id="cod22-m4",
                    game="cod22",
                    aim_mode="ads",
                    confidence=0.88,
                ),
            )
            service = service_module.RecoilSidecarService(profile_dir=profile_dir)

            active_profile = service.publish_active_profile(
                _recognizer_payload(
                    canonical_weapon_id="cod22-m4",
                    confidence=0.55,
                    degraded=True,
                    profile_ids=["profile-cod22-m4-ads-standing-v1"],
                    source="carry_forward",
                ),
                context={"aim_mode": "ads"},
            )

            self.assertEqual(active_profile.status, "degraded")
            self.assertEqual(active_profile.canonical_weapon_id, "cod22-m4")
            self.assertEqual(active_profile.profile_id, "profile-cod22-m4-ads-standing-v1")
            self.assertEqual(active_profile.profile_confidence, 0.88)
            self.assertEqual(active_profile.identity_confidence, 0.55)
            self.assertEqual(active_profile.updated_at, "2026-05-06T12:00:00Z")

    def test_missing_profile_yields_unknown_sidecar_status(self):
        service_module = _load_service_module()

        with tempfile.TemporaryDirectory() as temp_dir:
            profile_dir = Path(temp_dir) / "profiles"
            profile_dir.mkdir()
            service = service_module.RecoilSidecarService(profile_dir=profile_dir)
            recognizer_state = io.StringIO(
                json.dumps(
                    _recognizer_payload(
                        canonical_weapon_id="cod22-kastov-762",
                        confidence=0.89,
                        degraded=False,
                        profile_ids=[],
                    )
                )
            )

            active_profile = service.publish_active_profile(recognizer_state, context={"aim_mode": "ads"})

            self.assertEqual(active_profile.status, "unknown")
            self.assertEqual(active_profile.canonical_weapon_id, "cod22-kastov-762")
            self.assertIsNone(active_profile.profile_id)
            self.assertEqual(active_profile.game, "cod22")
            self.assertEqual(active_profile.stance, "standing")
            self.assertEqual(active_profile.aim_mode, "ads")
            self.assertIsNone(active_profile.profile_confidence)
            self.assertEqual(active_profile.identity_confidence, 0.89)
            self.assertEqual(active_profile.updated_at, "2026-05-06T12:00:00Z")


def _write_profile(path: Path, record: RecoilProfileRecord) -> None:
    path.write_text(json.dumps(record.to_dict(), indent=2, sort_keys=True) + "\n", encoding="utf-8")


def _profile_record(*, profile_id: str, canonical_weapon_id: str, game: str, aim_mode: str, confidence: float):
    return RecoilProfileRecord(
        profile_id=profile_id,
        canonical_weapon_id=canonical_weapon_id,
        game=game,
        stance="standing",
        aim_mode=aim_mode,
        sample_interval_ms=16,
        duration_ms=64,
        initial_delay_ms=0,
        samples_x=(0.0, 0.4, 0.8, 1.1),
        samples_y=(0.0, -1.8, -3.6, -4.5),
        sample_count=4,
        burst_count=5,
        variance_summary={"horizontal_stddev": 0.14, "vertical_stddev": 0.32},
        confidence=confidence,
        capture_resolution="1920x1080",
        capture_fps=240.0,
        collector_version="collector-0.1.0",
        created_at="2026-05-06T11:00:00Z",
    )


def _recognizer_payload(
    *,
    canonical_weapon_id: str,
    confidence: float,
    degraded: bool,
    profile_ids: list[str],
    source: str = "image",
):
    return {
        "type": "current_weapon",
        "game": "cod22",
        "canonical_weapon_id": canonical_weapon_id,
        "confidence": confidence,
        "source": source,
        "timestamp": "2026-05-06T12:00:00Z",
        "degraded": degraded,
        "matched_name": None,
        "profile_ids": profile_ids,
    }


if __name__ == "__main__":
    unittest.main()
