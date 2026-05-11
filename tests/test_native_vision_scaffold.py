from pathlib import Path
import re
import unittest


PROJECT_ROOT = Path(__file__).resolve().parent.parent
BUILD_SCRIPT = PROJECT_ROOT / "tools" / "build_native_vision.ps1"
SMOKE_SCRIPT = PROJECT_ROOT / "tools" / "run_native_vision_smoke.ps1"
INFER_SMOKE_SCRIPT = PROJECT_ROOT / "tools" / "run_native_vision_infer_smoke.ps1"
CAPTURE_SMOKE_SCRIPT = PROJECT_ROOT / "tools" / "run_native_vision_capture_smoke.ps1"
DEBUG_SCRIPT = PROJECT_ROOT / "tools" / "run_native_vision_debug.ps1"
NATIVE_DIR = PROJECT_ROOT / "native" / "vision_native"
CMAKE_FILE = NATIVE_DIR / "CMakeLists.txt"


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def _native_source_files():
    return [
        path
        for path in NATIVE_DIR.rglob("*")
        if "build" not in path.relative_to(NATIVE_DIR).parts
        and path.suffix.lower() in {".cc", ".cpp", ".cxx", ".h", ".hpp", ".cu", ".cuh"}
    ]


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

    def test_infer_smoke_script_runs_pybind_native_engine(self):
        content = _read(INFER_SMOKE_SCRIPT)

        self.assertIn("BuildFirst", content)
        self.assertIn("build_native_vision.ps1", content)
        self.assertIn("models\\best.engine", content)
        self.assertIn("PYTHONPATH", content)
        self.assertIn("vision_native_cpp", content)
        self.assertIn("NativeEngine", content)
        self.assertIn("infer_rgb", content)

    def test_capture_smoke_script_runs_pybind_native_dxgi_capture(self):
        content = _read(CAPTURE_SMOKE_SCRIPT)

        self.assertIn("BuildFirst", content)
        self.assertIn("PYTHONPATH", content)
        self.assertIn("vision_native_cpp", content)
        self.assertIn("NativeDxgiCapture", content)
        self.assertIn("grab", content)
        self.assertIn("memory_kind", content)

    def test_debug_script_runs_native_vision_debug_program(self):
        content = _read(DEBUG_SCRIPT)

        self.assertIn("BuildFirst", content)
        self.assertIn("build_native_vision.ps1", content)
        self.assertIn("vision_native_debug.exe", content)
        self.assertIn("Frames", content)
        self.assertIn("Aim", content)


class NativeVisionCMakeTests(unittest.TestCase):
    def setUp(self):
        if not CMAKE_FILE.exists():
            self.skipTest("native/vision_native scaffold is provided by another worker in this slice")

    def test_cmake_declares_tensorrt_roots_libraries_smoke_and_pybind_module(self):
        content = _read(CMAKE_FILE)

        self.assertIn("TensorRT_ROOT", content)
        self.assertIn("CUDA", content)
        self.assertRegex(content, r"\bnvinfer_10\b")
        self.assertRegex(content, r"\bnvinfer_plugin_10\b")
        self.assertRegex(content, r"add_executable\s*\(\s*vision_native_smoke\b")
        self.assertRegex(content, r"pybind11_add_module\s*\(")

    def test_phase1_native_inference_sources_are_declared(self):
        content = _read(CMAKE_FILE)

        self.assertIn("src/tensorrt_engine.cpp", content)
        self.assertIn("src/preprocess.cu", content)

    def test_phase2_native_dxgi_capture_sources_are_declared(self):
        content = _read(CMAKE_FILE)

        self.assertIn("src/dxgi_capture.cpp", content)
        self.assertIn("d3d11", content)
        self.assertIn("dxgi", content)

    def test_phase3_native_engine_and_debug_sources_are_declared(self):
        content = _read(CMAKE_FILE)

        self.assertIn("src/vision_engine.cpp", content)
        self.assertIn("src/vision_debug_main.cpp", content)
        self.assertRegex(content, r"add_executable\s*\(\s*vision_native_debug\b")

    def test_sources_expose_pybind_module_without_requiring_real_compile(self):
        source_files = _native_source_files()
        self.assertTrue(source_files, "native/vision_native should include C++ source files")

        combined = "\n".join(_read(path) for path in source_files)

        self.assertIn("PYBIND11_MODULE", combined)
        self.assertRegex(combined, r"\bvision_native\b")

    def test_engine_inspector_handles_ultralytics_metadata_prefix(self):
        combined = "\n".join(_read(path) for path in _native_source_files())

        self.assertIn("metadata_prefix_bytes", combined)
        self.assertIn("looks_like_ultralytics_metadata", combined)
        self.assertIn("deserializeCudaEngine", combined)

    def test_phase1_inference_protocol_and_cuda_preprocess_are_present(self):
        combined = "\n".join(_read(path) for path in _native_source_files())

        self.assertIn("FramePacket", combined)
        self.assertIn("DetectionBatch", combined)
        self.assertIn("run_inference_rgb", combined)
        self.assertIn("launch_rgb_hwc_to_chw_float", combined)
        self.assertIn("enqueueV3", combined)
        self.assertIn("setTensorAddress", combined)
        self.assertIn("300", combined)
        self.assertIn("6", combined)

    def test_phase2_native_dxgi_capture_protocol_is_present(self):
        combined = "\n".join(_read(path) for path in _native_source_files())

        self.assertIn("DxgiRoiCapture", combined)
        self.assertIn("DuplicateOutput", combined)
        self.assertIn("AcquireNextFrame", combined)
        self.assertIn("CopySubresourceRegion", combined)
        self.assertIn("ReleaseFrame", combined)
        self.assertIn("DXGI_ERROR_WAIT_TIMEOUT", combined)
        self.assertIn("MemoryKind::D3D11Texture", combined)
        self.assertIn("PixelFormat::BGRA8", combined)

    def test_phase3_native_engine_protocol_is_present(self):
        combined = "\n".join(_read(path) for path in _native_source_files())

        self.assertIn("VisionResult", combined)
        self.assertIn("VisionEngine", combined)
        self.assertIn("poll_once", combined)
        self.assertIn("set_aiming", combined)
        self.assertIn("reset", combined)
        self.assertIn("result_at_ns", combined)
        self.assertIn("auto_fire", combined)
        self.assertIn("post_ms", combined)
        self.assertIn("age_ms", combined)
        self.assertIn("boxes_seen", combined)
        self.assertIn("vision_native_debug", combined)

    def test_phase3_pybind_exposes_native_vision_engine(self):
        combined = "\n".join(_read(path) for path in _native_source_files())

        self.assertIn("NativeVisionEngine", combined)
        self.assertIn("poll_once", combined)
        self.assertIn("set_aiming", combined)

    def test_phase3a_native_engine_wires_dxgi_texture_into_inference(self):
        combined = "\n".join(_read(path) for path in _native_source_files())

        self.assertIn("cudaGraphicsD3D11RegisterResource", combined)
        self.assertIn("cudaGraphicsMapResources", combined)
        self.assertIn("cudaGraphicsSubResourceGetMappedArray", combined)
        self.assertIn("cudaMemcpy2DFromArrayAsync", combined)
        self.assertIn("cudaD3D11GetDevices", combined)
        self.assertIn("infer_bgra", combined)
        self.assertIn("launch_bgra", combined)
        self.assertIn("TensorRTEngine", combined)


class NativeVisionProductionIsolationTests(unittest.TestCase):
    def test_native_backend_is_default_for_gamepad_start_without_touching_python_runner(self):
        main_content = _read(PROJECT_ROOT / "main.py")
        start_content = _read(PROJECT_ROOT / "gamepad_start.bat")
        config_content = _read(PROJECT_ROOT / "config.toml.example")

        self.assertIn("--vision-backend", main_content)
        self.assertIn("load_tuning_config().runtime", main_content)
        self.assertIn('runtime_config.vision.backend', main_content)
        self.assertIn('backend = "native"', config_content)
        self.assertIn('quit_key = "0"', config_content)
        self.assertIn("config.toml", start_content)
        self.assertNotIn('set "VISION_BACKEND=native"', start_content)
        self.assertNotIn("--vision-backend %VISION_BACKEND%", start_content)
        self.assertNotIn('set "VISION_QUIT_KEY=0"', start_content)

        production_files = [
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
