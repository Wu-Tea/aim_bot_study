"""Return paths against desktop_web_test_server's isolated, non-actuating data."""
import json
import time
from pathlib import Path
from playwright.sync_api import sync_playwright, expect
from project_paths import PROJECT_ROOT

OUT = PROJECT_ROOT / 'output/playwright/navigation'
BRIDGE = """window.pywebview={api:{dispatch:async(command,payload={})=>{
 // Simulate cancelling the native model file picker; do not depend on a model.
 if(command==='model') return {ok:true,data:null};
 const r=await fetch('/rpc',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({command,payload})});
 return await r.json();}}};"""


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    checks, errors = [], []
    with sync_playwright() as p:
        browser = p.chromium.launch(channel='msedge', headless=True)
        page = browser.new_page(viewport={'width':1348,'height':928})
        page.add_init_script(BRIDGE)
        page.on('pageerror', lambda e: errors.append(str(e)))
        page.goto('http://127.0.0.1:8877')
        expect(page.locator('.save-state')).to_have_text('已保存')
        nav = lambda name: page.get_by_role('navigation').get_by_role('button', name=name, exact=True).click()
        button = lambda name: page.get_by_role('button', name=name, exact=True)
        heading = lambda name: expect(page.get_by_role('heading', name=name, exact=True)).to_be_visible()

        nav('配置管理')
        button('编辑配置：交互验证').click()
        heading('参数调校')
        value = page.locator('[id="f-gamepad.ads.output_limit_x"]')
        old = value.input_value(); value.fill('47')
        button('返回配置管理').click()
        heading('配置管理')
        button('编辑配置：交互验证').click()
        expect(value).to_have_value('47'); value.fill(old)
        expect(button('打开观察窗口')).to_be_enabled()
        assert page.locator('.workspace-fields').evaluate('(e)=>e.scrollHeight<=e.clientHeight')
        page.screenshot(path=str(OUT/'profile-return.png'))
        button('返回配置管理').click()
        button('新建配置').click()
        dialog = page.get_by_role('dialog')
        dialog.get_by_role('textbox', name='名称').fill('返回流程验证' + str(time.time_ns())[-6:])
        dialog.get_by_role('button', name='创建配置', exact=True).click()
        heading('模型与设备'); button('返回配置管理').click(); heading('配置管理')
        checks.append('Profile edit and new-profile setup return to the list; drafts survive')

        nav('参数调校');button('持续跟随').click()
        page.locator('.top-actions').get_by_role('button',name='选择模型',exact=True).click()
        heading('模型与设备');button('返回参数调校').click()
        expect(page.locator('.sectionnav button.active')).to_have_text('持续跟随')
        nav('配置管理')
        page.get_by_role('button',name='编辑配置：',exact=False).last.click()
        page.locator('.top-actions').get_by_role('button',name='选择模型',exact=True).click()
        button('返回参数调校').click();button('返回配置管理').click();heading('配置管理')
        nav('参数调校')
        checks.append('Cancelled model selection returns to the originating tuning tab')

        button('首次瞄准').click()
        value = page.locator('[id="f-gamepad.ads.output_limit_x"]')
        old = value.input_value();value.fill('')
        nav('运行反馈');page.keyboard.press('Control+s')
        expect(value).to_be_focused();heading('参数调校')
        value.fill(old);button('返回运行反馈').click();heading('运行反馈')
        checks.append('Validation detour has a return to the page that requested saving')

        nav('响应曲线');button('曲线库').click();button('制作融合曲线').click()
        page.get_by_label('基准曲线 A',exact=True).select_option('builtin:cod_linear_legacy.json')
        page.get_by_label('目标曲线 B',exact=True).select_option('builtin:cod_dynamic_legacy.json')
        button('计算转译曲线').click()
        expect(page.locator('#translationCurve')).to_be_visible()
        path = page.locator('#translationCurve').get_attribute('d')
        page.get_by_label('计算结果名称',exact=True).fill('未保存的融合草稿')
        page.get_by_label('验证输入',exact=False).fill('35')
        button('返回曲线库').click();expect(page.get_by_role('table')).to_be_visible()
        nav('参数调校');nav('响应曲线')
        button('继续制作融合曲线').click()
        expect(page.get_by_label('计算结果名称',exact=True)).to_have_value('未保存的融合草稿')
        expect(page.get_by_label('验证输入',exact=False)).to_have_value('35')
        assert page.locator('#translationCurve').get_attribute('d') == path
        button('曲线库').click();expect(page.get_by_role('table')).to_be_visible()
        button('继续制作融合曲线').click()
        expect(page.locator('#translationCurve')).to_be_visible()
        page.screenshot(path=str(OUT/'composition-return.png'))
        checks.append('Fusion back button and library tab preserve inputs, result, name and probe across pages')

        nav('参数调校');button('首次瞄准').click();button('打开观察窗口').click()
        expect(page.get_by_role('dialog',name='范围观察')).to_be_visible()
        button('关闭观察窗口').click();expect(page.get_by_role('dialog')).to_have_count(0)
        expect(button('打开观察窗口')).to_be_focused()
        nav('运行反馈');button('查看运行日志').click()
        expect(dialog).to_be_visible();dialog.get_by_role('button',name='关闭',exact=True).click()
        expect(dialog).to_have_count(0);heading('运行反馈')
        for label in ['编辑完整配置…','重新载入已保存内容…','恢复初始参数…']:
            button('更多操作').click();page.get_by_role('menuitem',name=label,exact=True).click()
            expect(dialog).to_be_visible()
            if label == '编辑完整配置…': page.keyboard.press('Escape')
            else: dialog.get_by_role('button',name='取消',exact=True).click()
            expect(dialog).to_have_count(0);heading('运行反馈')
        nav('配置管理');button('新建配置').click();button('关闭对话框').click()
        expect(dialog).to_have_count(0);heading('配置管理')
        checks.append('Observation, logs, raw config, reload, reset and create dialogs all have working exits')
        # A short window makes the real profile list scroll without fake layout.
        page.set_viewport_size({'width':1024,'height':400})
        page.locator('.workspace-fields').evaluate('e=>e.scrollTop=e.scrollHeight')
        row = page.get_by_role('button',name='编辑配置：',exact=False).last
        row.scroll_into_view_if_needed()
        scroll = page.locator('.workspace-fields').evaluate('e=>e.scrollTop')
        row.click();button('返回配置管理').click()
        assert abs(page.locator('.workspace-fields').evaluate('e=>e.scrollTop')-scroll)<1
        checks.append('Returning from profile editing restores the list scroll position')
        assert not errors, errors
        (OUT/'results.json').write_text(json.dumps({'checks':checks,'page_errors':errors},ensure_ascii=False,indent=2),encoding='utf-8')
        print(json.dumps({'checks':checks,'page_errors':errors},ensure_ascii=False))
        browser.close()


if __name__ == '__main__':
    main()
