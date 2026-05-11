import sys
import unittest
from types import SimpleNamespace
from unittest.mock import patch

import main
from config.loader import RuntimeConfig, RuntimeGamepadConfig, RuntimeVisionConfig


def _loaded_runtime(runtime: RuntimeConfig):
    return SimpleNamespace(runtime=runtime)


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

    def test_parse_args_uses_runtime_config_defaults(self):
        runtime = RuntimeConfig(
            vision=RuntimeVisionConfig(backend="native"),
            gamepad=RuntimeGamepadConfig(auto_fire_output="RT"),
        )
        with patch.object(sys, "argv", ["main.py"]), patch.object(
            main,
            "load_tuning_config",
            return_value=_loaded_runtime(runtime),
            create=True,
        ):
            args = main._parse_args()

        self.assertEqual(args.vision_backend, "native")
        self.assertEqual(args.auto_fire_output, "RT")

    def test_apply_runtime_overrides_sets_config_baseline_env(self):
        runtime = RuntimeConfig(
            vision=RuntimeVisionConfig(
                backend="native",
                capture_fps=120,
                crop_width=600,
                crop_height=480,
                perf_log=True,
                quit_key="0",
                native_cue_sidecar=False,
            ),
            gamepad=RuntimeGamepadConfig(),
        )
        args = SimpleNamespace(
            perf_log=False,
            vision_backend="native",
            crop_size=None,
            crop_width=None,
            crop_height=None,
            capture_fps=None,
            target_fps=None,
            vision_debug=False,
            vision_debug_save=False,
        )

        with patch.dict(main.os.environ, {}, clear=True):
            main._apply_runtime_overrides(args, runtime)

            self.assertEqual(main.os.environ["VISION_BACKEND"], "native")
            self.assertEqual(main.os.environ["VISION_CAPTURE_FPS"], "120")
            self.assertEqual(main.os.environ["VISION_CROP_WIDTH"], "600")
            self.assertEqual(main.os.environ["VISION_CROP_HEIGHT"], "480")
            self.assertEqual(main.os.environ["VISION_PERF_LOG"], "1")
            self.assertEqual(main.os.environ["VISION_QUIT_KEY"], "0")
            self.assertEqual(main.os.environ["VISION_NATIVE_CUE_SIDECAR"], "0")

    def test_apply_runtime_overrides_keeps_existing_env_above_config(self):
        runtime = RuntimeConfig(
            vision=RuntimeVisionConfig(
                capture_fps=120,
                crop_width=600,
                crop_height=480,
                quit_key="0",
                native_cue_sidecar=False,
            ),
            gamepad=RuntimeGamepadConfig(),
        )
        args = SimpleNamespace(
            perf_log=False,
            vision_backend="native",
            crop_size=None,
            crop_width=None,
            crop_height=None,
            capture_fps=None,
            target_fps=None,
            vision_debug=False,
            vision_debug_save=False,
        )

        with patch.dict(
            main.os.environ,
            {
                "VISION_CAPTURE_FPS": "144",
                "VISION_CROP_WIDTH": "704",
                "VISION_CROP_HEIGHT": "576",
                "VISION_PERF_LOG": "0",
                "VISION_QUIT_KEY": "Q",
                "VISION_NATIVE_CUE_SIDECAR": "1",
            },
            clear=True,
        ):
            main._apply_runtime_overrides(args, runtime)

            self.assertEqual(main.os.environ["VISION_CAPTURE_FPS"], "144")
            self.assertEqual(main.os.environ["VISION_CROP_WIDTH"], "704")
            self.assertEqual(main.os.environ["VISION_CROP_HEIGHT"], "576")
            self.assertEqual(main.os.environ["VISION_PERF_LOG"], "0")
            self.assertEqual(main.os.environ["VISION_QUIT_KEY"], "Q")
            self.assertEqual(main.os.environ["VISION_NATIVE_CUE_SIDECAR"], "1")

    def test_crop_size_cli_overrides_existing_width_and_height_env(self):
        runtime = RuntimeConfig(
            vision=RuntimeVisionConfig(crop_width=600, crop_height=480),
            gamepad=RuntimeGamepadConfig(),
        )
        args = SimpleNamespace(
            perf_log=False,
            vision_backend="native",
            crop_size=512,
            crop_width=None,
            crop_height=None,
            capture_fps=None,
            target_fps=None,
            vision_debug=False,
            vision_debug_save=False,
        )

        with patch.dict(
            main.os.environ,
            {"VISION_CROP_WIDTH": "704", "VISION_CROP_HEIGHT": "576"},
            clear=True,
        ):
            main._apply_runtime_overrides(args, runtime)

            self.assertEqual(main.os.environ["VISION_CROP_SIZE"], "512")
            self.assertEqual(main.os.environ["VISION_CROP_WIDTH"], "512")
            self.assertEqual(main.os.environ["VISION_CROP_HEIGHT"], "512")

    def test_main_passes_auto_fire_output_to_controller_factory(self):
        fake_controller = _FakeController()
        with patch.object(
            sys,
            "argv",
            [
                "main.py",
                "--controller-mode",
                "gamepad",
                "--auto-fire-output",
                "RT",
                "--vision-backend",
                "python",
            ],
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
            ["main.py", "--controller-mode", "gamepad", "--vision-backend", "python", "--vision-debug"],
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
            ["main.py", "--controller-mode", "gamepad", "--vision-backend", "python", "--vision-debug-save"],
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
