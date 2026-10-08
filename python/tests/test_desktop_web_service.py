"""Bridge lifecycle tests use temporary profiles and a non-actuating runtime double."""
from copy import deepcopy
from pathlib import Path
import tempfile
import unittest
from desktop_app.web_service import WorkspaceService
from desktop_app.runtime import RuntimeManager
from project_paths import PROJECT_ROOT


class TestRuntime:
    def __init__(self):
        self.real = RuntimeManager(PROJECT_ROOT)
        self.executable = self.real.executable
        self.record = None
        self.fail_start = False
        self.fail_reload = False
        self.starts = 0
        self.stops = 0

    def validate(self, path, games): self.real.validate(path, games)
    def inspect_defaults(self, game, text=None): return self.real.inspect_defaults(game, text)
    def ads_geometry_policy(self): return self.real.ads_geometry_policy()
    def inspect_model(self, path):
        if path == 'broken.engine': raise ValueError('模型损坏')
        return {'input_width':480,'input_height':384}
    def active(self): return self.record
    def status(self): return {'phase':'running' if self.record else 'stopped','record':self.record,'initialized':bool(self.record)}
    def start(self, game, document, path, identifier):
        self.starts += 1
        if self.fail_start: raise ValueError('测试启动失败')
        self.record = dict(game=game,config_path=str(path),profile_id=identifier,process_id=123,process_created=133333333333333337)
        return self.record
    def stop(self): self.stops += 1; self.record = None
    def reload_config(self):
        if self.fail_reload: raise ValueError('测试通道故障')
        return {'status':2,'revision':2,'message':'applied'}
    def learning(self): return None
    def frame_rates(self, record): return None
    def fusion_state(self): return None
    def input_devices(self): return [{'id':'test-controller','name':'测试手柄'}]


class WebServiceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.runtime = TestRuntime()
        self.service = WorkspaceService(self.temp.name,self.runtime,curve_source=PROJECT_ROOT)
        self.profile = self.call('create',{'name':'任意场景'})

    def tearDown(self): self.temp.cleanup()

    def call(self,command,p=None):
        result=self.service.dispatch(command,p)
        self.assertTrue(result['ok'],result.get('error'))
        return result['data']

    def test_crud_independent_drafts_and_conflict(self):
        p=deepcopy(self.profile);p['values']['gamepad.assist.input_deadzone']=0
        q=self.call('copy',{**p,'name':'副本'})
        self.assertNotEqual(p['id'],q['id'])
        self.assertEqual(q['values']['gamepad.assist.input_deadzone'],0)
        saved=self.call('save',p)['profile']
        self.assertEqual(saved['values']['gamepad.assist.input_deadzone'],0)
        self.assertFalse(self.service.dispatch('save',p)['ok'])
        renamed=self.call('rename',{**saved,'name':'新的名称','values':{}})
        self.assertEqual(renamed['name'],'新的名称')
        archive=self.call('delete',renamed)
        self.assertTrue(Path(archive).is_dir())
        self.assertFalse(self.service.repo.path(p['id']).exists())
        self.assertTrue(self.service.repo.path(q['id']).exists())

    def test_new_profile_requires_explicit_model_choice(self):
        self.assertEqual(self.profile['values']['runtime.vision.model_path'],'')
        self.assertFalse(self.service.dispatch('start',self.profile)['ok'])
        self.assertEqual(self.runtime.starts,0)

    def test_recoil_one_to_fifty_percent_matches_schema_and_persistence(self):
        field=next(f for f in self.call('bootstrap')['fields'] if f['path']=='gamepad.recoil.output_amount')
        self.assertEqual(tuple(field['limits']),(.01,.5))
        self.assertEqual(field['scale'],100)
        p=self.call('reload',{'id':self.profile['id']})
        for value in (.01,.05,.14,.5):
            p['values']['gamepad.recoil.output_amount']=value
            p=self.call('save',p)['profile']
            self.assertEqual(p['values']['gamepad.recoil.output_amount'],value)
        for value in (0,.009,.501,1):
            p['values']['gamepad.recoil.output_amount']=value
            self.assertFalse(self.service.dispatch('save',p)['ok'])

    def test_start_returns_process_identity_without_waiting_for_status_poll(self):
        p=deepcopy(self.profile)
        p['values'].update({'runtime.vision.model_path':'test.engine',
            'runtime.vision.tensor_width':480,'runtime.vision.tensor_height':384,
            'runtime.vision.capture_width':480,'runtime.vision.capture_height':384})
        result=self.call('start',p)
        self.assertEqual(result['status']['phase'],'starting')
        self.assertEqual(result['status']['record'],self.runtime.record)
        self.assertEqual(self.runtime.starts,1)

    def test_no_hidden_numeric_coercion_or_invalid_persistence(self):
        for path,value in [('gamepad.assist.input_deadzone',-.1),('runtime.vision.capture_fps',1.5),
                           ('gamepad.recoil.enabled','false'),('gamepad.ads.output_limit_x',float('nan'))]:
            with self.subTest(path=path):
                p=deepcopy(self.profile);p['values'][path]=value
                self.assertFalse(self.service.dispatch('save',p)['ok'])
                self.assertEqual(self.call('reload',{'id':p['id']})['values'],self.profile['values'])
                self.profile=self.call('reload',{'id':p['id']})

    def test_saved_state_survives_start_and_reload_failures(self):
        p=deepcopy(self.profile);p['values']['runtime.vision.model_path']='test.engine'
        self.runtime.fail_start=True
        r=self.call('start',p)
        self.assertIn('error',r);self.assertNotEqual(r['profile']['revision'],p['revision'])
        self.runtime.fail_start=False
        r=self.call('start',r['profile']);self.assertNotIn('error',r)
        self.runtime.fail_reload=True
        r=self.call('save',{**r['profile'],'apply':True})
        self.assertIn('error',r);self.assertIn('profile',r)

    def test_model_failure_does_not_stop_or_save(self):
        self.runtime.record={'game':'custom','process_id':1,'process_created':133333333333333337,'config_path':'other.toml'}
        p=deepcopy(self.profile);p['values']['runtime.vision.model_path']='broken.engine'
        self.assertFalse(self.service.dispatch('start',p)['ok'])
        self.assertEqual(self.runtime.stops,0)
        self.assertEqual(self.service.states[p['id']][2],p['revision'])

    def test_restart_confirmation_preserves_64_bit_identity(self):
        p=deepcopy(self.profile);p['values']['runtime.vision.model_path']='test.engine'
        self.runtime.record={'game':'custom','process_id':1,'process_created':133333333333333337,'config_path':'other.toml'}
        r=self.call('start',p)
        self.assertEqual(r['needs_restart'],[1,'133333333333333337'])
        self.assertEqual(self.runtime.stops,0)
        r=self.call('start',{**p,'confirmed_runtime':r['needs_restart']})
        self.assertEqual(self.runtime.stops,1);self.assertEqual(self.runtime.starts,1)

    def test_external_edit_is_not_overwritten(self):
        path=self.service.repo.path(self.profile['id'])
        external=path.read_bytes()+b'\n';path.write_bytes(external)
        self.assertFalse(self.service.dispatch('save',self.profile)['ok'])
        self.assertEqual(path.read_bytes(),external)

    def test_reset_recovers_invalid_draft(self):
        p=deepcopy(self.profile);p['values']['runtime.vision.capture_fps']=''
        reset=self.call('reset',p)
        self.assertEqual(reset['values'],self.profile['values'])
        self.assertEqual(reset['revision'],self.profile['revision'])

    def test_real_geometry_and_custom_curve_roundtrip(self):
        p=deepcopy(self.profile)
        result=self.call('geometry',dict(values=p['values'],height=22,pose='standing',offset=[0,0],mode='follow'))
        self.assertEqual(result['stick'],(0.,0.))
        p['curve']['algorithm']='custom_lut';p['curve']['definition']['points']=[[0,0],[.4,.25],[1,1]]
        saved=self.call('save',p)['profile']
        text=self.call('raw',saved)
        restored=self.call('raw_apply',{**saved,'text':text})
        self.assertEqual(restored['curve'],saved['curve'])
        self.assertEqual(restored['revision'],saved['revision'])

    def test_unexposed_raw_parameters_survive_save(self):
        # This valid native parameter has no visible form control.
        text=self.call('raw',self.profile)
        text=text.replace('[runtime.vision]', '[runtime.vision]\nrequire_isotropic_resize = false')
        draft=self.call('raw_apply',{**self.profile,'text':text})
        self.assertFalse(draft['config']['runtime']['vision']['require_isotropic_resize'])
        saved=self.call('save',draft)['profile']
        reopened=self.call('reload',{'id':saved['id']})
        self.assertFalse(reopened['config']['runtime']['vision']['require_isotropic_resize'])

    def test_model_dimension_proposal_does_not_save(self):
        draft=deepcopy(self.profile);draft['values']['runtime.vision.model_path']='test.engine'
        draft['values']['runtime.vision.tensor_width']=640
        result=self.call('start',draft)
        self.assertEqual(result['needs_model_sync']['runtime.vision.tensor_width'],480)
        self.assertEqual(self.runtime.starts,0)
        self.assertEqual(self.service.states[draft['id']][2],draft['revision'])

    def test_delete_current_runtime_is_rejected(self):
        p=self.profile
        self.runtime.record={'game':p['game'],'config_path':str(self.service.repo.runtime_path(p)),'process_id':1}
        self.assertFalse(self.service.dispatch('delete',p)['ok'])
        self.assertTrue(self.service.repo.path(p['id']).is_file())


if __name__=='__main__': unittest.main()
