"""Run against a fresh desktop_web_test_server: isolated data, no actuator."""
import json
import time
from pathlib import Path
from project_paths import PROJECT_ROOT
from playwright.sync_api import sync_playwright, expect
out=PROJECT_ROOT/'output/playwright/curve-selection'
out.mkdir(parents=True,exist_ok=True)
bridge="""window.rpcCalls=[]; window.pywebview={api:{dispatch:async(command,payload={})=>{
window.rpcCalls.push(command);
const r=await fetch('/rpc',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({command,payload})});return await r.json();}}};"""
errors=[]
created='Linear → Dynamic '+str(time.time_ns())[-6:]
with sync_playwright() as p:
 browser=p.chromium.launch(channel='msedge',headless=True)
 page=browser.new_page(viewport={'width':1360,'height':1000})
 page.add_init_script(bridge);page.on('pageerror',lambda e:errors.append(str(e)))
 page.goto('http://127.0.0.1:8877')
 expect(page.locator('.save-state')).to_have_text('已保存')
 def curves(): page.get_by_role('navigation').get_by_role('button',name='响应曲线',exact=True).click()
 def dispatch(command,payload=None):
  result=page.evaluate('([command,payload])=>window.pywebview.api.dispatch(command,payload)',[command,payload or {}])
  assert result['ok'],result
  return result['data']
 curves()
 expect(page.get_by_role('spinbutton',name='腰射（Hipfire）',exact=True)).to_be_visible()
 expect(page.get_by_role('spinbutton',name='开镜（ADS）',exact=True)).to_be_visible()
 expect(page.locator('.curve-prior-grid input')).to_have_count(2)
 expect(page.get_by_role('checkbox',name='自动学习响应',exact=True)).to_be_checked()
 expect(page.locator('.curve-workspace')).not_to_contain_text('减速区域')
 page.get_by_role('navigation').get_by_role('button',name='运行反馈',exact=True).click()
 table=page.get_by_role('table').filter(has_text='响应标定')
 expect(table.get_by_role('row')).to_have_count(3)
 expect(table).to_contain_text('腰射（Hipfire）')
 expect(table).to_contain_text('开镜（ADS）')
 expect(table).not_to_contain_text('减速')
 curves()
 expect(page.get_by_label('基准曲线 A',exact=True)).to_have_count(0)
 expect(page.get_by_label('目标曲线 B',exact=True)).to_have_count(0)
 inputs=page.get_by_label('用户输入转译曲线',exact=True)
 ai=page.get_by_label('AI 输出解算曲线',exact=True)
 expect(inputs.locator('option')).to_have_count(3)
 expect(ai.locator('option')).to_have_count(3)
 assert 'curve_compose' not in page.evaluate('window.rpcCalls')
 page.get_by_role('button',name='曲线库',exact=True).click()
 page.get_by_role('button',name='制作融合曲线',exact=True).click()
 page.get_by_label('基准曲线 A',exact=True).select_option('builtin:cod_linear_legacy.json')
 page.get_by_label('目标曲线 B',exact=True).select_option('builtin:cod_dynamic_legacy.json')
 page.get_by_role('button',name='计算转译曲线',exact=True).click()
 for key in ['baseCurve','targetCurve','translationCurve']: expect(page.locator('#'+key)).to_be_visible()
 expect(page.locator('.curve-readout')).to_contain_text('46.14%')
 page.get_by_role('textbox',name='计算结果名称',exact=True).fill(created)
 page.screenshot(path=str(out/'preparation.png'),full_page=True)
 page.get_by_role('button',name='存入曲线库',exact=True).click()
 expect(page.get_by_role('row').filter(has_text=created)).to_be_visible()
 page.get_by_role('button',name='转译设置',exact=True).click()
 inputs.select_option(label=created)
 ai.select_option('builtin:cod_dynamic_legacy.json')
 expect(page.get_by_role('checkbox',name='转译用户输入',exact=True)).to_have_count(0)
 expect(page.locator('#inputPreviewCurve')).to_be_visible()
 expect(page.locator('#aiPreviewCurve')).to_be_visible()
 expect(page.locator('.curve-preview-values')).to_contain_text('46.14%')
 expect(page.locator('.curve-preview-values')).to_contain_text('21.03%')
 preview_before=page.locator('#aiPreviewCurve').get_attribute('d')
 page.get_by_label('输入位置',exact=False).fill('25')
 expect(page.locator('.curve-preview-values output')).to_have_text('25%')
 page.get_by_label('输入位置',exact=False).fill('50')
 initial=page.get_by_role('spinbutton',name='腰射（Hipfire）',exact=True)
 initial.fill('8000'); assert initial.get_attribute('max') is None
 expect(page.locator('.save-state')).to_have_text('未保存')
 expect(page.locator('.save-state')).to_have_text('未保存')
 page.get_by_role('button',name='保存配置',exact=True).click()
 expect(page.locator('.save-state')).to_have_text('已保存')
 page.reload();expect(page.locator('.save-state')).to_have_text('已保存');curves()
 expect(inputs.locator('option:checked')).to_have_text(created)
 expect(ai.locator('option:checked')).to_contain_text('Dynamic')
 expect(initial).to_have_value('8000')
 expect(inputs).to_be_enabled()
 for w,h in [(1348,928),(1360,1000),(1024,768),(800,640)]:
  page.set_viewport_size({'width':w,'height':h})
  assert not page.evaluate('document.documentElement.scrollWidth>innerWidth')
  if w==1348:
   # Include the save notice, as in the user's reported window layout.
   initial.fill('8100');page.get_by_role('button',name='保存配置',exact=True).click()
   expect(page.locator('.notice')).to_be_visible()
   assert page.locator('.workspace-fields').evaluate('(e)=>e.scrollHeight<=e.clientHeight')
   assert initial.bounding_box()['y']<inputs.bounding_box()['y']<page.locator('#aiPreviewCurve').bounding_box()['y']
   expect(page.get_by_role('button',name='保存曲线设置',exact=True)).to_have_count(0)
   expect(page.get_by_role('checkbox',name='转译用户输入',exact=True)).to_have_count(0)
  page.screenshot(path=str(out/f'settings-{w}.png'),full_page=True)
 page.set_viewport_size({'width':1360,'height':1000})
 # Start the existing non-actuating runtime double through the real service.
 profile=dispatch('bootstrap')['profiles'][0]
 profile['values']['runtime.vision.model_path']='test.engine'
 result=dispatch('start',profile)
 assert 'error' not in result,result
 page.reload();expect(page.locator('.save-state')).to_have_text('已保存');curves()
 expect(page.locator('.run-state')).to_have_text('运行中')
 inputs.select_option('builtin:linear.json')
 expect(ai.locator('option:checked')).to_contain_text('Dynamic')
 expect(page.locator('.curve-preview-values')).to_contain_text('50.00%')
 assert page.locator('#aiPreviewCurve').get_attribute('d')==preview_before
 expect(page.locator('.save-state')).to_have_text('未保存')
 page.get_by_role('button',name='保存并应用',exact=True).click()
 expect(page.locator('.run-state')).to_have_text('运行中 · 待重启')
 page.reload();expect(page.locator('.save-state')).to_have_text('已保存');curves()
 expect(page.locator('.run-state')).to_have_text('运行中 · 待重启')
 expect(page.get_by_role('button',name='保存并重启',exact=True)).to_be_visible()
 page.screenshot(path=str(out/'restart-pending.png'),full_page=True)
 # No composition is performed by either independent runtime selector.
 assert 'curve_compose' not in page.evaluate('window.rpcCalls')
 page.get_by_role('button',name='保存并重启',exact=True).click()
 page.get_by_role('dialog').get_by_role('button',name='保存并重启',exact=True).click()
 expect(page.locator('.run-state')).to_have_text('运行中 · 已应用')
 page.screenshot(path=str(out/'running-confirmed.png'),full_page=True)
 dispatch('stop')
 page.reload();expect(page.locator('.save-state')).to_have_text('已保存');curves()
 expect(inputs.locator('option:checked')).to_contain_text('1:1')
 expect(ai.locator('option:checked')).to_contain_text('Dynamic')
 expect(page.get_by_role('checkbox',name='转译用户输入',exact=True)).to_have_count(0)
 # Legacy opt-out must display its effective 1:1 behavior, not a dormant LUT.
 current_id=page.locator('.profile-select').input_value()
 legacy=next(p for p in dispatch('bootstrap')['profiles'] if p['id']==current_id)
 legacy['values']['gamepad.follow_response_curve']=False
 legacy['values']['gamepad.output_transfer.custom_points']=';'.join(':'.join(str(v) for v in point) for point in legacy['curve']['definition']['points'])
 legacy['values']['gamepad.output_transfer.curve_name']='旧配置未启用的曲线'
 dispatch('save',{**legacy,'apply':False})
 page.reload();expect(page.locator('.save-state')).to_have_text('已保存');curves()
 expect(inputs.locator('option:checked')).to_contain_text('1:1')
 expect(page.locator('.curve-preview-values')).to_contain_text('50.00%')
 assert 'save' not in page.evaluate('window.rpcCalls')
 inputs.select_option('builtin:cod_dynamic_legacy.json')
 expect(page.locator('.curve-preview-values .curve-readout dd').first).to_have_text('21.03%')
 page.get_by_role('button',name='保存配置',exact=True).click()
 expect(page.locator('.save-state')).to_have_text('已保存')
 persisted=next(p for p in dispatch('bootstrap')['profiles'] if p['id']==current_id)
 assert persisted['values']['gamepad.follow_response_curve'] is True
 page.reload();expect(page.locator('.save-state')).to_have_text('已保存');curves()
 expect(inputs.locator('option:checked')).to_contain_text('Dynamic')
 assert not errors,errors
 (out/'result.json').write_text(json.dumps({'passed':True,'checks':['library preparation and three curves','independent C and B','identity input with Dynamic AI','no runtime base selector or composition','snapshot deduplication','single global save state','saved versus actual running binding','restart pending','8000 initial response','four viewport sizes','zero page errors']},ensure_ascii=False,indent=2),encoding='utf-8')
 print('PASS curve selection UI')
 browser.close()
