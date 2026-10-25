"""Playwright regression for the isolated desktop_web_test_server adapter only."""
import json
from pathlib import Path
from playwright.sync_api import sync_playwright, expect
from project_paths import PROJECT_ROOT

OUT = PROJECT_ROOT / 'output/playwright/preview-controls'
BRIDGE = """window.pywebview={api:{dispatch:async(command,payload={})=>{
 const r=await fetch('/rpc',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({command,payload})});
 return r.json();
}}};"""


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    checks, errors, calls = [], [], []
    with sync_playwright() as p:
        browser = p.chromium.launch(channel='msedge', headless=True)
        page = browser.new_page(viewport={'width':1360, 'height':900})
        page.add_init_script(BRIDGE)
        page.on('pageerror', lambda e: errors.append(str(e)))
        page.on('request', lambda r: calls.append(r.post_data_json['command']) if r.method == 'POST' and r.url.endswith('/rpc') else None)
        page.goto('http://127.0.0.1:8877')
        expect(page.locator('.profile-select option:checked')).to_have_text('交互验证')
        expect(page.locator('.preview-readout')).to_be_visible()
        page.get_by_role('button',name='目标与识别',exact=True).click()
        enemy = page.get_by_role('textbox',name='敌人标识颜色',exact=True)
        picker = page.get_by_label('敌人标识颜色色盘',exact=True)
        expect(picker).to_have_value('#ff00ff')
        enemy.fill('00ff00')
        expect(picker).to_have_value('#00ff00')
        enemy.fill('#oops')
        expect(enemy).to_have_attribute('aria-invalid','true')
        expect(enemy).to_have_value('#oops')
        picker.fill('#ff00ff')
        expect(enemy).to_have_value('#FF00FF')
        expect(enemy).to_have_attribute('aria-invalid','false')
        friend = page.get_by_label('友军标识颜色色盘',exact=True)
        friend.fill('#0080ff')
        expect(page.get_by_role('textbox',name='友军标识颜色',exact=True)).to_have_value('#0080FF')
        friend.fill('#00ffff')
        expect(page.locator('.save-state')).to_have_text('已保存')
        page.screenshot(path=str(OUT/'palette.png'))
        checks.append('Both color inputs synchronize with HEX; invalid draft is preserved and recoverable')
        page.get_by_role('button',name='持续跟随',exact=True).click()
        expect(page.locator('.preview-readout')).to_be_visible()
        svg = page.locator('.preview-surface svg')
        target = svg.locator('[data-target]')
        def point(loc):
            return loc.evaluate('(e)=>[Number(e.getAttribute("cx")),Number(e.getAttribute("cy"))]')
        before = point(target)
        svg.click(position={'x':25,'y':25})
        assert point(target) == before, (before,point(target))
        box=svg.bounding_box()
        scale=svg.evaluate('(e)=>e.viewBox.baseVal.width/e.getBoundingClientRect().width')
        x,y=box['x']+25,box['y']+25
        page.mouse.move(x,y); page.mouse.down()
        assert point(target)==before
        page.mouse.move(x+20,y+12,steps=4); page.mouse.up()
        after=point(target)
        assert abs(after[0]-before[0]-20*scale)<.001 and abs(after[1]-before[1]-12*scale)<.001,(before,after,scale)
        page.mouse.move(x+50,y+50)
        assert point(target)==after
        svg.focus(); page.keyboard.press('ArrowLeft')
        assert abs(point(target)[0]-(after[0]-1))<.001
        expect(page.locator('.save-state')).to_have_text('已保存')
        checks.append('Click does not move target; drag preserves pointer offset; release and keyboard work')
        page.get_by_role('button',name='中距',exact=True).click()
        page.wait_for_timeout(250)  # Existing geometry debounce + local bridge completion.
        count=calls.count('geometry')
        opener=page.get_by_role('button',name='打开观察窗口',exact=True)
        opener.click()
        dialog=page.get_by_role('dialog',name='范围观察')
        expect(dialog).to_be_visible()
        observation=dialog.locator('svg[role="img"]')
        expect(observation).to_have_attribute('viewBox','-320 -256 640 512')
        page.wait_for_timeout(250)
        assert calls.count('geometry')==count, 'Opening a second view must not duplicate geometry work'
        for stage in ['acquire','follow']:
            assert observation.locator(f'[data-range="{stage}"]').get_attribute('r') == svg.locator(f'[data-range="{stage}"]').get_attribute('r')
        for width,height in [(1360,900),(1024,768),(840,640),(824,600)]:
            page.set_viewport_size({'width':width,'height':height})
            page.wait_for_timeout(100)
            shape=observation.evaluate('''e=>{const r=e.getBoundingClientRect(),m=e.getScreenCTM();
              return {ratio:r.width/r.height,sx:m.a,sy:m.d,centerX:m.e,centerY:m.f,left:r.left,top:r.top,width:r.width,height:r.height};}''')
            assert abs(shape['sx']-shape['sy'])<1e-6,shape
            assert abs(shape['ratio']-1.25)<.004,shape
            assert abs(shape['centerX']-shape['left']-shape['width']/2)<.01,shape
            assert abs(shape['centerY']-shape['top']-shape['height']/2)<.01,shape
            bounds=dialog.bounding_box()
            assert bounds['x']>=0 and bounds['y']>=0 and bounds['x']+bounds['width']<=width and bounds['y']+bounds['height']<=height,bounds
            page.screenshot(path=str(OUT/f'observation-{width}.png'))
        checks.append('Observation uses capture coordinates, equal X/Y scale and centered crosshair at four viewport sizes')
        page.set_viewport_size({'width':1360,'height':900})
        dialog.get_by_role('button',name='到点',exact=True).click()
        assert point(observation.locator('[data-target]'))==[0,0]
        dialog.get_by_role('combobox',name='观察阶段').select_option('acquire')
        dialog.get_by_role('button',name='近距',exact=True).click()
        assert point(observation.locator('[data-target]'))[0]>0
        page.keyboard.press('Escape')
        expect(dialog).to_have_count(0)
        expect(opener).to_be_focused()
        assert point(target)[0]>0
        expect(page.locator('.save-state')).to_have_text('已保存')
        checks.append('Presets and stage changes share state; Escape returns focus without changing the profile')
        page.get_by_role('navigation').get_by_role('button',name='模型与设备',exact=True).click()
        page.get_by_role('spinbutton',name='捕获宽度',exact=True).fill('960')
        page.get_by_role('spinbutton',name='捕获高度',exact=True).fill('540')
        page.get_by_role('navigation').get_by_role('button',name='参数调校',exact=True).click()
        expect(page.locator('.preview-readout')).to_be_visible()
        opener.click()
        expect(page.locator('.observation-surface svg')).to_have_attribute('viewBox','-480 -270 960 540')
        page.screenshot(path=str(OUT/'observation-960x540.png'))
        checks.append('Capture dimensions follow the draft rather than a hard-coded 640x512')
        assert not errors,errors
        result={'checks':checks,'page_errors':errors,'geometry_calls':calls.count('geometry')}
        (OUT/'results.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
        print(json.dumps(result,ensure_ascii=False))
        browser.close()


if __name__=='__main__': main()
