import threading
import unittest
from unittest.mock import patch

from controllers.base_controller import ControllerTarget
from controllers.mouse_controller import MouseController
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
        ctrl._is_aiming = False
        ctrl._auto_fire_requested = False
        ctrl._acc_dx = 0.0
        ctrl._acc_dy = 0.0
        ctrl._manual_left_pressed = False
        ctrl._left_click_held = False
        ctrl._manual_override_until = None
        ctrl._input_session_id = 0
        ctrl.plugins = list(plugins)
        return ctrl

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


if __name__ == "__main__":
    unittest.main()
