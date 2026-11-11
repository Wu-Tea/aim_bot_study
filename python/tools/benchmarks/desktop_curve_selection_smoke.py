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
const r=await fetch('/rpc',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({command,payload})});const result=await r.json();if(!result.ok)console.error(command,result.error);return result;}}};"""
errors=[]
created='COD 线性 → COD 动态 '+str(time.time_ns())[-6:]
with sync_playwright() as p:
 browser=p.chromium.launch(channel='msedge',headless=True)
 page=browser.new_page(viewport={'width':1360,'height':1000})
 page.add_init_script(bridge);page.on('pageerror',lambda e:errors.append(str(e)));page.on('console',lambda m:print(m.text) if m.type=='error' else None)
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
 expect(page.get_by_role('checkbox',name='自动学习响应',exact=True)).to_have_count(0)
 expect(page.locator('.curve-workspace')).not_to_contain_text('减速区域')
 page.get_by_role('navigation').get_by_role('button',name='运行反馈',exact=True).click()
 table=page.get_by_role('table').filter(has_text='响应标定')
 expect(table).to_have_count(0)
 curves()
 expect(page.get_by_label('基准曲线 A',exact=True)).to_have_count(0)
 expect(page.get_by_label('目标曲线 B',exact=True)).to_have_count(0)
 inputs=page.get_by_label('用户输入转译曲线',exact=True)
 ai=page.get_by_label('AI 输出解算曲线',exact=True)
 expect(inputs.locator('option')).to_have_count(4)
 expect(ai.locator('option')).to_have_count(4)
 expect(inputs.locator('option[value="builtin:cod_standard_legacy.json"]')).to_have_text('COD 标准')
 expect(ai.locator('option[value="builtin:cod_standard_legacy.json"]')).to_have_text('COD 标准')
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
 expect(ai.locator('option:checked')).to_contain_text('COD 动态')
 expect(initial).to_have_value('8000')
 expect(inputs).to_be_enabled()
 for w,h in [(1348,928),(1360,1000),(1024,768),(800,640)]:
  page.set_viewport_size({'width':w,'height':h})
  assert not page.evaluate('document.documentElement.scrollWidth>innerWidth')
  if w==1348:
   # Include the save notice, as in the user's reported window layout.
   initial.fill('8100');page.get_by_role('button',name='保存配置',exact=True).click()
   expect(page.locator('.notice')).to_be_visible()
   page.screenshot(path=str(out/'settings-with-notice.png'),full_page=True)
   layout=page.locator('.workspace-fields').evaluate('(e)=>({height:e.clientHeight,content:e.scrollHeight})')
   assert layout['content']<=layout['height'],layout
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
 expect(ai.locator('option:checked')).to_contain_text('COD 动态')
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
 expect(ai.locator('option:checked')).to_contain_text('COD 动态')
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
 expect(inputs.locator('option:checked')).to_contain_text('COD 动态')
 # Upgrade an old profile snapshot without changing its effective mapping.
 legacy=next(p for p in dispatch('bootstrap')['profiles'] if p['id']==current_id)
 legacy['values'].update({'gamepad.output_transfer.custom_points':'0:0;1:1',
  'gamepad.output_transfer.curve_name':'旧版含死区配置',
  'gamepad.output_transfer.deadzone':.2,'gamepad.output_transfer.outer_deadzone':.1})
 dispatch('save',{**legacy,'apply':False})
 page.reload();expect(page.locator('.save-state')).to_have_text('已保存');curves()
 expect(page.locator('.curve-readout dd').first).to_have_text('55.00%')
 ai.select_option('builtin:linear.json')
 expect(page.locator('.curve-readout dd').first).to_have_text('55.00%')
 page.get_by_role('button',name='保存配置',exact=True).click()
 expect(page.locator('.save-state')).to_have_text('已保存')
 upgraded=next(p for p in dispatch('bootstrap')['profiles'] if p['id']==current_id)
 assert upgraded['values']['gamepad.output_transfer.deadzone']==0
 assert upgraded['values']['gamepad.output_transfer.output_start']==.2
 assert upgraded['values']['gamepad.output_transfer.output_end']==.1
 # Bootstrap issues fresh optimistic-write revisions; sync the test page too.
 page.reload();expect(page.locator('.save-state')).to_have_text('已保存');curves()
 # Deadzones belong to a library entry; preparation produces a standalone C.
 expect(page.get_by_role('spinbutton',name='起点死区',exact=True)).to_have_count(0)
 page.get_by_role('button',name='曲线库',exact=True).click()
 page.get_by_role('row').filter(has=page.get_by_role('cell',name='线性 1:1',exact=True)).get_by_role('button',name='查看 / 编辑').click()
 start=page.get_by_role('spinbutton',name='起点死区',exact=True)
 end=page.get_by_role('spinbutton',name='终点死区',exact=True)
 start.fill('20');end.fill('10')
 expect(page.get_by_label('选择控制点').locator('.select-current-label')).to_have_text('2：27% → 10%')
 expect(page.get_by_label('控制点输入 %',exact=True)).to_have_value('27')
 source_name='线性 · 起点20% / 终点10%'
 page.get_by_label('库中曲线名称',exact=True).fill(source_name)
 page.set_viewport_size({'width':1348,'height':928})
 page.screenshot(path=str(out/'library-deadzone-editor.png'),full_page=True)
 start.fill('90')
 expect(page.get_by_role('button',name='保存为副本',exact=True)).to_be_disabled()
 expect(page.get_by_role('alert')).to_contain_text('必须小于 100%')
 start.fill('20')
 page.get_by_role('button',name='保存为副本',exact=True).click()
 row=page.get_by_role('row').filter(has_text=source_name)
 expect(row).to_contain_text('20% / 10%')
 page.get_by_role('button',name='制作融合曲线',exact=True).click()
 page.get_by_label('基准曲线 A',exact=True).select_option(label=source_name+"（死区 20% / 10%）")
 page.get_by_label('目标曲线 B',exact=True).select_option('builtin:linear.json')
 page.get_by_role('button',name='计算转译曲线',exact=True).click()
 expect(page.locator('.curve-readout')).to_contain_text('55.00%')
 expect(page.locator('.curve-readout')).to_contain_text('0.000')
 page.screenshot(path=str(out/'library-deadzone-composition.png'),full_page=True)
 result_name='线性含死区 → 线性 1:1'
 page.get_by_label('计算结果名称',exact=True).fill(result_name)
 page.get_by_role('button',name='存入曲线库',exact=True).click()
 expect(page.get_by_role('row').filter(has_text=result_name)).to_be_visible()
 page.get_by_role('button',name='转译设置',exact=True).click()
 inputs.select_option(label=result_name)
 ai.select_option(label=source_name+"（死区 20% / 10%）")
 expect(page.locator('.curve-readout dd').nth(1)).to_have_text('42.86%')
 ai.select_option('builtin:linear.json')
 expect(page.locator('.curve-readout dd').nth(1)).to_have_text('50.00%')
 for probe,expected in [('0','0.00%'),('1','20.70%'),('10','27.00%'),('50','55.00%'),('100','90.00%')]:
  page.get_by_label('输入位置',exact=False).fill(probe)
  expect(page.locator('.curve-readout dd').first).to_have_text(expected)
 page.get_by_label('输入位置',exact=False).fill('50')
 page.get_by_role('button',name='保存配置',exact=True).click()
 expect(page.locator('.save-state')).to_have_text('已保存')
 page.reload();expect(page.locator('.save-state')).to_have_text('已保存');curves()
 expect(inputs.locator('option:checked')).to_have_text(result_name)
 expect(page.locator('.curve-readout dd').first).to_have_text('55.00%')
 persisted=next(p for p in dispatch('bootstrap')['profiles'] if p['id']==current_id)
 assert persisted['values']['gamepad.output_transfer.deadzone']==0
 assert persisted['values']['gamepad.output_transfer.output_start']==.2
 for w,h in [(1348,928),(1024,768),(800,640)]:
  page.set_viewport_size({'width':w,'height':h})
  assert not page.evaluate('document.documentElement.scrollWidth>innerWidth')
  if w==1348:assert page.locator('.workspace-fields').evaluate('(e)=>e.scrollHeight<=e.clientHeight')
  expect(start).to_have_count(0)
  page.screenshot(path=str(out/f'deadzones-{w}.png'),full_page=True)
 assert not errors,errors
 (out/'result.json').write_text(json.dumps({'passed':True,'checks':['library preparation and three curves','independent C and B','identity input with Dynamic AI','no runtime base selector or composition','snapshot deduplication','single global save state','saved versus actual running binding','restart pending','8000 initial response','four viewport sizes','curve library deadzone preparation, native projection and persistence','invalid endpoint ranges rejected','zero page errors']},ensure_ascii=False,indent=2),encoding='utf-8')
 print('PASS curve selection UI')
 browser.close()
