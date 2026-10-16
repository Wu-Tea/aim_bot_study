from pathlib import Path
import os,sys,json,time,subprocess,copy
clean=Path(sys.argv[1]).resolve();dev=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(clean/'python'))
from desktop_app.runtime import RuntimeManager
from desktop_app.web_service import WorkspaceService
from desktop_app.web_host import launch
from desktop_app.workspace import put
case=Path(sys.argv[2]).resolve();case.mkdir(parents=True,exist_ok=False)
(case/'config.native.example.toml').write_bytes((clean/'config.native.example.toml').read_bytes())
manager=RuntimeManager(case);manager.executable=clean/'native/build/Release/cod_native_runtime.exe'
service=WorkspaceService(case,manager,curve_source=clean)
def call(command,payload=None):
 result=service.dispatch(command,payload)
 assert result['ok'],result
 return result['data']
p=call('create',{'name':'clean 缺帧修复验证'})
settings={'runtime.vision.model_path':str(Path(sys.argv[3]).resolve()),'runtime.vision.capture_width':640,'runtime.vision.capture_height':512,'runtime.vision.tensor_width':480,'runtime.vision.tensor_height':384,'runtime.output.enabled':False,'gamepad.detection_haptics.enabled':False,'runtime.vision.keepwarm_when_idle':True}
for k,v in settings.items():
 put(p['config'],k,v)
 if k in p['values']:p['values'][k]=v
p=call('save',p)['profile'];identifier=p['id']
path=service.repo.runtime_path(p)
dump=subprocess.check_output([str(manager.executable),'--config',str(path),'--game',p['game'],'--dump-effective-config'],creationflags=0x08000000).decode()
assert 'runtime.output.enabled=0 ' in dump and 'gamepad.detection_haptics.enabled=0 ' in dump
errors=[];summary={}
def wait_until(fn,description,timeout=25):
 deadline=time.monotonic()+timeout
 while time.monotonic()<deadline:
  value=fn()
  if value:return value
  time.sleep(.1)
 raise RuntimeError('Timed out: '+description)
def check(window,_):
 try:
  wait_until(lambda:window.evaluate_js('document.body.innerText.includes("clean 缺帧修复验证")'),'GUI bootstrap')
  summary['gui_bootstrap']=True
  window.evaluate_js('Array.from(document.querySelectorAll("button")).find(e=>e.textContent.trim()==="启动配置").click()')
  wait_until(lambda:manager.status().get('initialized'),'real runtime startup',35)
  record=manager.active();assert Path(record['executable_path'])==manager.executable
  summary['real_runtime_initialized']=True
  summary['executable']=record['executable_path']
  wait_until(lambda:manager.learning(),'IPC learning channel')
  # Use the real WebView bridge to save and apply a fixed-recoil change.
  current=call('bootstrap')['profiles'][0]
  current['values']['gamepad.recoil.output_amount']=.18
  current['apply']=True
  window.run_js('window.__incidentReload=null;window.pywebview.api.dispatch("save",'+json.dumps(current,ensure_ascii=False)+').then(r=>window.__incidentReload=r)')
  response=wait_until(lambda:window.evaluate_js('window.__incidentReload'),'GUI save and reload',15)
  assert response['ok'],response
  data=response['data'];assert 'error' not in data,data
  reload=data['reload'];assert reload['status']==2,reload
  assert reload['completed_id']==reload['request_id'] and reload['revision']>=1,reload
  summary['gui_fixed_recoil_reload_applied']=True
  summary['reload_revision']=reload['revision']
  summary['output_enabled']=False
  rates=wait_until(lambda:manager.frame_rates(manager.active()),'frame-rate IPC')
  summary['frame_rate_ipc']=bool(rates)
  # Test the actual Stop button, normal stop event and child exit.
  wait_until(lambda:window.evaluate_js('Array.from(document.querySelectorAll("button")).some(e=>e.textContent.trim()==="停止"&&!e.disabled)'),'stop enabled')
  window.evaluate_js('Array.from(document.querySelectorAll("button")).find(e=>e.textContent.trim()==="停止").click()')
  wait_until(lambda:not manager.active(),'GUI normal stop',12)
  if manager.child is not None:assert manager.child.wait(timeout=3)==0
  summary['gui_normal_stop']=True
  print(json.dumps(summary,ensure_ascii=False),flush=True)
 except BaseException as e:
  errors.append(e)
 finally:
  if manager.active():
   try:manager.stop();wait_until(lambda:not manager.active(),'owned cleanup',12)
   except Exception:
    if manager.child and manager.child.poll() is None:manager.child.terminate();manager.child.wait(timeout=5)
  window.run_js('window.pywebview.api.dispatch("window_close")')
# Hidden real window on the current desktop keeps DXGI capture available and
# never takes focus; isolated-desktop layout/interaction tests ran separately.
launch(case,hidden=True,service=service,ready=check)
(case/'result.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
if errors:raise errors[0]
