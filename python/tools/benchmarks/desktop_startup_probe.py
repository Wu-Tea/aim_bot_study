"""Isolated fresh-process WebView2 startup and temporary profile-load timings.

OS disk caches are not cleared. This measures fresh GUI processes, not reboot
cold starts. No runtime output is started. Run with --label before/after.
"""
import time
ENTRY = time.perf_counter()
import argparse
from copy import deepcopy
import json
from pathlib import Path
import sys
import tempfile
import uuid

from project_paths import PROJECT_ROOT
from desktop_app.runtime import RuntimeManager
from desktop_app.web_service import WorkspaceService


class ReadOnlyRuntime(RuntimeManager):
    def status(self): return dict(phase='stopped', record=None, initialized=False)
    def fusion_state(self): return False
    def start(self, *args, **kwargs): raise AssertionError('Startup probe must not start output')


def child(root, output):
    from desktop_app.test_desktop import require_isolated_gui
    require_isolated_gui()
    from desktop_app.web_host import launch
    marks = {'python_imports_ms': (time.perf_counter()-ENTRY)*1000}
    begin = time.perf_counter()
    import webview
    marks['webview_import_ms'] = (time.perf_counter()-begin)*1000
    service=WorkspaceService(root,ReadOnlyRuntime(PROJECT_ROOT),curve_source=PROJECT_ROOT)
    calls=[]
    dispatch=service.dispatch
    def timed(command,payload=None):
        before=time.perf_counter()
        result=dispatch(command,payload)
        calls.append(dict(command=command,start_ms=(before-ENTRY)*1000,ms=(time.perf_counter()-before)*1000,ok=result['ok']))
        return result
    service.dispatch=timed
    create=webview.create_window
    def instrument(*args,**kwargs):
        window=create(*args,**kwargs)
        def shown(): marks['shown_ms']=(time.perf_counter()-ENTRY)*1000
        def loaded(): marks['loaded_ms']=(time.perf_counter()-ENTRY)*1000
        window.events.shown+=shown
        window.events.loaded+=loaded
        return window
    webview.create_window=instrument
    failures=[]
    def ready(window,service):
        try:
            def wait(expression):
                deadline=time.perf_counter()+15
                while time.perf_counter()<deadline:
                    if window.evaluate_js(expression): return
                    time.sleep(.01)
                raise AssertionError(expression)
            wait('!!document.getElementById("f-gamepad.ads.output_limit_x") && document.querySelector(".save-state")?.textContent==="已保存"')
            marks['form_ready_ms']=(time.perf_counter()-ENTRY)*1000
            wait('!!document.querySelector(".preview-readout")')
            marks['preview_ready_ms']=(time.perf_counter()-ENTRY)*1000
            marks['navigation']=window.evaluate_js('JSON.parse(JSON.stringify(performance.getEntriesByType("navigation")[0]))')
            first_views={}
            for name in ['响应曲线','配置管理','模型与设备','运行反馈','参数调校']:
                before=time.perf_counter()
                window.evaluate_js('Array.from(document.querySelectorAll("nav button")).find(e=>e.textContent==='+json.dumps(name)+').click()')
                wait('document.querySelector("h1")?.textContent==='+json.dumps(name))
                first_views[name]=(time.perf_counter()-before)*1000
            wait('!!document.querySelector(".preview-readout")')
            before=time.perf_counter()
            window.evaluate_js('document.querySelector("[aria-label=打开观察窗口]").click()')
            wait('!!document.querySelector(".observation-window")')
            first_views['观察窗口']=(time.perf_counter()-before)*1000
            marks['first_views_ms']=first_views
            marks['calls']=calls
            Path(output).write_text(json.dumps(marks,ensure_ascii=False,indent=2),encoding='utf-8')
        except Exception as error:
            failures.append(error)
        finally:
            # Use the host close path; do not bypass its closing guard.
            window.run_js('window.pywebview.api.dispatch("window_close")')
    launch(root,service=service,ready=ready)
    if failures: raise failures[0]


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--label',default='before')
    parser.add_argument('--child-root')
    parser.add_argument('--child-out')
    args=parser.parse_args()
    if args.child_root:
        child(args.child_root,args.child_out)
        return
    from desktop_app.test_desktop import run_isolated
    out=PROJECT_ROOT/'output/playwright/gui-startup'
    out.mkdir(parents=True,exist_ok=True)
    backend=[]; hosts=[]
    with tempfile.TemporaryDirectory(prefix='gui-startup-') as folder:
        root=Path(folder)
        service=WorkspaceService(root,ReadOnlyRuntime(PROJECT_ROOT),curve_source=PROJECT_ROOT)
        created=service.dispatch('create',{'name':'启动验证'})
        assert created['ok'],created.get('error')
        seed=service.repo.read(service.repo.path(created['data']['id']))[0]
        for count in (4,40):
            for i in range(len(service.repo.entries()),count):
                doc=deepcopy(seed);doc['id']='p_'+uuid.uuid4().hex;doc['name']=f'启动验证 {i+1}'
                service.repo.path(doc['id']).write_text(json.dumps(doc,ensure_ascii=False),encoding='utf-8')
            for trial in range(5):
                probe=WorkspaceService(root,ReadOnlyRuntime(PROJECT_ROOT),curve_source=PROJECT_ROOT)
                reads=[]
                read=probe.repo.read
                def counted(path):
                    reads.append(str(path))
                    return read(path)
                probe.repo.read=counted
                before=time.perf_counter()
                result=probe.dispatch('bootstrap')
                elapsed=(time.perf_counter()-before)*1000
                assert result['ok'],result.get('error')
                assert len(result['data']['profiles'])==count
                backend.append(dict(profiles=count,trial=trial,ms=elapsed,reads=len(reads)))
            if count==4:
                for trial in range(3):
                    result_file=out/f'{args.label}-host-{trial}.json'
                    code=run_isolated([str(Path(__file__).resolve()),'--child-root',str(root),'--child-out',str(result_file)],cwd=PROJECT_ROOT)
                    assert code==0,code
                    hosts.append(json.loads(result_file.read_text(encoding='utf-8')))
    result=dict(backend=backend,hosts=hosts)
    (out/(args.label+'.json')).write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(result,ensure_ascii=False))


if __name__=='__main__':main()
