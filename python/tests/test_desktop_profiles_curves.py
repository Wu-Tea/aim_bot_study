import json
from pathlib import Path
import subprocess
import tempfile
import tkinter as tk
import tomllib
import unittest
from unittest.mock import patch

from desktop_app.curves import CurveLibrary, decode_points, encode_points, read_curve, seed_points, validate_points
from desktop_app.profiles import ProfileLibrary
from desktop_app.settings import ConfigStore, update_text
from desktop_app.gui import AssistantWindow
from desktop_app.runtime import RuntimeManager

PROJECT = Path(__file__).resolve().parents[2]
PRODUCTION = PROJECT / 'native/build/Release/cod_native_runtime.exe'
BASE = '[runtime]\nprofile="balanced"\n[runtime.vision]\nmodel_path="missing.engine"\n[games.apex.gamepad.aim_response_curve]\nalgorithm="linear"\n'


class ProfileCurveStorageTests(unittest.TestCase):
    def test_profile_snapshot_with_windows_newlines_remains_valid_toml(self):
        with tempfile.TemporaryDirectory() as directory:
            source=BASE.replace('\n','\r\n')
            profile=ProfileLibrary(directory).create('Windows 配置','apex',source)
            raw=profile['path'].read_bytes()
            self.assertNotIn(b'\r\r\n',raw)
            self.assertEqual(tomllib.loads(raw.decode())['runtime']['game'],'apex')
    def test_profiles_are_independent_named_snapshots_and_rename_keeps_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root/'config.toml').write_text(BASE, encoding='utf-8')
            library = ProfileLibrary(root)
            first = library.create('Apex 训练', 'apex', BASE)
            second = library.create('Apex 比赛', 'apex', BASE)
            store = ConfigStore(root, first['path'])
            text, _, digest = store.read()
            store.save(update_text(text, {'games.apex.gamepad.ads.strength_scale': .75}), digest)
            self.assertNotIn('strength_scale', second['path'].read_text(encoding='utf-8'))
            self.assertEqual((root/'config.toml').read_text(encoding='utf-8'), BASE)
            library.rename(first, 'Apex 日常')
            entry = next(e for e in library.entries(tomllib.loads(BASE), {}) if e['id']==first['id'])
            self.assertEqual(entry['name'], 'Apex 日常')
            self.assertEqual(entry['path'], first['path'])

    def test_failed_profile_validation_is_not_published(self):
        with tempfile.TemporaryDirectory() as directory:
            library=ProfileLibrary(directory)
            def reject(_):
                raise ValueError('NATIVE_REJECT')
            with self.assertRaisesRegex(ValueError, 'NATIVE_REJECT'):
                library.create('拒绝的配置','apex',BASE,reject)
            self.assertEqual(list(library.folder.glob('*')), [])

    def test_curve_format_roundtrip_rejects_noninvertible_and_unknown_versions(self):
        with tempfile.TemporaryDirectory() as directory:
            library=CurveLibrary(directory)
            points=[[0,0],[.2,.1],[.5,.35],[1,1]]
            path=library.create('自定义响应',points)
            data=read_curve(path)
            self.assertEqual(data['schema_version'],1)
            self.assertEqual(decode_points(encode_points(data['points'])),points)
            for points in ([[0,0],[1,.9]], [[0,0],[.5,.8],[.7,.4],[1,1]], [[0,0],[.5,.2],[.5,.8],[1,1]], [[0,0],[float('nan'),.5],[1,1]], [[0,False],[1,1]]):
                with self.assertRaises(ValueError):
                    validate_points(points)
            data['schema_version']=2
            path.write_text(json.dumps(data),encoding='utf-8')
            with self.assertRaisesRegex(ValueError,'版本'):
                read_curve(path)

    def test_saved_custom_curve_is_consumed_by_production_native(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            points=seed_points('cod_dynamic_legacy_lut',PROJECT)
            candidate=update_text(BASE,{'games.apex.gamepad.aim_response_curve.algorithm':'custom_lut',
                'games.apex.gamepad.aim_response_curve.custom_points':encode_points(points)})
            path=root/'curve.toml'
            path.write_text(candidate,encoding='utf-8')
            result=subprocess.run([str(PRODUCTION),'--config',str(path),'--game','apex','--dump-effective-config'],capture_output=True,timeout=10,creationflags=0x08000000)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn('gamepad.aim_response_curve.algorithm=custom_lut source=game:apex',result.stdout.decode())

    def test_reload_reads_active_profile_file(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            (root/'config.toml').write_text('INVALID TOML',encoding='utf-8')
            profile=ProfileLibrary(root).create('独立配置','apex',BASE)
            manager=RuntimeManager(root)
            record={'game':'apex','config_path':str(profile['path'])}
            with patch.object(manager,'active',return_value=record),patch('desktop_app.control.reload_config',return_value={'status':2}) as reload:
                self.assertEqual(manager.reload_config()['status'],2)
            reload.assert_called_once_with(record)


class ProfileCurveWidgetTests(unittest.TestCase):
    def test_rebuilding_curve_presets_leaves_only_live_controls_for_busy_state(self):
        with patch('desktop_app.gui.simpledialog.askstring',return_value='我的曲线'):
            self.app.save_curve_preset()
        self.assertTrue(all(widget.winfo_exists() for widget in self.app.inputs))
        self.app.run_job('校验中', lambda: None)
        _, _, error=self.app.jobs.get(timeout=3)
        self.assertIsNone(error)
        self.assertIn('disabled',self.app.curve_editor.state())

    def test_editing_legacy_template_saves_a_new_profile_without_overwriting_original(self):
        self.app.variables['gamepad.ads.strength_scale'].set('0.75')
        with patch.object(self.app.manager,'validate'):
            self.app.save()
            done, result, error=self.app.jobs.get(timeout=3)
        self.assertIsNone(error)
        self.app.busy=False
        done(result)
        self.assertFalse(self.app.profile.get('legacy',False))
        self.assertNotEqual(self.app.store.path,self.project/'config.toml')
        self.assertEqual((self.project/'config.toml').read_text(encoding='utf-8'),BASE)
        self.assertEqual(self.app.store.read()[1]['gamepad']['ads']['strength_scale'],.75)

    @classmethod
    def setUpClass(cls):
        cls.host=tk.Tk()
        cls.host.withdraw()

    @classmethod
    def tearDownClass(cls):
        cls.host.destroy()
        import gc
        gc.collect()

    def setUp(self):
        self.folder=tempfile.TemporaryDirectory()
        self.project=Path(self.folder.name)
        (self.project/'config.toml').write_text(BASE,encoding='utf-8')
        self.root=tk.Toplevel(self.host)
        self.root.withdraw()
        self.app=AssistantWindow(self.root,self.project)

    def tearDown(self):
        self.root.after_cancel(self.app.poll_id)
        self.root.destroy()
        self.app=None
        import gc
        gc.collect()
        self.folder.cleanup()

    def test_drag_creates_custom_curve_draft_and_preserves_fixed_endpoints(self):
        self.app.curve_editor.move_point(5,.52,.48)
        self.assertEqual(self.app.curve_algorithm_variable().get(),'custom_lut')
        points=decode_points(self.app.curve_points_variable().get())
        self.assertEqual(points[5],[.52,.48])
        self.app.curve_editor.move_point(0,.2,.3)
        self.app.curve_editor.move_point(len(points)-1,.8,.7)
        self.assertEqual(self.app.curve_editor.points[0],[0.,0.])
        self.assertEqual(self.app.curve_editor.points[-1],[1.,1.])
        with patch.object(self.app.manager,'validate'):
            self.app.commit(self.app.collect_changes())
        data=self.app.store.read()[1]
        self.assertEqual(data['gamepad']['aim_response_curve']['algorithm'],'custom_lut')
        self.assertEqual(decode_points(data['gamepad']['aim_response_curve']['custom_points']),points)

    def test_profile_switch_preserves_separate_drafts_and_runtime_ownership(self):
        first=self.app.profile_library.create('第一份','apex',BASE)
        second=self.app.profile_library.create('第二份','apex',BASE)
        self.app.refresh_profiles()
        self.app.select_profile(first['id'])
        path='games.apex.gamepad.ads.strength_scale'
        self.app.variables[path].set('0.75')
        self.app.select_profile(second['id'])
        self.assertNotIn(path,self.app.collect_changes())
        self.assertFalse(self.app.owns_active_config({'game':'apex','config_path':str(first['path'])}))
        self.app.select_profile(first['id'])
        self.assertEqual(self.app.variables[path].get(),'0.75')
        with patch.object(self.app.manager,'validate'):
            self.app.commit(self.app.collect_changes())
        self.assertNotIn('strength_scale',second['path'].read_text(encoding='utf-8'))

    def test_curve_crossing_drag_is_constrained_and_disabled_editor_cannot_edit(self):
        editor=self.app.curve_editor
        editor.move_point(5,.9,.1)
        validate_points(editor.points)
        self.assertLess(editor.points[5][0],editor.points[6][0])
        self.assertGreater(editor.points[5][1],editor.points[4][1])
        before=[point[:] for point in editor.points]
        editor.state(['disabled'])
        editor.move_point(5,.5,.5)
        self.assertEqual(editor.points,before)
