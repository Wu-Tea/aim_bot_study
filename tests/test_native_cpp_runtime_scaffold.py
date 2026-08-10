from pathlib import Path
import unittest


PROJECT_ROOT = Path(__file__).resolve().parent.parent
NATIVE_DIR = PROJECT_ROOT / "native"
VISION_NATIVE_DIR = NATIVE_DIR / "vision_native"
CONTROLLER_NATIVE_DIR = NATIVE_DIR / "controller_native"
RUNTIME_APP_DIR = NATIVE_DIR / "runtime_app"
CMAKE_FILE = VISION_NATIVE_DIR / "CMakeLists.txt"


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


class NativeCppRuntimeScaffoldTests(unittest.TestCase):
    def test_current_runtime_sources_exist(self):
        expected = [
            RUNTIME_APP_DIR / "main.cpp",
            RUNTIME_APP_DIR / "runtime_loop.h",
            RUNTIME_APP_DIR / "runtime_loop.cpp",
            RUNTIME_APP_DIR / "perf_logger.h",
            RUNTIME_APP_DIR / "perf_logger.cpp",
            RUNTIME_APP_DIR / "runtime_telemetry.h",
            RUNTIME_APP_DIR / "runtime_telemetry.cpp",
            RUNTIME_APP_DIR / "telemetry_collectors.h",
            RUNTIME_APP_DIR / "telemetry_collectors.cpp",
            CONTROLLER_NATIVE_DIR / "runtime_config.h",
            CONTROLLER_NATIVE_DIR / "runtime_config.cpp",
            CONTROLLER_NATIVE_DIR / "native_gamepad_controller.h",
            CONTROLLER_NATIVE_DIR / "native_gamepad_controller.cpp",
            CONTROLLER_NATIVE_DIR / "ads_acquisition_controller.h",
            CONTROLLER_NATIVE_DIR / "bodylock_follow_controller.h",
            CONTROLLER_NATIVE_DIR / "aim_dynamics_shaper.h",
            CONTROLLER_NATIVE_DIR / "assist_control_state_machine.h",
            CONTROLLER_NATIVE_DIR / "response_model_aim_solver.h",
        ]
        for path in expected:
            with self.subTest(path=path):
                self.assertTrue(path.exists(), path)

    def test_cmake_declares_only_current_runtime_chain(self):
        content = _read(CMAKE_FILE)
        for token in [
            "cod_native_runtime",
            "ads_acquisition_controller.cpp",
            "bodylock_follow_controller.cpp",
            "aim_dynamics_shaper.cpp",
            "response_model_aim_solver.cpp",
            "perf_logger.cpp",
            "runtime_telemetry.cpp",
            "telemetry_collectors.cpp",
        ]:
            with self.subTest(token=token):
                self.assertIn(token, content)

        for retired in [
            "aim_perf_file_logger.cpp",
            "ai_aim.cpp",
            "aim_assist_dynamics.cpp",
            "target_tracker.cpp",
            "control_response_window.cpp",
            "gate2_5_live_shadow.cpp",
            "causal_online_response_learner.cpp",
            "aim_enhancement.cpp",
        ]:
            with self.subTest(retired=retired):
                self.assertNotIn(retired, content)

    def test_runtime_config_uses_one_telemetry_switch_and_directory(self):
        header = _read(CONTROLLER_NATIVE_DIR / "runtime_config.h")
        source = _read(CONTROLLER_NATIVE_DIR / "runtime_config.cpp")
        runtime = _read(RUNTIME_APP_DIR / "runtime_loop.cpp")

        self.assertIn("struct RuntimeTelemetryConfig", header)
        self.assertIn('std::string directory = "runs/native_perf"', header)
        self.assertIn("RUNTIME_TELEMETRY_ENABLED", source)
        self.assertIn("RUNTIME_TELEMETRY_DIRECTORY", source)
        self.assertIn("config.telemetry.enabled", runtime)
        self.assertIn("config.telemetry.directory", runtime)

        combined = header + source + runtime
        for retired in [
            "aim_perf_file_log",
            "aim_perf_log_dir",
            "aim_perf_log_interval_ticks",
            "VISION_AIM_PERF_FILE_LOG",
            "VISION_AIM_PERF_LOG_DIR",
            "VISION_AIM_PERF_LOG_INTERVAL_TICKS",
            "AimPerfFileLogger",
        ]:
            with self.subTest(retired=retired):
                self.assertNotIn(retired, combined)

    def test_runtime_connects_fresh_vision_to_current_controller(self):
        runtime = _read(RUNTIME_APP_DIR / "runtime_loop.cpp")
        controller_header = _read(CONTROLLER_NATIVE_DIR / "native_gamepad_controller.h")
        controller_source = _read(CONTROLLER_NATIVE_DIR / "native_gamepad_controller.cpp")

        self.assertIn("VisionEngine", runtime)
        self.assertIn("poll_once", runtime)
        self.assertIn("submit_vision_snapshot", controller_header)
        self.assertIn("submit_vision_snapshot(adapt_vision_result(result))", runtime)
        self.assertIn("TargetCoordinator", controller_header)
        self.assertIn("AdsAcquisitionController", controller_header)
        self.assertIn("BodylockFollowController", controller_header)
        self.assertIn("AimDynamicsShaper", controller_header)
        self.assertIn("AssistControlStateMachine", controller_header)
        self.assertIn("fire_authority", controller_source)
        self.assertIn("auto_fire_requested", controller_source)

    def test_vision_result_has_frame_freshness_without_enhancement_stage(self):
        types_header = _read(VISION_NATIVE_DIR / "include" / "vision_native" / "types.h")
        engine_source = _read(VISION_NATIVE_DIR / "src" / "vision_engine.cpp")
        module_source = _read(VISION_NATIVE_DIR / "src" / "vision_native_module.cpp")
        combined = types_header + engine_source + module_source

        self.assertIn("frame_updated", types_header)
        self.assertIn("frame_updated", engine_source)
        self.assertIn("frame_updated", module_source)
        self.assertNotIn("AimEnhancement", combined)
        self.assertNotIn("enhance_ms", combined)

    def test_controller_tick_and_neutral_shutdown_contract(self):
        runtime = _read(RUNTIME_APP_DIR / "runtime_loop.cpp")
        runtime_header = _read(RUNTIME_APP_DIR / "runtime_loop.h")
        config_header = _read(CONTROLLER_NATIVE_DIR / "runtime_config.h")

        self.assertIn("int controller_tick_hz = 1000", config_header)
        self.assertIn("config_.scheduler.controller_tick_hz", runtime)
        self.assertIn("AbsoluteDeadlineState", runtime)
        self.assertIn("capture_interval_for_fps", runtime)
        self.assertIn("config_.vision.capture_fps", runtime)
        self.assertIn("last_vision_poll_at_", runtime_header)
        self.assertIn("latest_vision_result_", runtime_header)
        self.assertIn("virtual_gamepad_.update(GamepadOutputState{})", runtime)

    def test_gamepad_input_and_output_backends_exist(self):
        xinput_header = _read(CONTROLLER_NATIVE_DIR / "xinput_reader.h")
        xinput_source = _read(CONTROLLER_NATIVE_DIR / "xinput_reader.cpp")
        sdl_source = _read(CONTROLLER_NATIVE_DIR / "sdl_gamepad_reader.cpp")
        vigem_source = _read(CONTROLLER_NATIVE_DIR / "virtual_gamepad.cpp")

        self.assertIn("PhysicalGamepadState", xinput_header)
        self.assertIn("scan_xinput_user_slots", xinput_header)
        self.assertIn("XInputGetState", xinput_source)
        self.assertIn("SDL_JoystickGetAxis", sdl_source)
        self.assertIn("vigem_target_x360_update", vigem_source)

    def test_recoil_remains_a_separate_feed_forward_stage(self):
        expected = [
            CONTROLLER_NATIVE_DIR / "recoil_profile.h",
            CONTROLLER_NATIVE_DIR / "recoil_profile.cpp",
            CONTROLLER_NATIVE_DIR / "recoil_calibration.h",
            CONTROLLER_NATIVE_DIR / "recoil_calibration.cpp",
            CONTROLLER_NATIVE_DIR / "recoil_compensation.h",
            CONTROLLER_NATIVE_DIR / "recoil_compensation.cpp",
        ]
        for path in expected:
            with self.subTest(path=path):
                self.assertTrue(path.exists(), path)

        controller = _read(CONTROLLER_NATIVE_DIR / "native_gamepad_controller.cpp")
        recoil = _read(CONTROLLER_NATIVE_DIR / "recoil_compensation.cpp")
        self.assertIn("profile_despike", recoil)
        self.assertIn("set_recognizer_state_path", controller)

    def test_runtime_entrypoint_and_launcher_remain_operable(self):
        main_cpp = _read(RUNTIME_APP_DIR / "main.cpp")
        launcher = PROJECT_ROOT / "scripts" / "launch" / "gamepad_native_cpp_start.bat"
        launcher_text = _read(launcher)

        for token in ["--config", "--perf-log", "--once", "--max-ticks", "RuntimeLoop"]:
            with self.subTest(token=token):
                self.assertIn(token, main_cpp)
        self.assertIn("cod_native_runtime.exe", launcher_text)
        self.assertIn("--config", launcher_text)
        self.assertIn("config.toml", launcher_text)


if __name__ == "__main__":
    unittest.main()
