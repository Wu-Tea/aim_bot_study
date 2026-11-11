"""Save/cancel/failure recovery through the isolated desktop service, no actuator.

RPC fault injection exercises UI boundary responses without touching user data.
"""
import json
import time
from playwright.sync_api import sync_playwright, expect
from project_paths import PROJECT_ROOT

OUT = PROJECT_ROOT / 'output/playwright/interaction'
BRIDGE = """window.calls=[];window.pywebview={api:{dispatch:async(command,payload={})=>{
 window.calls.push(command);
 if(command==='window_close')return {ok:true,data:true};
 if(window.failNext===command){window.failNext=null;return {ok:false,error:'测试：写入失败，请重试'};}
 if(window.cancelNext===command){window.cancelNext=null;return {ok:true,data:null};}
 const r=await fetch('/rpc',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({command,payload})});
 const result=await r.json();
 if(command==='save' && window.delayApply && result.ok){window.delayApply=false;window.pendingReload=true;result.data.reload={status:1,request_id:771};}
 if(command==='status' && window.pendingReload && result.ok)result.data.learning={...result.data.learning,request_id:771,status:window.finishReload?2:1};
 if(command==='bootstrap' && result.ok) window.savedProfiles=result.data.profiles;
 if(result.ok && result.data?.profile) window.savedProfiles=window.savedProfiles.map(p=>p.id===result.data.profile.id?result.data.profile:p);
 if(command==='save' && window.failApply && result.ok){window.failApply=false;delete result.data.reload;result.data.error='配置已保存，尚未应用：测试热加载失败';}
 return result;}}};"""


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    checks, errors = [], []
    with sync_playwright() as p:
        browser=p.chromium.launch(channel='msedge',headless=True)
        page=browser.new_page(viewport={'width':1348,'height':928})
        page.add_init_script(BRIDGE);page.on('pageerror',lambda e:errors.append(str(e)))
        page.goto('http://127.0.0.1:8877');expect(page.locator('.save-state')).to_have_text('已保存')
        btn=lambda name:page.get_by_role('button',name=name,exact=True)
        nav=lambda name:page.get_by_role('navigation').get_by_role('button',name=name,exact=True).click()
        def rpc(command,payload=None):
            result=page.evaluate('([c,p])=>window.pywebview.api.dispatch(c,p)',[command,payload or {}])
            assert result['ok'],result
            return result['data']
        def profile():
            selected=page.locator('.profile-select').input_value()
            # Bootstrap rotates editing revisions; inspect acknowledged writes
            # without issuing a second bootstrap behind the live editor.
            return page.evaluate('id=>window.savedProfiles.find(p=>p.id===id)', selected)
        def menu(label):
            page.locator('.top-actions').get_by_role('button',name='更多操作').click()
            page.get_by_role('menuitem',name=label,exact=True).click()
        def close_warning():
            page.evaluate('window.calls=[]');btn('关闭窗口').click()
            expect(page.get_by_role('dialog',name='关闭工作空间')).to_be_visible()
            assert 'window_close' not in page.evaluate('window.calls')
            page.get_by_role('dialog').get_by_role('button',name='取消',exact=True).click()

        fresh=rpc('create',{'name':'闭环配置-'+str(time.time_ns())[-6:]})
        rpc('select',{'id':fresh['id']});page.reload()
        expect(page.locator('.save-state')).to_have_text('已保存')

        page.evaluate("window.failNext='curve_library'");nav('响应曲线')
        expect(btn('重新读取曲线库')).to_be_visible();btn('重新读取曲线库').click()
        expect(page.get_by_label('用户输入转译曲线',exact=True)).to_be_enabled()
        btn('曲线库').click();btn('新建曲线').click()
        name='交互闭环-'+str(time.time_ns())[-6:]
        field=page.get_by_label('库中曲线名称',exact=True);field.fill(name)
        close_warning()
        page.get_by_label('选择控制点',exact=True).select_option('0');btn('插入点').click()
        point=page.get_by_label('控制点响应 %',exact=True);point.fill('35')
        page.evaluate('window.calls=[]');page.keyboard.press('Control+s')
        expect(page.locator('.curve-status')).to_contain_text('请先应用或取消')
        assert not set(page.evaluate('window.calls')) & {'save','curve_library_save'}
        expect(point).to_have_value('35');btn('取消点位编辑').click();expect(point).to_have_value('50')
        page.evaluate("window.failNext='curve_library_save'");page.keyboard.press('Control+s')
        expect(page.locator('.notice')).to_contain_text('写入失败');expect(field).to_have_value(name)
        page.evaluate('window.calls=[]');page.keyboard.press('Control+s')
        row=page.get_by_role('row').filter(has_text=name);expect(row).to_be_visible()
        expect(page.locator('.notice.error')).to_have_count(0)
        assert 'curve_library_save' in page.evaluate('window.calls') and 'save' not in page.evaluate('window.calls')
        assert any(x['name']==name for x in rpc('curve_library')['entries'])
        row.get_by_role('button',name='查看 / 编辑').click();field.fill(name+'-草稿')
        btn('放弃修改').click();btn('保留修改').click();expect(field).to_have_value(name+'-草稿')
        btn('返回曲线库').click();nav('运行反馈');close_warning();nav('响应曲线')
        btn('继续编辑').click();expect(field).to_have_value(name+'-草稿')
        btn('放弃修改').click();btn('确认放弃').click()
        expect(row).to_be_visible();row.get_by_role('button',name='查看 / 编辑').click();expect(field).to_have_value(name)
        page.evaluate("window.cancelNext='curve_export'");btn('导出…').click()
        expect(page.locator('.notice')).to_have_text('已取消导出')
        btn('导出…').click();expect(page.locator('.notice')).to_contain_text('已导出：')
        btn('移入归档').click();btn('取消').click();expect(field).to_have_value(name)
        btn('移入归档').click();page.evaluate("window.failNext='curve_library_archive'");btn('确认归档').click()
        expect(field).to_have_value(name);expect(btn('确认归档')).to_be_visible()
        btn('确认归档').click();expect(row).to_have_count(0)
        expect(page.locator('.curve-status')).to_have_text('已归档“'+name+'”')
        page.evaluate("window.cancelNext='curve_import'");btn('导入曲线…').click()
        expect(page.locator('.curve-status')).to_have_text('已取消导入')
        btn('导入曲线…').click();expect(field).to_be_visible()
        imported_name=field.input_value()
        btn('返回曲线库').click();expect(btn('继续编辑')).to_be_visible();close_warning()
        btn('继续编辑').click();expect(field).to_have_value(imported_name)
        field.fill(name+'-导入');page.keyboard.press('Control+s')
        expect(page.get_by_role('row').filter(has_text=name+'-导入')).to_be_visible()
        checks.append('Curve shortcut targets library; failed save/archive retain edits; retry persists; discard and export cancellation are explicit')

        btn('制作融合曲线').click()
        page.get_by_label('基准曲线 A',exact=True).select_option('builtin:cod_linear_legacy.json')
        close_warning();btn('放弃制作').click();btn('保留修改').click()
        expect(page.get_by_label('基准曲线 A',exact=True)).to_have_value('builtin:cod_linear_legacy.json')
        btn('放弃制作').click();btn('确认放弃').click();expect(btn('制作融合曲线')).to_be_visible()
        checks.append('Partial fusion drafts are protected on close and can explicitly be discarded')
        print('PASS library save/cancel/failure paths',flush=True)

        nav('参数调校');btn('首次瞄准').click()
        value=page.locator('[id="f-gamepad.ads.output_limit_x"]');value.fill('47')
        page.evaluate("window.failNext='save'");btn('保存配置').click()
        expect(page.locator('.notice')).to_contain_text('写入失败');expect(value).to_have_value('47')
        expect(page.locator('.save-state')).to_have_text('未保存')
        btn('保存配置').click();expect(page.locator('.save-state')).to_have_text('已保存')
        assert profile()['values']['gamepad.ads.output_limit_x']==0.47
        value.fill('49');menu('重新载入已保存内容…')
        page.get_by_role('dialog').get_by_role('button',name='取消',exact=True).click();expect(value).to_have_value('49')
        menu('重新载入已保存内容…');page.get_by_role('dialog').get_by_role('button',name='放弃修改并重新载入',exact=True).click()
        expect(value).to_have_value('47');expect(page.locator('.notice')).to_contain_text('已重新载入')
        menu('恢复初始参数…');page.get_by_role('dialog').get_by_role('button',name='载入初始参数',exact=True).click()
        expect(value).to_have_value('100');expect(page.locator('.notice')).to_contain_text('草稿')
        assert profile()['values']['gamepad.ads.output_limit_x']==0.47
        value.fill('47')
        checks.append('Profile save failure preserves input; retry writes disk; reload and reset distinguish disk from draft')
        print('PASS profile save/reset paths',flush=True)

        menu('编辑完整配置…');dialog=page.get_by_role('dialog');expect(dialog).to_be_visible();text=dialog.locator('textarea')
        original=text.input_value();text.fill(original+'\ninvalid = [\n')
        dialog.get_by_role('button',name='校验并载入草稿',exact=True).click()
        expect(dialog).to_be_visible();expect(text).to_have_value(original+'\ninvalid = [\n')
        expect(dialog.locator('.inline-error')).to_be_visible()
        expect(dialog.get_by_role('button',name='校验并载入草稿',exact=True)).to_be_enabled()
        page.keyboard.press('Escape');expect(dialog.get_by_role('button',name='放弃修改并返回')).to_be_visible()
        dialog.get_by_role('button',name='继续编辑').click();expect(text).to_have_value(original+'\ninvalid = [\n')
        btn('关闭对话框').click();dialog.get_by_role('button',name='放弃修改并返回').click()
        expect(dialog).to_have_count(0);expect(value).to_have_value('47')
        menu('编辑完整配置…');text=dialog.locator('textarea')
        original=text.input_value();assert 'output_limit_x = 0.47' in original
        text.fill(original.replace('output_limit_x = 0.47','output_limit_x = 0.48',1))
        dialog.get_by_role('button',name='校验并载入草稿',exact=True).click()
        expect(dialog).to_have_count(0);expect(value).to_have_value('48')
        assert profile()['values']['gamepad.ads.output_limit_x']==0.47
        btn('保存配置').click();expect(page.locator('.save-state')).to_have_text('已保存')
        assert profile()['values']['gamepad.ads.output_limit_x']==0.48
        checks.append('Raw-config validation failure is recoverable; cancel protects edits; Apply updates draft and Save persists it')

        nav('配置管理')
        def profile_menu(label):
            page.locator('.workspace-fields').get_by_role('button',name='更多操作').click()
            page.get_by_role('menuitem',name=label,exact=True).click()
        profile_menu('重命名…')
        renamed='重命名-'+str(time.time_ns())[-6:]
        dialog.get_by_role('textbox',name='名称').fill(renamed)
        page.evaluate("window.failNext='rename'");dialog.get_by_role('button',name='保存名称',exact=True).click()
        expect(dialog.locator('.inline-error')).to_contain_text('写入失败')
        expect(dialog.get_by_role('textbox',name='名称')).to_have_value(renamed)
        dialog.get_by_role('button',name='保存名称',exact=True).click()
        expect(dialog).to_have_count(0);expect(page.locator('.notice')).to_contain_text('已重命名')
        profile_menu('复制当前草稿…');dialog.get_by_role('button',name='取消',exact=True).click()
        expect(page.locator('.profile-select option:checked')).to_have_text(renamed)
        profile_menu('复制当前草稿…');dialog.get_by_role('button',name='创建副本',exact=True).click()
        expect(page.locator('.notice')).to_contain_text('已创建副本');btn('返回配置管理').click()
        page.evaluate("window.cancelNext='export_profile'");profile_menu('导出当前草稿…')
        expect(page.locator('.notice')).to_have_text('已取消导出')
        profile_menu('导出当前草稿…');expect(page.locator('.notice')).to_contain_text('已导出：')
        page.evaluate("window.cancelNext='import_choose'");btn('导入配置').click()
        expect(page.locator('.notice')).to_have_text('已取消导入')
        btn('导入配置').click();expect(dialog).to_be_visible()
        dialog.get_by_role('textbox',name='名称').fill(renamed+'-导入')
        dialog.get_by_role('button',name='导入配置',exact=True).click()
        expect(page.locator('.notice')).to_contain_text('已导入配置');btn('返回配置管理').click()
        imported_id=page.locator('.profile-select').input_value()
        profile_menu('删除当前配置…');dialog.get_by_role('button',name='取消',exact=True).click()
        expect(page.locator('.profile-select')).to_have_value(imported_id)
        profile_menu('删除当前配置…');dialog.get_by_role('button',name='删除并保留备份',exact=True).click()
        expect(page.locator('.profile-select option[value="'+imported_id+'"]')).to_have_count(0)
        expect(page.locator('.notice')).to_contain_text('可恢复备份')
        page.reload();expect(page.locator('.save-state')).to_have_text('已保存')
        checks.append('Profile rename retry, copy cancellation, export/import and deletion confirmation produce explicit results')

        candidate=profile();candidate['values']['runtime.vision.model_path']='test.engine'
        result=rpc('start',candidate);assert 'error' not in result,result
        page.reload();expect(page.locator('.run-state')).to_have_text('运行中')
        value=page.locator('[id="f-gamepad.ads.output_limit_x"]');value.fill('49')
        menu('仅保存配置');expect(page.locator('.run-state')).to_have_text('运行中 · 待应用')
        page.evaluate('window.failApply=true');btn('保存并应用').click()
        expect(page.locator('.save-state')).to_have_text('已保存')
        expect(page.locator('.run-state')).to_have_text('运行中 · 应用失败')
        page.screenshot(path=str(OUT/'apply-failure.png'))
        btn('重试应用').click();expect(page.locator('.run-state')).to_have_text('运行中 · 已应用')
        value.fill('50');page.evaluate('window.delayApply=true');btn('保存并应用').click()
        expect(page.locator('.run-state')).to_have_text('运行中 · 等待应用')
        page.evaluate('window.finishReload=true');expect(page.locator('.run-state')).to_have_text('运行中 · 已应用')
        menu('重启当前配置…');expect(dialog).to_be_visible()
        dialog.get_by_role('button',name='取消',exact=True).click()
        expect(page.locator('.run-state')).to_have_text('运行中 · 已应用')
        rpc('stop')
        checks.append('Saved-only, failed application and confirmed application have distinct states and retry/cancel paths')
        assert not errors,errors
        (OUT/'results.json').write_text(json.dumps({'checks':checks,'page_errors':errors},ensure_ascii=False,indent=2),encoding='utf-8')
        print(json.dumps({'checks':checks,'page_errors':errors},ensure_ascii=False))
        browser.close()


if __name__ == '__main__':
    main()
