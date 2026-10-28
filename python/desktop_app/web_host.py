"""WebView2 desktop shell; Python owns files/processes, native owns control/Vision."""
from pathlib import Path
import threading
from .web_service import WorkspaceService


def _style_native_frame(window):
    """Install the integrated caption on the WinForms UI thread before Show."""
    import ctypes
    from ctypes import wintypes
    import logging
    import sys

    if sys.platform != 'win32':
        return
    window.native.ShowIcon = False
    from .window_frame import install
    install(window)
    # Explicit caption/text/border colors are supported from Windows 11.
    if sys.getwindowsversion().build < 22000:
        return
    set_attribute = ctypes.WinDLL('dwmapi').DwmSetWindowAttribute
    set_attribute.argtypes = [wintypes.HWND, wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD]
    set_attribute.restype = ctypes.c_long
    hwnd = window.native.Handle.ToInt64()
    # COLORREF uses 0x00BBGGRR. Match the GUI background, text and subtle border.
    for attribute, color in ((35, 0x141211), (36, 0xF0EEEE), (34, 0x383230)):
        value = wintypes.DWORD(color)
        result = set_attribute(hwnd, attribute, ctypes.byref(value), ctypes.sizeof(value))
        if result < 0:
            logging.getLogger(__name__).warning(
                'Native frame attribute %s was not applied (HRESULT 0x%08X)',
                attribute, result & 0xffffffff)


def launch(root, *, hidden=False, service=None, ready=None):
    import webview
    assets = Path(__file__).parent / 'web_ui/dist/index.html'
    if not assets.is_file():
        raise RuntimeError('界面资源尚未构建。请运行 scripts/setup_desktop.ps1 后重新打开。')
    state = {'allow_close': False, 'loaded': False}
    service = service or WorkspaceService(root)
    class Bridge:
        def dispatch(self, command, payload=None):
            if command in ('window_minimize', 'window_toggle_maximize'):
                from .window_frame import command as frame_command
                return frame_command(window, command)
            if command == 'window_close':
                state['allow_close'] = True
                window.destroy()
                return {'ok': True, 'data': True}
            return service.dispatch(command, payload)
    window = webview.create_window('手柄助手', str(assets), js_api=Bridge(), width=1360, height=940,
        min_size=(840,640), background_color='#111214', hidden=hidden, text_select=True)
    def dialogs(mode, name, filters):
        result = window.create_file_dialog(webview.FileDialog.SAVE if mode=='save' else webview.FileDialog.OPEN,
            save_filename=name, file_types=filters)
        return result[0] if result else None
    service.dialogs = dialogs
    def closing():
        if state['allow_close'] or not state['loaded']: return True
        # FormClosing runs synchronously on the WinForms thread. Edge executes
        # JS on that same thread: waiting for JS here would deadlock closing.
        threading.Thread(target=lambda: window.run_js(
            'if(window.requestDesktopClose){window.requestDesktopClose()}else{window.pywebview.api.dispatch("window_close")}'),
            daemon=True,name='desktop-close-request').start()
        return False
    def loaded():
        state['loaded'] = True
        if ready: threading.Thread(target=ready,args=(window,service),daemon=True).start()
    def window_state():
        if state['loaded']:
            from System import Action
            from System.Windows.Forms import FormWindowState
            def notify():
                if window.native.IsDisposed:
                    return
                maximized = window.native.WindowState == FormWindowState.Maximized
                window.native.webview.CoreWebView2.ExecuteScriptAsync(
                    'window.dispatchEvent(new CustomEvent("desktop-window-state",'
                    '{detail:{maximized:' + str(maximized).lower() + '}}))')
            window.native.BeginInvoke(Action(notify))
    window.events.maximized += window_state
    window.events.restored += window_state
    window.events.closing += closing
    window.events.loaded += loaded
    window.events.before_show += _style_native_frame
    webview.start(gui='edgechromium', private_mode=True)
