"""Browser-only test adapter. Temporary data, native validation, NO actuator process."""
import json
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import sys
import tempfile

from project_paths import PROJECT_ROOT
sys.path.insert(0,str(PROJECT_ROOT/'python/tests'))
from test_desktop_web_service import TestRuntime
from desktop_app.web_service import WorkspaceService


def main():
    with tempfile.TemporaryDirectory(prefix='desktop-web-check-') as folder:
        root=Path(folder)
        runtime=TestRuntime()
        service=WorkspaceService(root,runtime,curve_source=PROJECT_ROOT)
        def dialogs(mode,name,filters):
            if mode=='save':return str(root/name)
            if 'TensorRT' in filters[0]:return str(root/'test.engine')
            for path in root.glob('*.json'):
                data=json.loads(path.read_text(encoding='utf-8'))
                if ('响应曲线' in filters[0]) == (data.get('kind')=='normalized_stick_response'):
                    return str(path)
            raise ValueError('测试适配器：请先导出文件，再测试导入。')
        service.dialogs=dialogs
        response=service.dispatch('create',{'name':'交互验证'})
        if not response['ok']: raise RuntimeError(response['error'])
        class Handler(SimpleHTTPRequestHandler):
            def __init__(self,*args,**kwargs):super().__init__(*args,directory=str(PROJECT_ROOT/'python/desktop_app/web_ui/dist'),**kwargs)
            def log_message(self,*args): pass
            def do_POST(self):
                if self.path!='/rpc':self.send_error(404);return
                p=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
                result=service.dispatch(p['command'],p.get('payload'))
                data=json.dumps(result,ensure_ascii=False).encode()
                self.send_response(200);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
        print('TEST ONLY http://127.0.0.1:8877 ; isolated profiles ; no actuator',flush=True)
        ThreadingHTTPServer(('127.0.0.1',8877),Handler).serve_forever()


if __name__=='__main__':main()
