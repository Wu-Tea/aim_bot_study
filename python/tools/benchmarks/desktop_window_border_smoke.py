"""Read existing native frame pixels; PrintWindow can hide default-frame overpaint."""
import ctypes
from ctypes import wintypes as w
import json
import os
from pathlib import Path
import sys
import tempfile
import time

from project_paths import PROJECT_ROOT
from desktop_app.test_desktop import require_isolated_gui
from desktop_app.web_host import launch
from desktop_app.web_service import WorkspaceService

sys.path.insert(0, str(PROJECT_ROOT / 'python/tests'))
from test_desktop_web_service import TestRuntime


def main():
    require_isolated_gui()
    out = Path(os.environ.get('BORDER_CHECK_OUTPUT', str(
        PROJECT_ROOT / 'output/playwright/gui-frame-border-20261028/regression')))
    out.mkdir(parents=True, exist_ok=True)
    failures = []
    with tempfile.TemporaryDirectory(prefix='desktop-border-') as folder:
        service = WorkspaceService(folder, TestRuntime(), curve_source=PROJECT_ROOT)
        assert service.dispatch('create', {'name': '边框验证'})['ok']

        def check(window, _):
            from System import Action
            from System.Drawing import Bitmap, Graphics
            from System.Drawing.Imaging import ImageFormat
            from System.Windows.Forms import Screen
            try:
                deadline = time.monotonic() + 15
                while not window.evaluate_js('!!document.querySelector(".preview-readout")'):
                    assert time.monotonic() < deadline, 'GUI did not finish loading'
                    time.sleep(.05)
                rows = []

                def sample():
                    native = window.native
                    # GetWindowDC samples are clipped outside the test monitor.
                    # Keep every edge on-screen, independent of persisted centering.
                    area = Screen.FromHandle(native.Handle).WorkingArea
                    native.SetBounds(area.Left + 16, area.Top + 16,
                                     min(1360, area.Width - 32), min(940, area.Height - 32))
                    hwnd = native.Handle.ToInt64()
                    user = ctypes.WinDLL('user32')
                    user.SendMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
                    user.GetWindowDC.argtypes = [w.HWND]
                    user.GetWindowDC.restype = w.HDC
                    user.ReleaseDC.argtypes = [w.HWND, w.HDC]
                    gdi = ctypes.WinDLL('gdi32')
                    gdi.GetPixel.argtypes = [w.HDC, ctypes.c_int, ctypes.c_int]
                    gdi.GetPixel.restype = w.DWORD
                    gdi.BitBlt.argtypes = [w.HDC, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                          ctypes.c_int, w.HDC, ctypes.c_int, ctypes.c_int, w.DWORD]
                    dwm = ctypes.WinDLL('dwmapi')
                    dwm.DwmSetWindowAttribute.argtypes = [w.HWND, w.DWORD, ctypes.c_void_p, w.DWORD]
                    theme = ctypes.WinDLL('uxtheme')
                    theme.SetWindowTheme.argtypes = [w.HWND, w.LPCWSTR, w.LPCWSTR]
                    for mode in ('composited', 'classic'):
                        if mode == 'classic':
                            # Per-window only. Exercise the default GDI border path as well.
                            policy = ctypes.c_int(1)
                            assert dwm.DwmSetWindowAttribute(hwnd, 2, ctypes.byref(policy), 4) == 0
                            assert theme.SetWindowTheme(hwnd, '', '') == 0
                        native.Refresh()
                        for active in (1, 0, 1):
                            user.SendMessageW(hwnd, 0x85, 1, 0)  # WM_NCPAINT
                            user.SendMessageW(hwnd, 0x86, active, 0)  # WM_NCACTIVATE
                            dc = user.GetWindowDC(hwnd)
                            assert dc
                            try:
                                points = [(native.Width // 2, i) for i in range(1, 5)]
                                points += [(i, native.Height // 2) for i in range(1, 5)]
                                points += [(native.Width-1-i, native.Height // 2) for i in range(1, 5)]
                                points += [(native.Width // 2, native.Height-1-i) for i in range(1, 5)]
                                colors = [int(gdi.GetPixel(dc, x, y)) for x, y in points]
                                rows.append(dict(mode=mode, active=active, colors=colors))
                                bitmap = Bitmap(native.Width, 48)
                                graphics = Graphics.FromImage(bitmap)
                                target = graphics.GetHdc()
                                try:
                                    assert gdi.BitBlt(target.ToInt64(), 0, 0, native.Width, 48,
                                                      dc, 0, 0, 0x00CC0020)
                                finally:
                                    graphics.ReleaseHdc(target)
                                    graphics.Dispose()
                                bitmap.Save(str(out / f'{mode}-{active}-edge.png'), ImageFormat.Png)
                                bitmap.Dispose()
                            finally:
                                user.ReleaseDC(hwnd, dc)
                    (out / 'pixels.json').write_text(json.dumps(rows, indent=2), encoding='utf-8')
                    for row in rows:
                        assert all(c != 0xffffffff and max(c & 255, c >> 8 & 255, c >> 16 & 255) < 128
                                   for c in row['colors']), row

                window.native.Invoke(Action(sample))
                print('PASS: native border remains dark in 6 paint/activation cases (96 pixels)', flush=True)
            except Exception as error:
                failures.append(error)
            finally:
                window.run_js('window.pywebview.api.dispatch("window_close")')

        launch(folder, service=service, ready=check)
    if failures:
        raise failures[0]


if __name__ == '__main__':
    main()
