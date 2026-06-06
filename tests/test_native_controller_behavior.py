import subprocess
import unittest
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parent.parent
CMAKE_FILE = PROJECT_ROOT / "native" / "vision_native" / "CMakeLists.txt"
BEHAVIOR_EXE = (
    PROJECT_ROOT
    / "native"
    / "vision_native"
    / "build"
    / "Release"
    / "cod_native_controller_tests.exe"
)


class NativeControllerBehaviorTests(unittest.TestCase):
    def test_cmake_declares_native_controller_behavior_tests(self):
        content = CMAKE_FILE.read_text(encoding="utf-8")
        self.assertIn("cod_native_controller_tests", content)
        self.assertIn("../controller_native/controller_behavior_tests.cpp", content)

    def test_native_controller_behavior_executable_passes(self):
        self.assertTrue(
            BEHAVIOR_EXE.exists(),
            "Run tools/build_native_vision.ps1 before this behavior wrapper test.",
        )
        completed = subprocess.run(
            [str(BEHAVIOR_EXE)],
            cwd=PROJECT_ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(
            completed.returncode,
            0,
            completed.stdout + completed.stderr,
        )
        self.assertIn("[NativeControllerTests] PASS", completed.stdout)


if __name__ == "__main__":
    unittest.main()
