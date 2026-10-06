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

    def test_real_tk_focus_stays_on_private_desktop_and_exit_status_is_preserved(self):
        original = desktop_name()
        output = io.StringIO()
        code = ('import os, tkinter as tk; from desktop_app.test_desktop import desktop_name, require_isolated_gui; '
                'require_isolated_gui(); root=tk.Tk(); root.update(); root.focus_force(); root.update(); '
                'assert desktop_name()==os.environ["AIM_GUI_TEST_DESKTOP"]; '
                'print("PRIVATE="+desktop_name()+" 中文输出"); root.destroy(); raise SystemExit(7)')
        with contextlib.redirect_stdout(output):
            status = run_isolated(['-c', code])
        self.assertEqual(status, 7)
        self.assertIn('PRIVATE=AimGuiTest_', output.getvalue())
        self.assertIn('中文输出', output.getvalue())
        self.assertEqual(desktop_name(), original)
