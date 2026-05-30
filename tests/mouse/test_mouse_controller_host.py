import threading
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

from controllers.base_controller import ControllerTarget, ControllerVisionState
from controllers.mouse import AIAimConfig, AutoFireConfig, RecoilCompensationConfig
from controllers.mouse_controller import MouseController, _MouseTelemetry
from controllers.mouse.state import MouseOutput
from pynput import mouse as pynput_mouse


class _FakePlugin:
    def __init__(self):
        self.reset_calls = 0
        self.apply_calls = 0
        self.last_frame = None

    def reset(self):
        self.reset_calls += 1

    def apply(self, frame, output):
        self.apply_calls += 1
        self.last_frame = frame


class _FakeListener:
    def __init__(self, *, on_move, on_click):
        self.on_move = on_move
        self.on_click = on_click
        self.start_calls = 0
        self.stop_calls = 0

    def start(self):
        self.start_calls += 1

    def stop(self):
        self.stop_calls += 1


class _FakeInjector:
    name = "fake"

    def __init__(self):
        self.calls = []

    def send(self, flags, dx=0, dy=0):
        self.calls.append((flags, dx, dy))


class _FailingInjector:
    name = "failing"

    def send(self, flags, dx=0, dy=0):
        raise OSError("blocked")


class _CallbackInjector:
    name = "callback"

    def __init__(self, callback):
        self.callback = callback
        self.calls = []

    def send(self, flags, dx=0, dy=0):
        self.calls.append((flags, dx, dy))
        self.callback(flags, dx, dy)


class MouseControllerHostTests(unittest.TestCase):
    def _make_controller(self, plugins):
        """Create a MouseController without starting the thread or listeners."""
        ctrl = MouseController.__new__(MouseController)
        ctrl.lock = threading.Lock()
        ctrl.running = False
        ctrl.ready = True
        ctrl.target_dx = 0.0
        ctrl.target_dy = 0.0
        ctrl.target_revision = 0
        ctrl.target_timestamp = None
        ctrl.target_info = None
        ctrl._inject_remainder_dx = 0.0
        ctrl._inject_remainder_dy = 0.0
        ctrl._local_motion_dx_since_target = 0.0
        ctrl._local_motion_dy_since_target = 0.0
        ctrl._local_motion_updated_at = None
        ctrl._is_aiming = False
        ctrl._auto_fire_requested = False
        ctrl._auto_fire_timestamp = None
        ctrl._vision_received_at = None
        ctrl._vision_submitted_at = None
        ctrl._acc_dx = 0.0
        ctrl._acc_dy = 0.0
        ctrl._pending_injected_dx = 0.0
        ctrl._pending_injected_dy = 0.0
        ctrl._pending_injected_until = None
        ctrl._manual_left_pressed = False
        ctrl._left_click_held = False
        ctrl._manual_override_until = None
        ctrl._manual_fire_suppress_until = None
        ctrl._input_session_id = 0
        ctrl._mouse_response_enabled = True
        ctrl._mouse_response_px_per_input = 1.0
        ctrl._mouse_response_min_px_per_input = 0.08
        ctrl._mouse_response_max_px_per_input = 3.0
        ctrl._mouse_response_local_min_px_per_input = 0.35
        ctrl._mouse_response_sample_alpha = 1.0
        ctrl._mouse_response_min_motion = 4.0
        ctrl._mouse_response_stall_factor = 0.72
        ctrl._physical_right_pressed = False
        ctrl._physical_aim_sync_resets = 0
        ctrl._physical_aim_sync_presses = 0
        ctrl._injection_error_count = 0
        ctrl._last_mouse_x = 100
        ctrl._last_mouse_y = 100
        ctrl._telemetry = None
        ctrl.plugins = list(plugins)
        return ctrl

    def test_default_plugin_chain_uses_configured_auto_fire_and_recoil_knobs(self):
        tuning = SimpleNamespace(
            mouse_ai_aim=AIAimConfig(),
            mouse_auto_fire=AutoFireConfig(
                max_source_age_ms=40.0,
                hold_seconds=0.100,
                release_seconds=0.025,
            ),
            mouse_recoil=RecoilCompensationConfig(amount_px=0.55),
        )

        with patch("config.load_tuning_config", return_value=tuning), patch(
            "controllers.mouse_controller.win32api.GetCursorPos",
            return_value=(100, 200),
        ), patch(
            "controllers.mouse_controller.pynput_mouse.Listener",
            side_effect=lambda **kwargs: _FakeListener(**kwargs),
        ), patch.object(
            MouseController,
            "start",
            return_value=None,
        ):
            ctrl = MouseController()

        auto_fire = ctrl.plugins[1].config
        recoil = ctrl.plugins[2].config
        self.assertEqual(auto_fire.max_source_age_ms, 40.0)
        self.assertEqual(auto_fire.hold_seconds, 0.100)
        self.assertEqual(auto_fire.release_seconds, 0.025)
        self.assertEqual(recoil.amount_px, 0.55)

    def test_mouse_telemetry_flushes_header_on_create(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "mouse-controller-test.csv"
            telemetry = _MouseTelemetry(path)
            try:
                content = path.read_text(encoding="utf-8")
            finally:
                telemetry.close()

        self.assertIn("timestamp", content)
        self.assertIn("manual_left_pressed", content)

    def test_mouse_telemetry_flushes_rows_every_short_debug_window(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "mouse-controller-test.csv"
            telemetry = _MouseTelemetry(path)
            try:
                row = ["0"] * len(_MouseTelemetry.HEADER)
                for _ in range(30):
                    telemetry.write_row(row)
                content = path.read_text(encoding="utf-8")
            finally:
                telemetry.close()

        self.assertGreaterEqual(len(content.splitlines()), 31)

    def test_update_stores_vision_signals(self):
        ctrl = self._make_controller([])
        target = ControllerTarget(
            aim_point_x=330.0,
            aim_point_y=220.0,
            screen_center_x=320.0,
            screen_center_y=256.0,
            body_box=(280.0, 140.0, 360.0, 320.0),
            target_source="observed",
        )
        ctrl.update(12.5, -8.0, target=target)
        self.assertAlmostEqual(ctrl.target_dx, 12.5)
        self.assertAlmostEqual(ctrl.target_dy, -8.0)
        self.assertIs(ctrl.target_info, target)
        self.assertEqual(ctrl.target_info.target_source, "observed")

    def test_update_vision_state_updates_target_and_auto_fire_timestamps(self):
        ctrl = self._make_controller([])
        target = ControllerTarget(
            aim_point_x=330.0,
            aim_point_y=220.0,
            screen_center_x=320.0,
            screen_center_y=256.0,
            body_box=(280.0, 140.0, 360.0, 320.0),
            target_source="observed",
            observed_at=12.345,
        )

        ctrl.update_vision_state(
            ControllerVisionState(
                dx=12.5,
                dy=-8.0,
                target=target,
                auto_fire_requested=True,
                received_at=12.340,
                submitted_at=12.350,
            )
        )

        self.assertAlmostEqual(ctrl.target_dx, 12.5)
        self.assertAlmostEqual(ctrl.target_dy, -8.0)
        self.assertIs(ctrl.target_info, target)
        self.assertEqual(ctrl.target_timestamp, 12.345)
        self.assertTrue(ctrl._auto_fire_requested)
        self.assertEqual(ctrl._auto_fire_timestamp, 12.345)
        self.assertEqual(ctrl._vision_received_at, 12.340)
        self.assertEqual(ctrl._vision_submitted_at, 12.350)

    def test_update_vision_state_blocks_auto_fire_without_fire_authority(self):
        ctrl = self._make_controller([])
        target = ControllerTarget(
            aim_point_x=330.0,
            aim_point_y=220.0,
            screen_center_x=320.0,
            screen_center_y=256.0,
            target_source="cue_hold",
            target_tier="cue_hold",
            fire_authority=False,
            observed_at=12.345,
        )

        ctrl.update_vision_state(
            ControllerVisionState(
                dx=12.5,
                dy=-8.0,
                target=target,
                auto_fire_requested=True,
                fire_authority=False,
                observed_at=12.345,
            )
        )

        self.assertFalse(ctrl._auto_fire_requested)
        self.assertIsNone(ctrl._auto_fire_timestamp)

    def test_update_vision_state_clears_target_without_aim_authority(self):
        ctrl = self._make_controller([])
        ctrl.target_dx = 10.0
        ctrl.target_dy = 5.0
        ctrl.target_info = ControllerTarget(
            aim_point_x=320.0,
            aim_point_y=220.0,
            screen_center_x=320.0,
            screen_center_y=256.0,
        )
        ctrl._auto_fire_requested = True
        ctrl._auto_fire_timestamp = 10.0
        target = ControllerTarget(
            aim_point_x=330.0,
            aim_point_y=220.0,
            screen_center_x=320.0,
            screen_center_y=256.0,
            target_source="predicted",
            target_tier="predicted",
            aim_authority=False,
            fire_authority=False,
            observed_at=12.345,
        )

        ctrl.update_vision_state(
            ControllerVisionState(
                dx=12.5,
                dy=-8.0,
                target=target,
                auto_fire_requested=True,
                aim_authority=False,
                fire_authority=False,
                observed_at=12.345,
            )
        )

        self.assertAlmostEqual(ctrl.target_dx, 0.0)
        self.assertAlmostEqual(ctrl.target_dy, 0.0)
        self.assertIsNone(ctrl.target_info)
        self.assertFalse(ctrl._auto_fire_requested)
        self.assertIsNone(ctrl._auto_fire_timestamp)

    def test_update_vision_state_without_target_suppresses_auto_fire(self):
        ctrl = self._make_controller([])
        ctrl.target_dx = 10.0
        ctrl.target_dy = 5.0
        ctrl.target_info = object()
        ctrl._auto_fire_requested = True
        ctrl._auto_fire_timestamp = 10.0

        with patch("controllers.mouse_controller.time.perf_counter", return_value=42.0):
            ctrl.update_vision_state(
                ControllerVisionState(
                    dx=0.0,
                    dy=0.0,
                    target=None,
                    auto_fire_requested=True,
                )
            )

        self.assertAlmostEqual(ctrl.target_dx, 0.0)
        self.assertAlmostEqual(ctrl.target_dy, 0.0)
        self.assertIsNone(ctrl.target_info)
        self.assertFalse(ctrl._auto_fire_requested)
        self.assertIsNone(ctrl._auto_fire_timestamp)

    def test_reset_clears_target_and_resets_plugins(self):
        p = _FakePlugin()
        ctrl = self._make_controller([p])
        ctrl.target_dx = 10.0
        ctrl.target_dy = 5.0
        ctrl._auto_fire_requested = True
        ctrl.target_info = ControllerTarget(
            aim_point_x=320.0,
            aim_point_y=240.0,
            screen_center_x=320.0,
            screen_center_y=256.0,
            target_source="reconstructed",
        )
        ctrl.reset()
        self.assertAlmostEqual(ctrl.target_dx, 0.0)
        self.assertAlmostEqual(ctrl.target_dy, 0.0)
        self.assertIsNone(ctrl.target_info)
        self.assertEqual(p.reset_calls, 1)
        self.assertFalse(ctrl._auto_fire_requested)

    def test_clear_target_clears_target_without_resetting_plugins(self):
        p = _FakePlugin()
        ctrl = self._make_controller([p])
        ctrl.target_dx = 10.0
        ctrl.target_dy = 5.0
        ctrl.target_revision = 7
        ctrl.target_timestamp = 123.0
        ctrl.target_info = ControllerTarget(
            aim_point_x=320.0,
            aim_point_y=240.0,
            screen_center_x=320.0,
            screen_center_y=256.0,
            target_source="observed",
        )

        ctrl.clear_target()

        self.assertAlmostEqual(ctrl.target_dx, 0.0)
        self.assertAlmostEqual(ctrl.target_dy, 0.0)
        self.assertIsNone(ctrl.target_info)
        self.assertEqual(ctrl.target_revision, 8)
        self.assertIsNotNone(ctrl.target_timestamp)
        self.assertEqual(p.reset_calls, 0)

    def test_set_auto_fire_stores_flag(self):
        ctrl = self._make_controller([])
        ctrl.set_auto_fire(True)
        self.assertTrue(ctrl._auto_fire_requested)
        ctrl.set_auto_fire(False)
        self.assertFalse(ctrl._auto_fire_requested)

    def test_set_auto_rb_is_alias(self):
        ctrl = self._make_controller([])
        ctrl.set_auto_rb(True)
        self.assertTrue(ctrl._auto_fire_requested)

    def test_build_frame_captures_state(self):
        p = _FakePlugin()
        ctrl = self._make_controller([p])
        ctrl._is_aiming = True
        ctrl.target_dx = 7.0
        ctrl.target_dy = -3.0
        ctrl._auto_fire_requested = True
        ctrl._acc_dx = 2.0
        ctrl._acc_dy = 1.0
        ctrl.target_revision = 5
        ctrl.target_timestamp = 99.0
        ctrl.target_info = ControllerTarget(
            aim_point_x=318.0,
            aim_point_y=252.0,
            screen_center_x=320.0,
            screen_center_y=256.0,
            body_box=(292.0, 180.0, 348.0, 310.0),
            target_source="predicted",
        )

        frame = ctrl._build_frame(timestamp=100.0)
        self.assertTrue(frame.is_aiming)
        self.assertAlmostEqual(frame.target_dx, 7.0)
        self.assertAlmostEqual(frame.target_dy, -3.0)
        self.assertTrue(frame.auto_fire_requested)
        self.assertAlmostEqual(frame.manual_dx, 2.0)
        self.assertAlmostEqual(frame.manual_dy, 1.0)
        self.assertEqual(frame.target_revision, 5)
        self.assertEqual(frame.target_timestamp, 99.0)
        self.assertIs(frame.target, ctrl.target_info)
        self.assertEqual(frame.target.target_source, "predicted")
        # Accumulators should be consumed
        self.assertAlmostEqual(ctrl._acc_dx, 0.0)
        self.assertAlmostEqual(ctrl._acc_dy, 0.0)

    def test_build_frame_exposes_adaptive_mouse_response_scale(self):
        ctrl = self._make_controller([])
        ctrl._mouse_response_px_per_input = 0.25

        frame = ctrl._build_frame(timestamp=100.0)

        self.assertAlmostEqual(frame.response_input_scale, 4.0)

    def test_update_vision_state_learns_low_mouse_response_from_same_target(self):
        ctrl = self._make_controller([])
        ctrl._mouse_response_px_per_input = 1.0
        ctrl.target_dx = 100.0
        ctrl.target_dy = 0.0
        ctrl.target_info = ControllerTarget(
            aim_point_x=420.0,
            aim_point_y=256.0,
            screen_center_x=320.0,
            screen_center_y=256.0,
            body_box=(380.0, 180.0, 460.0, 340.0),
            target_source="observed",
        )
        ctrl._local_motion_dx_since_target = 40.0
        ctrl._local_motion_dy_since_target = 0.0
        next_target = ControllerTarget(
            aim_point_x=412.0,
            aim_point_y=256.0,
            screen_center_x=320.0,
            screen_center_y=256.0,
            body_box=(372.0, 180.0, 452.0, 340.0),
            target_source="observed",
        )

        ctrl.update_vision_state(
            ControllerVisionState(
                dx=92.0,
                dy=0.0,
                target=next_target,
                observed_at=101.0,
            )
        )
        frame = ctrl._build_frame(timestamp=101.01)

        self.assertAlmostEqual(ctrl._mouse_response_px_per_input, 0.2)
        self.assertAlmostEqual(frame.response_input_scale, 5.0)

    def test_update_vision_state_does_not_collapse_response_on_latency_stall(self):
        ctrl = self._make_controller([])
        ctrl._mouse_response_px_per_input = 0.35
        ctrl.target_dx = 80.0
        ctrl.target_dy = 0.0
        ctrl.target_info = ControllerTarget(
            aim_point_x=400.0,
            aim_point_y=256.0,
            screen_center_x=320.0,
            screen_center_y=256.0,
            body_box=(360.0, 180.0, 440.0, 340.0),
            target_source="observed",
        )
        ctrl._local_motion_dx_since_target = 220.0
        ctrl._local_motion_dy_since_target = 0.0
        ctrl._local_motion_updated_at = 101.005
        next_target = ControllerTarget(
            aim_point_x=399.0,
            aim_point_y=256.0,
            screen_center_x=320.0,
            screen_center_y=256.0,
            body_box=(359.0, 180.0, 439.0, 340.0),
            target_source="observed",
        )

        ctrl.update_vision_state(
            ControllerVisionState(
                dx=79.0,
                dy=0.0,
                target=next_target,
                observed_at=101.0,
            )
        )

        self.assertAlmostEqual(ctrl._mouse_response_px_per_input, 0.35)

    def test_build_frame_arms_manual_override_window_for_strong_manual_drag(self):
        ctrl = self._make_controller([])
        ctrl._is_aiming = True
        ctrl._acc_dx = 24.0
        ctrl._acc_dy = 0.0

        frame = ctrl._build_frame(timestamp=10.0)
        self.assertTrue(frame.manual_override_active)

        held_frame = ctrl._build_frame(timestamp=10.05)
        self.assertTrue(held_frame.manual_override_active)

        released_frame = ctrl._build_frame(timestamp=10.25)
        self.assertFalse(released_frame.manual_override_active)

    @patch("controllers.mouse_controller.win32api.mouse_event")
    def test_apply_output_accumulates_fractional_motion_until_integer_move_exists(self, mouse_event):
        ctrl = self._make_controller([])

        first = MouseOutput(move_dx=0.6, move_dy=-0.6)
        ctrl._apply_output(first)
        mouse_event.assert_not_called()

        second = MouseOutput(move_dx=0.6, move_dy=-0.6)
        ctrl._apply_output(second)

        mouse_event.assert_called_once()
        args = mouse_event.call_args.args
        self.assertEqual(args[1:3], (1, -1))

    def test_apply_output_uses_configured_mouse_injector_backend(self):
        ctrl = self._make_controller([])
        injector = _FakeInjector()
        ctrl._injector = injector

        ctrl._apply_output(MouseOutput(move_dx=3.0, move_dy=-2.0))

        self.assertEqual(injector.calls, [(0x0001, 3, -2)])

    def test_apply_output_records_injection_error_without_crashing_thread(self):
        ctrl = self._make_controller([])
        ctrl._injector = _FailingInjector()

        move_x, move_y = ctrl._apply_output(MouseOutput(move_dx=3.0, move_dy=0.0))

        self.assertEqual((move_x, move_y), (0, 0))
        self.assertEqual(ctrl._injection_error_count, 1)
        self.assertAlmostEqual(ctrl._local_motion_dx_since_target, 0.0)
        self.assertAlmostEqual(ctrl._pending_injected_dx, 0.0)
        self.assertAlmostEqual(ctrl._pending_injected_dy, 0.0)
        self.assertIsNone(ctrl._pending_injected_until)

    def test_injection_error_logging_is_throttled(self):
        ctrl = self._make_controller([])
        ctrl._injector = _FailingInjector()

        with patch("builtins.print") as print_mock:
            for _ in range(5):
                ctrl._apply_output(MouseOutput(move_dx=1.0, move_dy=0.0))

        self.assertEqual(ctrl._injection_error_count, 5)
        self.assertEqual(print_mock.call_count, 3)

    @patch("controllers.mouse_controller.win32api.mouse_event")
    def test_injected_motion_discounts_stale_target_error_with_response_floor(self, mouse_event):
        ctrl = self._make_controller([])
        ctrl._is_aiming = True
        ctrl._mouse_response_px_per_input = 0.08
        ctrl._mouse_response_local_min_px_per_input = 0.35
        ctrl.target_dx = 10.0
        ctrl.target_dy = -5.0
        ctrl.target_revision = 3
        ctrl.target_timestamp = 99.0
        ctrl.target_info = ControllerTarget(
            aim_point_x=330.0,
            aim_point_y=251.0,
            screen_center_x=320.0,
            screen_center_y=256.0,
            target_source="observed",
        )

        ctrl._apply_output(MouseOutput(move_dx=3.0, move_dy=-2.0))
        frame = ctrl._build_frame(timestamp=100.0)

        mouse_event.assert_called_once()
        self.assertAlmostEqual(frame.target_dx, 8.95)
        self.assertAlmostEqual(frame.target_dy, -4.3)
        self.assertEqual(frame.target_revision, 3)
        self.assertEqual(ctrl.target_revision, 3)

    def test_large_repeated_local_motion_zeroes_stale_target_error(self):
        ctrl = self._make_controller([])
        ctrl._is_aiming = True
        ctrl._mouse_response_px_per_input = 0.08
        ctrl._mouse_response_local_min_px_per_input = 0.35
        ctrl.target_dx = 80.0
        ctrl.target_dy = 0.0
        ctrl.target_info = ControllerTarget(
            aim_point_x=400.0,
            aim_point_y=256.0,
            screen_center_x=320.0,
            screen_center_y=256.0,
            target_source="observed",
        )
        ctrl._local_motion_dx_since_target = 300.0

        frame = ctrl._build_frame(timestamp=100.0)

        self.assertAlmostEqual(frame.target_dx, 0.0)

    @patch("controllers.mouse_controller.win32api.mouse_event")
    def test_injected_motion_without_callback_does_not_trigger_manual_override(self, mouse_event):
        ctrl = self._make_controller([])
        ctrl._is_aiming = True

        ctrl._apply_output(MouseOutput(move_dx=16.0, move_dy=-4.0))
        frame = ctrl._build_frame(timestamp=100.0)

        mouse_event.assert_called_once()
        self.assertAlmostEqual(frame.manual_dx, 0.0)
        self.assertAlmostEqual(frame.manual_dy, 0.0)
        self.assertFalse(frame.manual_override_active)

    @patch("controllers.mouse_controller.win32api.mouse_event")
    @patch("controllers.mouse_controller.time.perf_counter", side_effect=[10.0, 10.001])
    def test_observed_injected_motion_is_suppressed_before_manual_accumulator(
        self,
        _perf_counter,
        mouse_event,
    ):
        ctrl = self._make_controller([])
        ctrl._is_aiming = True

        ctrl._apply_output(MouseOutput(move_dx=16.0, move_dy=-4.0))
        ctrl._on_mouse_move(116, 96)
        frame = ctrl._build_frame(timestamp=100.0)

        mouse_event.assert_called_once()
        self.assertAlmostEqual(frame.manual_dx, 0.0)
        self.assertAlmostEqual(frame.manual_dy, 0.0)
        self.assertFalse(frame.manual_override_active)

    @patch("controllers.mouse_controller.win32api.mouse_event")
    def test_user_motion_excess_survives_injected_motion_suppression(self, mouse_event):
        ctrl = self._make_controller([])
        ctrl._is_aiming = True

        ctrl._apply_output(MouseOutput(move_dx=6.0, move_dy=0.0))
        ctrl._on_mouse_move(140, 100)
        frame = ctrl._build_frame(timestamp=100.0)

        mouse_event.assert_called_once()
        self.assertAlmostEqual(frame.manual_dx, 10.0)
        self.assertAlmostEqual(frame.manual_dy, 0.0)
        self.assertTrue(frame.manual_override_active)

    @patch("controllers.mouse_controller.win32api.mouse_event")
    def test_scaled_injected_echo_is_suppressed_before_manual_accumulator(self, mouse_event):
        ctrl = self._make_controller([])
        ctrl._is_aiming = True

        ctrl._apply_output(MouseOutput(move_dx=6.0, move_dy=0.0))
        ctrl._on_mouse_move(120, 100)
        frame = ctrl._build_frame(timestamp=100.0)

        mouse_event.assert_called_once()
        self.assertAlmostEqual(frame.manual_dx, 0.0)
        self.assertAlmostEqual(frame.manual_dy, 0.0)
        self.assertFalse(frame.manual_override_active)

    @patch("controllers.mouse_controller.win32api.mouse_event")
    def test_reverse_echo_from_injected_motion_is_suppressed(self, mouse_event):
        ctrl = self._make_controller([])
        ctrl._is_aiming = True

        ctrl._apply_output(MouseOutput(move_dx=-24.0, move_dy=-10.0))
        ctrl._on_mouse_move(111, 107)
        frame = ctrl._build_frame(timestamp=100.0)

        mouse_event.assert_called_once()
        self.assertAlmostEqual(frame.manual_dx, 0.0)
        self.assertAlmostEqual(frame.manual_dy, 0.0)
        self.assertFalse(frame.manual_override_active)

    @patch("controllers.mouse_controller.win32api.mouse_event")
    def test_reverse_user_motion_excess_survives_injected_motion_suppression(
        self,
        mouse_event,
    ):
        ctrl = self._make_controller([])
        ctrl._is_aiming = True

        ctrl._apply_output(MouseOutput(move_dx=-6.0, move_dy=0.0))
        ctrl._on_mouse_move(140, 100)
        frame = ctrl._build_frame(timestamp=100.0)

        mouse_event.assert_called_once()
        self.assertAlmostEqual(frame.manual_dx, 10.0)
        self.assertAlmostEqual(frame.manual_dy, 0.0)
        self.assertTrue(frame.manual_override_active)

    def test_injected_echo_during_send_is_suppressed_before_manual_accumulator(self):
        ctrl = self._make_controller([])
        ctrl._is_aiming = True

        def echo_during_send(_flags, _dx, _dy):
            ctrl._on_mouse_move(111, 107)

        ctrl._injector = _CallbackInjector(echo_during_send)

        ctrl._apply_output(MouseOutput(move_dx=-24.0, move_dy=-10.0))
        frame = ctrl._build_frame(timestamp=100.0)

        self.assertEqual(ctrl._injector.calls, [(0x0001, -24, -10)])
        self.assertAlmostEqual(frame.manual_dx, 0.0)
        self.assertAlmostEqual(frame.manual_dy, 0.0)
        self.assertFalse(frame.manual_override_active)

    @patch("controllers.mouse_controller.win32api.mouse_event")
    def test_left_mouse_press_releases_synthetic_auto_fire_hold(self, mouse_event):
        ctrl = self._make_controller([])
        ctrl._left_click_held = True

        ctrl._on_mouse_click(0, 0, pynput_mouse.Button.left, True)

        mouse_event.assert_called_once()
        args = mouse_event.call_args.args
        self.assertEqual(args[0], 0x0004)
        self.assertFalse(ctrl._left_click_held)
        self.assertTrue(ctrl._manual_left_pressed)

    @patch("controllers.mouse_controller.win32api.GetAsyncKeyState", return_value=0x8000)
    @patch("controllers.mouse_controller.time.perf_counter")
    def test_left_mouse_release_keeps_auto_fire_suppressed_for_takeover_window(
        self,
        perf_counter,
        get_key,
    ):
        ctrl = self._make_controller([])
        ctrl._is_aiming = True
        ctrl._auto_fire_requested = True
        ctrl._auto_fire_timestamp = 9.95
        perf_counter.return_value = 10.00
        ctrl._on_mouse_click(0, 0, pynput_mouse.Button.left, True)

        perf_counter.return_value = 10.02
        ctrl._on_mouse_click(0, 0, pynput_mouse.Button.left, False)

        blocked = ctrl._build_frame(timestamp=10.10)
        self.assertFalse(blocked.manual_left_pressed)
        self.assertFalse(blocked.auto_fire_requested)

        resumed = ctrl._build_frame(timestamp=10.20)
        self.assertTrue(resumed.auto_fire_requested)
        get_key.assert_called_with(0x02)

    @patch("controllers.mouse_controller.win32api.GetAsyncKeyState", return_value=0)
    def test_left_mouse_press_starts_mouse_aim_session_without_right_button(self, get_key):
        ctrl = self._make_controller([])

        ctrl._on_mouse_click(0, 0, pynput_mouse.Button.left, True)
        ctrl._sync_physical_aim_state()

        self.assertTrue(ctrl._manual_left_pressed)
        self.assertTrue(ctrl._is_aiming)
        get_key.assert_called_with(0x02)

    @patch("controllers.mouse_controller.win32api.mouse_event")
    @patch("controllers.mouse_controller.win32api.GetAsyncKeyState", return_value=0)
    def test_left_mouse_release_without_right_button_fail_closes_aim_and_fire(
        self,
        get_key,
        mouse_event,
    ):
        ctrl = self._make_controller([])
        ctrl._manual_left_pressed = True
        ctrl._is_aiming = True
        ctrl._auto_fire_requested = True
        ctrl._left_click_held = True
        ctrl.target_dx = 18.0
        ctrl.target_dy = -5.0
        ctrl.target_info = ControllerTarget(
            aim_point_x=338.0,
            aim_point_y=251.0,
            screen_center_x=320.0,
            screen_center_y=256.0,
            target_source="observed",
        )

        ctrl._on_mouse_click(0, 0, pynput_mouse.Button.left, False)

        get_key.assert_called_with(0x02)
        mouse_event.assert_called_once()
        self.assertEqual(mouse_event.call_args.args[0], 0x0004)
        self.assertFalse(ctrl._manual_left_pressed)
        self.assertFalse(ctrl._is_aiming)
        self.assertFalse(ctrl._auto_fire_requested)
        self.assertFalse(ctrl._left_click_held)
        self.assertIsNone(ctrl.target_info)

    @patch("controllers.mouse_controller.win32api.mouse_event")
    def test_right_mouse_release_immediately_releases_synthetic_input_state(self, mouse_event):
        p = _FakePlugin()
        ctrl = self._make_controller([p])
        ctrl._is_aiming = True
        ctrl._auto_fire_requested = True
        ctrl._left_click_held = True
        ctrl.target_dx = 14.0
        ctrl.target_dy = -6.0

        ctrl._on_mouse_click(0, 0, pynput_mouse.Button.right, False)

        mouse_event.assert_called_once()
        args = mouse_event.call_args.args
        self.assertEqual(args[0], 0x0004)
        self.assertFalse(ctrl._is_aiming)
        self.assertFalse(ctrl._left_click_held)
        self.assertFalse(ctrl._auto_fire_requested)
        self.assertAlmostEqual(ctrl.target_dx, 0.0)
        self.assertAlmostEqual(ctrl.target_dy, 0.0)
        self.assertEqual(p.reset_calls, 1)

    @patch("controllers.mouse_controller.win32api.mouse_event")
    def test_stale_output_is_dropped_after_right_mouse_release(self, mouse_event):
        ctrl = self._make_controller([])
        ctrl._is_aiming = True
        frame = ctrl._build_frame(timestamp=10.0)

        ctrl._on_mouse_click(0, 0, pynput_mouse.Button.right, False)
        mouse_event.reset_mock()

        ctrl._apply_output(
            MouseOutput(move_dx=2.0, move_dy=-1.0, left_click=True),
            input_session_id=frame.input_session_id,
        )

        mouse_event.assert_not_called()

    @patch("controllers.mouse_controller.win32api.mouse_event")
    def test_stale_output_releases_synthetic_left_hold_fail_closed(self, mouse_event):
        ctrl = self._make_controller([])
        ctrl._input_session_id = 2
        ctrl._left_click_held = True

        ctrl._apply_output(MouseOutput(left_click=True), input_session_id=1)

        mouse_event.assert_called_once()
        self.assertEqual(mouse_event.call_args.args[0], 0x0004)
        self.assertFalse(ctrl._left_click_held)

    @patch("controllers.mouse_controller.win32api.mouse_event")
    @patch("controllers.mouse_controller.win32api.GetAsyncKeyState", return_value=0)
    def test_physical_right_release_poll_fail_closes_aim_and_fire(self, get_key, mouse_event):
        ctrl = self._make_controller([])
        ctrl._is_aiming = True
        ctrl._auto_fire_requested = True
        ctrl._left_click_held = True
        ctrl.target_dx = 30.0
        ctrl.target_dy = -4.0
        ctrl.target_info = ControllerTarget(
            aim_point_x=350.0,
            aim_point_y=252.0,
            screen_center_x=320.0,
            screen_center_y=256.0,
            target_source="observed",
        )

        ctrl._sync_physical_aim_state()

        get_key.assert_called_once_with(0x02)
        mouse_event.assert_called_once()
        self.assertEqual(mouse_event.call_args.args[0], 0x0004)
        self.assertFalse(ctrl._is_aiming)
        self.assertFalse(ctrl._auto_fire_requested)
        self.assertFalse(ctrl._left_click_held)
        self.assertIsNone(ctrl.target_info)
        self.assertEqual(ctrl._physical_aim_sync_resets, 1)
        self.assertEqual(ctrl._physical_aim_sync_presses, 0)
        self.assertFalse(ctrl._physical_right_pressed)

    @patch("controllers.mouse_controller.win32api.GetAsyncKeyState", return_value=0x8000)
    def test_physical_right_press_poll_recovers_missed_listener_press(self, get_key):
        ctrl = self._make_controller([])
        ctrl._is_aiming = False

        ctrl._sync_physical_aim_state()

        get_key.assert_called_once_with(0x02)
        self.assertTrue(ctrl._is_aiming)
        self.assertEqual(ctrl._physical_aim_sync_presses, 1)
        self.assertEqual(ctrl._physical_aim_sync_resets, 0)
        self.assertTrue(ctrl._physical_right_pressed)


if __name__ == "__main__":
    unittest.main()
