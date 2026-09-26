"""Check profile isolation without starting a controller or sending input."""
import json
from pathlib import Path
import subprocess
import shutil
import tempfile
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[1]
LAUNCH = ROOT / 'scripts/launch'


def powershell(*args):
    return subprocess.check_output(
        ['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', *args],
        cwd=ROOT, text=True, encoding='utf-8', errors='strict')


class ApexLauncherTests(unittest.TestCase):
    def test_windows_powershell_launch_preserves_utf8_config(self):
        # Exercise the real VBS host and start script, stopping at process spawn.
        # CP936 can consume the LF after this UTF-8 comment into a multibyte
        # character, commenting out capture_width and restoring its default.
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            launch = root / 'scripts/launch'
            launch.mkdir(parents=True)
            for name in ('gamepad_native_background_start.ps1', 'apex_config.ps1'):
                shutil.copyfile(LAUNCH / name, launch / name)
            config = (
                '# 中文配置\n[runtime.vision]\n# ★ 综合性价比\n'
                'capture_width = 640\ncapture_height = 512\n'
                'tensor_width = 480\ntensor_height = 384\n'
                'require_isotropic_resize = true\nmodel_path = "original.engine"\n'
                '[gamepad.aim_response_curve]\nalgorithm = "cod_dynamic_legacy_lut"\n'
            )
            (root / 'config.toml').write_bytes(config.encode('utf-8'))
            for relative in (
                'native/vision_native/build/Release/cod_native_runtime.exe',
                'artifacts/game-specialists-20260914/exports/apex/apex_480x384.engine',
            ):
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()
            script = str(launch / 'gamepad_native_background_start.ps1').replace("'", "''")
            result = subprocess.run([
                'powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass',
                '-Command', "function Get-CimInstance { return $null }; "
                "function Start-Process { throw 'TEST_PROCESS_BOUNDARY' }; "
                f"& '{script}' -Game apex",
            ], capture_output=True)
            self.assertEqual(result.returncode, 1)
            output = root / 'runs/runtime/background/apex'
            self.assertIn('TEST_PROCESS_BOUNDARY', (output / 'launcher.log').read_text(encoding='utf-8-sig'))
            expected = tomllib.loads(config)
            expected['runtime']['vision']['model_path'] = (
                'artifacts/game-specialists-20260914/exports/apex/apex_480x384.engine')
            expected['gamepad']['aim_response_curve']['algorithm'] = 'linear'
            generated = (output / 'config.apex.toml').read_text(encoding='utf-8-sig')
            self.assertEqual(tomllib.loads(generated), expected)
            self.assertIn('# ★ 综合性价比\ncapture_width = 640', generated)
            self.assertEqual((root / 'config.toml').read_bytes(), config.encode('utf-8'))

    def test_only_model_and_curve_change(self):
        # Use the tracked example so the test does not depend on local settings.
        source = ROOT / 'config.native.example.toml'
        before = source.read_bytes()
        result = powershell('-Command',
            ". ./scripts/launch/apex_config.ps1; "
            "$text = Get-Content -LiteralPath config.native.example.toml -Raw -Encoding UTF8; "
            "ConvertTo-ApexConfigText $text | ConvertTo-Json -Compress")
        actual = tomllib.loads(json.loads(result))
        expected = tomllib.loads(before.decode('utf-8-sig'))
        expected['runtime']['vision']['model_path'] = (
            'artifacts/game-specialists-20260914/exports/apex/apex_480x384.engine')
        expected['gamepad']['aim_response_curve']['algorithm'] = 'linear'
        self.assertEqual(actual, expected)
        self.assertEqual(source.read_bytes(), before)

    def test_missing_settings_are_rejected(self):
        result = powershell('-Command',
            ". ./scripts/launch/apex_config.ps1; "
            "try { ConvertTo-ApexConfigText '[runtime.vision]'; exit 9 } "
            "catch { Write-Output 'rejected' }")
        self.assertEqual(result.strip(), 'rejected')

    def test_start_stop_share_isolated_state(self):
        def preview(action, game):
            return json.loads(powershell('-File', str(LAUNCH / (
                'gamepad_native_background_' + action + '.ps1')),
                '-Game', game, '-PrintOnly'))
        apex = preview('start', 'apex')
        normal = preview('start', 'default')
        self.assertEqual(apex['state_path'], preview('stop', 'apex')['state_path'])
        self.assertEqual(normal['state_path'], preview('stop', 'default')['state_path'])
        self.assertNotEqual(apex['state_path'], normal['state_path'])
        self.assertNotEqual(apex['config_path'], normal['config_path'])
        self.assertEqual(Path(apex['model_path']).name, 'apex_480x384.engine')
        self.assertEqual(Path(normal['config_path']), ROOT / 'config.toml')


if __name__ == '__main__':
    unittest.main()
