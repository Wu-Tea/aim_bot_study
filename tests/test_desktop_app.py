import json
from pathlib import Path
import subprocess
import tempfile
import tkinter as tk
import tomllib
import unittest
from unittest.mock import Mock, patch

from desktop_app.settings import ConfigStore, effective, update_text
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


class DesktopWidgetTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.project = Path(self.directory.name)
        (self.project / 'config.toml').write_text(CONFIG, encoding='utf-8')
        self.root = tk.Tk()
        self.root.withdraw()
        self.app = AssistantWindow(self.root, self.project)

    def tearDown(self):
        self.root.after_cancel(self.app.poll_id)
        self.root.destroy()
        self.directory.cleanup()

    def test_start_runtime_does_not_auto_start_fusion(self):
        # A persisted preference from the old GUI must not couple launches.
        if hasattr(self.app, 'fusion_requested'):
            self.app.fusion_requested.set(True)
        with patch.object(self.app.manager, 'active', return_value=None), patch.object(self.app.manager, 'start', return_value={'process_id':123}) as start, patch.object(self.app.manager, 'set_fusion') as fusion:
            self.app.primary_action()
            import time
            deadline = time.monotonic() + 3
            while self.app.jobs.empty() and time.monotonic() < deadline:
                time.sleep(.01)
            self.assertFalse(self.app.jobs.empty())
            start.assert_called_once()
            fusion.assert_not_called()

    def test_close_gui_preserves_runtime_and_fusion(self):
        with patch.object(self.app.manager, 'stop') as stop, patch.object(self.app.manager, 'set_fusion') as fusion, patch.object(self.root, 'destroy'):
            self.app.close()
        stop.assert_not_called()
        fusion.assert_not_called()
        # close() cancelled polling; teardown should still own its timer.
        self.app.poll_id = self.root.after(10000, lambda: None)

    def test_fusion_button_closes_canvas_without_starting_or_stopping_runtime(self):
        with patch.object(self.app.manager, 'fusion_state', return_value={'process_id':321}), patch.object(self.app.manager, 'set_fusion') as fusion, patch.object(self.app.manager, 'start') as start, patch.object(self.app.manager, 'stop') as stop:
            self.app.fusion_button.invoke()
            self.app.jobs.get(timeout=3)
        fusion.assert_called_once_with(False)
        start.assert_not_called()
        stop.assert_not_called()

    def test_unsaved_game_settings_survive_switch_without_cross_game_leak(self):
        self.app.game.set('COD：Black Ops III')
        self.app.change_game()
        path = 'games.bo3.gamepad.output_transfer.deadzone'
        self.app.variables[path].set('0.24')
        self.app.game.set('Apex Legends')
        self.app.change_game()
        self.assertEqual(self.app.variables['games.apex.gamepad.output_transfer.deadzone'].get(), '0.16')
        self.app.game.set('COD：Black Ops III')
        self.app.change_game()
        self.assertEqual(self.app.variables[path].get(), '0.24')
        self.assertEqual(self.app.collect_changes(), {path: .24})

    def test_form_save_creates_only_selected_override(self):
        self.app.game.set('Apex Legends')
        self.app.change_game()
        self.app.variables['games.apex.gamepad.recoil.hipfire_multiplier'].set('0.6')
        with patch.object(self.app.manager, 'validate') as validate:
            self.app.commit(self.app.collect_changes())
        validate.assert_called_once()
        after = self.app.store.read()[1]
        self.assertEqual(after['games']['apex']['gamepad']['recoil']['hipfire_multiplier'], .6)
        self.assertEqual(after['games']['bo3'], tomllib.loads(CONFIG)['games']['bo3'])
        self.assertEqual(after['gamepad']['recoil']['feedback_amount'], .2)

    def test_form_save_is_accepted_and_read_back_by_production_native(self):
        production = Path(__file__).resolve().parents[1] / 'native/vision_native/build/Release/cod_native_runtime.exe'
        self.assertTrue(production.exists())
        self.app.manager.executable = production
        self.app.game.set('COD：Black Ops III')
        self.app.change_game()
        path = 'games.bo3.gamepad.output_transfer.deadzone'
        self.app.variables[path].set('0.23')
        self.app.commit(self.app.collect_changes())
        result = subprocess.run([str(production), '--config', str(self.project/'config.toml'), '--game', 'bo3', '--dump-effective-config'],
                                capture_output=True, timeout=10, creationflags=0x08000000)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('gamepad.output_transfer.deadzone=0.23 source=game:bo3', result.stdout.decode('utf-8'))
        self.assertEqual(self.app.store.read()[1]['games']['apex'], tomllib.loads(CONFIG)['games']['apex'])

    def test_bad_numeric_input_rejects_save_before_native_call(self):
        self.app.variables['gamepad.ads.strength_scale'].set('not a number')
        with self.assertRaisesRegex(ValueError, '输入格式'):
            self.app.collect_changes()
        self.assertEqual((self.project / 'config.toml').read_text(encoding='utf-8'), CONFIG)

    def test_learning_export_preserves_raw_values_and_identifies_estimate(self):
        values = {'status': 2, 'revision': 4, 'sampled_at_ms': 123, 'manual_fire_input': 'RT', 'fire_output': 'RB',
                  'regions': [{'effective': 510, 'learned': 530, 'confidence': .5, 'samples': 22}] * 4}
        with patch.object(self.app.manager, 'learning', return_value=values), patch.object(self.app.manager, 'active', return_value={'game': 'apex'}):
            self.app.export_learning()
        path, = (self.project/'runs/desktop/learning').glob('*.json')
        data = json.loads(path.read_text(encoding='utf-8'))
        self.assertEqual(data['learning'], values)
        self.assertEqual(data['region_order'], ['body_free','body_slow','ads_free','ads_slow'])
        self.assertIn('not independent', data['measurement_kind'])

    def test_saved_restart_only_changes_are_not_reported_as_hot_applied(self):
        self.app.applied({'status':3,'revision':1,'message':'restart required: runtime.vision.model_path'})
        self.assertTrue(self.app.restart_required)
        self.assertIn('需要重启',self.app.notice.get())
        self.app.applied({'status':2,'revision':2,'message':'applied'})
        self.assertFalse(self.app.restart_required)
        self.assertIn('学习数据已清理',self.app.notice.get())

    def test_older_pending_reload_cannot_claim_new_saved_configuration_applied(self):
        self.app.applied({'status':1,'revision':1,'request_id':123})
        self.assertEqual(self.app.pending_request_id,123)
        self.app.applied({'apply_error':'已有重载；这次请求未提交'})
        self.assertIsNone(self.app.pending_request_id)
        self.assertIn('尚未应用',self.app.notice.get())


if __name__ == '__main__':
    unittest.main()
