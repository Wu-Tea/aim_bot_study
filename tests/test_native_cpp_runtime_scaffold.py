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
    def test_native_runtime_directories_exist(self):
        self.assertTrue(CONTROLLER_NATIVE_DIR.exists())
        self.assertTrue(RUNTIME_APP_DIR.exists())

    def test_cmake_declares_native_runtime_executable(self):
        content = _read(CMAKE_FILE)
        self.assertIn("cod_native_runtime", content)
        self.assertIn("native/controller_native", content)
        self.assertIn("native/runtime_app", content)

    def test_runtime_entrypoint_and_core_headers_exist(self):
        expected = [
            RUNTIME_APP_DIR / "main.cpp",
            RUNTIME_APP_DIR / "runtime_loop.h",
            RUNTIME_APP_DIR / "runtime_loop.cpp",
            RUNTIME_APP_DIR / "perf_logger.h",
            RUNTIME_APP_DIR / "perf_logger.cpp",
            CONTROLLER_NATIVE_DIR / "runtime_config.h",
            CONTROLLER_NATIVE_DIR / "runtime_config.cpp",
            CONTROLLER_NATIVE_DIR / "native_gamepad_controller.h",
            CONTROLLER_NATIVE_DIR / "native_gamepad_controller.cpp",
            CONTROLLER_NATIVE_DIR / "virtual_gamepad.h",
            CONTROLLER_NATIVE_DIR / "virtual_gamepad.cpp",
            CONTROLLER_NATIVE_DIR / "xinput_reader.h",
            CONTROLLER_NATIVE_DIR / "xinput_reader.cpp",
            CONTROLLER_NATIVE_DIR / "sdl_gamepad_reader.h",
            CONTROLLER_NATIVE_DIR / "sdl_gamepad_reader.cpp",
        ]
        for path in expected:
            with self.subTest(path=path):
                self.assertTrue(path.exists(), path)

    def test_runtime_config_loader_reads_current_config_toml(self):
        header = _read(CONTROLLER_NATIVE_DIR / "runtime_config.h")
        source = _read(CONTROLLER_NATIVE_DIR / "runtime_config.cpp")
        self.assertIn("struct VisionRuntimeConfig", header)
        self.assertIn("struct GamepadRuntimeConfig", header)
        self.assertIn("load_runtime_config", header)
        self.assertIn("config.toml", source)
        self.assertIn("gamepad.ai_aim", source)
        self.assertIn("gamepad.recoil", source)
        self.assertIn("aim_assist_dynamics", source)
        self.assertIn("RECOIL_PROFILE_DIR", source)
        self.assertIn("RECOIL_RECOGNIZER_STATE_PATH", source)

    def test_native_runtime_vision_defaults_match_python_runtime_loader_defaults(self):
        config_header = _read(CONTROLLER_NATIVE_DIR / "runtime_config.h")
        self.assertIn("int capture_fps = 140", config_header)
        self.assertIn("bool perf_log = true", config_header)
        self.assertIn("bool native_cue_sidecar = false", config_header)

    def test_runtime_entrypoint_exposes_config_and_perf_log_flags(self):
        main_cpp = _read(RUNTIME_APP_DIR / "main.cpp")
        self.assertIn("--config", main_cpp)
        self.assertIn("--perf-log", main_cpp)
        self.assertIn("--once", main_cpp)
        self.assertIn("--max-ticks", main_cpp)
        self.assertIn("std::stoul", main_cpp)
        self.assertIn("load_runtime_config", main_cpp)
        self.assertIn("RuntimeLoop", main_cpp)

    def test_xinput_reader_contract_exists(self):
        header = _read(CONTROLLER_NATIVE_DIR / "xinput_reader.h")
        source = _read(CONTROLLER_NATIVE_DIR / "xinput_reader.cpp")
        self.assertIn("struct PhysicalGamepadState", header)
        self.assertIn("class XInputReader", header)
        self.assertIn("read", header)
        self.assertIn("detect_first_connected_user_index", header)
        self.assertIn("user_index", header)
        self.assertIn("XInputGetState", source)
        self.assertIn("XUSER_MAX_COUNT", source)
        self.assertIn("dpad_up", header)
        self.assertIn("back", header)
        self.assertIn("left_thumb", header)
        self.assertIn("XINPUT_GAMEPAD_DPAD_UP", source)
        self.assertIn("XINPUT_GAMEPAD_START", source)

    def test_runtime_auto_detects_xinput_before_creating_virtual_output(self):
        runtime_header = _read(RUNTIME_APP_DIR / "runtime_loop.h")
        runtime_source = _read(RUNTIME_APP_DIR / "runtime_loop.cpp")
        config_header = _read(CONTROLLER_NATIVE_DIR / "runtime_config.h")
        config_source = _read(CONTROLLER_NATIVE_DIR / "runtime_config.cpp")
        self.assertIn("xinput_auto_detect", config_header)
        self.assertIn("xinput_user_index", config_header)
        self.assertIn("GAMEPAD_XINPUT_USER_INDEX", config_source)
        self.assertIn("GAMEPAD_XINPUT_AUTO_DETECT", config_source)
        self.assertIn("select_xinput_user_index", runtime_source)
        self.assertIn("scan_xinput_user_slots", runtime_source)
        self.assertIn("input_reader_(select_xinput_user_index", runtime_source)
        self.assertNotIn("input_reader_(0)", runtime_source)
        self.assertIn("selected_xinput_user_index_", runtime_header)
        self.assertIn("XInput input index", runtime_source)

    def test_runtime_logs_full_xinput_slot_scan_and_selects_lowest_connected(self):
        reader_header = _read(CONTROLLER_NATIVE_DIR / "xinput_reader.h")
        reader_source = _read(CONTROLLER_NATIVE_DIR / "xinput_reader.cpp")
        runtime_source = _read(RUNTIME_APP_DIR / "runtime_loop.cpp")
        self.assertIn("struct XInputUserSlot", reader_header)
        self.assertIn("std::vector<XInputUserSlot>", reader_header)
        self.assertIn("scan_xinput_user_slots", reader_header)
        self.assertIn("std::vector<XInputUserSlot>", reader_source)
        self.assertIn("slots.push_back", reader_source)
        self.assertIn("for (DWORD index = 0; index < XUSER_MAX_COUNT; ++index)", reader_source)
        self.assertIn("slot.connected", reader_source)
        self.assertIn("scan_xinput_user_slots", runtime_source)
        self.assertIn("log_xinput_slot_table", runtime_source)
        self.assertIn("XInput slot scan", runtime_source)
        self.assertIn("lowest connected", runtime_source)

    def test_native_runtime_prefers_sdl_joystick_input_for_dualsense_style_gamepads(self):
        cmake = _read(CMAKE_FILE)
        sdl_header = _read(CONTROLLER_NATIVE_DIR / "sdl_gamepad_reader.h")
        sdl_source = _read(CONTROLLER_NATIVE_DIR / "sdl_gamepad_reader.cpp")
        runtime_header = _read(RUNTIME_APP_DIR / "runtime_loop.h")
        runtime_source = _read(RUNTIME_APP_DIR / "runtime_loop.cpp")
        self.assertIn("sdl_gamepad_reader.cpp", cmake)
        self.assertIn("SDL2_DLL", cmake)
        self.assertIn("SDL2.dll", cmake)
        self.assertIn("struct SdlJoystickDevice", sdl_header)
        self.assertIn("class SdlGamepadReader", sdl_header)
        self.assertIn("scan_sdl_joystick_devices", sdl_header)
        self.assertIn("LoadLibraryA", sdl_source)
        self.assertIn("SDL_JoystickGetAxis", sdl_source)
        self.assertIn("SDL_JoystickGetButton", sdl_source)
        self.assertIn("SDL_JoystickGetHat", sdl_source)
        self.assertIn("SDL_JOYSTICK_HIDAPI_PS5", sdl_source)
        self.assertIn("RIGHT_TRIGGER_AXIS_INDEX", sdl_source)
        self.assertIn("LEFT_TRIGGER_AXIS_INDEX", sdl_source)
        self.assertIn("sdl_input_reader_", runtime_header)
        self.assertIn("open_sdl_input_reader", runtime_source)
        self.assertIn("read_physical_gamepad", runtime_source)
        self.assertIn("SDL joystick scan", runtime_source)
        self.assertIn("selected SDL joystick", runtime_source)

    def test_virtual_gamepad_backend_contract_exists(self):
        header = _read(CONTROLLER_NATIVE_DIR / "virtual_gamepad.h")
        source = _read(CONTROLLER_NATIVE_DIR / "virtual_gamepad.cpp")
        self.assertIn("struct GamepadOutputState", header)
        self.assertIn("class VirtualGamepad", header)
        self.assertIn("update", header)
        self.assertTrue("ViGEm" in source or "vigem" in source)
        self.assertIn("LoadLibraryA", source)
        self.assertIn("vigem_target_x360_update", source)
        self.assertIn("dpad_up", header)
        self.assertIn("back", header)
        self.assertIn("kButtonDpadUp", source)
        self.assertIn("kButtonStart", source)
        self.assertIn("GAMEPAD_INPUT_LOG", source)
        self.assertIn("virtual_gamepad_log_enabled", source)

    def test_vigem_client_dll_is_packaged_beside_native_runtime(self):
        cmake = _read(CMAKE_FILE)
        source = _read(CONTROLLER_NATIVE_DIR / "virtual_gamepad.cpp")
        self.assertIn("ViGEmClient_DLL", cmake)
        self.assertIn("copy_if_different", cmake)
        self.assertIn("$<TARGET_FILE_DIR:cod_native_runtime>/ViGEmClient.dll", cmake)
        self.assertNotIn("site-packages", source)

    def test_native_gamepad_controller_owns_pass_through_pipeline(self):
        header = _read(CONTROLLER_NATIVE_DIR / "native_gamepad_controller.h")
        source = _read(CONTROLLER_NATIVE_DIR / "native_gamepad_controller.cpp")
        self.assertIn("class NativeGamepadController", header)
        self.assertIn("submit_vision_state", header)
        self.assertIn("build_output", source)
        self.assertIn("PhysicalGamepadState", source)
        self.assertIn("GamepadOutputState", source)
        self.assertIn("dpad_up", source)
        self.assertIn("left_thumb", source)

    def test_runtime_connects_vision_result_directly_to_controller(self):
        runtime_source = _read(RUNTIME_APP_DIR / "runtime_loop.cpp")
        controller_header = _read(CONTROLLER_NATIVE_DIR / "native_gamepad_controller.h")
        self.assertIn("VisionEngine", runtime_source)
        self.assertIn("poll_once", runtime_source)
        self.assertIn("submit_vision_result", controller_header)
        self.assertIn("VisionResult", controller_header)

    def test_runtime_keeps_controller_tick_fast_and_throttles_vision_polling(self):
        runtime_source = _read(RUNTIME_APP_DIR / "runtime_loop.cpp")
        runtime_header = _read(RUNTIME_APP_DIR / "runtime_loop.h")
        self.assertIn("std::chrono::milliseconds(1)", runtime_source)
        self.assertNotIn("std::chrono::milliseconds(4)", runtime_source)
        self.assertIn("capture_interval_for_fps", runtime_source)
        self.assertIn("should_poll_vision", runtime_source)
        self.assertIn("config_.vision.capture_fps", runtime_source)
        self.assertIn("std::lround(1'000'000.0", runtime_source)
        self.assertIn("config_.vision.capture_width,", runtime_source)
        self.assertIn("config_.vision.capture_height,", runtime_source)
        self.assertIn("0);", runtime_source)
        self.assertIn("last_vision_poll_at_", runtime_header)
        self.assertIn("latest_vision_result_", runtime_header)

    def test_runtime_neutralizes_virtual_gamepad_on_exit(self):
        runtime_source = _read(RUNTIME_APP_DIR / "runtime_loop.cpp")
        self.assertIn("GamepadOutputState{}", runtime_source)
        self.assertIn("virtual_gamepad_.update(GamepadOutputState{})", runtime_source)

    def test_runtime_handles_console_stop_requests_gracefully(self):
        main_cpp = _read(RUNTIME_APP_DIR / "main.cpp")
        runtime_header = _read(RUNTIME_APP_DIR / "runtime_loop.h")
        runtime_source = _read(RUNTIME_APP_DIR / "runtime_loop.cpp")
        self.assertIn("SetConsoleCtrlHandler", main_cpp)
        self.assertIn("CTRL_C_EVENT", main_cpp)
        self.assertIn("request_stop", main_cpp)
        self.assertIn("request_stop", runtime_header)
        self.assertIn("stop_requested_", runtime_header)
        self.assertIn("stop_requested_.store", runtime_source)
        self.assertIn("stop_requested_.load", runtime_source)
        self.assertIn("virtual_gamepad_.update(GamepadOutputState{})", runtime_source)

    def test_vision_result_marks_whether_poll_processed_a_new_frame(self):
        types_header = _read(VISION_NATIVE_DIR / "include" / "vision_native" / "types.h")
        engine_source = _read(VISION_NATIVE_DIR / "src" / "vision_engine.cpp")
        module_source = _read(VISION_NATIVE_DIR / "src" / "vision_native_module.cpp")
        self.assertIn("frame_updated", types_header)
        self.assertIn("frame_updated", engine_source)
        self.assertIn("frame_updated", module_source)

    def test_native_controller_contains_auto_fire_authority_gate(self):
        source = _read(CONTROLLER_NATIVE_DIR / "native_gamepad_controller.cpp")
        self.assertIn("fire_authority", source)
        self.assertIn("auto_fire_requested", source)
        self.assertIn("aiming", source)
        self.assertIn("require_aim_ready", source)
        self.assertIn("auto_fire_ready_frames", source)
        config_header = _read(CONTROLLER_NATIVE_DIR / "runtime_config.h")
        config_source = _read(CONTROLLER_NATIVE_DIR / "runtime_config.cpp")
        self.assertIn("auto_fire_ready_error_px", config_header)
        self.assertIn("auto_fire_ready_frames", config_header)
        self.assertIn("auto_fire_ready_min_ads_ms", config_source)
        self.assertIn("auto_fire_ready_max_ai_stick", config_source)

    def test_native_ai_aim_module_exists(self):
        header = CONTROLLER_NATIVE_DIR / "ai_aim.h"
        source = CONTROLLER_NATIVE_DIR / "ai_aim.cpp"
        config_header = _read(CONTROLLER_NATIVE_DIR / "runtime_config.h")
        config_source = _read(CONTROLLER_NATIVE_DIR / "runtime_config.cpp")
        controller_source = _read(CONTROLLER_NATIVE_DIR / "native_gamepad_controller.cpp")
        self.assertTrue(header.exists())
        self.assertTrue(source.exists())
        self.assertIn("class NativeAiAim", _read(header))
        self.assertIn("compute", _read(header))
        self.assertIn("weak_target_body_lock_force_scale", _read(source))
        self.assertIn("cue_hold_body_lock_force_scale", _read(source))
        self.assertIn("has_body_box", _read(header))
        self.assertIn("body_lock_activation_box_px", config_header)
        self.assertIn("body_lock_confidence_frames", config_header)
        self.assertIn("body_lock_opposing_suppression_max", config_source)
        self.assertIn("body_lock_orthogonal_suppression_max", config_source)
        self.assertIn("body_lock_helpful_preservation_floor", config_source)
        self.assertIn("body_lock_manual_overlap_scale", config_source)
        self.assertIn("body_lock_near_lock_error_px", config_header)
        self.assertIn("body_lock_vertical_orthogonal_bias", config_source)
        self.assertIn("body_lock_vertical_deadzone_px", config_source)
        self.assertIn("body_lock_vertical_tail_inner_px", config_source)
        self.assertIn("body_lock_vertical_tail_speed_threshold_px_per_sec", config_source)
        self.assertIn("body_lock_release_tail_scale", config_source)
        self.assertIn("body_lock_smoothing", config_source)
        self.assertIn("body_lock_lateral_motion_min_speed_px_per_sec", config_source)
        self.assertIn("body_lock_lateral_motion_lead_seconds", config_source)
        self.assertIn("body_lock_lateral_motion_lead_window_px", config_source)
        self.assertIn("body_lock_lateral_motion_lead_max_px", config_source)
        self.assertIn("body_lock_lateral_motion_tail_scale", config_source)
        self.assertIn("body_lock_lead_frames", config_source)
        self.assertIn("body_lock_lead_seconds", config_source)
        self.assertIn("body_lock_vertical_lead_scale", config_source)
        self.assertIn("body_lock_lead_max_px", config_source)
        self.assertIn("body_lock_target_match_iou", config_source)
        self.assertIn("body_lock_target_match_center_px", config_source)
        self.assertIn("ads_snap_window_ms", config_header)
        self.assertIn("ads_snap_smoothing", config_source)
        self.assertIn("ads_snap_max_ai_force", config_source)
        self.assertIn("ads_snap_max_target_dy_px", config_source)
        self.assertIn("ads_snap_reticle_speed_px_per_sec", config_source)
        self.assertIn("ads_snap_time_to_go_gain", config_source)
        self.assertIn("ads_snap_opposing_manual_suppression_max", config_source)
        self.assertIn("piecewise_mid_pixels", config_header)
        self.assertIn("deadzone_inner", config_source)
        self.assertIn("body_lock_box_tolerance_px", config_source)
        self.assertIn("body_x1", controller_source)
        self.assertIn("body_lock_upper_body_ratio", _read(source))
        self.assertIn("ads_snap_active", _read(header))
        self.assertIn("ads_snap_progress_ratio", _read(header))
        self.assertIn("reset", _read(header))

    def test_native_ai_aim_defaults_match_python_runtime_loader_defaults(self):
        config_header = _read(CONTROLLER_NATIVE_DIR / "runtime_config.h")
        self.assertIn("body_lock_smoothing = 0.14f", config_header)
        self.assertIn("cue_hold_body_lock_force_scale = 0.35f", config_header)

    def test_native_target_tracker_module_ports_projection_fields(self):
        header = CONTROLLER_NATIVE_DIR / "target_tracker.h"
        source = CONTROLLER_NATIVE_DIR / "target_tracker.cpp"
        self.assertTrue(header.exists())
        self.assertTrue(source.exists())
        self.assertIn("class NativeGamepadTargetTracker", _read(header))
        self.assertIn("record_output", _read(header))
        self.assertIn("target_projection_reticle_speed_px_per_sec", _read(CONTROLLER_NATIVE_DIR / "runtime_config.h"))
        config_source = _read(CONTROLLER_NATIVE_DIR / "runtime_config.cpp")
        self.assertIn("target_projection_reticle_speed_px_per_sec", config_source)
        self.assertIn("target_projection_velocity_lowpass_alpha", config_source)
        self.assertIn("target_projection_max_velocity_px_per_sec", config_source)
        self.assertIn("target_projection_weak_velocity_decay", config_source)
        cmake = _read(CMAKE_FILE)
        self.assertIn("../controller_native/target_tracker.cpp", cmake)

    def test_native_aim_assist_dynamics_module_exists(self):
        header = CONTROLLER_NATIVE_DIR / "aim_assist_dynamics.h"
        source = CONTROLLER_NATIVE_DIR / "aim_assist_dynamics.cpp"
        self.assertTrue(header.exists())
        self.assertTrue(source.exists())
        self.assertIn("class NativeAimAssistDynamics", _read(header))
        self.assertIn("sign", _read(source))
        self.assertIn("recoil", _read(source))

    def test_native_recoil_modules_exist(self):
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
        recoil_source = _read(CONTROLLER_NATIVE_DIR / "recoil_compensation.cpp")
        profile_header = _read(CONTROLLER_NATIVE_DIR / "recoil_profile.h")
        profile_source = _read(CONTROLLER_NATIVE_DIR / "recoil_profile.cpp")
        controller_source = _read(CONTROLLER_NATIVE_DIR / "native_gamepad_controller.cpp")
        self.assertIn("profile_despike", recoil_source)
        self.assertIn("target_direction_yield", recoil_source)
        self.assertIn("timeline", recoil_source)
        self.assertIn("canonical_weapon_id", profile_header)
        self.assertIn("load_matching_recoil_profile", profile_header)
        self.assertIn("profile_ids", profile_source)
        self.assertIn("recognizer_state_path", recoil_source)
        self.assertIn("set_recognizer_state_path", controller_source)
        calibration_header = _read(CONTROLLER_NATIVE_DIR / "recoil_calibration.h")
        calibration_source = _read(CONTROLLER_NATIVE_DIR / "recoil_calibration.cpp")
        config_header = _read(CONTROLLER_NATIVE_DIR / "runtime_config.h")
        config_source = _read(CONTROLLER_NATIVE_DIR / "runtime_config.cpp")
        cmake = _read(CMAKE_FILE)
        self.assertIn("struct RecoilCalibration", calibration_header)
        self.assertIn("load_matching_recoil_calibration", calibration_header)
        self.assertIn("pixels_per_full_stick_y_per_second", calibration_source)
        self.assertIn("calibration_directory", config_header)
        self.assertIn("calibration_directory", config_source)
        self.assertIn("RECOIL_CALIBRATION_DIR", config_source)
        self.assertIn("../controller_native/recoil_calibration.cpp", cmake)

    def test_native_recoil_defaults_match_python_runtime_loader_defaults(self):
        config_header = _read(CONTROLLER_NATIVE_DIR / "runtime_config.h")
        self.assertIn("selection_log_enabled = true", config_header)
        self.assertIn("feedback_amount = 0.20f", config_header)
        self.assertIn("profile_velocity_reference_ms = 10.0f", config_header)
        self.assertIn("profile_despike_threshold_px = 2.0f", config_header)
        self.assertIn("profile_despike_ratio = 3.0f", config_header)

    def test_native_perf_logger_keeps_python_parity_fields(self):
        source = _read(RUNTIME_APP_DIR / "perf_logger.cpp")
        required = [
            "[Perf][CPP]",
            "loop=",
            "native=",
            "consume=",
            "out_age=",
            "gpu_total=",
            "sync_wait=",
            "tier",
            "fire req",
            "box samples",
        ]
        for token in required:
            with self.subTest(token=token):
                self.assertIn(token, source)

    def test_native_runtime_emits_vision_diagnostics_not_gamepad_tables_by_default(self):
        source = _read(RUNTIME_APP_DIR / "runtime_loop.cpp")
        header = _read(RUNTIME_APP_DIR / "runtime_loop.h")
        self.assertIn("log_vision_result", source)
        self.assertIn("[Vision][CPP]", source)
        self.assertIn("aiming=", source)
        self.assertIn("updated=", source)
        self.assertIn("frame=", source)
        self.assertIn("boxes=", source)
        self.assertIn("target=", source)
        self.assertIn("source=", source)
        self.assertIn("stage=", source)
        self.assertIn("conf=", source)
        self.assertIn("dx=", source)
        self.assertIn("dy=", source)
        self.assertIn("aim_auth=", source)
        self.assertIn("fire_auth=", source)
        self.assertIn("cap=", source)
        self.assertIn("copy=", source)
        self.assertIn("pre=", source)
        self.assertIn("infer=", source)
        self.assertIn("decode=", source)
        self.assertIn("selector=", source)
        self.assertIn("enhance=", source)
        self.assertIn("age=", source)
        self.assertIn("GAMEPAD_INPUT_LOG", source)
        self.assertIn("GAMEPAD_PERF_LOG", source)
        self.assertIn("VISION_LOG_INTERVAL_TICKS", source)
        self.assertIn("input_log_enabled", source)
        self.assertIn("gamepad_perf_log_", header)
        self.assertIn("latest_vision_aiming_", header)
        self.assertIn("should_log_vision_tick", source)
        self.assertIn("log_vision", source)
        self.assertIn("log_gamepad_perf", source)

    def test_runtime_measures_virtual_gamepad_update_time(self):
        source = _read(RUNTIME_APP_DIR / "runtime_loop.cpp")
        self.assertIn("vigem_update_started", source)
        self.assertIn("vigem_update_finished", source)
        self.assertIn("snapshot.vigem_update_ms", source)
        self.assertNotIn("snapshot.vigem_update_ms = 0.0", source)

    def test_runtime_perf_ages_are_measured_at_controller_and_output_time(self):
        source = _read(RUNTIME_APP_DIR / "runtime_loop.cpp")
        self.assertIn("result_timestamp_ns", source)
        self.assertIn("controller_consume_started", source)
        self.assertIn("steady_time_point_ns", source)
        self.assertIn("elapsed_ms_between_ns", source)
        self.assertIn("snapshot.consume_ms", source)
        self.assertIn("snapshot.out_age_ms", source)
        self.assertNotIn("snapshot.consume_ms = 0.0", source)
        self.assertNotIn("snapshot.out_age_ms = result.age_ms", source)

    def test_native_cpp_launcher_exists(self):
        launcher = PROJECT_ROOT / "scripts" / "launch" / "gamepad_native_cpp_start.bat"
        self.assertTrue(launcher.exists())
        content = _read(launcher)
        self.assertIn("cod_native_runtime.exe", content)
        self.assertIn("--config", content)
        self.assertIn("config.toml", content)

    def test_native_cpp_runtime_docs_cover_gamepad_launch_and_acceptance(self):
        doc = PROJECT_ROOT / "docs" / "project" / "NATIVE_CPP_RUNTIME.md"
        self.assertTrue(doc.exists())
        content = _read(doc)
        required = [
            "cod_native_runtime.exe",
            "scripts\\launch\\gamepad_native_cpp_start.bat",
            "gamepad",
            "native vision",
            "Python fallback",
            "manual pass-through",
            "ai_aim",
            "recoil",
            "auto-fire gate",
            "perf stability",
            "gamepad_start.bat",
        ]
        for token in required:
            with self.subTest(token=token):
                self.assertIn(token, content)
        docs_index = _read(PROJECT_ROOT / "docs" / "project" / "README.md")
        self.assertIn("NATIVE_CPP_RUNTIME.md", docs_index)

    def test_native_cpp_gamepad_readiness_check_script_exists(self):
        script = PROJECT_ROOT / "tools" / "check_native_cpp_gamepad_runtime.ps1"
        self.assertTrue(script.exists())
        content = _read(script)
        required = [
            "build_native_vision.ps1",
            "cod_native_controller_tests.exe",
            "cod_native_runtime.exe",
            "--config config.toml --perf-log --once",
            "--config config.toml --perf-log --max-ticks",
            "native runtime short sustained smoke",
            "GAMEPAD_START_PRINT_ONLY",
            "gamepad_start.bat",
            "runtime binary has no Python dependency",
            "Assert-BinaryTextAbsent",
            "native launcher has no Python gameplay dependency",
            "Assert-TextAbsent",
            "native runtime source has no Python gameplay dependency",
            "Assert-DirectoryTextAbsent",
            "tests.test_native_cpp_runtime_scaffold",
            "tests.test_native_controller_behavior",
            "tests.test_startup_scripts",
            "NativeCppGamepadCheck",
        ]
        for token in required:
            with self.subTest(token=token):
                self.assertIn(token, content)

    def test_native_launcher_preserves_gamepad_runtime_choices(self):
        launcher = PROJECT_ROOT / "scripts" / "launch" / "gamepad_native_cpp_start.bat"
        content = _read(launcher)
        self.assertIn("AUTO_FIRE_ARG", content)
        self.assertIn("GAMEPAD_START_FIRE_CHOICE_OVERRIDE", content)
        self.assertIn("--auto-fire-output", content)
        self.assertIn("GAMEPAD_START_PRINT_ONLY", content)
        self.assertIn("ENABLE_RECOIL_RUNTIME", content)
        self.assertIn("GAMEPAD_START_RECOIL_CHOICE_OVERRIDE", content)
        self.assertIn("RECOIL_PROFILE_DIR", content)
        self.assertIn("RECOIL_RECOGNIZER_STATE_PATH", content)
        self.assertIn("artifacts\\recoil_app\\current_weapon.json", content)
        self.assertIn("RECOIL_CLEAR_STATE_ON_START", content)

    def test_native_recoil_recognizer_is_y_switch_triggered(self):
        header = _read(RUNTIME_APP_DIR / "runtime_loop.h")
        source = _read(RUNTIME_APP_DIR / "runtime_loop.cpp")
        recognizer_header = _read(CONTROLLER_NATIVE_DIR / "weapon_recognizer.h")
        self.assertIn("RecoilWeaponSwitchCaptureScheduler", recognizer_header)
        self.assertIn("update_recoil_recognizer_schedule", header)
        self.assertIn("poll_due_recoil_recognizer", header)
        self.assertIn("physical.y", source)
        self.assertIn("consume_due_capture", source)
        self.assertNotIn("should_poll_recoil_recognizer", header)
        self.assertNotIn("should_poll_recoil_recognizer", source)
        self.assertNotIn("recognizer_fps", source)

    def test_runtime_entrypoint_accepts_auto_fire_output_override(self):
        main_cpp = _read(RUNTIME_APP_DIR / "main.cpp")
        self.assertIn("--auto-fire-output", main_cpp)
        self.assertIn("auto_fire_output", main_cpp)
        self.assertIn("config.gamepad.auto_fire.fire_output", main_cpp)

    def test_native_runtime_ports_downward_pull_diagnostics(self):
        header = RUNTIME_APP_DIR / "downward_diagnostics.h"
        source = RUNTIME_APP_DIR / "downward_diagnostics.cpp"
        self.assertTrue(header.exists())
        self.assertTrue(source.exists())
        header_content = _read(header)
        source_content = _read(source)
        runtime_header = _read(RUNTIME_APP_DIR / "runtime_loop.h")
        runtime_source = _read(RUNTIME_APP_DIR / "runtime_loop.cpp")
        cmake = _read(CMAKE_FILE)
        self.assertIn("class DownwardPullDiagnostics", header_content)
        self.assertIn("GAMEPAD_DOWNWARD_DIAGNOSTICS", source_content)
        self.assertIn("GAMEPAD_DOWNWARD_DIAGNOSTICS_PATH", source_content)
        self.assertIn("GAMEPAD_DOWNWARD_DIAGNOSTICS_THRESHOLD", source_content)
        self.assertIn("gamepad_downward_pull.jsonl", source_content)
        self.assertIn("last_pipeline_traces", runtime_source)
        self.assertIn("record_if_triggered", runtime_source)
        self.assertIn("DownwardPullDiagnostics", runtime_header)
        self.assertIn("../runtime_app/downward_diagnostics.cpp", cmake)


if __name__ == "__main__":
    unittest.main()
