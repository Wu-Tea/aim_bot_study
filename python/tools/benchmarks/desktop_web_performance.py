"""Repeatable browser CPU/RPC probe; temporary test server only, no actuators.

Run against desktop_web_test_server.py with --label before/after. Status churn
is synthetic; these measurements describe GUI work, not live runtime FPS.
"""
import argparse
import json
from collections import Counter
from playwright.sync_api import sync_playwright, expect
from project_paths import PROJECT_ROOT

BRIDGE = """(() => {
window.probe={serializations:0,commits:0,calls:[],churn:false};
const stringify=JSON.stringify;
JSON.stringify=function(value,...args){
 if(value && Object.hasOwn(value,'values') && Object.hasOwn(value,'curve') &&
    Object.hasOwn(value,'config') && Object.keys(value).length===3) probe.serializations++;
 return stringify.call(this,value,...args);
};
window.__REACT_DEVTOOLS_GLOBAL_HOOK__={supportsFiber:true,inject:()=>1,
 onCommitFiberRoot:()=>probe.commits++,onCommitFiberUnmount:()=>{},checkDCE:()=>{}};
window.pywebview={api:{dispatch:async(command,payload={})=>{
 const body=JSON.stringify({command,payload});
 probe.calls.push({command,bytes:new TextEncoder().encode(body).length});
 const response=await fetch('/rpc',{method:'POST',headers:{'Content-Type':'application/json'},body});
 const result=await response.json();
 if(command==='status' && result.ok && probe.override) Object.assign(result.data,probe.override);
 if(command==='status' && result.ok && probe.churn) result.data.fusion=!!(probe.calls.filter(c=>c.command==='status').length%2);
 if(command==='status' && result.ok) probe.status=result.data;
 return result;
}}};
})();"""


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--label',required=True)
    parser.add_argument('--verify',action='store_true')
    args=parser.parse_args()
    out=PROJECT_ROOT/'output/playwright/gui-performance'
    out.mkdir(parents=True,exist_ok=True)
    rows,errors=[],[]
    with sync_playwright() as p:
        browser=p.chromium.launch(channel='msedge',headless=True)
        for trial in range(3):
            page=browser.new_page(viewport={'width':1360,'height':900})
            page.add_init_script(BRIDGE)
            page.on('pageerror',lambda e:errors.append(str(e)))
            page.goto('http://127.0.0.1:8877')
            expect(page.locator('.save-state')).to_have_text('已保存')
            expect(page.locator('.preview-readout')).to_be_visible()
            page.wait_for_timeout(250)
            cdp=page.context.new_cdp_session(page)
            cdp.send('Performance.enable')
            def metrics():
                return {m['name']:m['value'] for m in cdp.send('Performance.getMetrics')['metrics']}
            def measure(name,work):
                page.evaluate('probe.serializations=0;probe.commits=0;probe.calls=[]')
                before=metrics()
                work()
                after=metrics()
                counts=page.evaluate('({serializations:probe.serializations,commits:probe.commits,calls:probe.calls})')
                calls=counts.pop('calls')
                row=dict(trial=trial,scenario=name,**counts,
                    requests=dict(Counter(c['command'] for c in calls)),
                    geometry_bytes=sum(c['bytes'] for c in calls if c['command']=='geometry'),
                    **{k+'_ms':round((after[k]-before[k])*1000,3) for k in
                       ['ScriptDuration','LayoutDuration','RecalcStyleDuration','TaskDuration']})
                rows.append(row)
                return row
            for churn in [False,True]:
                page.evaluate('(v)=>probe.churn=v',churn)
                row=measure('changing_status' if churn else 'idle',lambda:page.wait_for_timeout(4200))
                assert row['requests'].get('status',0)>=4, row
                if args.verify:
                    assert row['serializations']==0, row
                    if not churn: assert row['commits']<=1,row
                    else: assert row['commits']>=4,row
            page.evaluate('probe.churn=false')
            page.get_by_role('button',name='目标与识别',exact=True).click()
            color=page.locator('[id="f-runtime.vision.enemy_marker_color"]')
            original=color.input_value()
            page.wait_for_timeout(200)
            def colors():
                for i in range(8):
                    color.fill('#FF00FE' if i%2==0 else original)
                    page.wait_for_timeout(120)
            row=measure('unrelated_color_edits',colors)
            if args.verify: assert row['requests'].get('geometry',0)==0,row
            page.get_by_role('button',name='首次瞄准',exact=True).click()
            field=page.locator('[id="f-gamepad.ads.output_limit_x"]')
            original=field.input_value()
            page.wait_for_timeout(200)
            def relevant():
                for i in range(8):
                    field.fill('43' if i%2==0 else original)
                    page.wait_for_timeout(120)
            row=measure('relevant_parameter_edits',relevant)
            assert row['requests'].get('geometry',0)==8,row
            def navigate():
                for _ in range(5):
                    for name in ['响应曲线','配置管理','模型与设备','运行反馈','参数调校']:
                        page.get_by_role('navigation').get_by_role('button',name=name,exact=True).click()
                        page.evaluate('()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r)))')
            measure('navigation_25',navigate)
            if trial==2:
                expect(page.locator('.preview-readout')).to_be_visible()
                page.screenshot(path=str(out/(args.label+'.png')))
                if args.verify:
                    # Identical snapshots may be skipped; any displayed state change must arrive.
                    page.get_by_role('navigation').get_by_role('button',name='运行反馈',exact=True).click()
                    for frames in (60,90):
                        page.evaluate('(frames)=>probe.override={phase:"waiting_device",fusion:true,device:"状态刷新验证",rates:{vision_frames:frames,elapsed_ns:1e9}}',frames)
                        expect(page.locator('.fps-grid strong').first).to_have_text(f'{frames:.1f}')
                        expect(page.locator('.runtime-tile h2')).to_have_text('等待手柄')
                        expect(page.get_by_role('button',name='关闭 Fusion',exact=True)).to_be_enabled()
                    page.evaluate('probe.override={capture:{enabled:true,message:"采集状态刷新验证"}}')
                    page.get_by_role('navigation').get_by_role('button',name='模型与设备',exact=True).click()
                    page.get_by_role('button',name='手柄与诊断',exact=True).click()
                    expect(page.locator('#performance-capture')).to_be_checked()
                    expect(page.get_by_text('采集状态刷新验证',exact=True)).to_be_visible()
                    page.evaluate('probe.override=null')
                    expect(page.locator('#performance-capture')).not_to_be_checked()
            page.close()
        browser.close()
    result=dict(rows=rows,errors=errors)
    (out/(args.label+'.json')).write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(result,ensure_ascii=False))
    assert not errors,errors


if __name__=='__main__':main()
