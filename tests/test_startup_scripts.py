from pathlib import Path
import json
import subprocess
import tomllib
import unittest


PROJECT_ROOT = Path(__file__).resolve().parent.parent
LAUNCH_DIR = PROJECT_ROOT / "scripts" / "launch"
DEBUG_LAUNCH_DIR = LAUNCH_DIR / "debug"


class StartupScriptTests(unittest.TestCase):
    def test_background_double_click_entries_are_zero_window_vbs_launchers(self):
        entries = {
            "gamepad_native_background_start.vbs":
                "gamepad_native_background_start.ps1",
            "gamepad_native_background_stop.vbs":
                "gamepad_native_background_stop.ps1",
            "gamepad_fusion_background_start.vbs":
                "gamepad_fusion_background_start.ps1",
            "gamepad_fusion_background_stop.vbs":
                "gamepad_fusion_background_stop.ps1",
        }

        for entry_name, powershell_name in entries.items():
            with self.subTest(entry_name=entry_name):
                entry = LAUNCH_DIR / entry_name
                self.assertTrue(entry.exists())
                content = entry.read_text(encoding="utf-8")
                self.assertIn("WScript.Shell", content)
                self.assertIn("shell.Run", content)
                self.assertIn(powershell_name, content)
                if entry_name.startswith("gamepad_fusion_"):
                    self.assertIn(", 0, True", content)
                    self.assertIn("exitCode", content)
                    self.assertIn("shell.Popup", content)
                    self.assertIn("launcher.log", content)
                else:
                    self.assertIn(", 0, False", content)

    def test_background_lifecycle_scripts_enforce_owned_pid_and_path(self):
        start = (
            LAUNCH_DIR / "gamepad_native_background_start.ps1"
        ).read_text(encoding="utf-8")
        stop = (
            LAUNCH_DIR / "gamepad_native_background_stop.ps1"
        ).read_text(encoding="utf-8")

        for content in (start, stop):
            self.assertIn("[switch]$PrintOnly", content)
            self.assertIn("native_runtime_state.json", content)
            self.assertIn("Win32_Process", content)
            self.assertIn("ExecutablePath", content)
            self.assertIn("ConvertTo-Json", content)

        self.assertIn("cod_native_runtime.exe", start)
        self.assertIn("config.toml", start)
        self.assertIn("Start-Process", start)
        self.assertIn("-WindowStyle Hidden", start)
        self.assertIn("-RedirectStandardOutput", start)
        self.assertIn("-RedirectStandardError", start)
        self.assertIn("Stop-Process -Id", stop)
        self.assertNotIn("Get-Process -Name", stop)
        self.assertNotIn("taskkill /IM", stop)

    def test_native_background_start_keeps_late_fusion_attach_ready(self):
        start = (
            LAUNCH_DIR / "gamepad_native_background_start.ps1"
        ).read_text(encoding="utf-8")

        self.assertIn('$env:FUSION_ENABLED = "1"', start)
        self.assertIn('$env:FUSION_SHOW_ALL_DETECTIONS = "0"', start)
        self.assertIn("fusion_channel_enabled", start)
        self.assertIn("fusion_session", start)

    def test_fusion_background_lifecycle_owns_only_recorded_canvas(self):
        start = (
            LAUNCH_DIR / "gamepad_fusion_background_start.ps1"
        ).read_text(encoding="utf-8")
        stop = (
            LAUNCH_DIR / "gamepad_fusion_background_stop.ps1"
        ).read_text(encoding="utf-8")

        for content in (start, stop):
            self.assertIn("[switch]$PrintOnly", content)
            self.assertIn("fusion_canvas_state.json", content)
            self.assertIn("Win32_Process", content)
            self.assertIn("ExecutablePath", content)
            self.assertIn("ConvertTo-Json", content)

        self.assertIn("fusion_canvas.exe", start)
        self.assertIn("native_runtime_state.json", start)
        self.assertIn("gamepad_native_background_start.ps1", start)
        self.assertIn("native_started_by_fusion", start)
        self.assertIn("ownedCanvasRunning", start)
        self.assertIn("Canvas/native Fusion session mismatch", start)
        self.assertIn('$env:FUSION_SESSION = $session', start)
        self.assertIn("-Wait", start)
        native_launch_start = start.index(
            "$nativeStartProcess = Start-Process"
        )
        native_launch_end = start.index(
            "$nativeStartedByFusion = $true", native_launch_start
        )
        native_launch = start[native_launch_start:native_launch_end]
        self.assertNotIn("-Wait", native_launch)
        self.assertIn("$nativeStartProcess.WaitForExit(30000)", native_launch)
        self.assertIn("--verify-capture-isolation", start)
        self.assertIn("$preflightLogPath", start)
        self.assertIn(
            "Get-Content -LiteralPath $canvasLogPath -Raw -ErrorAction Stop",
            start,
        )
        self.assertNotIn("ReadAllText($canvasLogPath)", start)
        self.assertIn("[FusionCanvas] channel connected", start)
        self.assertIn("[FusionCanvas] running", start)
        self.assertIn("AddSeconds(15)", start)
        self.assertIn("-WindowStyle Hidden", start)
        self.assertIn("-RedirectStandardOutput", start)
        self.assertIn("-RedirectStandardError", start)
        self.assertIn("$executablePath", stop)
        self.assertIn("Paths-Equal $recordedExecutablePath $executablePath", stop)
        self.assertIn("Stop-Process -Id", stop)
        self.assertNotIn("Get-Process -Name", stop)
        self.assertNotIn("taskkill /IM", stop)

    def test_fusion_canvas_log_allows_live_launcher_reads(self):
        source = (
            PROJECT_ROOT / "native" / "overlay_canvas" /
            "fusion_canvas.cpp"
        ).read_text(encoding="utf-8")

        self.assertIn("#include <share.h>", source)
        self.assertIn('_fsopen(path, "a", _SH_DENYNO)', source)

    def test_fusion_target_marker_uses_requested_yellow(self):
        source = (
            PROJECT_ROOT / "native" / "overlay_canvas" /
            "fusion_canvas.cpp"
        ).read_text(encoding="utf-8")

        self.assertIn(
            "D2D1::ColorF(0xFFE607, 1.0f), &brush_target_",
            source,
        )

    def test_background_lifecycle_print_only_is_side_effect_free(self):
        state_path = (
            PROJECT_ROOT / "runs" / "runtime" / "background" /
            "native_runtime_state.json"
        )
        state_existed_before = state_path.exists()
        scripts = {
            "gamepad_native_background_start.ps1": "preview_start",
            "gamepad_native_background_stop.ps1": "preview_stop",
        }

        for script_name, expected_action in scripts.items():
            with self.subTest(script_name=script_name):
                completed = subprocess.run(
                    [
                        "powershell",
                        "-NoProfile",
                        "-ExecutionPolicy",
                        "Bypass",
                        "-File",
                        str(LAUNCH_DIR / script_name),
                        "-PrintOnly",
                    ],
                    cwd=PROJECT_ROOT,
                    text=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    check=False,
                )
                self.assertEqual(completed.returncode, 0, completed.stdout)
                preview = json.loads(completed.stdout)
                self.assertEqual(preview["action"], expected_action)
                self.assertEqual(Path(preview["state_path"]), state_path)
                if expected_action == "preview_start":
                    self.assertEqual(
                        Path(preview["executable_path"]),
                        PROJECT_ROOT / "native" / "vision_native" / "build" /
                        "Release" / "cod_native_runtime.exe",
                    )
                    self.assertEqual(
                        Path(preview["config_path"]), PROJECT_ROOT / "config.toml"
                    )
                    self.assertEqual(
                        Path(preview["model_path"]),
                        PROJECT_ROOT / "models" / "best_480x384.engine",
                    )
                    self.assertTrue(preview["model_exists"])

        self.assertEqual(state_path.exists(), state_existed_before)

    def test_fusion_background_lifecycle_print_only_is_side_effect_free(self):
        state_path = (
            PROJECT_ROOT / "runs" / "fusion_canvas" / "background" /
            "fusion_canvas_state.json"
        )
        state_existed_before = state_path.exists()
        scripts = {
            "gamepad_fusion_background_start.ps1": "preview_fusion_start",
            "gamepad_fusion_background_stop.ps1": "preview_fusion_stop",
        }

        for script_name, expected_action in scripts.items():
            with self.subTest(script_name=script_name):
                completed = subprocess.run(
                    [
                        "powershell",
                        "-NoProfile",
                        "-ExecutionPolicy",
                        "Bypass",
                        "-File",
                        str(LAUNCH_DIR / script_name),
                        "-PrintOnly",
                    ],
                    cwd=PROJECT_ROOT,
                    text=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    check=False,
                )
                self.assertEqual(completed.returncode, 0, completed.stdout)
                preview = json.loads(completed.stdout)
                self.assertEqual(preview["action"], expected_action)
                self.assertEqual(Path(preview["state_path"]), state_path)
                if expected_action == "preview_fusion_start":
                    self.assertEqual(
                        Path(preview["executable_path"]),
                        PROJECT_ROOT / "native" / "vision_native" / "build" /
                        "Release" / "fusion_canvas.exe",
                    )
                    self.assertEqual(preview["session"], "dev")
                    self.assertTrue(preview["auto_start_native"])
                    self.assertEqual(
                        Path(preview["native_start_script"]),
                        LAUNCH_DIR / "gamepad_native_background_start.ps1",
                    )

        self.assertEqual(state_path.exists(), state_existed_before)

    def test_native_config_resolves_an_existing_480x384_engine(self):
        with (PROJECT_ROOT / "config.native.example.toml").open("rb") as stream:
            config = tomllib.load(stream)

        model_path = PROJECT_ROOT / config["runtime"]["vision"]["model_path"]
        self.assertEqual(
            model_path, PROJECT_ROOT / "models" / "best_480x384.engine"
        )
        self.assertTrue(model_path.is_file())

    def test_product_and_example_configs_keep_idle_vision_at_60_hz(self):
        for config_name in ("config.toml", "config.native.example.toml"):
            with self.subTest(config_name=config_name):
                with (PROJECT_ROOT / config_name).open("rb") as stream:
                    config = tomllib.load(stream)
                self.assertEqual(
                    config["runtime"]["vision"]["idle_capture_fps"], 60
                )

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

    def test_gamepad_start_defaults_to_native_cpp_and_keeps_python_fallback(self):
        content = (LAUNCH_DIR / "gamepad_start.bat").read_text(encoding="utf-8")

        self.assertFalse((PROJECT_ROOT / "gamepad_start.bat").exists())
        self.assertNotIn(".venv\\Scripts\\python.exe", content)
        self.assertIn("GAMEPAD_RUNTIME", content)
        self.assertIn("gamepad_native_cpp_start.bat", content)
        self.assertIn("Native C++ gamepad runtime", content)
        self.assertIn("Python fallback", content)
        self.assertIn("py -3.11", content)
        self.assertIn("config.toml", content)
        self.assertIn("AUTO_FIRE_ARG", content)
        self.assertIn("main.py --controller-mode gamepad !AUTO_FIRE_ARG!", content)
        self.assertIn("tools\\recoil_runtime_launcher.py", content)
        self.assertNotIn('set "VISION_PERF_LOG=1"', content)
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

    def test_gamepad_start_print_only_can_resolve_python_fallback(self):
        command = (
            'set GAMEPAD_RUNTIME=python&& '
            'set GAMEPAD_START_PRINT_ONLY=1&& '
            'set GAMEPAD_START_FIRE_CHOICE_OVERRIDE=1&& '
            'set GAMEPAD_START_RECOIL_CHOICE_OVERRIDE=2&& '
            'scripts\\launch\\gamepad_start.bat'
        )

        completed = subprocess.run(
            ["cmd", "/c", command],
            cwd=PROJECT_ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )

        self.assertEqual(completed.returncode, 0, completed.stdout)
        self.assertIn("Launching Python fallback gamepad runtime.", completed.stdout)
        self.assertIn("Resolved command:", completed.stdout)
        self.assertIn("main.py --controller-mode gamepad --auto-fire-output RB", completed.stdout)

    def test_gamepad_start_print_only_resolves_default_native_runtime(self):
        command = (
            'set GAMEPAD_START_PRINT_ONLY=1&& '
            'set GAMEPAD_START_FIRE_CHOICE_OVERRIDE=2&& '
            'set GAMEPAD_START_RECOIL_CHOICE_OVERRIDE=1&& '
            'scripts\\launch\\gamepad_start.bat'
        )

        completed = subprocess.run(
            ["cmd", "/c", command],
            cwd=PROJECT_ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )

        self.assertEqual(completed.returncode, 0, completed.stdout)
        self.assertIn("Launching Native C++ gamepad runtime.", completed.stdout)
        self.assertIn("cod_native_runtime.exe", completed.stdout)
        self.assertIn("--config config.toml", completed.stdout)
        self.assertIn("--perf-log", completed.stdout)
        self.assertIn("--auto-fire-output RT", completed.stdout)

    def test_native_gamepad_runtime_has_no_keyboard_termination_shortcut(self):
        runtime_loop = (
            PROJECT_ROOT / "native" / "runtime_app" / "runtime_loop.cpp"
        ).read_text(encoding="utf-8")
        runtime_main = (
            PROJECT_ROOT / "native" / "runtime_app" / "main.cpp"
        ).read_text(encoding="utf-8")
        native_launcher = (
            LAUNCH_DIR / "gamepad_native_cpp_start.bat"
        ).read_text(encoding="utf-8")

        self.assertNotIn("GetAsyncKeyState", runtime_loop)
        self.assertNotIn("virtual_key_from_quit_key", runtime_loop)
        self.assertNotIn("CTRL_C_EVENT", runtime_main)
        self.assertNotIn("CTRL_BREAK_EVENT", runtime_main)
        self.assertIn("CTRL_CLOSE_EVENT", runtime_main)
        self.assertNotIn("VISION_QUIT_KEY", native_launcher)

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
