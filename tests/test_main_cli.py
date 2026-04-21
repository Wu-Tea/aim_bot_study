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

    def test_parse_args_accepts_vision_debug(self):
        with patch.object(
            sys,
            "argv",
            ["main.py", "--controller-mode", "gamepad", "--vision-debug"],
        ):
            args = main._parse_args()

        self.assertTrue(args.vision_debug)

    def test_parse_args_accepts_vision_debug_save(self):
        with patch.object(
            sys,
            "argv",
            ["main.py", "--controller-mode", "gamepad", "--vision-debug-save"],
        ):
            args = main._parse_args()

        self.assertTrue(args.vision_debug_save)

    def test_parse_args_accepts_native_vision_backend(self):
        with patch.object(
            sys,
            "argv",
            ["main.py", "--controller-mode", "gamepad", "--vision-backend", "native"],
        ):
            args = main._parse_args()

        self.assertEqual(args.vision_backend, "native")

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

    def test_main_routes_native_backend_to_native_process(self):
        fake_controller = _FakeController()
        with patch.object(
            sys,
            "argv",
            ["main.py", "--controller-mode", "gamepad", "--vision-backend", "native"],
        ), patch.object(main.ControllerFactory, "get_controller", return_value=fake_controller), patch.object(
            main,
            "process_vision",
            return_value=None,
        ) as process_vision, patch.object(
            main,
            "process_native_vision",
            return_value=None,
            create=True,
        ) as process_native_vision, patch("builtins.print"):
            exit_code = main.main()

        self.assertEqual(exit_code, 0)
        process_vision.assert_not_called()
        process_native_vision.assert_called_once_with(controller=fake_controller)

    def test_main_sets_vision_backend_env_when_requested(self):
        fake_controller = _FakeController()
        with patch.dict(main.os.environ, {}, clear=True), patch.object(
            sys,
            "argv",
            ["main.py", "--controller-mode", "gamepad", "--vision-backend", "native"],
        ), patch.object(main.ControllerFactory, "get_controller", return_value=fake_controller), patch.object(
            main,
            "process_native_vision",
            return_value=None,
            create=True,
        ), patch("builtins.print"):
            exit_code = main.main()
            self.assertEqual(main.os.environ["VISION_BACKEND"], "native")

        self.assertEqual(exit_code, 0)

    def test_main_sets_debug_overlay_env_when_requested(self):
        fake_controller = _FakeController()
        with patch.dict(main.os.environ, {}, clear=True), patch.object(
            sys,
            "argv",
            ["main.py", "--controller-mode", "gamepad", "--vision-debug"],
        ), patch.object(main.ControllerFactory, "get_controller", return_value=fake_controller), patch.object(
            main,
            "process_vision",
            return_value=None,
        ), patch("builtins.print"):
            exit_code = main.main()
            self.assertEqual(main.os.environ["VISION_DEBUG_OVERLAY"], "1")

        self.assertEqual(exit_code, 0)

    def test_main_sets_debug_save_env_when_requested(self):
        fake_controller = _FakeController()
        with patch.dict(main.os.environ, {}, clear=True), patch.object(
            sys,
            "argv",
            ["main.py", "--controller-mode", "gamepad", "--vision-debug-save"],
        ), patch.object(main.ControllerFactory, "get_controller", return_value=fake_controller), patch.object(
            main,
            "process_vision",
            return_value=None,
        ), patch("builtins.print"):
            exit_code = main.main()
            self.assertEqual(main.os.environ["VISION_DEBUG_SAVE"], "1")

        self.assertEqual(exit_code, 0)


if __name__ == "__main__":
    unittest.main()
