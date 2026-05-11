import io
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import numpy as np

from runtime.recoil_sidecar.models import RecognizerState
from vision.recoil_collection.models import RecoilProfileRecord


def _load_runtime_module():
    import importlib

    try:
        return importlib.import_module("recoil_app.runtime")
    except ModuleNotFoundError as exc:
        raise AssertionError(f"Missing recoil_app runtime module: {exc}") from exc


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
                slot_index=1,
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
            recoil_runtime.handle_switch_pressed()

            recoil_runtime.handle_fire_state(is_firing=False, aim_mode="ads")
            recoil_runtime.handle_fire_state(is_firing=True, aim_mode="ads")
            recoil_runtime.handle_fire_state(is_firing=True, aim_mode="ads")

            self.assertEqual(learning_calls, [("cod22-黑色组织传奇", "ads")])

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
                slot_index=1,
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
            recoil_runtime.handle_switch_pressed()
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

    def test_handle_switch_pressed_uses_cached_slot_without_launching_new_capture(self):
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

            self.assertEqual(switch_tasks, [])

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


def _profile_record(*, profile_id: str, canonical_weapon_id: str, aim_mode: str, confidence: float):
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
        burst_count=4,
        variance_summary={"horizontal_stddev": 0.1, "vertical_stddev": 0.2},
        confidence=confidence,
        capture_resolution="2560x1440",
        capture_fps=144.0,
        collector_version="test",
        created_at="2026-05-06T18:00:00Z",
    )


if __name__ == "__main__":
    unittest.main()
