import contextlib
import io
import os
import unittest
from unittest.mock import patch

from desktop_app.test_desktop import desktop_name, require_isolated_gui, run_isolated


class DesktopIsolationTests(unittest.TestCase):
    def test_unisolated_gui_is_rejected_before_creating_windows(self):
        with patch.dict(os.environ, {}, clear=True):
            with self.assertRaisesRegex(RuntimeError, 'desktop_app.test_desktop'):
                require_isolated_gui()

    def test_native_window_stays_on_private_desktop_and_exit_status_is_preserved(self):
        original = desktop_name()
        output = io.StringIO()
        code = ('import os, ctypes; from ctypes import wintypes as w; '
                'from desktop_app.test_desktop import desktop_name, require_isolated_gui; '
                'require_isolated_gui(); u=ctypes.WinDLL("user32"); '
                'u.CreateWindowExW.restype=w.HWND; '
                'u.CreateWindowExW.argtypes=[w.DWORD,w.LPCWSTR,w.LPCWSTR,w.DWORD,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_int,w.HWND,w.HMENU,w.HINSTANCE,w.LPVOID]; '
                'hwnd=u.CreateWindowExW(0,"STATIC","Isolation check",0x10000000,0,0,80,40,None,None,None,None); assert hwnd; '
                'u.SetFocus.argtypes=[w.HWND]; u.SetFocus(hwnd); '
                'assert desktop_name()==os.environ["AIM_GUI_TEST_DESKTOP"]; '
                'print("PRIVATE="+desktop_name()+" 中文输出"); '
                'u.DestroyWindow.argtypes=[w.HWND]; assert u.DestroyWindow(hwnd); raise SystemExit(7)')
        with contextlib.redirect_stdout(output):
            status = run_isolated(['-c', code])
        self.assertEqual(status, 7)
        self.assertIn('PRIVATE=AimGuiTest_', output.getvalue())
        self.assertIn('中文输出', output.getvalue())
        self.assertEqual(desktop_name(), original)
