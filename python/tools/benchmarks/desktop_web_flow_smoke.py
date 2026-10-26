"""Configuration flow regression against desktop_web_test_server's temporary data."""
import json
from playwright.sync_api import sync_playwright, expect
from project_paths import PROJECT_ROOT

OUT = PROJECT_ROOT / 'output/playwright/config-flow'
BRIDGE = """window.pywebview={api:{dispatch:async(command,payload={})=>{
 const response=await fetch('/rpc',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({command,payload})});
 const result=await response.json();
 if(command==='bootstrap' && result.ok) window.testBootstrap=result.data;
 return result;
}}};"""


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    checks, calls, errors = [], [], []
    with sync_playwright() as p:
        browser=p.chromium.launch(channel='msedge',headless=True)
        page=browser.new_page(viewport={'width':1360,'height':900})
        page.add_init_script(BRIDGE)
        page.on('pageerror',lambda e:errors.append(str(e)))
        page.on('request',lambda r:calls.append(r.post_data_json) if r.method=='POST' and r.url.endswith('/rpc') else None)
        page.goto('http://127.0.0.1:8877')
        expect(page.locator('.save-state')).to_have_text('已保存')
        nav=lambda name:page.get_by_role('navigation').get_by_role('button',name=name,exact=True).click()
        save=page.get_by_role('button',name='保存配置',exact=True)
        expect(save).to_be_disabled()
        field=page.locator('[id="f-gamepad.ads.output_limit_x"]')
        original=field.input_value()
        field.fill('44')
        expect(save).to_be_enabled()
        expect(page.locator('.preview-readout')).to_be_visible()
        page.screenshot(path=str(OUT/'after-save-action.png'))
        save.click()
        expect(page.locator('.save-state')).to_have_text('已保存')
        assert not any(c['command']=='start' for c in calls)
        saved=[c for c in calls if c['command']=='save'][-1]
        assert saved['payload']['apply'] is False
        page.reload();expect(field).to_have_value('44')
        field.fill(original);save.click();expect(page.locator('.save-state')).to_have_text('已保存')
        checks.append('Visible idle save persists without a model, start or apply request')
        # A hidden invalid basic field must be revealed, focused and described.
        field.fill('')
        page.get_by_role('button',name='目标与识别',exact=True).click()
        before=len([c for c in calls if c['command']=='save'])
        save.click()
        expect(field).to_be_visible();expect(field).to_be_focused()
        expect(page.locator('.sectionnav button.active')).to_have_text('首次瞄准')
        expect(field).to_have_attribute('aria-invalid','true')
        assert field.get_attribute('aria-describedby')
        assert len([c for c in calls if c['command']=='save'])==before
        page.screenshot(path=str(OUT/'after-invalid-focus.png'))
        field.fill(original)
        checks.append('Save locates a hidden invalid field without sending an invalid save request')
        # Use the schema to exercise a collapsed advanced field and device page.
        page.get_by_role('button',name='输入与手动',exact=True).click()
        schema=page.evaluate('window.testBootstrap.fields')
        advanced=next(f for f in schema if f['group']=='手动输出补偿' and f['kind']=='float')
        advanced_input=page.locator('[id="f-'+advanced['path']+'"]')
        advanced_input.evaluate('e=>e.closest("details").open=true')
        old=advanced_input.input_value();advanced_input.fill('')
        nav('运行反馈');page.keyboard.press('Control+s')
        expect(advanced_input).to_be_visible();expect(advanced_input).to_be_focused()
        expect(page.locator('.sectionnav button.active')).to_have_text('输入与手动')
        advanced_input.fill(old)
        nav('模型与设备')
        width=page.get_by_role('spinbutton',name='捕获宽度',exact=True)
        old_width=width.input_value();width.fill('1')
        nav('参数调校');save.click()
        expect(width).to_be_visible();expect(width).to_be_focused()
        width.fill(old_width)
        checks.append('Error recovery opens folded advanced fields and switches to device settings')
        nav('响应曲线')
        response=page.get_by_role('spinbutton',name='控制点响应 %',exact=True)
        response.fill('12')
        for label in ['线性','动态','自定义','撤销曲线修改','重做曲线修改']:
            expect(page.get_by_role('button',name=label,exact=True)).to_be_disabled()
        expect(page.locator('.curve-toolbar').get_by_role('button',name='更多操作')).to_be_disabled()
        nav('参数调校');save.click()
        expect(response).to_be_visible();expect(response).to_be_focused();expect(response).to_have_value('12')
        page.screenshot(path=str(OUT/'after-pending-curve.png'))
        page.get_by_role('button',name='应用点位',exact=True).click()
        expect(page.get_by_role('button',name='动态',exact=True)).to_be_enabled()
        expect(page.get_by_role('button',name='撤销曲线修改',exact=True)).to_be_enabled()
        page.get_by_role('button',name='撤销曲线修改',exact=True).click()
        expect(response).to_have_value('10')
        checks.append('Pending point edits survive navigation and block destructive curve actions until resolved')
        nav('配置管理')
        page.get_by_role('button',name='新建配置',exact=True).click()
        dialog=page.get_by_role('dialog')
        dialog.get_by_role('textbox',name='名称').fill('流程验证副本')
        dialog.get_by_role('button',name='确定',exact=True).click()
        expect(dialog).to_have_count(0)
        nav('配置管理')
        page.get_by_role('button',name='编辑配置：交互验证',exact=True).click()
        expect(page.get_by_role('heading',name='参数调校',exact=True)).to_be_visible()
        page.get_by_role('button',name='首次瞄准',exact=True).click();field.fill('42')
        nav('配置管理')
        page.get_by_role('button',name='编辑配置：流程验证副本',exact=True).click()
        expect(field).to_have_value('100')
        nav('配置管理');page.get_by_role('button',name='编辑配置：交互验证',exact=True).click()
        expect(field).to_have_value('42')
        field.fill(original)
        checks.append('Each profile opens directly in editing and retains its independent draft')
        # Overlay a running status for UI-only ownership cases. Never start an actuator.
        active_id=page.locator('.profile-select').input_value()
        page.add_init_script('''const original=window.pywebview.api.dispatch;
        window.pywebview.api.dispatch=async(command,payload)=>{
          const result=await original(command,payload);
          if(command==='status' && result.ok) {
            result.data.phase='running';result.data.record={profile_id:ACTIVE,process_id:123};
          }
          return result;
        };'''.replace('ACTIVE',json.dumps(active_id)))
        page.reload()
        expect(page.get_by_role('button',name='保存并应用',exact=True)).to_be_visible()
        expect(save).to_have_count(0)
        other_id=page.locator('.profile-select option').evaluate_all('(es)=>es.find(e=>e.value!=='+json.dumps(active_id)+').value')
        page.locator('.profile-select').select_option(other_id)
        expect(save).to_be_visible()
        expect(page.get_by_role('button',name='切换并启动',exact=True)).to_be_visible()
        expect(page.locator('.run-state')).to_have_text('其他配置运行中')
        page.set_viewport_size({'width':840,'height':640})
        expect(page.locator('.preview-readout')).to_be_visible()
        assert page.locator('.top-actions').evaluate('(e)=>e.getBoundingClientRect().right<=innerWidth')
        page.screenshot(path=str(OUT/'after-other-profile-840.png'))
        checks.append('Running profile keeps Apply; editing another profile exposes Save without switching runtime')
        assert not errors,errors
        result={'checks':checks,'page_errors':errors}
        (OUT/'results.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
        print(json.dumps(result,ensure_ascii=False))
        browser.close()


if __name__=='__main__':main()
