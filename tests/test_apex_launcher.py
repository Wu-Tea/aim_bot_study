"""The compatibility launchers and GUI use one config and one native process."""
import json
from pathlib import Path
import subprocess
import tempfile
import tomllib
import unittest
from unittest.mock import patch

from desktop_app.runtime import RuntimeManager
from desktop_app.settings import effective

ROOT = Path(__file__).resolve().parents[1]
LAUNCH = ROOT / 'scripts/launch'


def preview(action, game):
    output = subprocess.check_output([
        'powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
        str(LAUNCH / f'gamepad_native_background_{action}.ps1'), '-Game', game, '-PrintOnly'],
        cwd=ROOT, text=True, encoding='utf-8')
    return json.loads(output)


class ApexLauncherTests(unittest.TestCase):
    def test_all_games_share_config_executable_and_process_record(self):
        starts = [preview('start', game) for game in ('default', 'apex', 'bo3')]
        for key in ('config_path', 'executable_path', 'state_path'):
            self.assertEqual(len({item[key] for item in starts}), 1, key)
        for game, start in zip(('default', 'apex', 'bo3'), starts):
            self.assertEqual(start['state_path'], preview('stop', game)['state_path'])
            self.assertEqual(start['arguments'], ['--config', str(ROOT / 'config.toml'), '--game', game])
        self.assertEqual(Path(starts[1]['model_path']).name, 'apex_480x384.engine')
        self.assertEqual(starts[0]['model_path'], starts[2]['model_path'])

    def test_apex_sparse_block_changes_only_model_curve_and_hipfire_recoil(self):
        source = ROOT / 'config.native.example.toml'
        before = source.read_bytes()
        data = tomllib.loads(before.decode('utf-8-sig'))
        base = effective(data, 'default')
        expected = effective(data, 'default')
        expected['runtime']['vision']['model_path'] = 'artifacts/game-specialists-20260914/exports/apex/apex_480x384.engine'
        expected['gamepad']['aim_response_curve']['algorithm'] = 'linear'
        expected['gamepad']['recoil']['hipfire_multiplier'] = .5
        self.assertEqual(effective(data, 'apex'), expected)
        self.assertNotEqual(base['runtime']['vision']['model_path'], expected['runtime']['vision']['model_path'])
        self.assertEqual(source.read_bytes(), before)

    def test_spawn_reads_utf8_config_directly_without_generating_profile_file(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder).resolve()
            source = '# ★ 综合性价比\n[runtime.vision]\ncapture_width=640\nmodel_path="shared.engine"\n[games.apex.runtime.vision]\nmodel_path="apex.engine"\n'
            (root / 'config.toml').write_text(source, encoding='utf-8')
            (root / 'apex.engine').touch()
            manager = RuntimeManager(root)
            manager.executable.parent.mkdir(parents=True)
            manager.executable.touch()
            with patch('desktop_app.runtime.matching_processes', return_value=[]), \
                 patch('desktop_app.runtime.process_identity', return_value={'created': 123, 'path': str(manager.executable)}), \
                 patch('desktop_app.runtime.subprocess.Popen') as spawn:
                spawn.return_value.pid = 1234
                record = manager.start('apex', tomllib.loads(source))
            self.assertEqual(spawn.call_args.args[0], [str(manager.executable), '--config', str(root / 'config.toml'), '--game', 'apex'])
            self.assertEqual((root / 'config.toml').read_text(encoding='utf-8'), source)
            self.assertFalse(list((root / 'runs').rglob('*.toml')))
            self.assertEqual(record['game'], 'apex')
            self.assertTrue(record['fusion_channel_enabled'])


if __name__ == '__main__':
    unittest.main()
