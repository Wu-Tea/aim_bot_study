import threading
import unittest

import vgamepad as vg

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
    def test_reset_clears_shared_target_signals_and_resets_plugins(self):
        controller = GamepadController.__new__(GamepadController)
        controller.lock = threading.Lock()
        controller.target_dx = 12.0
        controller.target_dy = -8.0
        controller.plugins = [_FakePlugin(), _FakePlugin()]

        GamepadController.reset(controller)

        self.assertEqual(controller.target_dx, 0.0)
        self.assertEqual(controller.target_dy, 0.0)
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


if __name__ == "__main__":
    unittest.main()
