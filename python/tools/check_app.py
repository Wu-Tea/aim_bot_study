"""Ordinary app checks: configuration, native behavior, and isolated WebView2 UI.

python python/tools/check_app.py [--scope config|gui|all] [--build]
No application profiles, gameplay output, input desktop or mouse are modified.
"""
import argparse
from contextlib import contextmanager
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'python'))
from desktop_app.test_desktop import run_isolated


def cmake_tool(name):
    installed = shutil.which(name)
    if installed:
        return installed
    cmake = shutil.which('cmake')
    if cmake:
        sibling = Path(cmake).with_name(name + '.exe')
        if sibling.is_file():
            return str(sibling)
    bundled = next(iter(Path(os.environ['ProgramFiles']).glob(
        f'Microsoft Visual Studio/2022/*/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/{name}.exe')), None)
    if not bundled:
        raise RuntimeError(f'{name} not found; install the native build tools first.')
    return str(bundled)


@contextmanager
def timed_stage(name):
    started = time.perf_counter()
    print(f'[Check] {name}', flush=True)
    try:
        yield
    finally:
        print(f'[Timing] {name}: {time.perf_counter() - started:.3f}s', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--scope', choices=('config','gui','all'), default='all')
    parser.add_argument('--build', action='store_true', help='Build Release runtime and ordinary native test runners first.')
    parser.add_argument('--jobs', type=int, default=3, help='Concurrent native test suites (default: 3; use 1 for serial timing).')
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    environment = os.environ.copy()
    environment['PYTHONPATH'] = str(ROOT / 'python') + os.pathsep + environment.get('PYTHONPATH','')
    os.environ['PYTHONPATH'] = environment['PYTHONPATH']
    native = ROOT / 'native/build/Release'
    if args.build:
        with timed_stage('native build'):
            subprocess.check_call([cmake_tool('cmake'), '--build', str(ROOT/'native/build'), '--config','Release','--target',
                'cod_native_runtime','cod_native_base_tests','cod_native_functional_tests','--parallel','6'],
                cwd=ROOT, stdout=sys.stdout, stderr=sys.stderr, creationflags=subprocess.CREATE_NO_WINDOW)
    if args.scope in ('config','all'):
        with timed_stage('native tests'):
            if args.scope == 'all':
                # Existing CTest suites own separate artifact directories.
                # Run the same base/functional cases; never shorten scenarios.
                subprocess.check_call([cmake_tool('ctest'), '--test-dir', str(ROOT/'native/build'),
                    '-C','Release','-L','^(base|functional)$','--parallel',str(args.jobs),
                    '--output-on-failure','--no-tests=error'], cwd=ROOT, env=environment,
                    stdout=sys.stdout, stderr=sys.stderr, creationflags=subprocess.CREATE_NO_WINDOW)
            else:
                for executable,suite in (('cod_native_base_tests.exe','BaseContracts'),
                                         ('cod_native_functional_tests.exe','FeatureAutoFireAndMarker')):
                    subprocess.check_call([str(native/executable),'--artifacts',str(ROOT/'runs/app-checks/native'),
                        '--suite',suite], cwd=ROOT, env=environment, stdout=sys.stdout, stderr=sys.stderr,
                        creationflags=subprocess.CREATE_NO_WINDOW)
    if args.scope == 'config':
        with timed_stage('configuration tests'):
            subprocess.check_call([sys.executable,'-m','unittest','discover','-s','python/tests','-p','test_desktop_app.py'],
                                  cwd=ROOT, env=environment, stdout=sys.stdout, stderr=sys.stderr,
                                  creationflags=subprocess.CREATE_NO_WINDOW)
    else:
        with timed_stage('desktop tests'):
            status = run_isolated(['-m','unittest','discover','-s','python/tests','-p','test_desktop*.py'], cwd=ROOT, timeout=120)
        if status:return status
        with timed_stage('WebView2 smoke'):
            status = run_isolated(['python/tools/benchmarks/desktop_web_host_smoke.py'], cwd=ROOT, timeout=90)
        if status:return status
    print(f'App checks completed: scope={args.scope}; GUI checks use a private Windows desktop.')
    return 0


if __name__ == '__main__':
    try:
        with timed_stage('total'):
            raise SystemExit(main())
    except subprocess.CalledProcessError as error:raise SystemExit(error.returncode)
    except subprocess.TimeoutExpired as error:
        print(f'App check timed out after {error.timeout}s: {error.cmd}', file=sys.stderr)
        raise SystemExit(1)
