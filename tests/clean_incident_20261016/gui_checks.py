from pathlib import Path
import sys,unittest,runpy,os
import importlib.util
clean=Path(sys.argv[1]).resolve()
dev=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(clean/'python'))
os.environ['PYTHONPATH']=os.pathsep.join([str(Path(__file__).resolve().parent),str(clean/'python')])
helper=dev/'python/desktop_app/test_desktop.py'
spec=importlib.util.spec_from_file_location('desktop_app.test_desktop',helper)
module=importlib.util.module_from_spec(spec)
sys.modules[spec.name]=module
spec.loader.exec_module(module)
from desktop_app.test_desktop import run_isolated
if '--child' not in sys.argv:
 raise SystemExit(run_isolated([str(Path(__file__).resolve()),str(clean),'--child'],cwd=clean))
suite=unittest.TestLoader().discover(str(dev/'python/tests'),pattern='test_desktop*.py')
for name,module in list(sys.modules.items()):
 if name.startswith('test_desktop'):
  if hasattr(module,'PROJECT'):module.PROJECT=clean
  if hasattr(module,'PROJECT_ROOT'):module.PROJECT_ROOT=clean
  if hasattr(module,'PRODUCTION'):module.PRODUCTION=clean/'native/build/Release/cod_native_runtime.exe'
result=unittest.TextTestRunner(verbosity=1).run(suite)
if not result.wasSuccessful():raise SystemExit(1)
runpy.run_path(str(dev/'python/tools/benchmarks/desktop_web_host_smoke.py'),run_name='__main__')
for name,module in list(sys.modules.items()):
 if name.startswith('desktop_app') and name!='desktop_app.test_desktop' and getattr(module,'__file__',None):
  assert Path(module.__file__).resolve().is_relative_to(clean), (name,module.__file__)
print('Production desktop modules all loaded from clean; tests/helper retained in DEV.')
