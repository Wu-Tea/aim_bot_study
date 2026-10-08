"""Exercise the actual Edge WebView2 host on the private GUI test desktop."""
import json
from pathlib import Path
import sys
import tempfile
import time
from project_paths import PROJECT_ROOT
from desktop_app.test_desktop import require_isolated_gui
from desktop_app.web_host import launch
from desktop_app.web_service import WorkspaceService
sys.path.insert(0,str(PROJECT_ROOT/'python/tests'))
from test_desktop_web_service import TestRuntime


def main():
    require_isolated_gui()
    failure=[]
    with tempfile.TemporaryDirectory(prefix='desktop-host-check-') as folder:
        service=WorkspaceService(folder,TestRuntime(),curve_source=PROJECT_ROOT)
        result=service.dispatch('create',{'name':'宿主桥接验证'})
        if not result['ok']:raise RuntimeError(result['error'])
        def check(window,_):
            try:
                deadline=time.monotonic()+25
                while time.monotonic()<deadline:
                    state=window.evaluate_js('({title:document.title,text:document.body.innerText,bridge:!!window.pywebview?.api})')
                    if state and '宿主桥接验证' in state['text']:break
                    time.sleep(.2)
                else:raise RuntimeError('WebView2 did not bootstrap: '+str(state))
                layouts=[]
                for page_name in ['参数调校','响应曲线','配置管理','模型与设备','运行反馈']:
                    window.evaluate_js('Array.from(document.querySelectorAll("nav button")).find(e=>e.textContent===' + json.dumps(page_name) + ').click()')
                    time.sleep(.2)
                    layout=window.evaluate_js('''(() => {
                        const outer=document.querySelector('.viewport');
                        const body=document.querySelector('.workspace-fields');
                        return {width:innerWidth,height:innerHeight,
                            outer:[outer.scrollHeight,outer.clientHeight],
                            body:[body.scrollHeight,body.clientHeight]};
                    })()''')
                    assert all(layout[key][0]<=layout[key][1]+1 for key in ['outer','body']), (page_name,layout)
                    layouts.append({'page':page_name,**layout})
                window.evaluate_js('Array.from(document.querySelectorAll("nav button")).find(e=>e.textContent==="参数调校").click()')
                window.evaluate_js("document.querySelector('[aria-label=\"当前配置\"]').focus()")
                window.evaluate_js('''(() => {
                    const input=document.querySelector('input[id="f-gamepad.ads.output_limit_x"]');
                    Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set.call(input,'44');
                    input.dispatchEvent(new Event('input',{bubbles:true}));
                })()''')
                deadline=time.monotonic()+5
                while time.monotonic()<deadline:
                    if window.evaluate_js('document.body.innerText.includes("有未保存修改")'):break
                    time.sleep(.1)
                else:raise RuntimeError('Native host did not register draft edit')
                window.destroy()  # Exercise the native closing event, not only JS.
                deadline=time.monotonic()+5
                while time.monotonic()<deadline:
                    if window.evaluate_js('!!document.querySelector("[role=dialog]")'):break
                    time.sleep(.1)
                else:raise RuntimeError('Native close did not protect unsaved draft')
                window.evaluate_js('Array.from(document.querySelectorAll("[role=dialog] button")).find(e=>e.textContent==="取消").click()')
                print(json.dumps({'webview2_bootstrap':True,'title':state['title'],'bridge':state['bridge'],
                    'profile_loaded':True,'native_close_guard':True,'layouts':layouts}),flush=True)
            except Exception as error:failure.append(error)
            finally:window.run_js('window.pywebview.api.dispatch("window_close")')
        launch(folder,service=service,ready=check)
    if failure:raise failure[0]


if __name__=='__main__':main()
