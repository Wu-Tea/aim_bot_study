"""Exercise the real VBS entry on a private desktop, with an empty temp project.

Run: python -m desktop_app.test_desktop python/tools/benchmarks/desktop_launcher_smoke.py
No models, user profiles or controller output are opened.
"""
import ctypes
from ctypes import wintypes as w
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time

from desktop_app.test_desktop import require_isolated_gui
from desktop_app.runtime import kernel
from project_paths import PROJECT_ROOT


def main():
    require_isolated_gui()
    user = ctypes.WinDLL('user32', use_last_error=True)
    user.FindWindowW.argtypes = [w.LPCWSTR, w.LPCWSTR]
    user.FindWindowW.restype = w.HWND
    user.IsWindowVisible.argtypes = [w.HWND]
    user.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [w.HWND, ctypes.POINTER(w.DWORD)]
    with tempfile.TemporaryDirectory(prefix='GUI 启动测试 ') as folder:
        launcher = Path(folder) / '启动助手.vbs'
        launcher.write_bytes((PROJECT_ROOT / '启动助手.vbs').read_bytes())
        env = os.environ.copy()
        env['PYTHONPATH'] = str(PROJECT_ROOT / 'python')
        started = time.perf_counter()
        script = subprocess.Popen([str(Path(os.environ['SystemRoot']) / 'System32/wscript.exe'),
                                   str(launcher)], cwd=folder, env=env)
        window = None
        process = None
        created_ms = None
        try:
            while time.perf_counter() - started < 10:
                window = user.FindWindowW(None, '手柄助手')
                if window:
                    if created_ms is None:
                        created_ms = round((time.perf_counter() - started) * 1000, 1)
                        pid = w.DWORD()
                        user.GetWindowThreadProcessId(window, ctypes.byref(pid))
                        process = kernel.OpenProcess(0x100000, False, pid.value)
                    if user.IsWindowVisible(window):
                        print(json.dumps({'first_launch_visible': True,
                            'created_ms': created_ms,
                            'visible_ms': round((time.perf_counter() - started) * 1000, 1)}), flush=True)
                        break
                time.sleep(.02)
            else:
                raise AssertionError(f'First launch did not show GUI; window_created={bool(window)}')
        finally:
            if window:
                user.PostMessageW(window, 0x0010, 0, 0)  # Normal WM_CLOSE, never kill.
            script.wait(timeout=5)
            if process:
                try:
                    if kernel.WaitForSingleObject(process, 10000) != 0:
                        raise AssertionError('Launcher test GUI did not close normally')
                finally:
                    kernel.CloseHandle(process)


if __name__ == '__main__':
    main()
