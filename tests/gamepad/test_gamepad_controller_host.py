import threading
import unittest
from unittest.mock import patch

import vgamepad as vg

from controllers.base_controller import ControllerTarget
from controllers.gamepad.weapon_switch_recognition import YButtonTextWeaponRecognizer
from controllers.gamepad_controller import GamepadController
from controllers.gamepad.plugin import PluginApplicationTrace
from controllers.gamepad.state import GamepadFrame
from controllers.gamepad.state import GamepadOutput
from runtime.recoil_sidecar.models import RecognizerState
from vision.recoil_collection.models import RecoilProfileRecord
from vision.weapon_identity.models import WeaponIdentityRecord


class _FakePlugin:
    def __init__(self):
        self.reset_calls = 0

    def reset(self):
        self.reset_calls += 1

    def apply(self, frame, output):
        return None


class _TraceAwareFakePlugin:
    def __init__(self, name, delta_right_y=0, auto_fire_active=None):
        self.name = name
        self.delta_right_y = delta_right_y
        self.auto_fire_active = auto_fire_active

    def reset(self):
        return None

    def apply(self, frame, output):
        output.right_y += self.delta_right_y
        if self.auto_fire_active is not None:
            output.auto_fire_active = self.auto_fire_active


class _FakeDiagnostics:
    def __init__(self, enabled=True):
        self.config = type("_Config", (), {"enabled": enabled})()
        self.calls = []

    def record_if_triggered(self, *, frame, output, plugin_traces, plugins):
        self.calls.append(
            {
                "frame": frame,
                "output": output,
                "plugin_traces": plugin_traces,
                "plugins": plugins,
            }
        )
        return True


class _FakeVirtualGamepad:
    def __init__(self):
        self.pressed = []
        self.released = []
        self.left = None
        self.right = None
        self.lt = None
        self.rt = None

    def left_joystick(self, x_value, y_value):
        self.left = (x_value, y_value)

    def right_joystick(self, x_value, y_value):
        self.right = (x_value, y_value)

    def left_trigger(self, value):
        self.lt = value

    def right_trigger(self, value):
        self.rt = value

    def press_button(self, button):
        self.pressed.append(button)

    def release_button(self, button):
        self.released.append(button)


class _FakeRecoilSidecarService:
    def __init__(self, *, active_profile, recognizer_state, matching_profiles):
        self.active_profile = active_profile
        self.recognizer_state = recognizer_state
        self.matching_profiles = tuple(matching_profiles)
        self.publish_calls = []
        self.load_calls = []

    def publish_active_profile(self, source=None, *, context=None):
        self.publish_calls.append({"source": source, "context": context})
        return self.active_profile

    def read_recognizer_state(self, source=None):
        return self.recognizer_state

    def load_matching_profiles(self, recognizer_state, *, context=None):
        self.load_calls.append({"recognizer_state": recognizer_state, "context": context})
        return self.matching_profiles


class _RaisingRecognizerStateService(_FakeRecoilSidecarService):
    def read_recognizer_state(self, source=None):
        raise ValueError("malformed recognizer state")


class GamepadControllerHostTests(unittest.TestCase):
    def test_handle_weapon_switch_button_delegates_to_recoil_app_bridge_when_present(self):
        controller = GamepadController.__new__(GamepadController)
        handled = []
        controller._recoil_app_bridge = type(
            "_FakeBridge",
            (),
            {"handle_buttons": lambda _self, buttons: handled.append(dict(buttons))},
        )()
        controller._switch_weapon_recognizer = type(
            "_FakeSwitchRecognizer",
            (),
            {"handle_switch_pressed": lambda _self: handled.append("legacy")},
        )()
        controller._last_buttons = {}

        GamepadController._handle_weapon_switch_button(controller, {"y": True})

        self.assertEqual(handled, [{"y": True}])
        self.assertEqual(controller._last_buttons, {"y": True})

    def test_handle_weapon_switch_button_fires_only_on_y_rising_edges(self):
        controller = GamepadController.__new__(GamepadController)
        controller._recoil_app_bridge = None
        calls = []
        controller._switch_weapon_recognizer = type(
            "_FakeSwitchRecognizer",
            (),
            {"handle_switch_pressed": lambda _self: calls.append("switch")},
        )()
        controller._last_buttons = {}

        GamepadController._handle_weapon_switch_button(controller, {"y": False})
        GamepadController._handle_weapon_switch_button(controller, {"y": True})
        GamepadController._handle_weapon_switch_button(controller, {"y": True})
        GamepadController._handle_weapon_switch_button(controller, {"y": False})
        GamepadController._handle_weapon_switch_button(controller, {"y": True})

        self.assertEqual(calls, ["switch", "switch"])

    def test_axis_to_xbox_uses_nearest_value_across_full_xusb_range(self):
        controller = GamepadController.__new__(GamepadController)

        self.assertEqual(GamepadController._axis_to_xbox(controller, 1.0), 32767)
        self.assertEqual(GamepadController._axis_to_xbox(controller, -1.0), -32768)
        self.assertEqual(GamepadController._axis_to_xbox(controller, 0.1), 3277)
        self.assertEqual(GamepadController._axis_to_xbox(controller, -0.1), -3277)

    def test_apply_stick_deadzone_no_longer_discards_small_manual_inputs(self):
        controller = GamepadController.__new__(GamepadController)
        controller.PHYS_STICK_DEADZONE = 2500

        self.assertEqual(GamepadController._apply_stick_deadzone(controller, 1638), 1638)
        self.assertEqual(GamepadController._apply_stick_deadzone(controller, -1638), -1638)

    def test_reset_clears_shared_target_signals_and_resets_plugins(self):
        controller = GamepadController.__new__(GamepadController)
        controller.lock = threading.Lock()
        controller.target_dx = 12.0
        controller.target_dy = -8.0
        controller.target_info = ControllerTarget(
            aim_point_x=320.0,
            aim_point_y=220.0,
            screen_center_x=320.0,
            screen_center_y=256.0,
            body_box=(280.0, 140.0, 360.0, 320.0),
        )
        controller.plugins = [_FakePlugin(), _FakePlugin()]

        GamepadController.reset(controller)

        self.assertEqual(controller.target_dx, 0.0)
        self.assertEqual(controller.target_dy, 0.0)
        self.assertIsNone(controller.target_info)
        self.assertEqual(controller.plugins[0].reset_calls, 1)
        self.assertEqual(controller.plugins[1].reset_calls, 1)

    def test_clear_target_clears_shared_target_signals_without_resetting_plugins(self):
        controller = GamepadController.__new__(GamepadController)
        controller.lock = threading.Lock()
        controller.target_dx = 12.0
        controller.target_dy = -8.0
        controller.target_revision = 7
        controller.target_timestamp = 10.0
        controller.target_info = ControllerTarget(
            aim_point_x=320.0,
            aim_point_y=220.0,
            screen_center_x=320.0,
            screen_center_y=256.0,
            body_box=(280.0, 140.0, 360.0, 320.0),
        )
        controller.plugins = [_FakePlugin(), _FakePlugin()]

        with patch("controllers.gamepad_controller.time.perf_counter", return_value=42.0):
            GamepadController.clear_target(controller)

        self.assertEqual(controller.target_dx, 0.0)
        self.assertEqual(controller.target_dy, 0.0)
        self.assertIsNone(controller.target_info)
        self.assertEqual(controller.target_revision, 8)
        self.assertEqual(controller.target_timestamp, 42.0)
        self.assertEqual(controller.plugins[0].reset_calls, 0)
        self.assertEqual(controller.plugins[1].reset_calls, 0)

    def test_set_auto_rb_is_a_compatibility_alias_for_set_auto_fire(self):
        controller = GamepadController.__new__(GamepadController)
        controller.lock = threading.Lock()
        controller._auto_fire_requested = False

        GamepadController.set_auto_rb(controller, True)

        self.assertTrue(controller._auto_fire_requested)

    def test_apply_output_uses_button_api_for_dpad(self):
        controller = GamepadController.__new__(GamepadController)
        controller.virtual_gamepad = _FakeVirtualGamepad()

        output = GamepadOutput(
            left_x=1,
            left_y=2,
            right_x=3,
            right_y=4,
            left_trigger=5,
            right_trigger=6,
            buttons={"rb": False},
            dpad=vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_UP,
        )

        GamepadController._apply_output(controller, output)

        self.assertIn(vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_UP, controller.virtual_gamepad.pressed)
        self.assertIn(vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_DOWN, controller.virtual_gamepad.released)
        self.assertEqual(controller.virtual_gamepad.left, (1, 2))
        self.assertEqual(controller.virtual_gamepad.right, (3, 4))

    def test_build_frame_keeps_controller_target_metadata(self):
        controller = GamepadController.__new__(GamepadController)
        controller.lock = threading.Lock()
        controller._is_aiming = True
        controller.target_dx = 6.0
        controller.target_dy = -4.0
        controller.target_revision = 3
        controller.target_timestamp = 12.5
        controller._auto_fire_requested = False
        controller.target_info = ControllerTarget(
            aim_point_x=320.0,
            aim_point_y=210.0,
            screen_center_x=320.0,
            screen_center_y=256.0,
            body_box=(282.0, 128.0, 358.0, 316.0),
        )

        frame = GamepadController._build_frame(
            controller,
            timestamp=13.0,
            left_x=0,
            left_y=0,
            manual_right_x=100,
            manual_right_y=-50,
            left_trigger=255,
            right_trigger=0,
            buttons={"rb": False},
            dpad=0,
        )

        self.assertEqual(frame.target_dx, 6.0)
        self.assertEqual(frame.target_dy, -4.0)
        self.assertEqual(frame.target_revision, 3)
        self.assertEqual(frame.target_timestamp, 12.5)
        self.assertEqual(frame.target.aim_point_x, 320.0)
        self.assertEqual(frame.target.screen_center_y, 256.0)
        self.assertEqual(frame.target.body_box, (282.0, 128.0, 358.0, 316.0))

    def test_apply_plugin_pipeline_emits_traces_to_diagnostics_when_enabled(self):
        controller = GamepadController.__new__(GamepadController)
        controller.plugins = [
            _TraceAwareFakePlugin("aim", delta_right_y=-1200),
            _TraceAwareFakePlugin("fire", auto_fire_active=True),
            _TraceAwareFakePlugin("recoil", delta_right_y=-9830),
        ]
        controller._downward_pull_diagnostics = _FakeDiagnostics(enabled=True)

        output = GamepadOutput(
            left_x=0,
            left_y=0,
            right_x=0,
            right_y=0,
            left_trigger=255,
            right_trigger=0,
            buttons={"rb": False},
        )
        frame = GamepadFrame(
            timestamp=13.0,
            left_x=0,
            left_y=0,
            manual_right_x=0,
            manual_right_y=0,
            left_trigger=255,
            right_trigger=0,
            buttons={"rb": False},
            is_aiming=True,
            target_dx=0.0,
            target_dy=0.0,
            auto_fire_requested=False,
            dpad=0,
        )

        traces = GamepadController._apply_plugin_pipeline(controller, frame, output)

        self.assertEqual([trace.plugin_name for trace in traces], ["aim", "fire", "recoil"])
        self.assertEqual(output.right_y, -11030)
        self.assertTrue(output.auto_fire_active)
        self.assertEqual(len(controller._downward_pull_diagnostics.calls), 1)
        emitted = controller._downward_pull_diagnostics.calls[0]["plugin_traces"]
        self.assertIsInstance(emitted[0], PluginApplicationTrace)
        self.assertEqual(emitted[2].delta_right_y, -9830)

    def test_get_active_recoil_profile_returns_matching_ready_profile_for_ads(self):
        controller = GamepadController.__new__(GamepadController)
        controller._recoil_app_bridge = None
        ready_profile = _profile_record(
            profile_id="profile-cod22-m4-ads-standing-v1",
            canonical_weapon_id="cod22-m4",
            aim_mode="ads",
            samples_y=(0.0, -80.0, -160.0),
        )
        controller._recoil_sidecar_service = _FakeRecoilSidecarService(
            active_profile={
                "canonical_weapon_id": "cod22-m4",
                "profile_id": "profile-cod22-m4-ads-standing-v1",
                "game": "cod22",
                "stance": "standing",
                "aim_mode": "ads",
                "profile_confidence": 0.88,
                "identity_confidence": 0.92,
                "updated_at": "2026-05-06T12:00:00Z",
                "status": "ready",
            },
            recognizer_state={
                "type": "current_weapon",
                "game": "cod22",
                "canonical_weapon_id": "cod22-m4",
                "confidence": 0.92,
                "source": "image",
                "timestamp": "2026-05-06T12:00:00Z",
                "degraded": False,
                "matched_name": None,
                "profile_ids": ["profile-cod22-m4-ads-standing-v1"],
            },
            matching_profiles=[ready_profile],
        )

        profile = GamepadController._get_active_recoil_profile(controller, is_aiming=True)

        self.assertIsNotNone(profile)
        self.assertEqual(profile.profile_id, "profile-cod22-m4-ads-standing-v1")
        self.assertEqual(profile.aim_mode, "ads")
        self.assertEqual(
            controller._recoil_sidecar_service.publish_calls[0]["context"],
            {"stance": "standing", "aim_mode": "ads"},
        )

    def test_get_active_recoil_profile_returns_none_when_sidecar_is_degraded(self):
        controller = GamepadController.__new__(GamepadController)
        controller._recoil_app_bridge = None
        controller._recoil_sidecar_service = _FakeRecoilSidecarService(
            active_profile={
                "canonical_weapon_id": "cod22-m4",
                "profile_id": "profile-cod22-m4-ads-standing-v1",
                "game": "cod22",
                "stance": "standing",
                "aim_mode": "ads",
                "profile_confidence": 0.88,
                "identity_confidence": 0.45,
                "updated_at": "2026-05-06T12:00:00Z",
                "status": "degraded",
            },
            recognizer_state=None,
            matching_profiles=[],
        )

        profile = GamepadController._get_active_recoil_profile(controller, is_aiming=True)

        self.assertIsNone(profile)

    def test_get_active_recoil_profile_degrades_when_recognizer_state_read_raises(self):
        controller = GamepadController.__new__(GamepadController)
        controller._recoil_app_bridge = None
        controller._recoil_sidecar_service = _RaisingRecognizerStateService(
            active_profile={
                "canonical_weapon_id": "cod22-m4",
                "profile_id": "profile-cod22-m4-ads-standing-v1",
                "game": "cod22",
                "stance": "standing",
                "aim_mode": "ads",
                "profile_confidence": 0.88,
                "identity_confidence": 0.92,
                "updated_at": "2026-05-06T12:00:00Z",
                "status": "ready",
            },
            recognizer_state=None,
            matching_profiles=[],
        )

        profile = GamepadController._get_active_recoil_profile(controller, is_aiming=True)

        self.assertIsNone(profile)

    def test_get_active_recoil_profile_uses_recoil_app_bridge_when_present(self):
        controller = GamepadController.__new__(GamepadController)
        expected_profile = _profile_record(
            profile_id="profile-cod22-黑色组织传奇-ads-standing-v1",
            canonical_weapon_id="cod22-黑色组织传奇",
            aim_mode="ads",
            samples_y=(0.0, -80.0, -160.0),
        )
        calls = []
        controller._recoil_app_bridge = type(
            "_FakeBridge",
            (),
            {"get_active_profile": lambda _self, *, is_aiming: calls.append(is_aiming) or expected_profile},
        )()
        controller._recoil_sidecar_service = None

        profile = GamepadController._get_active_recoil_profile(controller, is_aiming=True)

        self.assertIs(profile, expected_profile)
        self.assertEqual(calls, [True])


class YButtonTextWeaponRecognizerTests(unittest.TestCase):
    def test_handle_switch_pressed_replays_cached_slot_immediately(self):
        published = []
        recognizer = YButtonTextWeaponRecognizer(
            game="cod20",
            identity_records=(_weapon_record(canonical_weapon_id="cod20-王者", game="cod20", display_name="王者"),),
            state_writer=published.append,
            capture_runner=lambda slot_index, switch_epoch, task: None,
        )
        recognizer.complete_switch_resolution(
            slot_index=1,
            switch_epoch=0,
            state=_recognizer_state("cod20", "cod20-王者", "王者"),
        )

        recognizer.handle_switch_pressed()

        self.assertEqual(recognizer.active_slot_index, 1)
        self.assertEqual(len(published), 1)
        self.assertEqual(published[0].canonical_weapon_id, "cod20-王者")
        self.assertEqual(published[0].source, "switch_cache")

    def test_stale_switch_epoch_updates_cache_without_overriding_current_slot(self):
        published = []
        recognizer = YButtonTextWeaponRecognizer(
            game="cod21",
            identity_records=(_weapon_record(canonical_weapon_id="cod21-黑色组织xxx", game="cod21", display_name="黑色组织xxx"),),
            state_writer=published.append,
            capture_runner=lambda slot_index, switch_epoch, task: None,
        )

        recognizer.handle_switch_pressed()
        recognizer.handle_switch_pressed()
        recognizer.complete_switch_resolution(
            slot_index=1,
            switch_epoch=1,
            state=_recognizer_state("cod21", "cod21-黑色组织xxx", "黑色组织xxx"),
        )

        self.assertEqual(published, [])

        recognizer.handle_switch_pressed()

        self.assertEqual(recognizer.active_slot_index, 1)
        self.assertEqual(len(published), 1)
        self.assertEqual(published[0].canonical_weapon_id, "cod21-黑色组织xxx")
        self.assertEqual(published[0].source, "switch_cache")


def _profile_record(*, profile_id: str, canonical_weapon_id: str, aim_mode: str, samples_y: tuple[float, ...]):
    return RecoilProfileRecord(
        profile_id=profile_id,
        canonical_weapon_id=canonical_weapon_id,
        game="cod22",
        stance="standing",
        aim_mode=aim_mode,
        sample_interval_ms=10,
        duration_ms=len(samples_y) * 10,
        initial_delay_ms=0,
        samples_x=tuple(0.0 for _ in samples_y),
        samples_y=samples_y,
        sample_count=len(samples_y),
        burst_count=5,
        variance_summary={"horizontal_stddev": 0.1, "vertical_stddev": 0.2},
        confidence=0.88,
        capture_resolution="2560x1440",
        capture_fps=144.0,
        collector_version="test",
        created_at="2026-05-06T12:00:00Z",
    )


def _weapon_record(*, canonical_weapon_id: str, game: str, display_name: str):
    return WeaponIdentityRecord(
        canonical_weapon_id=canonical_weapon_id,
        game=game,
        weapon_family="unknown",
        display_name=display_name,
        alias_names=(),
        blueprint_names=(),
        signature_refs=(),
        notes="",
        created_at="2026-05-06T12:00:00Z",
        updated_at="2026-05-06T12:00:00Z",
    )


def _recognizer_state(game: str, canonical_weapon_id: str, matched_name: str) -> RecognizerState:
    return RecognizerState(
        game=game,
        canonical_weapon_id=canonical_weapon_id,
        confidence=0.93,
        source="switch_text",
        timestamp="2026-05-06T18:00:00Z",
        degraded=False,
        matched_name=matched_name,
        profile_ids=(),
    )


if __name__ == "__main__":
    unittest.main()
