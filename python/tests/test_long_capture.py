from datetime import datetime, timedelta, timezone
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from desktop_app.long_capture import attach_capture, capture_status, set_capture_enabled, _capture_script
import subprocess


class LongCaptureTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root/'runs/desktop').mkdir(parents=True)
        self.base = self.root/'output/session'
        self.base.mkdir(parents=True)
        self.config = self.root/'config.toml'
        self.config.write_text('[runtime]\nprofile="balanced"\n')
        self.now = datetime.now(timezone.utc)
        self.info = {'enabled': True, 'directory': str(self.base),
                     'expires_utc': (self.now+timedelta(hours=12)).isoformat()}
        (self.base/'monitor-health.json').write_text(json.dumps({'gpu_running': True, 'presentmon_running': True}))

    def marker(self):
        (self.root/'runs/desktop/long-capture.json').write_text(json.dumps(self.info))

    def test_opt_in_and_preserves_source(self):
        env = {}
        self.assertIsNone(attach_capture(self.root,'a',self.config,env,self.now))
        original = self.config.read_bytes()
        self.marker()
        folder = attach_capture(self.root,'a',self.config,env,self.now)
        self.assertEqual(self.config.read_bytes(),original)
        self.assertEqual((folder/'source-config.toml').read_bytes(),original)
        self.assertEqual(env['AIMLAB_LIVE_TRACE_DIR'],str(folder/'trace'))
        with self.assertRaises(FileExistsError):
            attach_capture(self.root,'a',self.config,env,self.now)

    def test_expired_disabled_and_stopped_do_not_attach(self):
        for mode in ('expired','disabled','stopped'):
            self.info['enabled'] = mode!='disabled'
            self.info['expires_utc'] = (self.now+timedelta(hours=-1 if mode=='expired' else 12)).isoformat()
            if mode=='stopped': (self.base/'stop').touch()
            self.marker(); env={}
            self.assertIsNone(attach_capture(self.root,mode,self.config,env,self.now))
            self.assertEqual(env,{})

    def test_rejects_unhealthy_or_outside_directory(self):
        self.marker()
        (self.base/'monitor-health.json').write_text('{"failed":true}')
        with self.assertRaises(ValueError): attach_capture(self.root,'a',self.config,{},self.now)
        self.info['directory']=str(self.root/'outside');self.marker()
        with self.assertRaises(ValueError): attach_capture(self.root,'a',self.config,{},self.now)

    def test_stale_health_rejected_before_creating_session(self):
        self.marker()
        with self.assertRaises(ValueError):
            attach_capture(self.root,'a',self.config,{},self.now+timedelta(seconds=60))
        self.assertFalse((self.base/'runtime/a').exists())

    def test_status_reports_actual_attachment_and_expiry(self):
        self.assertEqual(capture_status(self.root)['state'], 'off')
        self.marker()
        self.assertEqual(capture_status(self.root)['state'], 'armed')
        self.assertEqual(capture_status(self.root, {'process_id': 123})['state'], 'unattached')
        record = {'capture_directory': str(self.base/'runtime/a')}
        self.assertEqual(capture_status(self.root, record)['state'], 'recording')
        self.assertEqual(capture_status(self.root, now=self.now+timedelta(seconds=60))['state'], 'error')
        expired = capture_status(self.root, now=self.now+timedelta(hours=13))
        self.assertEqual(expired['state'], 'expired')
        self.assertFalse(expired['enabled'])
        (self.root/'runs/desktop/long-capture.json').write_text('{broken')
        self.assertEqual(capture_status(self.root)['state'], 'error')

    def test_toggle_stops_collectors_and_preserves_logs(self):
        self.marker()
        evidence = self.base/'evidence.csv'
        evidence.write_text('keep me')
        def script(root, name):
            self.assertEqual(name, 'stop.ps1')
            self.info['enabled'] = False
            self.marker()
            (self.base/'monitor-health.json').write_text('{"complete":true}')
        with patch('desktop_app.long_capture._capture_script', side_effect=script) as runner:
            self.assertEqual(set_capture_enabled(self.root, True)['state'], 'armed')
            runner.assert_not_called()
            self.assertEqual(set_capture_enabled(self.root, False)['state'], 'off')
            runner.assert_called_once()
        self.assertEqual(evidence.read_text(), 'keep me')

    def test_toggle_does_not_claim_success_when_collectors_keep_running(self):
        self.marker()
        with patch('desktop_app.long_capture._capture_script'), patch('desktop_app.long_capture.time.monotonic', side_effect=[0, 16]):
            with self.assertRaisesRegex(ValueError, '尚未确认停止'):
                set_capture_enabled(self.root, False)

    def test_enable_requires_healthy_collectors_after_script(self):
        with patch('desktop_app.long_capture._capture_script') as runner:
            with self.assertRaises(ValueError):
                set_capture_enabled(self.root, True)
            runner.assert_called_once_with(self.root, 'arm.ps1')

    def test_script_does_not_wait_for_monitor_to_close_inherited_pipes(self):
        def run(command, **options):
            self.assertNotIn('capture_output', options)
            self.assertEqual(options['stdin'], subprocess.DEVNULL)
            self.assertEqual(options['stderr'], subprocess.STDOUT)
            self.assertGreaterEqual(options['stdout'].fileno(), 0)
            return subprocess.CompletedProcess(command, 0)
        with patch('desktop_app.long_capture.shutil.which', return_value='powershell'), patch('desktop_app.long_capture.subprocess.run', side_effect=run):
            _capture_script(self.root, 'arm.ps1')

    def test_dead_collectors_with_stale_running_health_can_be_stopped(self):
        self.marker()
        original = (self.base/'monitor-health.json').read_bytes()
        def stop(root, name):
            self.assertEqual(name, 'stop.ps1')
            self.info['enabled'] = False
            self.marker()
        with patch('desktop_app.long_capture._capture_script', side_effect=stop), patch('desktop_app.long_capture.collectors_running', return_value=False, create=True), patch('desktop_app.long_capture.time.monotonic', side_effect=[0,16]):
            state = set_capture_enabled(self.root, False)
        self.assertEqual(state['state'], 'off')
        self.assertIn('异常结束', state['message'])
        self.assertEqual((self.base/'monitor-health.json').read_bytes(), original)
