"""Application entry: WebView2 window and native runtime command actions."""
from project_paths import PROJECT_ROOT
import argparse
import json
import os
from pathlib import Path
import time

from .runtime import RuntimeManager
from .settings import ConfigStore, effective, lookup


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', default=str(PROJECT_ROOT))
    parser.add_argument('--action', choices=['start', 'stop', 'status', 'migrate', 'preview-start', 'preview-stop'])
    parser.add_argument('--game', default='default')
    args = parser.parse_args()
    project = Path(args.root).resolve()
    if args.action:
        store = ConfigStore(project)
        if args.action == 'migrate':
            print(json.dumps({'migrated': store.migrate_games()}))
            return
        manager = RuntimeManager(project)
        if args.action == 'start':
            active = manager.active()
            print(json.dumps(active if active and active['game'] == args.game else manager.start(args.game, store.read()[1])))
        elif args.action == 'stop':
            manager.stop()
            deadline = time.monotonic() + 8
            while manager.active():
                if time.monotonic() >= deadline:
                    raise RuntimeError('退出请求已发送，但程序尚未退出。')
                time.sleep(.05)
        elif args.action.startswith('preview-'):
            data = effective(store.read()[1], args.game)
            model = project / lookup(data, 'runtime.vision.model_path', '')
            print(json.dumps({'action': args.action.replace('-', '_'), 'game': args.game,
                'executable_path': str(manager.executable), 'config_path': str(store.path),
                'state_path': str(manager.state_path), 'model_path': str(model), 'model_exists': model.is_file(),
                'arguments': ['--config', str(store.path), '--game', args.game],
                'fusion_channel_enabled': True, 'fusion_session': os.environ.get('FUSION_SESSION', 'dev')}))
        else:
            print(json.dumps(manager.status(), ensure_ascii=False))
        return
    # One settings writer/window per project; the native runtime has its own
    # process and remains alive when the window closes.
    import ctypes
    import hashlib
    from ctypes import wintypes
    from .runtime import kernel
    lock_name = 'Local\\gamepad_assistant_' + hashlib.sha256(os.path.normcase(str(project)).encode()).hexdigest()[:16]
    ui_lock = kernel.CreateMutexW(None, False, lock_name)
    if not ui_lock:
        raise OSError('无法创建界面实例锁')
    if ctypes.get_last_error() == 183:
        user32 = ctypes.WinDLL('user32')
        user32.FindWindowW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR]
        user32.FindWindowW.restype = wintypes.HWND
        user32.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
        user32.SetForegroundWindow.argtypes = [wintypes.HWND]
        window = user32.FindWindowW(None, '手柄助手')
        if window:
            user32.ShowWindow(window, 9)
            user32.SetForegroundWindow(window)
        kernel.CloseHandle(ui_lock)
        return
    try:
        from .web_host import launch
        launch(project)
    except Exception as error:
        # pythonw has no console: startup failures must stay visible.
        ctypes.windll.user32.MessageBoxW(None, str(error), '手柄助手无法打开', 0x10)
        raise
    finally:
        kernel.CloseHandle(ui_lock)



if __name__ == '__main__':
    main()
