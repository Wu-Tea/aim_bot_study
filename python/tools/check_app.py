"""Ordinary app checks: configuration, native behavior, and isolated Tk UI.

python python/tools/check_app.py [--scope config|gui|all] [--build]
No application profiles, gameplay output, input desktop or mouse are modified.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'python'))
from desktop_app.test_desktop import run_isolated


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--scope', choices=('config','gui','all'), default='all')
    parser.add_argument('--build', action='store_true', help='Build Release runtime and ordinary native test runners first.')
    args = parser.parse_args()
    environment = os.environ.copy()
    environment['PYTHONPATH'] = str(ROOT / 'python') + os.pathsep + environment.get('PYTHONPATH','')
    os.environ['PYTHONPATH'] = environment['PYTHONPATH']
    native = ROOT / 'native/build/Release'
    if args.build:
        cmake = shutil.which('cmake')
        if not cmake:
            cmake = next(iter(Path(os.environ['ProgramFiles']).glob(
                'Microsoft Visual Studio/2022/*/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe')), None)
        if not cmake:raise RuntimeError('CMake not found; build the native targets first.')
        subprocess.check_call([str(cmake), '--build', str(ROOT/'native/build'), '--config','Release','--target',
            'cod_native_runtime','cod_native_base_tests','cod_native_functional_tests','--parallel','6'],
            cwd=ROOT, stdout=sys.stdout, stderr=sys.stderr, creationflags=subprocess.CREATE_NO_WINDOW)
    if args.scope in ('config','all'):
        for executable,suite in (('cod_native_base_tests.exe','BaseContracts'),
                                 ('cod_native_functional_tests.exe','FeatureAutoFireAndMarker')):
            command = [str(native/executable),'--artifacts',str(ROOT/'runs/app-checks/native')]
            if args.scope == 'config':command += ['--suite',suite]
            subprocess.check_call(command, cwd=ROOT, env=environment, stdout=sys.stdout, stderr=sys.stderr,
                                  creationflags=subprocess.CREATE_NO_WINDOW)
    if args.scope == 'config':
        subprocess.check_call([sys.executable,'-m','unittest','discover','-s','python/tests','-p','test_desktop_app.py'],
                              cwd=ROOT, env=environment, stdout=sys.stdout, stderr=sys.stderr,
                              creationflags=subprocess.CREATE_NO_WINDOW)
    else:
        status = run_isolated(['-m','unittest','discover','-s','python/tests','-p','test_desktop*.py'], cwd=ROOT)
        if status:return status
    print(f'App checks completed: scope={args.scope}; GUI checks use a private Windows desktop.')
    return 0


if __name__ == '__main__':
    try:raise SystemExit(main())
    except subprocess.CalledProcessError as error:raise SystemExit(error.returncode)
