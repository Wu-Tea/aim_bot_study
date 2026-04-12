import sys
import unittest
from unittest.mock import patch

import main


class _FakeController:
    def reset(self):
        return None

    def stop(self):
        return None


class MainCliTests(unittest.TestCase):
    def test_parse_args_accepts_auto_fire_output(self):
        with patch.object(
            sys,
            "argv",
            ["main.py", "--controller-mode", "gamepad", "--auto-fire-output", "RT"],
        ):
            args = main._parse_args()

        self.assertEqual(args.controller_mode, "gamepad")
        self.assertEqual(args.auto_fire_output, "RT")

    def test_main_passes_auto_fire_output_to_controller_factory(self):
        fake_controller = _FakeController()
        with patch.object(
            sys,
            "argv",
            ["main.py", "--controller-mode", "gamepad", "--auto-fire-output", "RT"],
        ), patch.object(main.ControllerFactory, "get_controller", return_value=fake_controller) as get_controller, patch.object(
            main,
            "process_vision",
            return_value=None,
        ), patch("builtins.print"):
            exit_code = main.main()

        self.assertEqual(exit_code, 0)
        get_controller.assert_called_once_with(
            controller_mode="gamepad",
            auto_fire_output="RT",
        )


if __name__ == "__main__":
    unittest.main()
