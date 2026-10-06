import json
import gc
from pathlib import Path
import subprocess
import tempfile
import tkinter as tk
import tomllib
import unittest
from unittest.mock import Mock, patch

from desktop_app.settings import ConfigStore, UiPreferences, effective, update_text
from desktop_app.runtime import RuntimeManager, process_identity
from desktop_app.gui import AssistantWindow
from desktop_app.fields import validate_fields

CONFIG = '''# 中文注释：保留它
[runtime]
profile = "balanced"
[runtime.vision]
model_path = "models/shared#model.engine" # 模型说明
capture_fps = 200
[gamepad.ads]
strength_scale = 0.8
[gamepad.recoil]
feedback_amount = 0.20
[games.apex.runtime.vision]
model_path = "models/apex.engine"
[games.apex.gamepad.recoil]
hipfire_multiplier = 0.5
[games.bo3.gamepad.output_transfer]
enabled = true
deadzone = 0.20
'''


class DesktopConfigTests(unittest.TestCase):
    def test_truncated_native_config_cannot_supply_partial_defaults(self):
        manager=RuntimeManager(Path.cwd())
        result=Mock(returncode=0,stdout=b'runtime.vision.capture_width=640 source=user\n',stderr=b'')
        with patch('desktop_app.runtime.subprocess.run',return_value=result), self.assertRaisesRegex(ValueError,'不完整'):
            manager.inspect_defaults('default')

    def test_catalog_defaults_match_native_and_all_fields_have_metadata(self):
        from project_paths import PROJECT_ROOT
        from desktop_app.parameter_catalog import CATALOG, PARAMETERS
        self.assertEqual(len(PARAMETERS),len(CATALOG['parameters']))
        values=RuntimeManager(PROJECT_ROOT).inspect_defaults('default','[runtime]\ngame="default"\n')
        for path,parameter in PARAMETERS.items():
            with self.subTest(path=path):
                self.assertIn(path,values)
                self.assertAlmostEqual(values[path],parameter['default'],places=6)
                self.assertLessEqual(parameter['min'],parameter['default'])
                self.assertLessEqual(parameter['default'],parameter['max'])
                self.assertTrue(parameter['help'])
    def test_native_device_scan_preserves_names_and_uses_no_profile_directory(self):
        manager=RuntimeManager(Path.cwd())
        result=Mock(returncode=0,stdout=json.dumps([{'id':'path:abcd','name':'索尼 DualSense'}],ensure_ascii=False).encode('utf-8'))
        with patch('desktop_app.runtime.subprocess.run',return_value=result) as run:
            self.assertEqual(manager.input_devices()[0]['name'],'索尼 DualSense')
        self.assertEqual(run.call_args.args[0],[str(manager.executable),'--list-input-devices'])
        self.assertEqual(run.call_args.kwargs['cwd'],manager.executable.parent)
        self.assertTrue(run.call_args.kwargs['creationflags'])
        result.stdout=b'[{"id":"same","name":"A"},{"id":"same","name":"B"}]'
        with patch('desktop_app.runtime.subprocess.run',return_value=result),self.assertRaisesRegex(ValueError,'标识重复'):
            manager.input_devices()

    def test_restore_inheritance_preserves_comments_and_other_games(self):
        before = update_text(CONFIG, {'games.apex.gamepad.ads.strength_scale': .5})
        before = before.replace('strength_scale = 0.5', 'strength_scale = 0.5 # game calibration')
        after = update_text(before, {'games.apex.gamepad.ads.strength_scale': None})
        data = tomllib.loads(after)
        self.assertEqual(effective(data, 'apex')['gamepad']['ads']['strength_scale'], .8)
        self.assertEqual(data['games']['bo3'], tomllib.loads(CONFIG)['games']['bo3'])
        self.assertIn('# game calibration', after)
        self.assertEqual(update_text(after, {'games.apex.gamepad.ads.strength_scale': None}), after)

    def test_preferences_handle_invalid_json_and_preserve_other_preferences(self):
        with tempfile.TemporaryDirectory() as directory:
            preferences = UiPreferences(directory)
            preferences.path.parent.mkdir(parents=True)
            for raw in ('{', 'null', '[]', '42'):
                preferences.path.write_text(raw, encoding='utf-8')
                self.assertEqual(preferences.read(), {})
                preferences.save_game('apex')
                self.assertEqual(preferences.read()['game'], 'apex')
            preferences.path.write_text('{"game":"default","extra":true}', encoding='utf-8')
            preferences.save_game('bo3')
            self.assertEqual(preferences.read(), {'game': 'bo3', 'extra': True})
            self.assertEqual(list(preferences.path.parent.glob('.ui-*.json')), [])

    def test_region_prior_ranges_and_game_isolation(self):
        keys = ('body_free_initial_scale', 'body_slow_initial_scale',
                'ads_free_initial_scale', 'ads_slow_initial_scale')
        for key in keys:
            for value in (0, 80, 1363.69, 4000):
                validate_fields({'gamepad': {'ai_aim': {key: value}}}, ['default'])
            for value in (-1, 79, 4001, float('nan'), float('inf'), '900'):
                with self.assertRaises(ValueError):
                    validate_fields({'gamepad': {'ai_aim': {key: value}}}, ['default'])
        expected = (1363.69, 1008.82, 1656.79, 1397.20)
        updates = {f'gamepad.ai_aim.{key}': value for key, value in zip(keys, expected)}
        updates.update({f'games.{game}.gamepad.ai_aim.{key}': 0
                        for game in ('apex', 'bo3') for key in keys})
        document = tomllib.loads(update_text(CONFIG, updates))
        self.assertEqual(tuple(effective(document, 'default')['gamepad']['ai_aim'][key] for key in keys), expected)
        for game in ('apex', 'bo3'):
            self.assertEqual(tuple(effective(document, game)['gamepad']['ai_aim'][key] for key in keys), (0, 0, 0, 0))

    def test_runtime_stop_does_not_stop_fusion(self):
        with tempfile.TemporaryDirectory() as directory:
            manager = RuntimeManager(directory)
            with patch.object(manager, 'active', return_value=None), patch.object(manager, 'set_fusion') as fusion:
                manager.stop()
            fusion.assert_not_called()

    def test_pending_reload_is_not_acknowledgement_of_a_new_request(self):
        from desktop_app.control import ControlChannel
        channel = ControlChannel.__new__(ControlChannel)
        channel.read = Mock(return_value={'status': 1, 'request_id': 123})
        with self.assertRaisesRegex(ValueError, '这次请求未提交'):
            channel.reload()

    def test_advanced_editor_rejects_wrong_types_before_native_fallback(self):
        for setting in ('strength_scale="bad"', 'strength_scale=true'):
            data = tomllib.loads('[gamepad.ads]\n' + setting)
            with self.assertRaisesRegex(ValueError, '配置类型'):
                validate_fields(data, ['default'])

    def test_crashed_process_is_failed_even_without_native_error_marker(self):
        with tempfile.TemporaryDirectory() as directory:
            manager = RuntimeManager(directory)
            output = Path(directory) / 'output.log'
            output.write_text('startup began\n', encoding='utf-8')
            error = Path(directory) / 'error.log'
            error.write_text('unexpected native crash\n', encoding='utf-8')
            manager.state_path.parent.mkdir(parents=True)
            manager.state_path.write_text(json.dumps({'process_id': 123, 'stdout_path': str(output), 'stderr_path': str(error)}))
            manager.child = Mock(pid=123)
            manager.child.poll.return_value = 1
            with patch.object(manager, 'active', return_value=None):
                self.assertEqual(manager.status()['phase'], 'failed')

    def test_startup_readiness_survives_long_log(self):
        with tempfile.TemporaryDirectory() as directory:
            manager = RuntimeManager(directory)
            output = Path(directory) / 'output.log'
            output.write_text('[NativeRuntime] physical input backend=SDL joystick index=0 name="DualSense Edge"\n'
                              'virtual DualShock 4 gamepad is online\n'
                              '[NativeRuntime] initialized; entering controller loop\n' + 'later log\n' * 10000,
                              encoding='utf-8')
            record = {'process_id': 123, 'game': 'bo3', 'stdout_path': str(output),
                      'stderr_path': str(Path(directory) / 'error.log')}
            with patch.object(manager, 'active', return_value=record):
                status = manager.status()
            self.assertEqual(status['phase'], 'running')
            self.assertEqual(status['device'], 'DualSense Edge')
            self.assertTrue(status['virtual_connected'])

    def test_edit_preserves_comments_and_unrelated_game_values(self):
        changed = update_text(CONFIG, {'games.bo3.gamepad.output_transfer.deadzone': .23,
                                       'runtime.vision.model_path': 'models/new#model.engine'})
        before, after = tomllib.loads(CONFIG), tomllib.loads(changed)
        self.assertIn('# 中文注释：保留它', changed)
        self.assertIn('# 模型说明', changed)
        self.assertEqual(after['games']['apex'], before['games']['apex'])
        self.assertEqual(after['runtime']['vision']['model_path'], 'models/new#model.engine')
        self.assertEqual(after['games']['bo3']['gamepad']['output_transfer']['deadzone'], .23)
        self.assertEqual(effective(after, 'apex')['gamepad']['ads']['strength_scale'], .8)
        self.assertNotIn('output_transfer', effective(after, 'apex')['gamepad'])

    def test_save_validation_failure_does_not_touch_user_file(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            store = ConfigStore(root)
            store.path.write_text(CONFIG, encoding='utf-8')
            text, _, digest = store.read()
            candidate = update_text(text, {'gamepad.ads.strength_scale': .6})
            def reject(_):
                raise ValueError('INVALID_NATIVE_CONFIGURATION')
            with self.assertRaisesRegex(ValueError, 'INVALID_NATIVE'):
                store.save(candidate, digest, reject)
            self.assertEqual(store.path.read_text(encoding='utf-8'), CONFIG)
            self.assertEqual(list(root.glob('.config-*.toml')), [])

    def test_save_backups_and_detects_external_edits(self):
        with tempfile.TemporaryDirectory() as directory:
            store = ConfigStore(directory)
            store.path.write_text(CONFIG, encoding='utf-8')
            text, _, digest = store.read()
            store.save(update_text(text, {'gamepad.ads.strength_scale': .6}), digest)
            backup, = (Path(directory) / 'runs/desktop/config-backups').glob('*.toml')
            self.assertEqual(backup.read_text(encoding='utf-8'), CONFIG)
            with self.assertRaisesRegex(ValueError, '其他程序修改'):
                store.save(text, digest)
            self.assertEqual(store.read()[1]['gamepad']['ads']['strength_scale'], .6)

    def test_migration_preserves_user_bo3_calibration_and_is_idempotent(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            store = ConfigStore(root)
            store.path.write_text('[runtime]\nprofile="balanced"\n', encoding='utf-8')
            (root / 'config.bo3.toml').write_text('[gamepad.output_transfer]\nenabled=true\ndeadzone=0.27\n')
            self.assertTrue(store.migrate_games())
            data = store.read()[1]
            self.assertEqual(data['games']['bo3']['gamepad']['output_transfer']['deadzone'], .27)
            self.assertEqual(data['games']['apex']['gamepad']['recoil']['hipfire_multiplier'], .5)
            original = store.path.read_bytes()
            self.assertFalse(store.migrate_games())
            self.assertEqual(store.path.read_bytes(), original)

    def test_running_identity_ignores_exited_process(self):
        child = subprocess.Popen(['D:/env/python/python.exe', '-c', 'pass'], creationflags=0x08000000)
        child.wait(timeout=5)
        self.assertIsNone(process_identity(child.pid))

    def test_unknown_or_reused_pid_is_not_owned(self):
        with tempfile.TemporaryDirectory() as directory:
            manager = RuntimeManager(directory)
            manager.state_path.parent.mkdir(parents=True)
            import os
            identity = process_identity(os.getpid())
            manager.state_path.write_text(json.dumps({'process_id': os.getpid(), 'process_created': identity['created'] + 1}))
            manager.executable = Path(identity['path'])
            self.assertIsNone(manager.active())
            manager.stop()


# The independent-profile UI contracts are exercised in test_desktop_workspace.py.
