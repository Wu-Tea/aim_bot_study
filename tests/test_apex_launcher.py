"""Check profile isolation without starting a controller or sending input."""
import json
from pathlib import Path
import subprocess
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[1]
LAUNCH = ROOT / 'scripts/launch'


def powershell(*args):
    return subprocess.check_output(
        ['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', *args],
        cwd=ROOT, text=True, encoding='utf-8', errors='strict')


class ApexLauncherTests(unittest.TestCase):
    def test_only_model_and_curve_change(self):
        # Use the tracked example so the test does not depend on local settings.
        source = ROOT / 'config.native.example.toml'
        before = source.read_bytes()
        result = powershell('-Command',
            ". ./scripts/launch/apex_config.ps1; "
            "$text = Get-Content -LiteralPath config.native.example.toml -Raw; "
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
