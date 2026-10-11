"""Independent profile storage and curve transaction contracts."""
from copy import deepcopy
import json
import gc
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import threading
import tomllib
import unittest
from unittest.mock import patch

from desktop_app.curve_model import CurveModel
from desktop_app.curves import curve_document, decode_points, encode_points, validate_points
from desktop_app.runtime import RuntimeManager
from desktop_app.settings import effective, lookup
from desktop_app.workspace import ProfileRepository, projection, snapshot, toml_text

PROJECT=Path(__file__).resolve().parents[2]
PRODUCTION=PROJECT/'native/build/Release/cod_native_runtime.exe'


class IndependentStorageTests(unittest.TestCase):
    def test_free_profile_crud_and_native_projection(self):
        manager=RuntimeManager(PROJECT)
        with tempfile.TemporaryDirectory() as folder:
            repo=ProfileRepository(folder,curve_source=PROJECT)
            p=repo.create('任意训练 / 我的配置',validate=lambda path:manager.validate(path,['custom']))
            self.assertEqual(p['game'],'custom')
            self.assertEqual(p['curve']['algorithm'],'linear')
            expected=repo.path(p['id']).read_bytes()
            p['name']='自由重命名'
            expected=repo.save(p,expected)
            q=repo.duplicate(p,'独立副本')
            self.assertNotEqual(p['id'],q['id'])
            archive=repo.delete(p,expected)
            self.assertEqual([x['id'] for x in repo.entries()],[q['id']])
            self.assertFalse(repo.runtime_path(p).exists())
            restored=repo.import_file(archive/repo.path(p['id']).name,'恢复','custom')
            self.assertEqual(restored['config'],p['config'])
            self.assertNotEqual(restored['id'],p['id'])
            arbitrary=repo.create('另一个场景','training_range_42',validate=lambda path:manager.validate(path,['training_range_42']))
            self.assertEqual(repo.read(repo.path(arbitrary['id']))[0]['game'],'training_range_42')

    def test_import_choices_come_from_file_and_delete_rejects_external_changes(self):
        with tempfile.TemporaryDirectory() as folder:
            repo=ProfileRepository(folder,curve_source=PROJECT)
            legacy=Path(folder)/'legacy.toml'
            legacy.write_text('[games.training]\n[games.other_title]\n',encoding='utf-8')
            self.assertEqual(repo.import_choices(legacy),['default','training','other_title'])
            p=repo.create('自由配置')
            self.assertEqual(repo.import_choices(repo.path(p['id'])),['custom'])
            expected=repo.path(p['id']).read_bytes()
            repo.runtime_path(p).write_text('# external',encoding='utf-8')
            with self.assertRaises(ValueError):repo.delete(p,expected)
            self.assertEqual(repo.path(p['id']).read_bytes(),expected)
            self.assertEqual(repo.runtime_path(p).read_text(encoding='utf-8'),'# external')

    def test_new_game_defaults_roundtrip_with_coherent_vision_geometry(self):
        from desktop_app.workspace import FIELDS
        manager=RuntimeManager(PROJECT)
        with tempfile.TemporaryDirectory() as folder:
            repo=ProfileRepository(folder,curve_source=PROJECT)
            for game in ('default','apex','bo3'):
                defaults=manager.inspect_defaults(game,f'[runtime]\ngame="{game}"\n')
                defaults['runtime.vision.model_path']=''
                profile=repo.create('默认规格检查 '+game,game,defaults,
                    validate=lambda path:manager.validate(path,[game]))
                document=projection(profile)
                vision=document['runtime']['vision']
                self.assertEqual((vision['capture_width'],vision['capture_height']),(640,512))
                self.assertEqual((vision['tensor_width'],vision['tensor_height']),(480,384))
                self.assertEqual(vision['capture_fps'],200)
                actual=manager.inspect_defaults(game,toml_text(document))
                for field in FIELDS:
                    path=field[0]
                    with self.subTest(game=game,path=path):
                        self.assertIn(path,actual)
                        expected=lookup(document,path)
                        if field[2] is float:self.assertAlmostEqual(actual[path],expected,places=5)
                        else:self.assertEqual(actual[path],expected)
                manager.validate(repo.runtime_path(profile),[game])

    def test_every_editable_numeric_field_roundtrips_native_without_replacement(self):
        from desktop_app.workspace import FIELDS,put
        from desktop_app.fields import field_value
        manager=RuntimeManager(PROJECT)
        for field in FIELDS:
            path,label,kind,default,limits=field
            if kind not in (int,float):continue
            lo,hi=limits
            for value in (lo,default,hi):
                with self.subTest(path=path,value=value):
                    document={}
                    put(document,'gamepad.ads.activation_trigger',0.)
                    put(document,'gamepad.ads.scope_ready_trigger',1.)
                    put(document,'gamepad.ads.completion_radius_px',1.)
                    put(document,'gamepad.assist.arrival_radius_px',1.)
                    put(document,'gamepad.bodylock.activation_range_px',2000.)
                    put(document,'gamepad.ai_aim.manual_intent_begin',0.)
                    put(document,'gamepad.ai_aim.manual_intent_full',1.)
                    put(document,'gamepad.auto_fire.pulse_width_ms',1.)
                    put(document,'gamepad.auto_fire.pulse_period_ms',5000.)
                    put(document,path,field_value(field,value))
                    actual=manager.inspect_defaults('default',toml_text(document))
                    self.assertIn(path,actual)
                    self.assertAlmostEqual(actual[path],value,delta=max(1e-6,abs(value)*1e-6))
            for value in (lo-1,hi+1):
                with self.subTest(path=path,invalid=value), self.assertRaises(ValueError):field_value(field,value)


    def test_legacy_parameters_convert_once_to_explicit_limits_and_times(self):
        from desktop_app.parameter_catalog import configured_value
        source={'gamepad':{'ads':{'strength_scale':.2,'vertical_strength_scale':.4},
            'bodylock':{'strength':2.,'vertical_strength':.3,'feedback_distance_px':27},
            'ai_aim':{'hipfire_multiplier':2.},'recoil':{'feedback_amount':.01}}}
        before=deepcopy(source)
        converted=snapshot(source,'apex',PROJECT)['config']
        self.assertAlmostEqual(lookup(converted,'gamepad.ads.output_limit_x'),.2*2**.5,places=6)
        self.assertEqual(lookup(converted,'gamepad.bodylock.output_limit_x'),1.)
        self.assertEqual(lookup(converted,'gamepad.bodylock.response_time_x_ms'),27.)
        self.assertEqual(lookup(converted,'gamepad.assist.hipfire_ratio'),1.)
        self.assertEqual(lookup(converted,'gamepad.recoil.output_amount'),.01)
        self.assertEqual(source,before)
        converted['gamepad']['bodylock']['output_limit_x']=.2
        self.assertEqual(configured_value(converted,'gamepad.bodylock.response_time_x_ms'),27.)
        values=RuntimeManager(PROJECT).inspect_defaults('apex',toml_text(converted))
        for path in ('gamepad.ads.output_limit_x','gamepad.bodylock.output_limit_x',
                     'gamepad.bodylock.response_time_x_ms','gamepad.assist.hipfire_ratio','gamepad.recoil.output_amount'):
            self.assertAlmostEqual(values[path],lookup(converted,path),places=5)
    def test_legacy_feedback_import_preserves_effective_distance_and_source(self):
        for path in ('gamepad.bodylock.tolerance_px','gamepad.ai_aim.body_lock_box_tolerance_px'):
            from desktop_app.workspace import put,configured_value
            for value in (0,1,8,12,18,24,2000):
                document={};put(document,path,value);before=deepcopy(document)
                migrated=snapshot(document,'apex',PROJECT)['config']
                self.assertAlmostEqual(lookup(migrated,'gamepad.bodylock.response_time_x_ms'),min(1000,max(18,value*1.5)/.15),places=3)
                self.assertEqual(document,before)
                self.assertEqual(configured_value(document,'gamepad.bodylock.feedback_distance_px'),max(18,value*1.5))
            put(document,'gamepad.bodylock.feedback_distance_px',1)
            self.assertEqual(configured_value(document,'gamepad.bodylock.feedback_distance_px'),1)
        with self.assertRaises(ValueError):snapshot({'gamepad':{'bodylock':{'feedback_distance_px':0}}},'apex',PROJECT)

    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.root=Path(self.temp.name)
        self.repo=ProfileRepository(self.root,PROJECT)
    def tearDown(self):self.temp.cleanup()

    def test_empty_library_does_not_seed_or_rewrite_history(self):
        path=self.root/'config.toml';path.write_text('historical user bytes',encoding='utf-8')
        self.assertEqual(self.repo.entries(),[])
        self.assertEqual(path.read_text(encoding='utf-8'),'historical user bytes')

    def test_defaults_independence_rename_and_apex_linear_reopen(self):
        a=self.repo.create('Apex 日常','apex');b=self.repo.duplicate(a,'Apex 比赛')
        saved,expected=self.repo.read(self.repo.path(a['id']))
        saved['name']='Apex 训练';saved['config']['gamepad']['ads']['output_limit_x']=.735
        self.repo.save(saved,expected)
        reopened=self.repo.read(self.repo.path(a['id']))[0]
        self.assertEqual(reopened['curve']['algorithm'],'linear')
        self.assertEqual(reopened['id'],a['id'])
        self.assertEqual(lookup(self.repo.read(self.repo.path(b['id']))[0]['config'],'gamepad.ads.output_limit_x'),1.)
        self.assertTrue(list((self.root/'runs/desktop/profile-backups').glob('*.json')))

    def test_flat_apex_profile_has_no_inheritance_and_native_consumes_custom_curve(self):
        p=self.repo.create('曲线','apex')
        _,expected=self.repo.read(self.repo.path(p['id']))
        points=[[0,0],[.2,.08],[.5,.37],[1,1]]
        p['curve']={'algorithm':'custom_lut','definition':curve_document('精调',points)}
        self.repo.save(p,expected,lambda path:RuntimeManager(PROJECT).validate(path,['apex']))
        doc=tomllib.loads(self.repo.runtime_path(p).read_text(encoding='utf-8'))
        self.assertNotIn('games',doc)
        self.assertEqual(effective(doc,'apex')['gamepad']['aim_response_curve']['custom_points'],encode_points(points))
        result=subprocess.run([str(PRODUCTION),'--config',str(self.repo.runtime_path(p)),'--game','apex','--dump-effective-config'],
            capture_output=True,timeout=15,creationflags=0x08000000)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn('gamepad.aim_response_curve.algorithm=custom_lut',result.stdout.decode())
        with self.assertRaises(ValueError):effective(doc,'bo3')

    def test_import_flattens_selected_override_and_preserves_source_and_hidden_fields(self):
        text='[runtime]\ngame="default"\n[gamepad.ads]\noutput_limit_x=.8\n[games.apex.gamepad.ads]\noutput_limit_x=.6\n'
        # TOML requires leading zero on floats.
        text=text.replace('=.','=0.')
        path=self.root/'import.toml';path.write_text(text,encoding='utf-8')
        p=self.repo.import_file(path,'导入','apex')
        self.assertEqual(lookup(p['config'],'gamepad.ads.output_limit_x'),.6)
        self.assertNotIn('games',p['config'])
        self.assertEqual(path.read_text(encoding='utf-8'),text)
        source=tomllib.loads(text);before=deepcopy(source)
        snapshot(source,'bo3',PROJECT)
        self.assertEqual(source,before)
        common={'runtime':{'game':'default'},'gamepad':{'ads':{'output_limit_x':.9}}}
        self.assertEqual(lookup(snapshot(common,'apex',PROJECT)['config'],'gamepad.ads.output_limit_x'),.9)

    def test_bad_import_types_are_not_silently_coerced(self):
        with self.assertRaises(ValueError):self.repo.create('坏配置','apex',source={'gamepad':{'recoil':{'enabled':'false'}}})
        self.assertEqual(self.repo.entries(),[])

    def test_rejected_validation_and_external_changes_do_not_overwrite_user_data(self):
        def reject(_):raise ValueError('NATIVE_REJECT')
        with self.assertRaisesRegex(ValueError,'NATIVE_REJECT'):self.repo.create('拒绝','apex',validate=reject)
        self.assertEqual(list(self.repo.folder.glob('*')),[])
        p=self.repo.create('配置','apex');_,expected=self.repo.read(self.repo.path(p['id']))
        self.repo.path(p['id']).write_bytes(expected+b' ')
        with self.assertRaisesRegex(ValueError,'外部修改'):self.repo.save(p,expected)
        self.assertEqual(self.repo.path(p['id']).read_bytes(),expected+b' ')

    def test_modified_projection_is_reported_instead_of_overwritten(self):
        p=self.repo.create('配置','apex');_,expected=self.repo.read(self.repo.path(p['id']))
        path=self.repo.runtime_path(p);path.write_text('# user change\n',encoding='utf-8')
        with self.assertRaisesRegex(ValueError,'外部修改'):self.repo.save(p,expected)
        self.assertEqual(path.read_text(encoding='utf-8'),'# user change\n')

    def test_corrupt_entry_does_not_hide_valid_profiles(self):
        p=self.repo.create('配置','apex')
        (self.repo.folder/('p_'+'f'*32+'.json')).write_text('{}',encoding='utf-8')
        self.assertEqual([e['id'] for e in self.repo.entries()],[p['id']])
        self.assertEqual(len(self.repo.errors),1)

    def test_close_precision_points_are_accepted_by_native_loader(self):
        p=self.repo.create('精校','apex');_,expected=self.repo.read(self.repo.path(p['id']))
        points=[[0,0],[.09999512344,.2],[.10000612344,.4],[1,1]]
        p['curve']={'algorithm':'custom_lut','definition':curve_document('精校',points)}
        self.repo.save(p,expected,lambda path:RuntimeManager(PROJECT).validate(path,['apex']))

    def test_json_import_keeps_game_curve_and_immutable_baseline(self):
        p=self.repo.create('配置','apex')
        q=self.repo.import_file(self.repo.path(p['id']),'副本','default')
        self.assertEqual(q['game'],'apex')
        self.assertNotEqual(q['id'],p['id'])
        self.assertEqual(q['initial']['curve'],p['curve'])


class CurveTransactionTests(unittest.TestCase):
    def test_precision_points_survive_serialization_at_decimal_magnitude_boundary(self):
        points=[[0,0],[.09999512344,.2],[.10000612344,.4],[1,1]]
        self.assertEqual(decode_points(encode_points(points)),validate_points(points))

    def test_drag_is_one_undo_transaction_with_crossing_and_endpoint_constraints(self):
        initial=[[0,0],[.2,.1],[.5,.4],[1,1]]
        model=CurveModel(initial);model.begin()
        for x,y in [(.3,.2),(.6,.9),(.9,1.2)]:model.drag(1,x,y)
        model.commit();validate_points(model.points)
        self.assertEqual(len(model.undo_stack),1)
        self.assertLess(model.points[1][0],.5);self.assertLess(model.points[1][1],.4)
        model.undo();self.assertEqual(model.points,initial)
        model.redo();self.assertNotEqual(model.points,initial)
        model.drag(0,.2,.3);self.assertEqual(model.points[0],[0,0])

    def test_precision_rejects_crossing_without_clamping_and_supports_point_lifecycle(self):
        model=CurveModel([[0,0],[.5,.5],[1,1]])
        with self.assertRaises(ValueError):model.exact(1,.6,1.2)
        self.assertEqual(model.points[1],[.5,.5])
        model.exact(1,.513456,.428765);self.assertEqual(model.points[1],[.513456,.428765])
        model.add(.75,.8);model.remove();model.undo()
        self.assertEqual(len(model.points),4)
