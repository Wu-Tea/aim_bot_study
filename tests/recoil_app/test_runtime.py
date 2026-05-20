import io
import json
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from pathlib import Path
from unittest.mock import patch

import numpy as np

from runtime.recoil_sidecar.models import RecognizerState
from vision.recoil_collection.capture import MotionTraceSample
from vision.recoil_collection.capture import RecoilCollectorConfig
from vision.recoil_collection.models import RecoilBurstWindow
from vision.recoil_collection.models import RecoilBurstSampleSeries
from vision.recoil_collection.models import RecoilCollectionSession
from vision.recoil_collection.models import RecoilProfileRecord
from vision.recoil_collection.models import RecoilProfileSummary
from vision.recoil_collection.models import RecoilSample


def _load_runtime_module():
    import importlib

    try:
        return importlib.import_module("recoil_app.runtime")
    except ModuleNotFoundError as exc:
        raise AssertionError(f"Missing recoil_app runtime module: {exc}") from exc


class RecoilRuntimeImportTests(unittest.TestCase):
    def test_runtime_imports_without_preloading_vision_modules(self):
        result = subprocess.run(
            [
                sys.executable,
                "-B",
                "-c",
                "import recoil_app.runtime as runtime; print(runtime.RecoilRuntime.__name__)",
            ],
            cwd=Path(__file__).resolve().parents[2],
            text=True,
            capture_output=True,
            timeout=30,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("RecoilRuntime", result.stdout)


class RecoilIdentityStoreTests(unittest.TestCase):
    def test_resolve_or_create_creates_identity_from_display_name(self):
        runtime = _load_runtime_module()

        with tempfile.TemporaryDirectory() as temp_dir:
            store = runtime.IdentityStore(Path(temp_dir))
            record = store.resolve_or_create(
                game="cod22",
                display_name="黑色组织传奇",
                timestamp="2026-05-06T18:00:00Z",
            )

            self.assertEqual(record.canonical_weapon_id, "cod22-黑色组织传奇")
            self.assertEqual(record.display_name, "黑色组织传奇")
            self.assertTrue((Path(temp_dir) / "identity-cod22-cod22-黑色组织传奇.json").exists())


    def test_resolve_or_create_persists_only_minimal_fields(self):
        runtime = _load_runtime_module()

        with tempfile.TemporaryDirectory() as temp_dir:
            store = runtime.IdentityStore(Path(temp_dir))
            record = store.resolve_or_create(
                game="cod22",
                display_name="最后通牒",
                timestamp="2026-05-06T18:00:00Z",
            )
            payload = json.loads(
                (Path(temp_dir) / f"identity-{record.game}-{record.canonical_weapon_id}.json").read_text(encoding="utf-8")
            )

            self.assertEqual(
                payload,
                {
                    "canonical_weapon_id": "cod22-最后通牒",
                    "created_at": "2026-05-06T18:00:00Z",
                    "display_name": "最后通牒",
                    "game": "cod22",
                    "updated_at": "2026-05-06T18:00:00Z",
                },
            )

    def test_resolve_or_create_reuses_unique_fuzzy_existing_identity(self):
        runtime = _load_runtime_module()

        with tempfile.TemporaryDirectory() as temp_dir:
            store = runtime.IdentityStore(Path(temp_dir))
            existing = store.resolve_or_create(
                game="cod22",
                display_name="最后通牒",
                timestamp="2026-05-06T18:00:00Z",
            )

            resolved = store.resolve_or_create(
                game="cod22",
                display_name="最后通",
                timestamp="2026-05-06T18:01:00Z",
            )

            self.assertEqual(resolved, existing)
            self.assertEqual(len(store.records_for_game("cod22")), 1)


class RecoilProfileStoreTests(unittest.TestCase):
    def test_upsert_and_lookup_returns_best_matching_profile(self):
        runtime = _load_runtime_module()

        with tempfile.TemporaryDirectory() as temp_dir:
            store = runtime.RecoilProfileStore(Path(temp_dir))
            ads_profile = _profile_record(
                profile_id="profile-cod22-黑色组织传奇-ads-standing-v1",
                canonical_weapon_id="cod22-黑色组织传奇",
                aim_mode="ads",
                confidence=0.91,
            )
            hipfire_profile = _profile_record(
                profile_id="profile-cod22-黑色组织传奇-hipfire-standing-v1",
                canonical_weapon_id="cod22-黑色组织传奇",
                aim_mode="hipfire",
                confidence=0.83,
            )

            store.upsert(ads_profile)
            store.upsert(hipfire_profile)

            self.assertEqual(
                store.get_best_profile(
                    game="cod22",
                    canonical_weapon_id="cod22-黑色组织传奇",
                    stance="standing",
                    aim_mode="ads",
                ).profile_id,
                ads_profile.profile_id,
            )
            self.assertEqual(
                store.get_best_profile(
                    game="cod22",
                    canonical_weapon_id="cod22-黑色组织传奇",
                    stance="standing",
                    aim_mode="hipfire",
                ).profile_id,
                hipfire_profile.profile_id,
            )

    def test_get_best_profile_reuses_cached_directory_snapshot_when_unchanged(self):
        runtime = _load_runtime_module()

        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            store = runtime.RecoilProfileStore(temp_path)
            profile = _profile_record(
                profile_id="profile-cod22-黑色组织传奇-ads-standing-v1",
                canonical_weapon_id="cod22-黑色组织传奇",
                aim_mode="ads",
                confidence=0.91,
            )
            (temp_path / f"{profile.profile_id}.json").write_text(
                json.dumps(profile.to_dict(), ensure_ascii=False, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
            store.reload()

            first = store.get_best_profile(
                game="cod22",
                canonical_weapon_id="cod22-黑色组织传奇",
                stance="standing",
                aim_mode="ads",
            )
            generation_after_first = store.load_generation
            second = store.get_best_profile(
                game="cod22",
                canonical_weapon_id="cod22-黑色组织传奇",
                stance="standing",
                aim_mode="ads",
            )

            self.assertEqual(first.profile_id, profile.profile_id)
            self.assertEqual(second.profile_id, profile.profile_id)
            self.assertEqual(store.load_generation, generation_after_first)

    def test_get_best_profile_reloads_when_profile_file_is_created_externally(self):
        runtime = _load_runtime_module()

        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            store = runtime.RecoilProfileStore(temp_path)
            initial_generation = store.load_generation
            profile = _profile_record(
                profile_id="profile-cod22-ocp-ads-standing-current",
                canonical_weapon_id="cod22-ocp",
                aim_mode="ads",
                confidence=0.91,
            )

            self.assertIsNone(
                store.get_best_profile(
                    game="cod22",
                    canonical_weapon_id="cod22-ocp",
                    stance="standing",
                    aim_mode="ads",
                )
            )
            (temp_path / f"{profile.profile_id}.json").write_text(
                json.dumps(profile.to_dict(), ensure_ascii=False, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )

            resolved = store.get_best_profile(
                game="cod22",
                canonical_weapon_id="cod22-ocp",
                stance="standing",
                aim_mode="ads",
            )

            self.assertIsNotNone(resolved)
            self.assertEqual(resolved.profile_id, profile.profile_id)
            self.assertGreater(store.load_generation, initial_generation)

    def test_get_best_profile_skips_profiles_that_are_not_ready_for_compensation(self):
        runtime = _load_runtime_module()

        with tempfile.TemporaryDirectory() as temp_dir:
            store = runtime.RecoilProfileStore(Path(temp_dir))
            single_episode_profile = _profile_record(
                profile_id="profile-cod22-m4-ads-standing-low-v1",
                canonical_weapon_id="cod22-m4",
                aim_mode="ads",
                confidence=0.95,
                profile_type="magazine_curve_v1",
                burst_count=1,
                support_counts=(1, 1, 1),
                fit_summary={"accepted_episode_count": 1.0},
            )
            ready_profile = _profile_record(
                profile_id="profile-cod22-m4-ads-standing-ready-v1",
                canonical_weapon_id="cod22-m4",
                aim_mode="ads",
                confidence=0.82,
                profile_type="magazine_curve_v1",
                burst_count=2,
                support_counts=(2, 2, 2),
                fit_summary={"accepted_episode_count": 2.0},
            )

            store.upsert(single_episode_profile)

            self.assertIsNone(
                store.get_best_profile(
                    game="cod22",
                    canonical_weapon_id="cod22-m4",
                    stance="standing",
                    aim_mode="ads",
                )
            )

            store.upsert(ready_profile)

            self.assertEqual(
                store.get_best_profile(
                    game="cod22",
                    canonical_weapon_id="cod22-m4",
                    stance="standing",
                    aim_mode="ads",
                ).profile_id,
                ready_profile.profile_id,
            )
            self.assertEqual(
                store.profile_ids_for_weapon(
                    game="cod22",
                    canonical_weapon_id="cod22-m4",
                    stance="standing",
                ),
                (ready_profile.profile_id,),
            )

    def test_profile_statuses_explain_unready_candidates(self):
        runtime = _load_runtime_module()

        with tempfile.TemporaryDirectory() as temp_dir:
            store = runtime.RecoilProfileStore(Path(temp_dir))
            store.upsert(
                _profile_record(
                    profile_id="profile-cod22-m4-ads-standing-low-confidence-v1",
                    canonical_weapon_id="cod22-m4",
                    aim_mode="ads",
                    confidence=0.12,
                    profile_type="magazine_curve_v1",
                    burst_count=5,
                    support_counts=(5, 5, 5),
                    fit_summary={"accepted_episode_count": 5.0},
                )
            )

            self.assertEqual(
                store.profile_statuses_for_weapon(
                    game="cod22",
                    canonical_weapon_id="cod22-m4",
                    stance="standing",
                ),
                (
                    {
                        "profile_id": "profile-cod22-m4-ads-standing-low-confidence-v1",
                        "aim_mode": "ads",
                        "profile_type": "magazine_curve_v1",
                        "ready": True,
                        "reason": "ready",
                        "confidence": 0.12,
                        "burst_count": 5,
                        "accepted_episode_count": 5.0,
                    },
                ),
            )


class RecoilRuntimeTests(unittest.TestCase):
    def test_handle_switch_pressed_auto_creates_identity_and_publishes_state(self):
        runtime = _load_runtime_module()

        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            state_path = temp_path / "current_weapon.json"
            printed = io.StringIO()
            recoil_runtime = runtime.RecoilRuntime(
                game="cod22",
                identity_store=runtime.IdentityStore(temp_path / "identities"),
                profile_store=runtime.RecoilProfileStore(temp_path / "profiles"),
                state_path=state_path,
                frame_grabber_factory=lambda: _FakeFrameGrabber(np.zeros((1080, 1920, 3), dtype=np.uint8)),
                ocr_reader=lambda frame: [("box", "黑色组织传奇", 0.99)],
                sleep_fn=lambda seconds: None,
                timestamp_fn=lambda: "2026-05-06T18:00:00Z",
                switch_task_runner=lambda slot_index, switch_epoch, task: task(),
                learning_task_runner=lambda canonical_weapon_id, aim_mode, task: task(),
                stdout=printed,
            )

            recoil_runtime.handle_switch_pressed()

            self.assertIsNotNone(recoil_runtime.current_state)
            self.assertEqual(recoil_runtime.current_state.canonical_weapon_id, "cod22-黑色组织传奇")
            self.assertTrue(state_path.exists())
            self.assertIn("compensation=off(record)", printed.getvalue())

    def test_publish_state_includes_unready_profile_diagnostics(self):
        runtime = _load_runtime_module()

        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            state_path = temp_path / "current_weapon.json"
            profile_store = runtime.RecoilProfileStore(temp_path / "profiles")
            profile_store.upsert(
                _profile_record(
                    profile_id="profile-cod22-m4-ads-standing-low-confidence-v1",
                    canonical_weapon_id="cod22-m4",
                    aim_mode="ads",
                    confidence=0.08,
                    profile_type="magazine_curve_v1",
                    burst_count=1,
                    support_counts=(1, 1, 1),
                    fit_summary={"accepted_episode_count": 1.0},
                )
            )
            printed = io.StringIO()
            recoil_runtime = runtime.RecoilRuntime(
                game="cod22",
                mode="recoil",
                identity_store=runtime.IdentityStore(temp_path / "identities"),
                profile_store=profile_store,
                state_path=state_path,
                stdout=printed,
            )

            recoil_runtime.complete_switch_resolution(
                slot_index=0,
                switch_epoch=0,
                state=RecognizerState(
                    game="cod22",
                    canonical_weapon_id="cod22-m4",
                    confidence=0.92,
                    source="switch_text",
                    timestamp="2026-05-06T18:00:00Z",
                    degraded=False,
                    matched_name="M4",
                    profile_ids=(),
                ),
            )

            payload = json.loads(state_path.read_text(encoding="utf-8"))
            self.assertEqual(payload["profile_status"], "no_ready_profile:accepted_episodes_below_min")
            self.assertEqual(payload["active_profile_ids"], [])
            self.assertEqual(payload["profile_candidates"][0]["reason"], "accepted_episodes_below_min")
            self.assertIn("reason=no_ready_profile:accepted_episodes_below_min", printed.getvalue())

    def test_publish_state_refreshes_active_profile_ids_from_profile_store(self):
        runtime = _load_runtime_module()

        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            state_path = temp_path / "current_weapon.json"
            profile = _profile_record(
                profile_id="profile-cod22-m4-ads-standing-ready-v1",
                canonical_weapon_id="cod22-m4",
                aim_mode="ads",
                confidence=0.91,
                profile_type="magazine_curve_v1",
                burst_count=3,
                support_counts=(3, 3, 3),
                fit_summary={"accepted_episode_count": 3.0},
            )
            profile_store = runtime.RecoilProfileStore(temp_path / "profiles")
            profile_store.upsert(profile)
            _write_calibration(temp_path / "recoil_calibration", aim_mode="ads")
            recoil_runtime = runtime.RecoilRuntime(
                game="cod22",
                mode="recoil",
                identity_store=runtime.IdentityStore(temp_path / "identities"),
                profile_store=profile_store,
                state_path=state_path,
                stdout=io.StringIO(),
            )

            recoil_runtime.complete_switch_resolution(
                slot_index=0,
                switch_epoch=0,
                state=RecognizerState(
                    game="cod22",
                    canonical_weapon_id="cod22-m4",
                    confidence=0.92,
                    source="switch_text",
                    timestamp="2026-05-06T18:00:00Z",
                    degraded=False,
                    matched_name="M4",
                    profile_ids=(),
                ),
            )

            payload = json.loads(state_path.read_text(encoding="utf-8"))
            self.assertEqual(payload["profile_status"], "ready_profile")
            self.assertEqual(payload["active_profile_ids"], [profile.profile_id])

    def test_publish_state_log_lists_ready_and_unready_aim_modes(self):
        runtime = _load_runtime_module()

        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            state_path = temp_path / "current_weapon.json"
            profile_store = runtime.RecoilProfileStore(temp_path / "profiles")
            profile_store.upsert(
                _profile_record(
                    profile_id="profile-cod22-m4-ads-standing-ready-v1",
                    canonical_weapon_id="cod22-m4",
                    aim_mode="ads",
                    confidence=0.91,
                    profile_type="magazine_curve_v1",
                    burst_count=2,
                    support_counts=(2, 2, 2),
                    fit_summary={"accepted_episode_count": 2.0},
                )
            )
            profile_store.upsert(
                _profile_record(
                    profile_id="profile-cod22-m4-hipfire-standing-low-v1",
                    canonical_weapon_id="cod22-m4",
                    aim_mode="hipfire",
                    confidence=0.50,
                    profile_type="magazine_curve_v1",
                    burst_count=1,
                    support_counts=(1, 1, 1),
                    fit_summary={"accepted_episode_count": 1.0},
                )
            )
            _write_calibration(temp_path / "recoil_calibration", aim_mode="ads")
            printed = io.StringIO()
            recoil_runtime = runtime.RecoilRuntime(
                game="cod22",
                mode="recoil",
                identity_store=runtime.IdentityStore(temp_path / "identities"),
                profile_store=profile_store,
                state_path=state_path,
                stdout=printed,
            )

            recoil_runtime.complete_switch_resolution(
                slot_index=0,
                switch_epoch=0,
                state=RecognizerState(
                    game="cod22",
                    canonical_weapon_id="cod22-m4",
                    confidence=0.92,
                    source="switch_text",
                    timestamp="2026-05-06T18:00:00Z",
                    degraded=False,
                    matched_name="M4",
                    profile_ids=(),
                ),
            )

            output = printed.getvalue()
            self.assertIn("ready_modes=ads", output)
            self.assertIn("unready_modes=hipfire:accepted_episodes_below_min", output)

    def test_recoil_mode_allows_uncalibrated_profile_trial_when_calibration_is_missing(self):
        runtime = _load_runtime_module()

        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            profile_store = runtime.RecoilProfileStore(temp_path / "profiles")
            profile = _profile_record(
                profile_id="profile-cod22-m4-ads-standing-current",
                canonical_weapon_id="cod22-m4",
                aim_mode="ads",
                confidence=0.95,
                profile_type="magazine_curve_v1",
                burst_count=2,
                support_counts=(2, 2, 2),
                fit_summary={"accepted_episode_count": 2.0},
            )
            profile_store.upsert(profile)
            state_path = temp_path / "current_weapon.json"
            recoil_runtime = runtime.RecoilRuntime(
                game="cod22",
                mode="recoil",
                identity_store=runtime.IdentityStore(temp_path / "identities"),
                profile_store=profile_store,
                state_path=state_path,
                stdout=io.StringIO(),
            )
            recoil_runtime.complete_switch_resolution(
                slot_index=0,
                switch_epoch=0,
                state=RecognizerState(
                    game="cod22",
                    canonical_weapon_id="cod22-m4",
                    confidence=0.95,
                    source="switch_text",
                    timestamp="2026-05-20T00:00:00Z",
                    degraded=False,
                    matched_name="M4",
                    profile_ids=(),
                ),
            )

            payload = json.loads(state_path.read_text(encoding="utf-8"))
            active_profile = recoil_runtime.get_active_profile(aim_mode="ads")

        self.assertEqual(active_profile.profile_id, profile.profile_id)
        self.assertEqual(payload["profile_status"], "ready_profile")
        self.assertEqual(payload["active_profile_ids"], [profile.profile_id])
        self.assertFalse(payload["profile_candidates"][0]["calibration_available"])
        self.assertEqual(payload["profile_candidates"][0]["reason"], "ready")

    def test_recoil_mode_returns_profile_bundle_when_calibration_exists(self):
        runtime = _load_runtime_module()

        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            profile_store = runtime.RecoilProfileStore(temp_path / "profiles")
            profile = _profile_record(
                profile_id="profile-cod22-m4-ads-standing-current",
                canonical_weapon_id="cod22-m4",
                aim_mode="ads",
                confidence=0.95,
                profile_type="magazine_curve_v1",
                burst_count=2,
                support_counts=(2, 2, 2),
                fit_summary={"accepted_episode_count": 2.0},
            )
            profile_store.upsert(profile)
            calibration_dir = temp_path / "recoil_calibration"
            _write_calibration(calibration_dir, aim_mode="ads")
            recoil_runtime = runtime.RecoilRuntime(
                game="cod22",
                mode="recoil",
                identity_store=runtime.IdentityStore(temp_path / "identities"),
                profile_store=profile_store,
                calibration_dir=calibration_dir,
                stdout=io.StringIO(),
            )
            recoil_runtime.complete_switch_resolution(
                slot_index=0,
                switch_epoch=0,
                state=RecognizerState(
                    game="cod22",
                    canonical_weapon_id="cod22-m4",
                    confidence=0.95,
                    source="switch_text",
                    timestamp="2026-05-20T00:00:00Z",
                    degraded=False,
                    matched_name="M4",
                    profile_ids=(),
                ),
            )

            active_profile, calibration = recoil_runtime.get_active_profile(aim_mode="ads")

        self.assertEqual(active_profile.profile_id, profile.profile_id)
        self.assertEqual(calibration.game, "cod22")
        self.assertEqual(calibration.aim_mode, "ads")

    def test_fire_rising_edge_starts_learning_only_when_profile_is_missing(self):
        runtime = _load_runtime_module()

        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            learning_calls = []
            recoil_runtime = runtime.RecoilRuntime(
                game="cod22",
                identity_store=runtime.IdentityStore(temp_path / "identities"),
                profile_store=runtime.RecoilProfileStore(temp_path / "profiles"),
                frame_grabber_factory=lambda: _FakeFrameGrabber(np.zeros((1080, 1920, 3), dtype=np.uint8)),
                ocr_reader=lambda frame: [("box", "黑色组织传奇", 0.99)],
                sleep_fn=lambda seconds: None,
                timestamp_fn=lambda: "2026-05-06T18:00:00Z",
                switch_task_runner=lambda slot_index, switch_epoch, task: task(),
                learning_task_runner=lambda canonical_weapon_id, aim_mode, task: learning_calls.append(
                    (canonical_weapon_id, aim_mode)
                ),
            )
            recoil_runtime.complete_switch_resolution(
                slot_index=0,
                switch_epoch=0,
                state=RecognizerState(
                    game="cod22",
                    canonical_weapon_id="cod22-黑色组织传奇",
                    confidence=0.95,
                    source="switch_text",
                    timestamp="2026-05-06T18:00:00Z",
                    degraded=False,
                    matched_name="黑色组织传奇",
                    profile_ids=(),
                ),
            )

            recoil_runtime.handle_fire_state(is_firing=False, aim_mode="ads")
            recoil_runtime.handle_fire_state(is_firing=True, aim_mode="ads")
            recoil_runtime.handle_fire_state(is_firing=True, aim_mode="ads")

            self.assertEqual(learning_calls, [("cod22-黑色组织传奇", "ads")])

    def test_fire_rising_edge_keeps_learning_when_only_unready_profile_exists(self):
        runtime = _load_runtime_module()

        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            profile_store = runtime.RecoilProfileStore(temp_path / "profiles")
            profile_store.upsert(
                _profile_record(
                    profile_id="profile-cod22-m4-ads-standing-low-v1",
                    canonical_weapon_id="cod22-m4",
                    aim_mode="ads",
                    confidence=0.95,
                    profile_type="magazine_curve_v1",
                    burst_count=1,
                    support_counts=(1, 1, 1),
                    fit_summary={"accepted_episode_count": 1.0},
                )
            )
            learning_calls = []
            recoil_runtime = runtime.RecoilRuntime(
                game="cod22",
                identity_store=runtime.IdentityStore(temp_path / "identities"),
                profile_store=profile_store,
                frame_grabber_factory=lambda: _FakeFrameGrabber(np.zeros((1080, 1920, 3), dtype=np.uint8)),
                ocr_reader=lambda frame: [("box", "m4", 0.99)],
                sleep_fn=lambda seconds: None,
                timestamp_fn=lambda: "2026-05-06T18:00:00Z",
                switch_task_runner=lambda slot_index, switch_epoch, task: task(),
                learning_task_runner=lambda canonical_weapon_id, aim_mode, task: learning_calls.append(
                    (canonical_weapon_id, aim_mode)
                ),
            )
            recoil_runtime.complete_switch_resolution(
                slot_index=0,
                switch_epoch=0,
                state=RecognizerState(
                    game="cod22",
                    canonical_weapon_id="cod22-m4",
                    confidence=0.95,
                    source="switch_text",
                    timestamp="2026-05-06T18:00:00Z",
                    degraded=False,
                    matched_name="m4",
                    profile_ids=(),
                ),
            )

            recoil_runtime.handle_fire_state(is_firing=False, aim_mode="ads")
            recoil_runtime.handle_fire_state(is_firing=True, aim_mode="ads")

            self.assertEqual(learning_calls, [("cod22-m4", "ads")])

    def test_learning_capture_refits_magazine_profile_across_multiple_recordings(self):
        runtime = _load_runtime_module()

        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            recoil_runtime = runtime.RecoilRuntime(
                game="cod22",
                identity_store=runtime.IdentityStore(temp_path / "identities"),
                profile_store=runtime.RecoilProfileStore(temp_path / "profiles"),
                collector_config=RecoilCollectorConfig(
                    capture_fps=100,
                    min_clean_bursts=1,
                    target_clean_bursts=2,
                ),
                timestamp_fn=lambda: "2026-05-06T18:00:00Z",
            )
            state = RecognizerState(
                game="cod22",
                canonical_weapon_id="cod22-m4",
                confidence=0.95,
                source="switch_text",
                timestamp="2026-05-06T18:00:00Z",
                degraded=False,
                matched_name="m4",
                profile_ids=(),
            )
            first_result = _magazine_collection_result(
                canonical_weapon_id="cod22-m4",
                aim_mode="ads",
                y_values=(0.0, -1.0, -1.0, -2.0),
                timestamp="2026-05-06T18:00:00Z",
            )
            second_result = _magazine_collection_result(
                canonical_weapon_id="cod22-m4",
                aim_mode="ads",
                y_values=(0.0, -1.2, -1.2, -2.2),
                timestamp="2026-05-06T18:00:02Z",
            )

            with patch.object(runtime, "collect_recoil_profile", side_effect=[first_result, second_result]):
                recoil_runtime._run_learning_capture(current_state=state, aim_mode="ads")
                self.assertIsNone(
                    recoil_runtime.profile_store.get_best_profile(
                        game="cod22",
                        canonical_weapon_id="cod22-m4",
                        stance="standing",
                        aim_mode="ads",
                    )
                )

                recoil_runtime._run_learning_capture(current_state=state, aim_mode="ads")

            profile = recoil_runtime.profile_store.get_best_profile(
                game="cod22",
                canonical_weapon_id="cod22-m4",
                stance="standing",
                aim_mode="ads",
            )
            self.assertIsNotNone(profile)
            self.assertEqual(profile.profile_type, "magazine_curve_v1")
            self.assertEqual(profile.profile_id, "profile-cod22-m4-ads-standing-current")
            self.assertEqual(profile.support_counts, (2, 2, 2, 2))
            self.assertEqual(profile.fit_summary["accepted_episode_count"], 2.0)
            self.assertEqual(profile.samples_y, (0.0, -1.1, -1.1, -2.1))
            profile_files = sorted(
                path.name
                for path in (temp_path / "profiles").glob("profile-cod22-m4-ads-standing*.json")
                if not path.name.endswith(".summary.json")
            )
            summary_files = sorted(
                path.name
                for path in (temp_path / "profiles").glob("profile-cod22-m4-ads-standing*.summary.json")
            )
            episode_files = sorted((temp_path / "profiles" / "_episodes").glob("episode-*.json"))
            self.assertEqual(profile_files, ["profile-cod22-m4-ads-standing-current.json"])
            self.assertEqual(summary_files, ["profile-cod22-m4-ads-standing-current.summary.json"])
            self.assertEqual(len(episode_files), 2)

    def test_magazine_episode_storage_preserves_pause_plateaus_from_full_recording_trace(self):
        runtime = _load_runtime_module()

        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            recoil_runtime = runtime.RecoilRuntime(
                game="cod22",
                identity_store=runtime.IdentityStore(temp_path / "identities"),
                profile_store=runtime.RecoilProfileStore(temp_path / "profiles"),
                stdout=io.StringIO(),
            )
            state = RecognizerState(
                game="cod22",
                canonical_weapon_id="cod22-m4",
                confidence=0.95,
                source="switch_text",
                timestamp="2026-05-06T18:00:00Z",
                degraded=False,
                matched_name="M4",
                profile_ids=(),
            )
            session = RecoilCollectionSession(
                session_id="session-cod22-m4-ads-standing-segmented",
                canonical_weapon_id="cod22-m4",
                game="cod22",
                stance="standing",
                aim_mode="ads",
                capture_resolution="640x640",
                capture_fps=100.0,
                collector_version="test",
                started_at="2026-05-06T18:00:00Z",
            )
            windows = (
                RecoilBurstWindow(
                    burst_id=f"{session.session_id}-burst-001",
                    session_id=session.session_id,
                    start_offset_ms=0,
                    end_offset_ms=30,
                    start_reason="motion",
                    end_reason="motion_settled",
                ),
                RecoilBurstWindow(
                    burst_id=f"{session.session_id}-burst-002",
                    session_id=session.session_id,
                    start_offset_ms=50,
                    end_offset_ms=70,
                    start_reason="motion",
                    end_reason="end_of_samples",
                ),
            )
            burst_series = (
                _burst_series_from_y_values(
                    burst_id=windows[0].burst_id,
                    session_id=session.session_id,
                    y_values=(0.0, -1.0, -2.0),
                    start_offset_ms=0,
                ),
                _burst_series_from_y_values(
                    burst_id=windows[1].burst_id,
                    session_id=session.session_id,
                    y_values=(-2.0, -3.0),
                    start_offset_ms=50,
                ),
            )
            motion_samples = tuple(
                MotionTraceSample(offset_ms=index * 10, x=0.0, y=value, center_motion=0.0)
                for index, value in enumerate((0.0, -1.0, -2.0, -2.0, -2.0, -3.0, -4.0))
            )

            recoil_runtime._save_magazine_episode_series(
                current_state=state,
                aim_mode="ads",
                captured_at="2026-05-06T18:00:00Z",
                burst_series=burst_series,
                motion_samples=motion_samples,
                burst_windows=windows,
            )

            loaded = recoil_runtime._load_magazine_episode_series(
                game="cod22",
                canonical_weapon_id="cod22-m4",
                aim_mode="ads",
            )
            self.assertEqual(len(loaded), 1)
            self.assertEqual(loaded[0].sample_count, 7)
            self.assertEqual(tuple(sample.offset_ms for sample in loaded[0].samples), (0, 10, 20, 30, 40, 50, 60))
            self.assertEqual(tuple(sample.y for sample in loaded[0].samples), (0.0, -1.0, -2.0, -2.0, -2.0, -3.0, -4.0))

    def test_magazine_episode_storage_includes_quality_diagnostics(self):
        runtime = _load_runtime_module()

        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            recoil_runtime = runtime.RecoilRuntime(
                game="cod22",
                identity_store=runtime.IdentityStore(temp_path / "identities"),
                profile_store=runtime.RecoilProfileStore(temp_path / "profiles"),
                stdout=io.StringIO(),
            )
            state = RecognizerState(
                game="cod22",
                canonical_weapon_id="cod22-m4",
                confidence=0.95,
                source="switch_text",
                timestamp="2026-05-20T00:00:00Z",
                degraded=False,
                matched_name="M4",
                profile_ids=(),
            )
            series = _burst_series_from_y_values(
                burst_id="session-a-burst-001",
                session_id="session-a",
                y_values=(0.0, -6.0, -8.0, 10.0),
            )

            recoil_runtime._save_magazine_episode_series(
                current_state=state,
                aim_mode="ads",
                captured_at="2026-05-20T00:00:00Z",
                burst_series=(series,),
            )

            episode_file = next((temp_path / "profiles" / "_episodes").glob("episode-*.json"))
            payload = json.loads(episode_file.read_text(encoding="utf-8"))

        self.assertIn("diagnostics", payload)
        self.assertEqual(payload["diagnostics"]["sample_count"], 4)
        self.assertEqual(payload["diagnostics"]["vertical_min"], -8.0)
        self.assertEqual(payload["diagnostics"]["vertical_max"], 10.0)
        self.assertIn("vertical_direction_reversal", payload["diagnostics"]["findings"])

    def test_load_magazine_episode_series_ignores_legacy_short_fragments_when_full_episodes_exist(self):
        runtime = _load_runtime_module()

        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            recoil_runtime = runtime.RecoilRuntime(
                game="cod22",
                identity_store=runtime.IdentityStore(temp_path / "identities"),
                profile_store=runtime.RecoilProfileStore(temp_path / "profiles"),
                stdout=io.StringIO(),
            )
            state = RecognizerState(
                game="cod22",
                canonical_weapon_id="cod22-m4",
                confidence=0.95,
                source="switch_text",
                timestamp="2026-05-06T18:00:00Z",
                degraded=False,
                matched_name="M4",
                profile_ids=(),
            )
            long_episode = _burst_series_from_y_values(
                burst_id="session-long-burst-001",
                session_id="session-long",
                y_values=tuple(float(index * -3) for index in range(60)),
            )
            short_fragment = _burst_series_from_y_values(
                burst_id="session-short-burst-001",
                session_id="session-short",
                y_values=(0.0, -2.0, -4.0),
            )

            recoil_runtime._save_magazine_episode_series(
                current_state=state,
                aim_mode="ads",
                captured_at="2026-05-06T18:00:00Z",
                burst_series=(short_fragment,),
            )
            recoil_runtime._save_magazine_episode_series(
                current_state=state,
                aim_mode="ads",
                captured_at="2026-05-06T18:00:02Z",
                burst_series=(long_episode,),
            )

            loaded = recoil_runtime._load_magazine_episode_series(
                game="cod22",
                canonical_weapon_id="cod22-m4",
                aim_mode="ads",
            )
            self.assertEqual(tuple(series.burst_id for series in loaded), ("session-long-burst-001",))

    def test_learning_capture_writes_recoil_and_anti_recoil_trajectory_plots(self):
        runtime = _load_runtime_module()

        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            plot_dir = temp_path / "plots"
            printed = io.StringIO()
            recoil_runtime = runtime.RecoilRuntime(
                game="cod22",
                identity_store=runtime.IdentityStore(temp_path / "identities"),
                profile_store=runtime.RecoilProfileStore(temp_path / "profiles"),
                plot_dir=plot_dir,
                collector_config=RecoilCollectorConfig(
                    capture_fps=100,
                    min_clean_bursts=1,
                    target_clean_bursts=1,
                ),
                timestamp_fn=lambda: "2026-05-06T18:00:00Z",
                stdout=printed,
            )
            state = RecognizerState(
                game="cod22",
                canonical_weapon_id="cod22-m4",
                confidence=0.95,
                source="switch_text",
                timestamp="2026-05-06T18:00:00Z",
                degraded=False,
                matched_name="m4",
                profile_ids=(),
            )
            result = _magazine_collection_result(
                canonical_weapon_id="cod22-m4",
                aim_mode="ads",
                y_values=(0.0, -1.0, -1.0, -2.0),
                timestamp="2026-05-06T18:00:00Z",
            )

            with patch.object(runtime, "collect_recoil_profile", return_value=result):
                recoil_runtime._run_learning_capture(current_state=state, aim_mode="ads")

            profile_id = "profile-cod22-m4-ads-standing-current"
            self.assertTrue((plot_dir / f"{profile_id}.recoil.png").exists())
            self.assertTrue((plot_dir / f"{profile_id}.anti_recoil.png").exists())
            self.assertTrue((plot_dir / f"{profile_id}.timeline.png").exists())
            self.assertFalse((plot_dir / f"{profile_id}.png").exists())
            self.assertIn("plot_written", printed.getvalue())
            self.assertNotIn("learn_error", printed.getvalue())

    def test_learning_capture_writes_episode_fit_and_replay_plots(self):
        runtime = _load_runtime_module()

        with tempfile.TemporaryDirectory() as temp_dir:
            plot_dir = Path(temp_dir) / "plots"
            profile = _profile_record(
                profile_id="profile-cod22-m4-ads-standing-current",
                canonical_weapon_id="cod22-m4",
                aim_mode="ads",
                confidence=0.95,
                profile_type="magazine_curve_v1",
                burst_count=2,
                support_counts=(2, 2, 2),
                fit_summary={"accepted_episode_count": 2.0},
            )

            paths = runtime._write_profile_plots(plot_dir, profile)

        names = {path.name for path in paths}
        self.assertIn("profile-cod22-m4-ads-standing-current.recoil.png", names)
        self.assertIn("profile-cod22-m4-ads-standing-current.anti_recoil.png", names)
        self.assertIn("profile-cod22-m4-ads-standing-current.timeline.png", names)

    def test_profile_plot_writer_uses_unicode_safe_png_output(self):
        runtime = _load_runtime_module()

        with tempfile.TemporaryDirectory() as temp_dir:
            plot_dir = Path(temp_dir) / "plots"
            profile = _profile_record(
                profile_id="profile-cod22-\u5c0f\u52a8\u8109-ads-standing-current",
                canonical_weapon_id="cod22-\u5c0f\u52a8\u8109",
                aim_mode="ads",
                confidence=0.95,
                profile_type="magazine_curve_v1",
                burst_count=2,
                support_counts=(2, 2, 2),
                fit_summary={"accepted_episode_count": 2.0},
            )

            with patch("cv2.imwrite", return_value=False):
                recoil_path, anti_recoil_path, timeline_path = runtime._write_profile_plots(plot_dir, profile)

            self.assertTrue(recoil_path.exists())
            self.assertTrue(anti_recoil_path.exists())
            self.assertTrue(timeline_path.exists())
            self.assertGreater(recoil_path.stat().st_size, 0)
            self.assertGreater(anti_recoil_path.stat().st_size, 0)
            self.assertGreater(timeline_path.stat().st_size, 0)

    def test_recoil_mode_never_starts_learning_session(self):
        runtime = _load_runtime_module()

        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            learning_calls = []
            recoil_runtime = runtime.RecoilRuntime(
                game="cod22",
                mode="recoil",
                identity_store=runtime.IdentityStore(temp_path / "identities"),
                profile_store=runtime.RecoilProfileStore(temp_path / "profiles"),
                frame_grabber_factory=lambda: _FakeFrameGrabber(np.zeros((1080, 1920, 3), dtype=np.uint8)),
                ocr_reader=lambda frame: [("box", "黑色组织传奇", 0.99)],
                sleep_fn=lambda seconds: None,
                timestamp_fn=lambda: "2026-05-06T18:00:00Z",
                switch_task_runner=lambda slot_index, switch_epoch, task: task(),
                learning_task_runner=lambda canonical_weapon_id, aim_mode, task: learning_calls.append(
                    (canonical_weapon_id, aim_mode)
                ),
            )
            recoil_runtime.complete_switch_resolution(
                slot_index=0,
                switch_epoch=0,
                state=RecognizerState(
                    game="cod22",
                    canonical_weapon_id="cod22-黑色组织传奇",
                    confidence=0.95,
                    source="switch_text",
                    timestamp="2026-05-06T18:00:00Z",
                    degraded=False,
                    matched_name="黑色组织传奇",
                    profile_ids=(),
                ),
            )
            recoil_runtime.handle_fire_state(is_firing=True, aim_mode="ads")

            self.assertEqual(learning_calls, [])

    def test_record_mode_does_not_expose_active_profile_for_compensation(self):
        runtime = _load_runtime_module()

        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            profile_store = runtime.RecoilProfileStore(temp_path / "profiles")
            expected_profile = _profile_record(
                profile_id="profile-cod22-榛戣壊缁勭粐浼犲-ads-standing-v1",
                canonical_weapon_id="cod22-榛戣壊缁勭粐浼犲",
                aim_mode="ads",
                confidence=0.94,
            )
            profile_store.upsert(expected_profile)
            recoil_runtime = runtime.RecoilRuntime(
                game="cod22",
                mode="record",
                identity_store=runtime.IdentityStore(temp_path / "identities"),
                profile_store=profile_store,
            )
            recoil_runtime.complete_switch_resolution(
                slot_index=0,
                switch_epoch=0,
                state=RecognizerState(
                    game="cod22",
                    canonical_weapon_id="cod22-榛戣壊缁勭粐浼犲",
                    confidence=0.95,
                    source="switch_text",
                    timestamp="2026-05-06T18:00:00Z",
                    degraded=False,
                    matched_name="榛戣壊缁勭粐浼犲",
                    profile_ids=(expected_profile.profile_id,),
                ),
            )

            self.assertIsNone(recoil_runtime.get_active_profile(aim_mode="ads"))

    def test_record_mode_defaults_to_single_window_learning_threshold(self):
        runtime = _load_runtime_module()

        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            recoil_runtime = runtime.RecoilRuntime(
                game="cod22",
                mode="record",
                identity_store=runtime.IdentityStore(temp_path / "identities"),
                profile_store=runtime.RecoilProfileStore(temp_path / "profiles"),
            )

            self.assertEqual(recoil_runtime._collector_config.min_clean_bursts, 1)
            self.assertEqual(recoil_runtime._collector_config.capture_fps, 60)

    def test_select_best_name_trims_trailing_ammo_and_ui_noise(self):
        runtime = _load_runtime_module()

        selected = runtime._select_best_name(
            [
                "点22塔恩托 40 999 5097081 3509110813 00478901G 50911081 ngc 3509110813+",
                "格克霍娃",
            ]
        )

        self.assertEqual(selected, "点22塔恩托")

    def test_select_best_name_prefers_joined_short_two_line_weapon_name(self):
        runtime = _load_runtime_module()

        selected = runtime._select_best_name(
            [
                "KT-3",
                "勇士",
                "KT-3勇士",
            ]
        )

        self.assertEqual(selected, "KT-3勇士")

    def test_select_best_name_rejects_ammo_labels_and_prefers_weapon_name(self):
        runtime = _load_runtime_module()

        selected = runtime._select_best_name(
            [
                "9毫米鲁格手枪弹",
                "7.62BLK",
                "RAM-9",
            ]
        )

        self.assertEqual(selected, "RAM-9")

    def test_cod20_and_cod21_switch_capture_roi_reads_full_primary_crop(self):
        runtime = _load_runtime_module()

        self.assertEqual(runtime._build_switch_capture_roi("cod20"), runtime._FULL_FRAME_ROI)
        self.assertEqual(runtime._build_switch_capture_roi("cod21"), runtime._FULL_FRAME_ROI)

    def test_handle_switch_pressed_swallows_switch_capture_errors(self):
        runtime = _load_runtime_module()

        with tempfile.TemporaryDirectory() as temp_dir:
            printed = io.StringIO()
            recoil_runtime = runtime.RecoilRuntime(
                game="cod22",
                identity_store=runtime.IdentityStore(Path(temp_dir) / "identities"),
                profile_store=runtime.RecoilProfileStore(Path(temp_dir) / "profiles"),
                frame_grabber_factory=lambda: _RaisingFrameGrabber(RuntimeError("capture failed")),
                sleep_fn=lambda seconds: None,
                timestamp_fn=lambda: "2026-05-06T18:00:00Z",
                switch_task_runner=lambda slot_index, switch_epoch, task: task(),
                stdout=printed,
            )

            recoil_runtime.handle_switch_pressed()

            self.assertIsNone(recoil_runtime.current_state)
            self.assertIn("switch_capture_error", printed.getvalue())

    def test_handle_switch_pressed_rejects_numeric_only_ocr_name(self):
        runtime = _load_runtime_module()

        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            recoil_runtime = runtime.RecoilRuntime(
                game="cod22",
                identity_store=runtime.IdentityStore(temp_path / "identities"),
                profile_store=runtime.RecoilProfileStore(temp_path / "profiles"),
                frame_grabber_factory=lambda: _FakeFrameGrabber(np.zeros((1080, 1920, 3), dtype=np.uint8)),
                ocr_reader=lambda frame: [("box", "7 133", 0.99)],
                sleep_fn=lambda seconds: None,
                timestamp_fn=lambda: "2026-05-06T18:00:00Z",
                switch_task_runner=lambda slot_index, switch_epoch, task: task(),
            )

            recoil_runtime.handle_switch_pressed()

            self.assertIsNone(recoil_runtime.current_state)
            self.assertEqual(list((temp_path / "identities").glob("identity-*.json")), [])

    def test_handle_switch_pressed_reuses_cached_frame_grabber(self):
        runtime = _load_runtime_module()

        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            factory_calls = []
            recoil_runtime = runtime.RecoilRuntime(
                game="cod22",
                identity_store=runtime.IdentityStore(temp_path / "identities"),
                profile_store=runtime.RecoilProfileStore(temp_path / "profiles"),
                frame_grabber_factory=lambda: factory_calls.append("factory") or _FakeFrameGrabber(
                    np.zeros((1080, 1920, 3), dtype=np.uint8)
                ),
                ocr_reader=lambda frame: [("box", "榛戣壊缁勭粐浼犲", 0.99)],
                sleep_fn=lambda seconds: None,
                timestamp_fn=lambda: "2026-05-06T18:00:00Z",
                switch_task_runner=lambda slot_index, switch_epoch, task: task(),
            )

            recoil_runtime.handle_switch_pressed()
            recoil_runtime.handle_switch_pressed()

            self.assertEqual(factory_calls, ["factory"])

    def test_handle_switch_pressed_stops_after_first_valid_single_pass_hit(self):
        runtime = _load_runtime_module()

        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            frame_grabber = _FakeFrameGrabber(np.zeros((120, 320, 3), dtype=np.uint8))
            recoil_runtime = runtime.RecoilRuntime(
                game="cod22",
                identity_store=runtime.IdentityStore(temp_path / "identities"),
                profile_store=runtime.RecoilProfileStore(temp_path / "profiles"),
                frame_grabber_factory=lambda: frame_grabber,
                sleep_fn=lambda seconds: None,
                timestamp_fn=lambda: "2026-05-06T18:00:00Z",
                switch_task_runner=lambda slot_index, switch_epoch, task: task(),
            )

            with patch.object(runtime, "extract_text_candidates", side_effect=[("黑色组织传奇",)]) as mock_extract:
                recoil_runtime.handle_switch_pressed()

            self.assertEqual(frame_grabber.grab_calls, 1)
            self.assertEqual(mock_extract.call_count, 1)
            self.assertIsNotNone(recoil_runtime.current_state)

    def test_switch_capture_delays_are_clamped_to_300ms_or_above(self):
        runtime = _load_runtime_module()

        recoil_runtime = runtime.RecoilRuntime(
            game="cod22",
            identity_store=runtime.IdentityStore(Path(tempfile.gettempdir()) / "recoil-test-identities"),
            profile_store=runtime.RecoilProfileStore(Path(tempfile.gettempdir()) / "recoil-test-profiles"),
            switch_capture_delays_ms=(150, 240, 340, 460),
        )

        self.assertEqual(recoil_runtime._resolve_switch_capture_delays_ms(), (600, 760))

    def test_handle_switch_pressed_always_launches_capture_even_when_previous_state_exists(self):
        runtime = _load_runtime_module()

        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            switch_tasks = []
            recoil_runtime = runtime.RecoilRuntime(
                game="cod22",
                identity_store=runtime.IdentityStore(temp_path / "identities"),
                profile_store=runtime.RecoilProfileStore(temp_path / "profiles"),
                switch_task_runner=lambda slot_index, switch_epoch, task: switch_tasks.append(
                    {"slot_index": slot_index, "switch_epoch": switch_epoch, "task": task}
                ),
                stdout=io.StringIO(),
            )
            recoil_runtime.complete_switch_resolution(
                slot_index=1,
                switch_epoch=0,
                state=RecognizerState(
                    game="cod22",
                    canonical_weapon_id="cod22-OCP火器",
                    confidence=0.95,
                    source="switch_text",
                    timestamp="2026-05-06T18:00:00Z",
                    degraded=False,
                    matched_name="OCP火器",
                    profile_ids=(),
                ),
            )

            recoil_runtime.handle_switch_pressed()

            self.assertIsNone(recoil_runtime.current_state)
            self.assertEqual(len(switch_tasks), 1)
            self.assertEqual(switch_tasks[0]["slot_index"], 1)

    def test_runtime_motion_sampler_uses_polling_capture_source_instead_of_screen_capture_thread(self):
        runtime = _load_runtime_module()

        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            recoil_runtime = runtime.RecoilRuntime(
                game="cod22",
                identity_store=runtime.IdentityStore(temp_path / "identities"),
                profile_store=runtime.RecoilProfileStore(temp_path / "profiles"),
                motion_frame_grabber_factory=lambda: _FakeFrameGrabber(np.zeros((64, 64, 3), dtype=np.uint8)),
            )
            sampler = recoil_runtime._build_runtime_motion_sampler()
            capture_types = []

            def _fake_collect_motion_trace_from_thread(*, capture_thread, config, fire_input_source):
                del config
                del fire_input_source
                capture_types.append(type(capture_thread).__name__)
                return ()

            with patch.object(runtime, "_collect_motion_trace_from_thread", side_effect=_fake_collect_motion_trace_from_thread):
                self.assertEqual(sampler(), ())

            self.assertEqual(capture_types, ["_PollingFrameCaptureSource"])


class RecoilFrameGrabberTests(unittest.TestCase):
    def test_image_grab_frame_grabber_uses_bbox_region_capture(self):
        runtime = _load_runtime_module()
        calls = []

        class _FakeImageGrabModule:
            @staticmethod
            def grab(*, bbox=None, all_screens=False):
                calls.append({"bbox": bbox, "all_screens": all_screens})
                return np.zeros((64, 128, 3), dtype=np.uint8)

        frame_grabber = runtime._ImageGrabFrameGrabber(
            image_grab_module=_FakeImageGrabModule,
            bbox=(1600, 900, 1856, 1040),
            all_screens=False,
        )

        frame = frame_grabber.grab()

        self.assertEqual(frame.shape, (64, 128, 3))
        self.assertEqual(calls, [{"bbox": (1600, 900, 1856, 1040), "all_screens": False}])

    def test_backend_frame_grabber_retries_initial_empty_dxgi_frame(self):
        runtime = _load_runtime_module()
        expected = np.zeros((64, 128, 3), dtype=np.uint8)
        calls = []

        class _FakeBackend:
            def __init__(self):
                self.closed = False

            def grab(self):
                calls.append("grab")
                if len(calls) == 1:
                    return None
                return expected

            def close(self):
                self.closed = True

        backend = _FakeBackend()
        frame_grabber = runtime._BackendSwitchFrameGrabber(
            backend=backend,
            retry_attempts=3,
            retry_delay_seconds=0.0,
            sleep_fn=lambda seconds: None,
        )

        frame = frame_grabber.grab()

        self.assertIs(frame, expected)
        self.assertEqual(calls, ["grab", "grab"])

    def test_build_primary_display_roi_bbox_uses_wider_padding_for_cod22_text(self):
        runtime = _load_runtime_module()
        adapter = runtime.get_adapter("cod22")

        bbox = runtime._build_primary_display_roi_bbox(adapter.weapon_name_text_roi)

        self.assertGreater(bbox[2] - bbox[0], 300)


class _FakeFrameGrabber:
    def __init__(self, frame):
        self.frame = frame
        self.grab_calls = 0

    def grab(self):
        self.grab_calls += 1
        return self.frame

    def close(self):
        return None


class _RaisingFrameGrabber:
    def __init__(self, error: Exception):
        self.error = error

    def grab(self):
        raise self.error

    def close(self):
        return None


def _profile_record(
    *,
    profile_id: str,
    canonical_weapon_id: str,
    aim_mode: str,
    confidence: float,
    profile_type: str = "burst_average_v1",
    burst_count: int = 4,
    support_counts: tuple[int, ...] = (),
    fit_summary: dict[str, float] | None = None,
):
    return RecoilProfileRecord(
        profile_id=profile_id,
        canonical_weapon_id=canonical_weapon_id,
        game="cod22",
        stance="standing",
        aim_mode=aim_mode,
        sample_interval_ms=10,
        duration_ms=30,
        initial_delay_ms=0,
        samples_x=(0.0, 0.0, 0.0),
        samples_y=(0.0, -40.0, -80.0),
        sample_count=3,
        burst_count=burst_count,
        variance_summary={"horizontal_stddev": 0.1, "vertical_stddev": 0.2},
        confidence=confidence,
        capture_resolution="2560x1440",
        capture_fps=144.0,
        collector_version="test",
        created_at="2026-05-06T18:00:00Z",
        profile_type=profile_type,
        support_counts=support_counts,
        fit_summary=fit_summary or {},
    )


def _magazine_collection_result(
    *,
    canonical_weapon_id: str,
    aim_mode: str,
    y_values: tuple[float, ...],
    timestamp: str,
):
    session = RecoilCollectionSession(
        session_id=f"session-{canonical_weapon_id}-{aim_mode}-standing-{timestamp.replace(':', '')}",
        canonical_weapon_id=canonical_weapon_id,
        game="cod22",
        stance="standing",
        aim_mode=aim_mode,
        capture_resolution="640x640",
        capture_fps=100.0,
        collector_version="test",
        started_at=timestamp,
    )
    burst = RecoilBurstSampleSeries(
        burst_id=f"{session.session_id}-mag-001",
        session_id=session.session_id,
        sample_interval_ms=10,
        samples=tuple(
            RecoilSample(offset_ms=index * 10, x=0.0, y=value)
            for index, value in enumerate(y_values)
        ),
        sample_count=len(y_values),
    )
    profile = RecoilProfileRecord(
        profile_id=f"profile-{canonical_weapon_id}-{aim_mode}-standing-{timestamp.replace(':', '')}",
        canonical_weapon_id=canonical_weapon_id,
        game="cod22",
        stance="standing",
        aim_mode=aim_mode,
        sample_interval_ms=10,
        duration_ms=len(y_values) * 10,
        initial_delay_ms=0,
        samples_x=tuple(0.0 for _ in y_values),
        samples_y=y_values,
        sample_count=len(y_values),
        burst_count=1,
        variance_summary={"horizontal_stddev": 0.0, "vertical_stddev": 0.0},
        confidence=0.5,
        capture_resolution="640x640",
        capture_fps=100.0,
        collector_version="test",
        created_at=timestamp,
        profile_type="magazine_curve_v1",
        support_counts=tuple(1 for _ in y_values),
        fit_summary={"accepted_episode_count": 1.0},
    )
    summary = RecoilProfileSummary(
        profile_id=profile.profile_id,
        canonical_weapon_id=profile.canonical_weapon_id,
        game=profile.game,
        stance=profile.stance,
        aim_mode=profile.aim_mode,
        sample_count=profile.sample_count,
        burst_count=profile.burst_count,
        confidence=profile.confidence,
        peak_abs_x=0.0,
        peak_abs_y=max(abs(value) for value in profile.samples_y),
        created_at=profile.created_at,
    )
    return SimpleNamespace(
        session=session,
        burst_series=(burst,),
        motion_samples=tuple(
            MotionTraceSample(
                offset_ms=index * 10,
                x=0.0,
                y=value,
                center_motion=0.0,
            )
            for index, value in enumerate(y_values)
        ),
        burst_windows=(
            RecoilBurstWindow(
                burst_id=burst.burst_id,
                session_id=session.session_id,
                start_offset_ms=0,
                end_offset_ms=len(y_values) * 10,
                start_reason="manual",
                end_reason="manual",
            ),
        ),
        extracted_profile=SimpleNamespace(profile=profile),
        profile_summary=summary,
    )


def _burst_series_from_y_values(
    *,
    burst_id: str,
    session_id: str,
    y_values: tuple[float, ...],
    start_offset_ms: int = 0,
    sample_interval_ms: int = 10,
) -> RecoilBurstSampleSeries:
    return RecoilBurstSampleSeries(
        burst_id=burst_id,
        session_id=session_id,
        sample_interval_ms=sample_interval_ms,
        samples=tuple(
            RecoilSample(offset_ms=start_offset_ms + (index * sample_interval_ms), x=0.0, y=value)
            for index, value in enumerate(y_values)
        ),
        sample_count=len(y_values),
    )


def _write_calibration(root: Path, *, aim_mode: str) -> None:
    from vision.recoil_collection.calibration import RecoilControlCalibration
    from vision.recoil_collection.calibration import save_calibration

    save_calibration(
        root / f"cod22-{aim_mode}-standing.json",
        RecoilControlCalibration(
            game="cod22",
            aim_mode=aim_mode,
            stance="standing",
            pixels_per_full_stick_x_per_second=500.0,
            pixels_per_full_stick_y_per_second=1000.0,
            created_at="2026-05-20T00:00:00Z",
        ),
    )


if __name__ == "__main__":
    unittest.main()
