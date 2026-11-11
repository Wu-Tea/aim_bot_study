"""Real browser flow with temporary profiles, native validation and no actuator."""
import json
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import sys
import tempfile
import threading

from playwright.sync_api import sync_playwright, expect
from project_paths import PROJECT_ROOT
from desktop_app.web_service import WorkspaceService

sys.path.insert(0, str(PROJECT_ROOT / 'python/tests'))
from test_desktop_web_service import TestRuntime


def main():
    out = PROJECT_ROOT / 'output/playwright/response-measurement'
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='response-measurement-ui-') as folder:
        runtime = TestRuntime()
        service = WorkspaceService(folder, runtime, curve_source=PROJECT_ROOT)
        def call(command, payload=None):
            result = service.dispatch(command, payload)
            assert result['ok'], result
            return result['data']
        draft = call('create', {'name': '响应测量验证'})
        draft['values'].update({'runtime.vision.model_path': 'test.engine',
                               'gamepad.ai_aim.body_free_initial_scale': 500,
                               'gamepad.ai_aim.ads_free_initial_scale': 3000})
        saved = call('start', draft)['profile']
        data = {'status': 0, 'request_id': 0, 'revision': 1,
                'regions': [{'effective': 500, 'learned': 1800, 'confidence': .99, 'samples': 42},
                            {'effective': 3000, 'learned': 0, 'confidence': 0, 'samples': 0}]}
        runtime.learning = lambda: data
        original_reload = runtime.reload_config
        def reload_config():
            result = original_reload()
            if result['status'] == 2:
                data['regions'][0]['effective'] = service.repo.entries()[0]['config']['gamepad']['ai_aim']['body_free_initial_scale']
            return result
        runtime.reload_config = reload_config
        class Handler(SimpleHTTPRequestHandler):
            def __init__(self, *args, **kwargs):
                super().__init__(*args, directory=str(PROJECT_ROOT / 'python/desktop_app/web_ui/dist'), **kwargs)
            def log_message(self, *args): pass
            def do_POST(self):
                request = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
                result = service.dispatch(request['command'], request.get('payload'))
                encoded = json.dumps(result).encode()
                self.send_response(200)
                self.send_header('Content-Type', 'application/json')
                self.send_header('Content-Length', str(len(encoded)))
                self.end_headers()
                self.wfile.write(encoded)
        server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        try:
            with sync_playwright() as p:
                browser = p.chromium.launch(channel='msedge', headless=True)
                page = browser.new_page(viewport={'width': 1348, 'height': 928})
                errors = []
                page.on('pageerror', lambda error: errors.append(str(error)))
                page.add_init_script("""window.pywebview={api:{dispatch:async(command,payload={})=>
                    (await fetch('/rpc',{method:'POST',headers:{'Content-Type':'application/json'},
                    body:JSON.stringify({command,payload})})).json()}};""")
                page.goto(f'http://127.0.0.1:{server.server_port}')
                page.get_by_role('navigation').get_by_role('button', name='运行反馈', exact=True).click()
                rows = page.locator('tbody tr')
                expect(rows.nth(0)).to_contain_text('1800.00')
                expect(rows.nth(1).get_by_role('button', name='填入配置', exact=True)).to_be_disabled()
                path = service.repo.runtime_path(service.states[saved['id']][0])
                before = path.read_bytes()
                rows.nth(0).get_by_role('button', name='填入配置', exact=True).click()
                expect(page.locator('.save-state')).to_have_text('未保存')
                expect(rows.nth(0).locator('td').nth(1)).to_have_text('500.00')
                assert path.read_bytes() == before
                rows.nth(0).get_by_role('button', name='恢复已保存值').click()
                expect(page.locator('.save-state')).to_have_text('已保存')
                rows.nth(0).get_by_role('button', name='填入配置', exact=True).click()
                expect(rows.nth(0).get_by_role('button', name='已填入', exact=True)).to_be_disabled()
                page.screenshot(path=str(out / 'pending.png'))
                page.keyboard.press('Control+s')
                expect(page.locator('.save-state')).to_have_text('已保存')
                expect(rows.nth(0).locator('td').nth(1)).to_have_text('1800.00')
                assert data['regions'][0]['samples'] == 42
                for width, height in [(1348, 928), (1120, 760)]:
                    page.set_viewport_size({'width': width, 'height': height})
                    page.screenshot(path=str(out / f'applied-{width}.png'))
                    assert page.locator('.workspace-fields').evaluate('(e)=>e.scrollWidth<=e.clientWidth+1')
                assert not errors, errors
                browser.close()
        finally:
            server.shutdown()
        print(json.dumps({'independent_values': True, 'unavailable_disabled': True,
                          'stage_without_save': True, 'revert': True, 'save_and_apply': True,
                          'measurement_retained': True, 'screenshots': str(out)}))


if __name__ == '__main__':
    main()
