"""Verify BO3 config isolation and duplicate-instance protection."""
from pathlib import Path
import tempfile
import tomllib
import unittest
from unittest.mock import patch

from desktop_app.settings import effective
from desktop_app.runtime import RuntimeManager

ROOT = Path(__file__).resolve().parents[2]


class Bo3LauncherTests(unittest.TestCase):
    def test_bo3_override_is_isolated_from_other_games(self):
        data = tomllib.loads((ROOT / 'config.native.example.toml').read_text(encoding='utf-8-sig'))
        base = effective(data, 'default')
        expected = effective(data, 'default')
        expected['gamepad']['output_transfer'] = {'enabled': True, 'axial': False, 'deadzone': .16, 'game_exponent': 1.0}
        expected['gamepad']['aim_response_curve']['algorithm'] = 'linear'
        self.assertEqual(effective(data, 'bo3'), expected)
        self.assertEqual(effective(data, 'default'), base)
        self.assertNotIn('output_transfer', effective(data, 'apex')['gamepad'])
        with self.assertRaises(ValueError):
            effective(data, 'unknown')

    def test_active_game_blocks_second_process_before_spawn(self):
        with tempfile.TemporaryDirectory() as folder:
            manager = RuntimeManager(folder)
            with patch.object(manager, 'active', return_value={'game': 'apex'}), \
                 patch('desktop_app.runtime.subprocess.Popen') as spawn:
                with self.assertRaisesRegex(ValueError, 'apex'):
                    manager.start('bo3', {})
                spawn.assert_not_called()

    def test_unrecorded_process_blocks_second_process_before_spawn(self):
        with tempfile.TemporaryDirectory() as folder:
            manager = RuntimeManager(folder)
            with patch('desktop_app.runtime.matching_processes', return_value=[4321]), \
                 patch('desktop_app.runtime.subprocess.Popen') as spawn:
                with self.assertRaisesRegex(ValueError, '其他入口'):
                    manager.start('bo3', {})
                spawn.assert_not_called()

    def test_launcher_does_not_generate_configs_or_register_other_executable_paths(self):
        for action in ('start', 'stop'):
            source = (ROOT / f'scripts/launch/gamepad_native_background_{action}.ps1').read_text(encoding='utf-8-sig')
            self.assertIn('-m desktop_app.gui', source)
            self.assertNotIn('ConvertTo-', source)
            self.assertNotIn('Test-NativeGamepadInput', source)
            self.assertNotIn('Ensure-HidHideAccess', source)
            self.assertNotIn('artifacts\\runtime\\bo3', source)


if __name__ == '__main__':
    unittest.main()
