from pathlib import Path
import unittest


PROJECT_ROOT = Path(__file__).resolve().parent.parent


class StartupScriptTests(unittest.TestCase):
    def test_gamepad_start_uses_system_python_launcher_instead_of_broken_venv_python(self):
        content = (PROJECT_ROOT / "gamepad_start.bat").read_text(encoding="utf-8")

        self.assertNotIn(".venv\\Scripts\\python.exe", content)
        self.assertIn("py -3.11", content)

    def test_mouse_start_uses_system_python_launcher_instead_of_broken_venv_python(self):
        content = (PROJECT_ROOT / "mouse_start.bat").read_text(encoding="utf-8")

        self.assertNotIn(".venv\\Scripts\\python.exe", content)
        self.assertIn("py -3.11", content)


if __name__ == "__main__":
    unittest.main()
