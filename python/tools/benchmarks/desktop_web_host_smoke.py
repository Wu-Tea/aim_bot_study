"""Exercise the actual Edge WebView2 host on the private GUI test desktop."""
import json
from pathlib import Path
import sys
import tempfile
import time
from datetime import datetime, timedelta, timezone
from unittest.mock import patch
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
                window.evaluate_js('Array.from(document.querySelectorAll("nav button")).find(e=>e.textContent==="模型与设备").click()')
                window.evaluate_js('Array.from(document.querySelectorAll("button")).find(e=>e.textContent==="手柄与诊断").click()')
                time.sleep(.3)
                def capture_ui():
                    return window.evaluate_js('''(() => {const e=document.getElementById('performance-capture');
                        return {checked:e.checked,disabled:e.disabled,visible:e.getBoundingClientRect().height>0};})()''')
                assert capture_ui()==dict(checked=False,disabled=False,visible=True), capture_ui()
                def fake_collectors(root, enabled):
                    base=Path(root)/'output/gui-capture'
                    base.mkdir(parents=True,exist_ok=True)
                    marker=Path(root)/'runs/desktop/long-capture.json'
                    marker.parent.mkdir(parents=True,exist_ok=True)
                    marker.write_text(json.dumps(dict(enabled=enabled,directory=str(base),
                        expires_utc=(datetime.now(timezone.utc)+timedelta(hours=12)).isoformat())))
                    (base/'monitor-health.json').write_text('{"gpu_running":true,"presentmon_running":true}')
                with patch('desktop_app.web_service.set_capture_enabled',side_effect=fake_collectors) as setter:
                    for enabled in (True,False):
                        window.evaluate_js('document.getElementById("performance-capture").click()')
                        deadline=time.monotonic()+5
                        while time.monotonic()<deadline:
                            if capture_ui()['checked']==enabled:break
                            time.sleep(.1)
                        else:raise RuntimeError('Capture toggle did not update')
                        setter.assert_called_with(Path(folder).resolve(),enabled)
                service.manager.record={'process_id':123,'profile_id':service.repo.entries()[0]['id']}
                deadline=time.monotonic()+5
                while time.monotonic()<deadline:
                    if capture_ui()['disabled']:break
                    time.sleep(.1)
                else:raise RuntimeError('Capture toggle remained editable during runtime')
                service.manager.record=None
                window.evaluate_js('Array.from(document.querySelectorAll("nav button")).find(e=>e.textContent==="参数调校").click()')
                window.evaluate_js('Array.from(document.querySelectorAll("button")).find(e=>e.textContent==="目标与识别").click()')
                time.sleep(.2)
                haptics=window.evaluate_js('''(() => {
                    const enabled=document.getElementById('f-gamepad.detection_haptics.enabled');
                    const strength=document.getElementById('f-gamepad.detection_haptics.strength');
                    return {enabled:enabled.checked,strength:strength.value,
                        visible:enabled.getBoundingClientRect().height>0 && strength.getBoundingClientRect().height>0};
                })()''')
                assert haptics=={'enabled':False,'strength':'24','visible':True}, haptics
                palette=window.evaluate_js('''(() => {
                    const input=document.querySelector('[aria-label="敌人标识颜色色盘"]');
                    return {type:input.type,value:input.value};
                })()''')
                assert palette=={'type':'color','value':'#ff00ff'}, palette
                for color in ('#00ff00','#ff00ff'):
                    window.evaluate_js('''(() => {
                        const input=document.querySelector('[aria-label="敌人标识颜色色盘"]');
                        Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set.call(input,''' + json.dumps(color) + ''');
                        input.dispatchEvent(new Event('input',{bubbles:true}));
                    })()''')
                    actual=window.evaluate_js('document.getElementById("f-runtime.vision.enemy_marker_color").value')
                    assert actual==color.upper(), actual
                window.evaluate_js('document.querySelector("[aria-label=\\"打开观察窗口\\"]").click()')
                time.sleep(.2)
                observation=window.evaluate_js('''(() => {
                    const svg=document.querySelector('.observation-surface svg');
                    const matrix=svg.getScreenCTM();
                    return {viewBox:svg.getAttribute('viewBox'),equalScale:Math.abs(matrix.a-matrix.d)<1e-6};
                })()''')
                assert observation=={'viewBox':'-320 -256 640 512','equalScale':True}, observation
                window.evaluate_js('document.querySelector("[aria-label=\\"关闭观察窗口\\"]").click()')
                window.evaluate_js('''(() => {
                    document.getElementById('f-gamepad.detection_haptics.enabled').click();
                    const input=document.getElementById('f-gamepad.detection_haptics.strength');
                    Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set.call(input,'42');
                    input.dispatchEvent(new Event('input',{bubbles:true}));
                })()''')
                time.sleep(.2)
                window.evaluate_js('document.dispatchEvent(new KeyboardEvent("keydown",{key:"s",ctrlKey:true,bubbles:true}))')
                deadline=time.monotonic()+10
                while time.monotonic()<deadline:
                    # Save is asynchronous. A directory scan can overlap the
                    # profile replacement; inspect the completed snapshot in
                    # this existing wait instead of indexing a transient list.
                    entries=service.repo.entries()
                    saved=entries[0]['config'].get('gamepad',{}).get('detection_haptics',{}) if entries else {}
                    if saved.get('enabled') is True and abs(saved.get('strength',0)-.42)<1e-6:break
                    time.sleep(.1)
                else:raise RuntimeError('WebView2 haptics controls did not persist: '+str(saved))
                # Repository persistence precedes the asynchronous bridge reply.
                # Wait for React to re-enable navigation before clicking it.
                deadline=time.monotonic()+5
                while time.monotonic()<deadline:
                    if window.evaluate_js('''(() => {const e=Array.from(document.querySelectorAll("button")).find(e=>e.textContent==="首次瞄准");return !!e && !e.matches(':disabled');})()'''):break
                    time.sleep(.05)
                else:raise RuntimeError('Save bridge did not re-enable tuning navigation')
                window.evaluate_js('Array.from(document.querySelectorAll("button")).find(e=>e.textContent==="首次瞄准").click()')
                time.sleep(.2)
                window.evaluate_js("document.querySelector('[aria-label=\"当前配置\"]').focus()")
                window.evaluate_js('''(() => {
                    const input=document.querySelector('input[id="f-gamepad.ads.output_limit_x"]');
                    Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set.call(input,'44');
                    input.dispatchEvent(new Event('input',{bubbles:true}));
                })()''')
                deadline=time.monotonic()+5
                while time.monotonic()<deadline:
                    if window.evaluate_js('document.querySelector(".save-state")?.textContent === "未保存"'):break
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
                    'profile_loaded':True,'native_close_guard':True,'directional_haptics_saved':True,
                    'capture_toggle_and_running_guard':True,'palette_and_observation':True,'layouts':layouts}),flush=True)
            except Exception as error:failure.append(error)
            finally:window.run_js('window.pywebview.api.dispatch("window_close")')
        launch(folder,service=service,ready=check)
    if failure:raise failure[0]


if __name__=='__main__':main()
