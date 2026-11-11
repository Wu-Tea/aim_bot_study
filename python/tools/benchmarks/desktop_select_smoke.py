"""Real picker visuals and keyboard behavior against the isolated web service."""
import json
from playwright.sync_api import sync_playwright, expect
from project_paths import PROJECT_ROOT

OUT=PROJECT_ROOT/'output/playwright/select'
BRIDGE="""window.pywebview={api:{dispatch:async(command,payload={})=>{
 const r=await fetch('/rpc',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({command,payload})});return r.json();}}};"""

with sync_playwright() as p:
    OUT.mkdir(parents=True,exist_ok=True)
    browser=p.chromium.launch(channel='msedge',headless=True)
    page=browser.new_page(viewport={'width':1348,'height':928});page.add_init_script(BRIDGE)
    errors=[];page.on('pageerror',lambda e:errors.append(str(e)))
    page.goto('http://127.0.0.1:8877');expect(page.locator('.save-state')).to_have_text('已保存')
    assert page.evaluate("CSS.supports('appearance','base-select')")
    page.get_by_role('button',name='开火与压枪',exact=True).click()
    select=page.locator('[id="f-gamepad.auto_fire.fire_output"]')
    assert select.evaluate("e=>getComputedStyle(e).appearance")=='base-select'
    select.click()
    assert select.evaluate("e=>e.matches(':open')")
    select.locator('option[value="RB"]').click();expect(select).to_have_value('RB')
    select.focus();page.keyboard.press('Space');page.keyboard.press('ArrowUp');page.keyboard.press('Enter')
    expect(select).to_have_value('RT')
    select.focus();page.keyboard.press('Space');page.keyboard.press('ArrowDown');page.keyboard.press('Escape')
    expect(select).to_have_value('RT');expect(select).to_be_focused()
    select.click();page.screenshot(path=str(OUT/'fire-open.png'))
    picker=select.evaluate("e=>{const s=getComputedStyle(e,'::picker(select)');return {background:s.backgroundColor,radius:s.borderRadius,maxHeight:s.maxHeight};}")
    assert picker['background']=='rgb(32, 34, 39)',picker
    page.get_by_role('heading',name='参数调校',exact=True).click()
    assert not select.evaluate("e=>e.matches(':open')")
    expect(select).to_have_value('RT')
    page.get_by_role('navigation').get_by_role('button',name='响应曲线',exact=True).click()
    page.get_by_label('AI 输出解算曲线',exact=True).click()
    page.screenshot(path=str(OUT/'curve-open.png'))
    page.keyboard.press('Escape')
    page.get_by_role('button',name='曲线库',exact=True).click()
    page.get_by_role('row').filter(has_text='Linear → Dynamic').first.get_by_role('button',name='查看 / 编辑').click()
    points=page.get_by_label('选择控制点',exact=True);points.click()
    assert len(points.locator('option').all())>20
    assert float(points.evaluate("e=>getComputedStyle(e,'::picker(select)').height").replace('px',''))<=320
    page.screenshot(path=str(OUT/'points-open.png'));page.keyboard.press('Escape')
    page.get_by_role('navigation').get_by_role('button',name='参数调校',exact=True).click()
    page.get_by_role('button',name='首次瞄准',exact=True).click()
    page.get_by_role('button',name='打开观察窗口',exact=True).click()
    dialog=page.get_by_role('dialog',name='范围观察')
    stage=dialog.get_by_label('观察阶段',exact=True);stage.click()
    stage.locator('option[value="follow"]').click();expect(stage).to_have_value('follow')
    page.keyboard.press('Escape');expect(dialog).to_have_count(0)
    page.emulate_media(forced_colors='active')
    page.get_by_role('button',name='开火与压枪',exact=True).click()
    assert select.evaluate('e=>getComputedStyle(e).appearance')=='auto'
    assert not errors,errors
    print(json.dumps({'passed':True,'picker':picker,'keyboard':True,'escape_and_outside':True,'long_list':True,'dialog':True,'forced_colors':True,'page_errors':errors}))
    browser.close()
