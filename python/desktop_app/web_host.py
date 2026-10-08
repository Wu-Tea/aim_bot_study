"""WebView2 desktop shell; Python owns files/processes, native owns control/Vision."""
from pathlib import Path
import threading
from .web_service import WorkspaceService


def launch(root, *, hidden=False, service=None, ready=None):
    import webview
    assets = Path(__file__).parent / 'web_ui/dist/index.html'
    if not assets.is_file():
        raise RuntimeError('界面资源尚未构建。请运行 scripts/setup_desktop.ps1 后重新打开。')
    state = {'allow_close': False, 'loaded': False}
    service = service or WorkspaceService(root)
    class Bridge:
        def dispatch(self, command, payload=None):
            if command == 'window_close':
                state['allow_close'] = True
                window.destroy()
                return {'ok': True, 'data': True}
            return service.dispatch(command, payload)
    window = webview.create_window('手柄助手', str(assets), js_api=Bridge(), width=1360, height=940,
        min_size=(840,640), background_color='#e9ecea', hidden=hidden, text_select=True)
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
    window.events.closing += closing
    window.events.loaded += loaded
    webview.start(gui='edgechromium', private_mode=True)
