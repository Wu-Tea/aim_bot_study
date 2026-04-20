from pathlib import Path
import unittest


PROJECT_ROOT = Path(__file__).resolve().parent.parent


class StartupScriptTests(unittest.TestCase):
    def test_gamepad_start_uses_system_python_launcher_instead_of_broken_venv_python(self):
        content = (PROJECT_ROOT / "gamepad_start.bat").read_text(encoding="utf-8")

        self.assertNotIn(".venv\\Scripts\\python.exe", content)
        self.assertIn("py -3.11", content)
        self.assertIn("VISION_FAST_PREPROCESSOR", content)
        self.assertIn("VISION_FAST_PATH", content)
        self.assertIn("VISION_CAPTURE_FPS", content)
        self.assertIn("VISION_IDLE_CAPTURE_FPS", content)
        self.assertIn("Select Vision preprocessor:", content)
        self.assertIn("Vision settings:", content)

    def test_gamepad_debug_uses_system_python_launcher_and_enables_debug_flag(self):
        content = (PROJECT_ROOT / "gamepad_debug.bat").read_text(encoding="utf-8")

        self.assertNotIn(".venv\\Scripts\\python.exe", content)
        self.assertIn("py -3.11", content)
        self.assertIn("--vision-debug", content)
        self.assertIn("--vision-debug-save", content)

    def test_mouse_start_uses_system_python_launcher_instead_of_broken_venv_python(self):
        content = (PROJECT_ROOT / "mouse_start.bat").read_text(encoding="utf-8")

        self.assertNotIn(".venv\\Scripts\\python.exe", content)
        self.assertIn("py -3.11", content)


if __name__ == "__main__":
    unittest.main()
