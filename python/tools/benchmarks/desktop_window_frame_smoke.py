"""Native frame colors and window operations on the private test desktop."""
import ctypes
from ctypes import wintypes as w
import json
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
    out = PROJECT_ROOT / 'output/playwright/gui-redesign-20261028'
    out.mkdir(parents=True, exist_ok=True)
    failures = []
    with tempfile.TemporaryDirectory(prefix='desktop-frame-') as folder:
        service = WorkspaceService(folder, TestRuntime(), curve_source=PROJECT_ROOT)
        created = service.dispatch('create', {'name': '标题栏验证'})
        assert created['ok'], created.get('error')

        def check(window, _):
            from System import Action
            from System.Windows.Forms import FormWindowState
            try:
                assert window.events.shown.wait(15), 'Native window was not shown'
                deadline = time.monotonic() + 15
                while not window.evaluate_js('!!document.querySelector(".preview-readout")'):
                    if time.monotonic() >= deadline:
                        raise RuntimeError('GUI did not finish loading')
                    time.sleep(.05)
                native = window.native
                hwnd = native.Handle.ToInt64()
                assert not native.ShowIcon
                assert native.MinimizeBox and native.MaximizeBox
                states = []
                dimensions = [(native.Width, native.Height, native.ClientSize.Width, native.ClientSize.Height)]
                def exercise():
                    for state in [FormWindowState.Maximized, FormWindowState.Normal,
                                  FormWindowState.Minimized, FormWindowState.Normal]:
                        native.WindowState = state
                        assert native.WindowState == state
                        states.append(str(state))
                        dimensions.append((native.Width, native.Height, native.ClientSize.Width, native.ClientSize.Height))
                native.Invoke(Action(exercise))
                assert dimensions[0] == dimensions[2] == dimensions[4], dimensions
                user = ctypes.WinDLL('user32', use_last_error=True)
                user.SendMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
                user.SendMessageW.restype = ctypes.c_ssize_t
                rect = w.RECT()
                user.GetWindowRect.argtypes = [w.HWND, ctypes.POINTER(w.RECT)]
                assert user.GetWindowRect(hwnd, ctypes.byref(rect))
                x, y = rect.left + 200, rect.top + 20
                hit = user.SendMessageW(hwnd, 0x84, 0, (y & 0xffff) << 16 | (x & 0xffff))
                assert hit == 1, hit  # Legacy caption no longer intercepts HTML controls.
                edge_hits = []
                for dx, dy, expected in [(1, 1, 13), (rect.right-rect.left-2, 1, 14),
                        (1, rect.bottom-rect.top-2, 16),
                        (rect.right-rect.left-2, rect.bottom-rect.top-2, 17),
                        (1, 100, 10), (rect.right-rect.left-2, 100, 11),
                        (100, 1, 12), (100, rect.bottom-rect.top-2, 15)]:
                    point = ((rect.top+dy) & 0xffff) << 16 | ((rect.left+dx) & 0xffff)
                    actual = user.SendMessageW(hwnd, 0x84, 0, point)
                    assert actual == expected, (actual, expected)
                    edge_hits.append(actual)
                tasks = []
                def start_capture():
                    assert native.webview.CoreWebView2.Settings.IsNonClientRegionSupportEnabled
                    tasks.append(native.webview.CoreWebView2.CallDevToolsProtocolMethodAsync(
                        'Page.captureScreenshot', '{}'))
                native.Invoke(Action(start_capture))
                deadline = time.monotonic() + 10
                while not tasks[0].IsCompleted:
                    assert time.monotonic() < deadline, 'Screenshot timed out'
                    time.sleep(.05)
                import base64
                capture = json.loads(str(tasks[0].Result))
                (out / 'native-window.png').write_bytes(base64.b64decode(capture['data']))
                # Exercise the actual HTML controls and bridge, not only WinForms setters.
                window.evaluate_js('document.querySelector("[aria-label=最大化]").click()')
                deadline = time.monotonic() + 5
                while str(native.WindowState) != 'Maximized':
                    assert time.monotonic() < deadline
                    time.sleep(.05)
                while not window.evaluate_js('!!document.querySelector("[aria-label=还原窗口]")'):
                    assert time.monotonic() < deadline
                    time.sleep(.05)
                window.evaluate_js('document.querySelector("[aria-label=还原窗口]").click()')
                deadline = time.monotonic() + 5
                while str(native.WindowState) != 'Normal':
                    assert time.monotonic() < deadline
                    time.sleep(.05)
                window.evaluate_js('document.querySelector("[aria-label=最小化]").click()')
                deadline = time.monotonic() + 5
                while str(native.WindowState) != 'Minimized':
                    assert time.monotonic() < deadline
                    time.sleep(.05)
                native.Invoke(Action(lambda: setattr(native, 'WindowState', FormWindowState.Normal)))
                # Closing through HTML must use the existing unsaved-draft guard.
                window.evaluate_js('''(() => {
                    const input=document.querySelector('.number input');
                    Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set.call(input,'97');
                    input.dispatchEvent(new Event('input',{bubbles:true}));
                })()''')
                deadline = time.monotonic() + 5
                while not window.evaluate_js('!!document.querySelector(".save-state.dirty")'):
                    assert time.monotonic() < deadline
                    time.sleep(.05)
                window.evaluate_js('document.querySelector("[aria-label=关闭窗口]").click()')
                deadline = time.monotonic() + 5
                while not window.evaluate_js('!!document.querySelector("[role=dialog]")'):
                    assert time.monotonic() < deadline
                    time.sleep(.05)
                assert '关闭工作空间' in window.evaluate_js('document.querySelector("[role=dialog]").innerText')
                result = dict(python_title_icon_hidden=True, native_resize=True,
                              caption_hit_test=hit, window_states=states, integrated_caption=True, html_buttons=True,
                              dimensions=dimensions, edge_hits=edge_hits, unsaved_close_guard=True,
                              frame_setup_ms=round(window._frame_setup_ms, 2))
                (out / 'native-window.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
                print(json.dumps(result), flush=True)
            except Exception as error:
                failures.append(error)
            finally:
                window.run_js('window.pywebview.api.dispatch("window_close")')
        launch(folder, service=service, ready=check)
    if failures:
        raise failures[0]


if __name__ == '__main__':
    main()
