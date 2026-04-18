import threading
import unittest

import vgamepad as vg

from controllers.base_controller import ControllerTarget
from controllers.gamepad_controller import GamepadController
from controllers.gamepad.state import GamepadOutput


class _FakePlugin:
    def __init__(self):
        self.reset_calls = 0

    def reset(self):
        self.reset_calls += 1

    def apply(self, frame, output):
        return None


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


class GamepadControllerHostTests(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
