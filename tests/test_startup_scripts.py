from pathlib import Path
import unittest


PROJECT_ROOT = Path(__file__).resolve().parent.parent
LAUNCH_DIR = PROJECT_ROOT / "scripts" / "launch"
DEBUG_LAUNCH_DIR = LAUNCH_DIR / "debug"


class StartupScriptTests(unittest.TestCase):
    def test_root_batch_shims_are_removed_after_launcher_consolidation(self):
        expected_launchers = {
            "gamepad_start.bat": LAUNCH_DIR / "gamepad_start.bat",
            "gamepad_debug.bat": DEBUG_LAUNCH_DIR / "gamepad_debug.bat",
            "gamepad_native_debug.bat": DEBUG_LAUNCH_DIR / "gamepad_native_debug.bat",
            "mouse_start.bat": LAUNCH_DIR / "mouse_start.bat",
            "mouse_native_debug.bat": DEBUG_LAUNCH_DIR / "mouse_native_debug.bat",
            "recoil_app_start.bat": LAUNCH_DIR / "recoil_app_start.bat",
            "recoil_toolkit.bat": PROJECT_ROOT / "scripts" / "legacy" / "recoil_toolkit.bat",
        }

        for root_name, consolidated_path in expected_launchers.items():
            with self.subTest(root_name=root_name):
                self.assertFalse((PROJECT_ROOT / root_name).exists())
                self.assertTrue(consolidated_path.exists())

    def test_gamepad_start_uses_system_python_launcher_instead_of_broken_venv_python(self):
        content = (LAUNCH_DIR / "gamepad_start.bat").read_text(encoding="utf-8")

        self.assertFalse((PROJECT_ROOT / "gamepad_start.bat").exists())
        self.assertNotIn(".venv\\Scripts\\python.exe", content)
        self.assertIn("py -3.11", content)
        self.assertIn("config.toml", content)
        self.assertIn("AUTO_FIRE_ARG", content)
        self.assertIn("main.py --controller-mode gamepad !AUTO_FIRE_ARG!", content)
        self.assertNotIn('set "VISION_PERF_LOG=1"', content)
        self.assertNotIn("--perf-log", content)
        self.assertNotIn('set "VISION_BACKEND=native"', content)
        self.assertNotIn('set "VISION_CAPTURE_FPS=140"', content)
        self.assertNotIn('set "VISION_QUIT_KEY=0"', content)
        self.assertNotIn('set "VISION_NATIVE_CUE_SIDECAR=0"', content)
        self.assertNotIn("--vision-backend %VISION_BACKEND%", content)
        self.assertIn("Vision settings:", content)
        self.assertNotIn("VISION_FAST_PREPROCESSOR", content)
        self.assertNotIn("VISION_IDLE_CAPTURE_FPS", content)
        self.assertNotIn("preprocessor=", content)
        self.assertNotIn("idle_capture_fps=", content)
        self.assertNotIn("Select Vision preprocessor:", content)
        self.assertNotIn("Native (experimental)", content)

    def test_gamepad_debug_uses_system_python_launcher_debug_flag_and_backend_prompt(self):
        content = (DEBUG_LAUNCH_DIR / "gamepad_debug.bat").read_text(encoding="utf-8")

        self.assertFalse((PROJECT_ROOT / "gamepad_debug.bat").exists())
        self.assertNotIn(".venv\\Scripts\\python.exe", content)
        self.assertIn("py -3.11", content)
        self.assertIn("Select Vision backend:", content)
        self.assertIn("VISION_BACKEND", content)
        self.assertIn('set "VISION_CAPTURE_FPS=140"', content)
        self.assertIn('set "VISION_QUIT_KEY=0"', content)
        self.assertIn("--vision-backend %VISION_BACKEND%", content)
        self.assertIn("--vision-debug", content)
        self.assertIn("--vision-debug-save", content)

    def test_gamepad_native_debug_uses_native_backend_and_debug_window(self):
        content = (DEBUG_LAUNCH_DIR / "gamepad_native_debug.bat").read_text(encoding="utf-8")

        self.assertFalse((PROJECT_ROOT / "gamepad_native_debug.bat").exists())
        self.assertNotIn(".venv\\Scripts\\python.exe", content)
        self.assertIn("py -3.11", content)
        self.assertIn("--vision-backend native", content)
        self.assertIn("--vision-debug", content)
        self.assertIn("VISION_PERF_LOG", content)
        self.assertIn('set "VISION_CAPTURE_FPS=140"', content)
        self.assertIn('set "VISION_QUIT_KEY=0"', content)

    def test_mouse_start_uses_system_python_launcher_instead_of_broken_venv_python(self):
        content = (LAUNCH_DIR / "mouse_start.bat").read_text(encoding="utf-8")

        self.assertFalse((PROJECT_ROOT / "mouse_start.bat").exists())
        self.assertNotIn(".venv\\Scripts\\python.exe", content)
        self.assertIn("py -3.11", content)

    def test_mouse_scripts_keep_keyboard_quit_failsafe_enabled(self):
        scripts = (
            LAUNCH_DIR / "mouse_start.bat",
            DEBUG_LAUNCH_DIR / "mouse_native_debug.bat",
        )
        for script_path in scripts:
            with self.subTest(script_name=script_path.name):
                content = script_path.read_text(encoding="utf-8")

                self.assertIn('set "VISION_QUIT_KEY=Q"', content)
                self.assertNotIn('set "VISION_QUIT_KEY=0"', content)

    def test_mouse_native_debug_runs_injection_probe_before_launch(self):
        content = (DEBUG_LAUNCH_DIR / "mouse_native_debug.bat").read_text(encoding="utf-8")

        self.assertFalse((PROJECT_ROOT / "mouse_native_debug.bat").exists())
        self.assertIn("MOUSE_PROBE_INPUT", content)
        self.assertIn("tools\\probe_mouse_injection.py --backend %MOUSE_INJECTION_BACKEND%", content)
        self.assertIn("Mouse injection probe failed", content)
        self.assertIn("exit /b 1", content)


if __name__ == "__main__":
    unittest.main()
