from pathlib import Path
import unittest
import re


PROJECT_ROOT = Path(__file__).resolve().parent.parent


class StartupScriptTests(unittest.TestCase):
    def test_gamepad_start_uses_system_python_launcher_instead_of_broken_venv_python(self):
        content = (PROJECT_ROOT / "gamepad_start.bat").read_text(encoding="utf-8")

        self.assertNotIn(".venv\\Scripts\\python.exe", content)
        self.assertIn("py -3.11", content)
        self.assertIn("VISION_BACKEND", content)
        self.assertIn('set "VISION_BACKEND=native"', content)
        self.assertIn("--perf-log", content)
        self.assertIn("VISION_CAPTURE_FPS", content)
        self.assertIn("VISION_TRACK_FPS", content)
        self.assertIn("VISION_WARMSCAN_FPS", content)
        self.assertIn("VISION_SCAN_FPS", content)
        self.assertIn("VISION_RECOVERY_SCAN_FPS", content)
        self.assertRegex(content, r'if not defined VISION_CAPTURE_FPS set "VISION_CAPTURE_FPS=\d+"')
        self.assertIn('if not defined VISION_TRACK_FPS set "VISION_TRACK_FPS=160"', content)
        self.assertIn('if not defined VISION_WARMSCAN_FPS set "VISION_WARMSCAN_FPS=20"', content)
        self.assertIn('if not defined VISION_SCAN_FPS set "VISION_SCAN_FPS=80"', content)
        self.assertIn('if not defined VISION_RECOVERY_SCAN_FPS set "VISION_RECOVERY_SCAN_FPS=125"', content)
        self.assertIn('set "VISION_QUIT_KEY=0"', content)
        self.assertIn("--vision-backend %VISION_BACKEND%", content)
        self.assertIn("Vision settings:", content)
        self.assertNotIn("VISION_FAST_PREPROCESSOR", content)
        self.assertNotIn("VISION_IDLE_CAPTURE_FPS", content)
        self.assertNotIn("preprocessor=", content)
        self.assertNotIn("idle_capture_fps=", content)
        self.assertNotIn("Select Vision preprocessor:", content)
        self.assertNotIn("Native (experimental)", content)

    def test_gamepad_debug_uses_system_python_launcher_debug_flag_and_backend_prompt(self):
        content = (PROJECT_ROOT / "gamepad_debug.bat").read_text(encoding="utf-8")

        self.assertNotIn(".venv\\Scripts\\python.exe", content)
        self.assertIn("py -3.11", content)
        self.assertIn("Select Vision backend:", content)
        self.assertIn("VISION_BACKEND", content)
        self.assertIn('set "VISION_CAPTURE_FPS=240"', content)
        self.assertIn('set "VISION_TRACK_FPS=160"', content)
        self.assertIn('set "VISION_WARMSCAN_FPS=20"', content)
        self.assertIn('set "VISION_SCAN_FPS=80"', content)
        self.assertIn('set "VISION_RECOVERY_SCAN_FPS=125"', content)
        self.assertIn('set "VISION_QUIT_KEY=0"', content)
        self.assertIn("--vision-backend %VISION_BACKEND%", content)
        self.assertIn("--vision-debug", content)
        self.assertIn("--vision-debug-save", content)

    def test_gamepad_native_debug_uses_native_backend_and_debug_window(self):
        content = (PROJECT_ROOT / "gamepad_native_debug.bat").read_text(encoding="utf-8")

        self.assertNotIn(".venv\\Scripts\\python.exe", content)
        self.assertIn("py -3.11", content)
        self.assertIn("--vision-backend native", content)
        self.assertIn("--vision-debug", content)
        self.assertIn("VISION_PERF_LOG", content)
        self.assertIn('set "VISION_CAPTURE_FPS=240"', content)
        self.assertIn('set "VISION_TRACK_FPS=160"', content)
        self.assertIn('set "VISION_WARMSCAN_FPS=20"', content)
        self.assertIn('set "VISION_SCAN_FPS=80"', content)
        self.assertIn('set "VISION_RECOVERY_SCAN_FPS=125"', content)
        self.assertIn('set "VISION_QUIT_KEY=0"', content)

    def test_mouse_start_uses_system_python_launcher_instead_of_broken_venv_python(self):
        content = (PROJECT_ROOT / "mouse_start.bat").read_text(encoding="utf-8")

        self.assertNotIn(".venv\\Scripts\\python.exe", content)
        self.assertIn("py -3.11", content)
        self.assertIn("VISION_BACKEND", content)
        self.assertIn('set "VISION_BACKEND=native"', content)
        self.assertIn("VISION_PERF_LOG", content)
        self.assertIn('set "VISION_CAPTURE_FPS=240"', content)
        self.assertIn('set "VISION_TRACK_FPS=160"', content)
        self.assertIn('set "VISION_WARMSCAN_FPS=20"', content)
        self.assertIn('set "VISION_SCAN_FPS=80"', content)
        self.assertIn('set "VISION_RECOVERY_SCAN_FPS=125"', content)
        self.assertIn('set "VISION_QUIT_KEY=0"', content)
        self.assertIn("--vision-backend %VISION_BACKEND%", content)
        self.assertIn("Vision settings:", content)
        self.assertNotIn("--vision-debug", content)
        self.assertNotIn("--vision-debug-save", content)

    def test_mouse_native_debug_uses_native_backend_and_debug_window(self):
        content = (PROJECT_ROOT / "mouse_native_debug.bat").read_text(encoding="utf-8")

        self.assertNotIn(".venv\\Scripts\\python.exe", content)
        self.assertIn("py -3.11", content)
        self.assertIn("VISION_BACKEND", content)
        self.assertIn('set "VISION_BACKEND=native"', content)
        self.assertIn("VISION_PERF_LOG", content)
        self.assertIn('set "VISION_CAPTURE_FPS=240"', content)
        self.assertIn('set "VISION_TRACK_FPS=160"', content)
        self.assertIn('set "VISION_WARMSCAN_FPS=20"', content)
        self.assertIn('set "VISION_SCAN_FPS=80"', content)
        self.assertIn('set "VISION_RECOVERY_SCAN_FPS=125"', content)
        self.assertIn('set "VISION_QUIT_KEY=0"', content)
        self.assertIn("--vision-backend %VISION_BACKEND%", content)
        self.assertIn("--vision-debug", content)
        self.assertIn("--vision-debug-save", content)
        self.assertIn("Vision settings:", content)


if __name__ == "__main__":
    unittest.main()
