from pathlib import Path
import re
import unittest


PROJECT_ROOT = Path(__file__).resolve().parent.parent
BUILD_SCRIPT = PROJECT_ROOT / "tools" / "build_native_vision.ps1"
SMOKE_SCRIPT = PROJECT_ROOT / "tools" / "run_native_vision_smoke.ps1"
NATIVE_DIR = PROJECT_ROOT / "native" / "vision_native"
CMAKE_FILE = NATIVE_DIR / "CMakeLists.txt"


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


class NativeVisionScriptTests(unittest.TestCase):
    def test_build_script_configures_native_vision_with_vs_cmake(self):
        content = _read(BUILD_SCRIPT)

        self.assertIn("TensorRTRoot", content)
        self.assertIn("D:\\env\\TensorRT-10.15.1.29", content)
        self.assertIn("CudaPath", content)
        self.assertIn("C:\\Program Files\\NVIDIA GPU Computing Toolkit\\CUDA\\v13.1", content)
        self.assertIn("BuildDir", content)
        self.assertIn("Configuration", content)
        self.assertRegex(content, r'\$Configuration\s*=\s*"Release"')
        self.assertIn("VsDevCmd.bat", content)
        self.assertIn("CMake\\bin\\cmake.exe", content)
        self.assertIn("native\\vision_native", content)
        self.assertIn("-DTensorRT_ROOT=$TensorRTRoot", content)
        self.assertIn("-DCUDAToolkit_ROOT=$CudaPath", content)
        self.assertIn("-Dpybind11_DIR=$Pybind11CMakeDir", content)
        self.assertIn("-DPython_EXECUTABLE=$PythonExe", content)
        self.assertIn("pybind11", content)
        self.assertIn("--cmakedir", content)
        self.assertIn("--build", content)
        self.assertIn("--config", content)

    def test_smoke_script_runs_built_smoke_exe_with_runtime_dll_paths(self):
        content = _read(SMOKE_SCRIPT)

        self.assertIn("TensorRTRoot", content)
        self.assertIn("CudaPath", content)
        self.assertIn("BuildDir", content)
        self.assertIn("Configuration", content)
        self.assertRegex(content, r'\$Configuration\s*=\s*"Release"')
        self.assertIn("BuildFirst", content)
        self.assertIn("build_native_vision.ps1", content)
        self.assertIn("models\\best.engine", content)
        self.assertIn("TensorRTRoot", content)
        self.assertIn("CudaPath", content)
        self.assertIn("bin", content)
        self.assertIn("PATH", content)
        self.assertIn("vision_native_smoke.exe", content)
        self.assertIn("& $SmokeExe", content)


class NativeVisionCMakeTests(unittest.TestCase):
    def setUp(self):
        if not CMAKE_FILE.exists():
            self.skipTest("native/vision_native scaffold is provided by another worker in this slice")

    def test_cmake_declares_tensorrt_roots_libraries_smoke_and_pybind_module(self):
        content = _read(CMAKE_FILE)

        self.assertIn("TensorRT_ROOT", content)
        self.assertRegex(content, r"\bnvinfer_10\b")
        self.assertRegex(content, r"\bnvinfer_plugin_10\b")
        self.assertRegex(content, r"add_executable\s*\(\s*vision_native_smoke\b")
        self.assertRegex(content, r"pybind11_add_module\s*\(")

    def test_sources_expose_pybind_module_without_requiring_real_compile(self):
        source_files = [
            path
            for path in NATIVE_DIR.rglob("*")
            if path.suffix.lower() in {".cc", ".cpp", ".cxx", ".h", ".hpp", ".cu", ".cuh"}
        ]
        self.assertTrue(source_files, "native/vision_native should include C++ source files")

        combined = "\n".join(_read(path) for path in source_files)

        self.assertIn("PYBIND11_MODULE", combined)
        self.assertRegex(combined, r"\bvision_native\b")

    def test_engine_inspector_handles_ultralytics_metadata_prefix(self):
        source = _read(NATIVE_DIR / "src" / "tensorrt_inspector.cpp")

        self.assertIn("metadata_prefix_bytes", source)
        self.assertIn("looks_like_ultralytics_metadata", source)
        self.assertIn("deserializeCudaEngine(bytes.plan.data(), bytes.plan.size())", source)


class NativeVisionProductionIsolationTests(unittest.TestCase):
    def test_native_scaffold_is_not_connected_to_production_runner(self):
        production_files = [
            PROJECT_ROOT / "main.py",
            PROJECT_ROOT / "controller.py",
            PROJECT_ROOT / "vision" / "runner.py",
            PROJECT_ROOT / "vision" / "fastpath.py",
            PROJECT_ROOT / "vision" / "inference.py",
        ]

        for path in production_files:
            with self.subTest(path=path.relative_to(PROJECT_ROOT)):
                content = _read(path)
                self.assertNotIn("vision_native", content)
                self.assertNotIn("native_vision", content)


if __name__ == "__main__":
    unittest.main()
