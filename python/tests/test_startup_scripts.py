from pathlib import Path
import json
import math
import os
import subprocess
import tempfile
import tomllib
import unittest


PROJECT_ROOT = Path(__file__).resolve().parents[2]
LAUNCH_DIR = PROJECT_ROOT / "scripts" / "launch"
DEBUG_LAUNCH_DIR = LAUNCH_DIR / "debug"


class StartupScriptTests(unittest.TestCase):
    def test_disabled_gamepad_output_does_not_initialize_virtual_device(self):
        executable = PROJECT_ROOT / "native/build/Release/cod_native_runtime.exe"
        if not executable.exists():
            self.skipTest("native runtime has not been built")
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / "config.toml"
            config.write_text(
                '[runtime.output]\nenabled=false\n'
                '[runtime.vision]\nmodel_path="missing-owner-probe.engine"\n',
                encoding="utf-8",
            )
            result = subprocess.run(
                [str(executable), "--config", str(config), "--max-ticks", "1"],
                cwd=PROJECT_ROOT,
                env={**os.environ, "GAMEPAD_INPUT_LOG": "1"},
                capture_output=True, text=True, timeout=25,
            )
        self.assertNotEqual(result.returncode, 0)  # Missing model still fails.
        self.assertNotIn("[NativeRuntime] ViGEm", result.stdout + result.stderr)







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
                        PROJECT_ROOT / "native" / "build" /
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
                        PROJECT_ROOT / "native" / "build" /
                        "Release" / "fusion_canvas.exe",
                    )
                    self.assertEqual(preview["session"], "dev")
                    self.assertTrue(preview["auto_start_native"])
                    self.assertEqual(
                        Path(preview["native_start_script"]),
                        LAUNCH_DIR / "gamepad_native_background_start.ps1",
                    )

        self.assertEqual(state_path.exists(), state_existed_before)

    def test_gui_fusion_attach_only_cannot_start_native(self):
        from desktop_app.runtime import RuntimeManager
        manager = RuntimeManager(PROJECT_ROOT)
        if manager.active() or manager.fusion_state():
            self.skipTest('requires no existing native/canvas process')
        script = LAUNCH_DIR / 'gamepad_fusion_background_start.ps1'
        arguments = ['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(script), '-AttachOnly']
        preview = subprocess.run(arguments + ['-PrintOnly'], cwd=PROJECT_ROOT, capture_output=True, text=True, timeout=10)
        self.assertEqual(preview.returncode, 0, preview.stderr)
        self.assertFalse(json.loads(preview.stdout)['auto_start_native'])
        actual = subprocess.run(arguments, cwd=PROJECT_ROOT, capture_output=True, text=True, timeout=20)
        self.assertNotEqual(actual.returncode, 0)
        self.assertIsNone(manager.active())
        self.assertIsNone(manager.fusion_state())
        log = (PROJECT_ROOT/'runs/fusion_canvas/background/launcher.log').read_text(encoding='utf-8-sig')
        self.assertIn('attach-only Fusion launch does not start it',log.splitlines()[-1])

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





    @unittest.skipUnless(os.name == 'nt', 'Native mouse config integration')
    def test_mouse_config_defaults_file_and_cli_precedence_without_devices(self):
        runtime = PROJECT_ROOT / 'native/build/Release/cod_native_mouse_runtime.exe'
        if not runtime.exists():
            self.skipTest('Native mouse runtime has not been built')
        with tempfile.TemporaryDirectory(prefix='cod_mouse_config_') as directory:
            config = Path(directory) / 'mouse.toml'
            config.write_text('[runtime]\nprofile="balanced"\n', encoding='utf-8')

            def inspect(*args):
                result = subprocess.run([str(runtime), '--config', str(config), '--check-config', *args],
                    cwd=PROJECT_ROOT, capture_output=True, timeout=10)
                output = (result.stdout + result.stderr).decode('utf-8', errors='replace')
                self.assertEqual(result.returncode, 0, output)
                self.assertIn('no device or Vision startup', output)
                return output

            output = inspect()
            self.assertIn('speed=2 breakaway=4 bodylock_deadzone=0.5', output)
            self.assertIn('speed_source=default', output)
            contents = '[mouse]\nspeed=2.5\nbreakaway=6\nbodylock_deadzone=0.7\n'
            config.write_text(contents, encoding='utf-8')
            output = inspect('--profile', 'low_latency')
            self.assertIn('speed=2.5 breakaway=6 bodylock_deadzone=0.7', output)
            self.assertIn('speed_source=user', output)
            output = inspect('--mouse-speed', '1.25', '--mouse-bodylock-deadzone', '0')
            self.assertIn('speed=1.25 breakaway=6 bodylock_deadzone=0', output)
            self.assertIn('speed_source=cli deadzone_source=cli', output)
            self.assertEqual(config.read_text(encoding='utf-8'), contents)

    @unittest.skipUnless(os.name == 'nt', 'Native mouse config integration')
    def test_mouse_config_rejects_invalid_input_before_device_startup(self):
        runtime = PROJECT_ROOT / 'native/build/Release/cod_native_mouse_runtime.exe'
        if not runtime.exists():
            self.skipTest('Native mouse runtime has not been built')
        with tempfile.TemporaryDirectory(prefix='cod_mouse_config_invalid_') as directory:
            config = Path(directory) / 'mouse.toml'
            for values in ('speed=nan', 'speed=2junk', 'speed=4', 'speed=2\nbreakaway=1',
                           'bodylock_deadzone=-0.1', 'bodylock_deadzone=1', 'bodylock_deadzone="0.5"'):
                with self.subTest(values=values):
                    config.write_text('[mouse]\n' + values + '\n', encoding='utf-8')
                    result = subprocess.run([str(runtime), '--config', str(config), '--check-config'],
                        cwd=PROJECT_ROOT, capture_output=True, timeout=10)
                    output = (result.stdout + result.stderr).decode('utf-8', errors='replace')
                    self.assertNotEqual(result.returncode, 0, output)
                    self.assertIn('mouse.', output)
                    self.assertNotIn('[MouseSupervisor]', output)

    @unittest.skipUnless(os.name == 'nt', 'Native mouse response configuration')
    def test_mouse_response_config_reaches_conversion_and_cli_overrides(self):
        runtime = PROJECT_ROOT / 'native/build/Release/cod_native_mouse_runtime.exe'
        if not runtime.exists():
            self.skipTest('Native mouse runtime has not been built')
        with tempfile.TemporaryDirectory(prefix='cod_mouse_response_') as directory:
            config = Path(directory) / 'mouse.toml'
            contents = '[mouse]\ndpi=800\nsensitivity=6.5\nfov=90\nads_multiplier=0.75\n'
            config.write_text(contents, encoding='utf-8')

            def inspect(*args):
                result = subprocess.run([str(runtime), '--config', str(config), '--check-config', *args],
                    cwd=PROJECT_ROOT, capture_output=True, timeout=10)
                output = (result.stdout + result.stderr).decode('utf-8', errors='replace')
                self.assertEqual(result.returncode, 0, output)
                self.assertIn('no device or Vision startup', output)
                values = dict(part.split('=', 1) for part in output.split() if '=' in part)
                for key in ('dpi', 'sensitivity', 'fov', 'ads_multiplier',
                            'hipfire_px_per_count', 'ads_px_per_count', 'cm_per_360'):
                    self.assertIn(key, values, output)
                return values

            values = inspect()
            self.assertEqual([float(values[k]) for k in ('dpi', 'sensitivity', 'fov', 'ads_multiplier')],
                             [800, 6.5, 90, 0.75])
            self.assertEqual(values['dpi_source'], 'user')
            expected = 1080 * (16 / 9) / (2 * math.tan(math.radians(90) / 2)) * 0.0066 * 6.5 * math.pi / 180
            self.assertAlmostEqual(float(values['hipfire_px_per_count']), expected, places=6)
            self.assertAlmostEqual(float(values['ads_px_per_count']), expected * 0.75, places=6)
            self.assertAlmostEqual(float(values['cm_per_360']), 360 * 2.54 / (800 * 6.5 * 0.0066), places=5)
            dpi_only = inspect('--mouse-dpi', '1600')
            self.assertEqual(dpi_only['dpi_source'], 'cli')
            self.assertEqual(dpi_only['sensitivity_source'], 'user')
            self.assertEqual(dpi_only['hipfire_px_per_count'], values['hipfire_px_per_count'])
            self.assertAlmostEqual(float(dpi_only['cm_per_360']), float(values['cm_per_360']) / 2, places=5)
            all_cli = inspect('--mouse-dpi', '1200', '--mouse-sensitivity', '5', '--mouse-fov', '104',
                              '--mouse-ads-multiplier', '1.5')
            self.assertEqual([float(all_cli[k]) for k in ('dpi', 'sensitivity', 'fov', 'ads_multiplier')],
                             [1200, 5, 104, 1.5])
            self.assertTrue(all(all_cli[k + '_source'] == 'cli'
                                for k in ('dpi', 'sensitivity', 'fov', 'ads_multiplier')))
            self.assertEqual(config.read_text(encoding='utf-8'), contents)
            config.write_text('[mouse]\nspeed=2\n', encoding='utf-8')
            defaults = inspect()
            self.assertEqual([float(defaults[k]) for k in ('dpi', 'sensitivity', 'fov', 'ads_multiplier')],
                             [1200, 5, 104, 1])
            self.assertEqual(defaults['fov_source'], 'default')

    @unittest.skipUnless(os.name == 'nt', 'Native mouse response configuration')
    def test_mouse_response_config_rejects_invalid_values_before_devices(self):
        runtime = PROJECT_ROOT / 'native/build/Release/cod_native_mouse_runtime.exe'
        if not runtime.exists():
            self.skipTest('Native mouse runtime has not been built')
        with tempfile.TemporaryDirectory(prefix='cod_mouse_response_invalid_') as directory:
            config = Path(directory) / 'mouse.toml'
            for value in ('dpi=0', 'dpi=nan', 'sensitivity=-1', 'sensitivity=5junk',
                          'fov=0', 'fov=180', 'ads_multiplier=0', 'ads_multiplier=inf'):
                with self.subTest(value=value):
                    config.write_text('[mouse]\n' + value + '\n', encoding='utf-8')
                    result = subprocess.run([str(runtime), '--config', str(config), '--check-config'],
                        cwd=PROJECT_ROOT, capture_output=True, timeout=10)
                    output = (result.stdout + result.stderr).decode('utf-8', errors='replace')
                    self.assertNotEqual(result.returncode, 0, output)
                    self.assertIn('mouse.', output)
                    self.assertNotIn('[MouseSupervisor]', output)

    @unittest.skipUnless(os.name == 'nt', 'Windows launcher integration')
    def test_mouse_default_command_resolves_virtual_hid_without_starting_input(self):
        environment = dict(os.environ, MOUSE_START_PRINT_ONLY='1', MOUSE_START_NO_PAUSE='1')
        environment.pop('MOUSE_RUNTIME_TRANSPORT', None)
        for launcher in (LAUNCH_DIR / 'mouse_start.bat', DEBUG_LAUNCH_DIR / 'mouse_native_debug.bat'):
            with self.subTest(launcher=launcher.name):
                result = subprocess.run(
                    ['cmd.exe', '/d', '/c', str(launcher), '--relay-test', 'block', '--duration-seconds', '1'],
                    cwd=PROJECT_ROOT, env=environment, capture_output=True, timeout=15,
                )
                output = (result.stdout + result.stderr).decode('utf-8', errors='replace')
                self.assertEqual(result.returncode, 0, output)
                commands = [line for line in output.splitlines() if line.startswith('Resolved command:')]
                self.assertEqual(len(commands), 1, output)
                self.assertIn('--transport virtual-hid', commands[0])
                self.assertIn('--relay-test block --duration-seconds 1', commands[0])

    @unittest.skipUnless(os.name == 'nt', 'Native mouse diagnostics and recoil configuration')
    def test_mouse_recoil_logging_config(self):
        runtime = PROJECT_ROOT / 'native/build/Release/cod_native_mouse_runtime.exe'
        if not runtime.exists():
            self.skipTest('Native mouse runtime has not been built')
        with tempfile.TemporaryDirectory(prefix='mouse_recoil_config_') as directory:
            config = Path(directory) / 'mouse.toml'
            cases = [('', 0, 'recoil enabled=1 counts_per_second=30 require_ads=1'),
                     ('recoil_enabled=false\nrecoil_counts_per_second=42.5\nrecoil_require_ads=false\n'
                      'log_enabled=false\nlog_directory="test logs"\nlog_max_mb=64', 0,
                      'logging enabled=0 directory=test logs max_mb=64')]
            cases += [(value, 1, 'mouse.') for value in (
                'recoil_enabled=1', 'recoil_require_ads="false"', 'recoil_counts_per_second=-1',
                'recoil_counts_per_second=nan', 'recoil_counts_per_second=10001', 'log_enabled=yes',
                'log_directory=""', 'log_max_mb=0', 'log_max_mb=1.5')]
            for value, code, expected in cases:
                with self.subTest(value=value):
                    config.write_text('[mouse]\n' + value + '\n', encoding='utf-8')
                    result = subprocess.run([str(runtime), '--config', str(config), '--check-config'],
                        cwd=PROJECT_ROOT, capture_output=True, timeout=10)
                    output = (result.stdout + result.stderr).decode('utf-8', errors='replace')
                    self.assertEqual(result.returncode, code, output)
                    self.assertIn(expected, output)
                    self.assertNotIn('[MouseSupervisor]', output)

    @unittest.skipUnless(os.name == 'nt', 'Native mouse BodyLock configuration')
    def test_mouse_bodylock_range_and_ramp_config(self):
        runtime = PROJECT_ROOT / 'native/build/Release/cod_native_mouse_runtime.exe'
        if not runtime.exists():
            self.skipTest('Native mouse runtime has not been built')
        with tempfile.TemporaryDirectory(prefix='mouse_bodylock_config_') as directory:
            config = Path(directory) / 'mouse.toml'
            cases = [('', 0, 'bodylock range_px=180 accel_ms=40 decel_ms=25'),
                     ('bodylock_range_px=220\nbodylock_accel_ms=60\nbodylock_decel_ms=18', 0,
                      'bodylock range_px=220 accel_ms=60 decel_ms=18'),
                     ('bodylock_range_px=0\nbodylock_accel_ms=0\nbodylock_decel_ms=0', 0,
                      'bodylock range_px=0 accel_ms=0 decel_ms=0')]
            cases += [(value, 1, 'mouse.') for value in (
                'bodylock_range_px=nan', 'bodylock_range_px=15', 'bodylock_range_px=2049',
                'bodylock_accel_ms=0.5', 'bodylock_accel_ms=251', 'bodylock_decel_ms=-1',
                'bodylock_decel_ms=inf')]
            for value, code, expected in cases:
                with self.subTest(value=value):
                    config.write_text('[mouse]\n' + value + '\n', encoding='utf-8')
                    result = subprocess.run([str(runtime), '--config', str(config), '--check-config'],
                        cwd=PROJECT_ROOT, capture_output=True, timeout=10)
                    output = (result.stdout + result.stderr).decode('utf-8', errors='replace')
                    self.assertEqual(result.returncode, code, output)
                    self.assertIn(expected, output)
                    self.assertNotIn('[MouseSupervisor]', output)

    @unittest.skipUnless(os.name == 'nt', 'Native mouse point tolerance configuration')
    def test_mouse_bodylock_point_tolerance_config(self):
        runtime = PROJECT_ROOT / 'native/build/Release/cod_native_mouse_runtime.exe'
        if not runtime.exists():
            self.skipTest('Native mouse runtime has not been built')
        with tempfile.TemporaryDirectory(prefix='mouse_point_config_') as directory:
            config = Path(directory) / 'mouse.toml'
            cases = [('', 0, 'point_tolerance_px=3'),
                     ('bodylock_point_tolerance_px=1.5', 0, 'point_tolerance_px=1.5'),
                     ('bodylock_point_tolerance_px=0', 0, 'point_tolerance_px=0')]
            cases += [('bodylock_point_tolerance_px=' + v, 1, 'mouse.bodylock_point_tolerance_px')
                      for v in ('nan', 'inf', '-1', '16.1', '3junk')]
            for value, code, expected in cases:
                with self.subTest(value=value):
                    config.write_text('[mouse]\n' + value + '\n', encoding='utf-8')
                    result = subprocess.run([str(runtime), '--config', str(config), '--check-config'],
                        cwd=PROJECT_ROOT, capture_output=True, timeout=10)
                    output = (result.stdout + result.stderr).decode('utf-8', errors='replace')
                    self.assertEqual(result.returncode, code, output)
                    self.assertIn(expected, output)

    @unittest.skipUnless(os.name == 'nt', 'Windows launcher integration')
    def test_mouse_start_preserves_failure_and_writes_diagnostic_log(self):
        runtime = PROJECT_ROOT / 'native/build/Release/cod_native_mouse_runtime.exe'
        if not runtime.exists():
            self.skipTest('Native mouse runtime has not been built')
        environment = dict(os.environ, MOUSE_START_NO_PAUSE='1')
        environment.pop('MOUSE_START_PRINT_ONLY', None)
        # Invalid CLI fails before device registration, capture, or Vision startup.
        result = subprocess.run(
            ['cmd.exe', '/d', '/c', str(LAUNCH_DIR / 'mouse_start.bat'),
             '--mouse-startup-regression-invalid'],
            cwd=PROJECT_ROOT, env=environment, capture_output=True, timeout=20,
        )
        output = (result.stdout + result.stderr).decode('utf-8', errors='replace')
        self.assertEqual(result.returncode, 1, output)
        logs = [line[len('Mouse startup log: '):].strip() for line in output.splitlines()
                if line.startswith('Mouse startup log: ')]
        self.assertEqual(len(logs), 1, output)
        log_path = Path(logs[0])
        self.assertTrue(log_path.is_relative_to(PROJECT_ROOT / 'runs/mouse_startup'))
        log = log_path.read_text(encoding='utf-8-sig')
        self.assertIn('--mouse-startup-regression-invalid', log)
        self.assertIn('exit_code=1', log)




if __name__ == "__main__":
    unittest.main()
