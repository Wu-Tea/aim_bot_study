from __future__ import annotations

import ctypes
from contextlib import contextmanager
from ctypes import wintypes as w
from datetime import datetime, timezone
import json
import hashlib
import os
from pathlib import Path
import subprocess
import threading
import tomllib
import uuid
import time

from .settings import effective, lookup
from .fields import COMMON_FIELDS, GAME_FIELDS, field_value, validate_fields

CREATE_NO_WINDOW = 0x08000000
kernel = ctypes.WinDLL('kernel32', use_last_error=True)
kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
kernel.OpenProcess.restype = w.HANDLE
kernel.CloseHandle.argtypes = [w.HANDLE]
kernel.QueryFullProcessImageNameW.argtypes = [w.HANDLE, w.DWORD, w.LPWSTR, ctypes.POINTER(w.DWORD)]
kernel.GetProcessTimes.argtypes = [w.HANDLE] + [ctypes.POINTER(w.FILETIME)] * 4
kernel.GetExitCodeProcess.argtypes = [w.HANDLE, ctypes.POINTER(w.DWORD)]
kernel.OpenEventW.argtypes = [w.DWORD, w.BOOL, w.LPCWSTR]
kernel.OpenEventW.restype = w.HANDLE
kernel.SetEvent.argtypes = [w.HANDLE]
kernel.CreateMutexW.argtypes = [ctypes.c_void_p, w.BOOL, w.LPCWSTR]
kernel.CreateMutexW.restype = w.HANDLE
kernel.WaitForSingleObject.argtypes = [w.HANDLE, w.DWORD]
kernel.ReleaseMutex.argtypes = [w.HANDLE]


@contextmanager
def launch_lock(root):
    name = 'Local\\cod_native_launch_' + hashlib.sha256(os.path.normcase(str(root)).encode()).hexdigest()[:16]
    handle = kernel.CreateMutexW(None, False, name)
    if not handle:
        raise OSError('无法创建启动锁')
    acquired = kernel.WaitForSingleObject(handle, 0) in (0, 128)
    try:
        if not acquired:
            raise ValueError('另一个入口正在处理启动，请等待它完成。')
        yield
    finally:
        if acquired:
            kernel.ReleaseMutex(handle)
        kernel.CloseHandle(handle)


def process_identity(pid):
    handle = kernel.OpenProcess(0x1000, False, int(pid))
    if not handle:
        return None
    try:
        exit_code = w.DWORD()
        if not kernel.GetExitCodeProcess(handle, ctypes.byref(exit_code)) or exit_code.value != 259:
            return None
        size = w.DWORD(32768)
        path = ctypes.create_unicode_buffer(size.value)
        times = [w.FILETIME() for _ in range(4)]
        if not kernel.QueryFullProcessImageNameW(handle, 0, path, ctypes.byref(size)):
            return None
        if not kernel.GetProcessTimes(handle, *[ctypes.byref(value) for value in times]):
            return None
        return {'path': os.path.normcase(os.path.abspath(path.value)),
                'created': (times[0].dwHighDateTime << 32) | times[0].dwLowDateTime}
    finally:
        kernel.CloseHandle(handle)


class ProcessEntry(ctypes.Structure):
    _fields_ = [('dwSize', w.DWORD), ('cntUsage', w.DWORD), ('th32ProcessID', w.DWORD),
                ('th32DefaultHeapID', ctypes.c_size_t), ('th32ModuleID', w.DWORD),
                ('cntThreads', w.DWORD), ('th32ParentProcessID', w.DWORD),
                ('pcPriClassBase', w.LONG), ('dwFlags', w.DWORD), ('szExeFile', w.WCHAR * 260)]


def matching_processes(executable):
    kernel.CreateToolhelp32Snapshot.argtypes = [w.DWORD, w.DWORD]
    kernel.CreateToolhelp32Snapshot.restype = w.HANDLE
    kernel.Process32FirstW.argtypes = [w.HANDLE, ctypes.POINTER(ProcessEntry)]
    kernel.Process32NextW.argtypes = [w.HANDLE, ctypes.POINTER(ProcessEntry)]
    snapshot = kernel.CreateToolhelp32Snapshot(2, 0)
    if snapshot == ctypes.c_void_p(-1).value:
        raise OSError('无法枚举运行程序')
    result = []
    try:
        entry = ProcessEntry()
        entry.dwSize = ctypes.sizeof(entry)
        more = kernel.Process32FirstW(snapshot, ctypes.byref(entry))
        while more:
            if entry.szExeFile.lower() == executable.name.lower():
                identity = process_identity(entry.th32ProcessID)
                if identity and identity['path'] == os.path.normcase(str(executable)):
                    result.append(entry.th32ProcessID)
            more = kernel.Process32NextW(snapshot, ctypes.byref(entry))
    finally:
        kernel.CloseHandle(snapshot)
    return result


def tail(path, size=65536):
    try:
        with Path(path).open('rb') as stream:
            stream.seek(0, 2)
            stream.seek(max(0, stream.tell() - size))
            return stream.read().decode('utf-8', errors='replace')
    except OSError:
        return ''


def startup_output(path):
    # Readiness is established at startup and must survive a growing log.
    try:
        with Path(path).open('rb') as stream:
            return stream.read(65536).decode('utf-8', errors='replace')
    except OSError:
        return ''


class RuntimeManager:
    def __init__(self, root):
        self.root = Path(root).resolve()
        self.executable = self.root / 'native/build/Release/cod_native_runtime.exe'
        self.state_path = self.root / 'runs/runtime/background/native_runtime_state.json'
        self.lock = threading.Lock()
        self.child = None
        self.stopping_pid = None

    def validate(self, config_path, games):
        document = tomllib.loads(Path(config_path).read_text(encoding='utf-8-sig'))
        validate_fields(document, games)
        for game in games:
            result = subprocess.run([str(self.executable), '--config', str(config_path), '--game', game,
                                     '--dump-effective-config'], cwd=self.root,
                                    capture_output=True, creationflags=CREATE_NO_WINDOW, timeout=30)
            error = result.stderr.decode('utf-8', errors='replace').strip()
            if result.returncode or 'unknown config key:' in error:
                raise ValueError(f'{game} 配置校验失败：\n{error or result.stdout.decode("utf-8", errors="replace")}')

    def inspect_defaults(self, game):
        """Use the native loader for absent fields, including profile defaults."""
        if not self.executable.is_file():
            return {}
        result = subprocess.run([str(self.executable), '--config', str(self.root / 'config.toml'),
                                 '--game', game, '--dump-effective-config'], cwd=self.root,
                                capture_output=True, creationflags=CREATE_NO_WINDOW, timeout=30)
        if result.returncode:
            raise ValueError('无法读取原生配置：' + result.stderr.decode('utf-8', errors='replace').strip())
        fields = {field[0]: field for field in GAME_FIELDS + COMMON_FIELDS}
        values = {}
        for line in result.stdout.decode('utf-8').splitlines():
            setting, separator, _source = line.rpartition(' source=')
            path, equals, raw = setting.partition('=')
            if not separator or not equals or path not in fields:
                continue
            field = fields[path]
            value = raw == '1' if field[2] is bool else field_value(field, raw)
            values[path] = value
        return values

    def active(self):
        paths = [self.state_path] + [self.state_path.parent / game / self.state_path.name for game in ('apex', 'bo3')]
        for path in paths:
            try:
                record = json.loads(path.read_text(encoding='utf-8-sig'))
                identity = process_identity(record['process_id'])
                if not identity or identity['path'] != os.path.normcase(str(self.executable)):
                    continue
                if record.get('process_created') and record['process_created'] != identity['created']:
                    continue
                record.setdefault('game', path.parent.name if path != self.state_path else 'default')
                record.setdefault('stdout_path', str(path.parent / 'native_runtime.stdout.log'))
                record.setdefault('stderr_path', str(path.parent / 'native_runtime.stderr.log'))
                return record
            except (OSError, ValueError, KeyError, TypeError):
                continue
        return None

    def start(self, game, document):
        with self.lock, launch_lock(self.root):
            active = self.active()
            if active:
                raise ValueError(f'当前正在运行 {active["game"]}，请先停止或使用切换并重启。')
            if matching_processes(self.executable):
                raise ValueError('已有其他入口启动了控制程序，请从原入口停止后再启动。')
            validate_fields(document, [game])
            settings = effective(document, game)
            model = self.root / lookup(settings, 'runtime.vision.model_path', '')
            if not self.executable.is_file():
                raise ValueError('运行程序不存在，请先完成 Native 构建。')
            if not model.is_file():
                raise ValueError(f'模型文件不存在：{model}')
            if os.environ.get('FUSION_FORCE_OFF', '').lower() in ('1', 'true', 'on'):
                raise ValueError('FUSION_FORCE_OFF 环境设置禁止启动当前显示通道。')
            session = datetime.now().strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:6]
            logs = self.root / 'runs/desktop/sessions' / session
            logs.mkdir(parents=True)
            stdout = logs / 'runtime.stdout.log'
            stderr = logs / 'runtime.stderr.log'
            env = os.environ.copy()
            env['GAMEPAD_INPUT_LOG'] = '1'
            env['FUSION_SESSION'] = env.get('FUSION_SESSION', '').strip() or 'dev'
            env['FUSION_ENABLED'] = '1'
            env['FUSION_SHOW_ALL_DETECTIONS'] = '0'
            arguments = [str(self.executable), '--config', str(self.root / 'config.toml'), '--game', game]
            with stdout.open('wb') as out, stderr.open('wb') as err:
                self.child = subprocess.Popen(arguments, cwd=self.root, env=env, stdout=out, stderr=err,
                                              creationflags=CREATE_NO_WINDOW)
            identity = process_identity(self.child.pid)
            if not identity:
                self.child.wait(timeout=1)
                raise ValueError(tail(stderr) or '程序启动后立即退出，请查看日志。')
            record = {'process_id': self.child.pid, 'process_created': identity['created'], 'game': game,
                      'executable_path': str(self.executable), 'config_path': str(self.root / 'config.toml'),
                      'stdout_path': str(stdout), 'stderr_path': str(stderr),
                      'fusion_session': env['FUSION_SESSION'], 'fusion_channel_enabled': True,
                      'started_at_utc': datetime.now(timezone.utc).isoformat()}
            self.state_path.parent.mkdir(parents=True, exist_ok=True)
            temporary = self.state_path.with_suffix('.tmp')
            temporary.write_text(json.dumps(record, ensure_ascii=False, indent=2), encoding='utf-8')
            temporary.replace(self.state_path)
            self.stopping_pid = None
            return record

    def stop(self):
        with self.lock:
            record = self.active()
            if not record:
                return
            name = f'Local\\cod_native_runtime_stop_{record["process_id"]}'
            event = kernel.OpenEventW(2, False, name)
            if not event:
                raise ValueError('该运行实例不支持正常退出信号，请从原启动入口停止后更新程序。')
            try:
                if not kernel.SetEvent(event):
                    raise OSError('发送退出请求失败')
            finally:
                kernel.CloseHandle(event)
            self.stopping_pid = record['process_id']

    def reload_config(self):
        from .control import reload_config
        with self.lock, launch_lock(self.root):
            record = self.active()
            if not record:
                raise ValueError('应用未运行；设置会在下次启动时生效。')
            document = tomllib.loads((self.root / 'config.toml').read_text(encoding='utf-8-sig'))
            validate_fields(document, [record['game']])
            return reload_config(record)

    def learning(self):
        from .control import learning_state
        record = self.active()
        if not record:
            return None
        try:
            return learning_state(record)
        except (OSError, ValueError):
            return None

    def fusion_state(self):
        path = self.root / 'runs/fusion_canvas/background/fusion_canvas_state.json'
        try:
            record = json.loads(path.read_text(encoding='utf-8-sig'))
            identity = process_identity(record['process_id'])
            expected = self.root / 'native/build/Release/fusion_canvas.exe'
            if identity and identity['path'] == os.path.normcase(str(expected)) and (
                not record.get('process_created') or record['process_created'] == identity['created']):
                return record
        except (OSError, ValueError, KeyError, TypeError):
            pass
        return None

    def set_fusion(self, enabled):
        record = self.fusion_state()
        if enabled and record:
            return record
        if not enabled and not record:
            return None
        if enabled:
            if not self.active():
                raise ValueError('请先启动手柄应用，再打开 Fusion。')
            deadline = time.monotonic() + 25
            while not self.status().get('initialized'):
                if not self.active() or time.monotonic() >= deadline:
                    raise ValueError('原生程序尚未准备好，Fusion 未启动。')
                time.sleep(.05)
            # Start-Process descendants can inherit pipe handles on Windows.
            # Waiting for pipe EOF would wait for the canvas lifetime, even
            # after the launcher exits. Files give this invocation ownership
            # of launcher completion without tying it to the display process.
            logs = self.root / 'runs/fusion_canvas/background'
            logs.mkdir(parents=True, exist_ok=True)
            with (logs / 'gui-launch.stdout.log').open('wb') as out, (logs / 'gui-launch.stderr.log').open('wb') as err:
                result = subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                    str(self.root / 'scripts/launch/gamepad_fusion_background_start.ps1'), '-AttachOnly'], cwd=self.root,
                    stdout=out, stderr=err, stdin=subprocess.DEVNULL,
                    creationflags=CREATE_NO_WINDOW, timeout=50)
            if result.returncode:
                log = tail(self.root / 'runs/fusion_canvas/background/launcher.log')
                raise ValueError('Fusion 启动失败：\n' + (log.splitlines()[-1] if log else tail(logs / 'gui-launch.stderr.log')))
            record = self.fusion_state()
            if not record:
                raise ValueError('Fusion 未发布有效运行状态。')
            identity = process_identity(record['process_id'])
            record['process_created'] = identity['created']
            path = self.root / 'runs/fusion_canvas/background/fusion_canvas_state.json'
            path.write_text(json.dumps(record, ensure_ascii=False, indent=2), encoding='utf-8')
            return record
        # Close the owned canvas through its window lifecycle; the native
        # controller process is unaffected by this display switch.
        user32 = ctypes.WinDLL('user32', use_last_error=True)
        callback_type = ctypes.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
        user32.GetWindowThreadProcessId.argtypes = [w.HWND, ctypes.POINTER(w.DWORD)]
        user32.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
        windows = []
        def collect(window, _):
            pid = w.DWORD()
            user32.GetWindowThreadProcessId(window, ctypes.byref(pid))
            if pid.value == record['process_id']:
                windows.append(window)
            return True
        callback = callback_type(collect)
        user32.EnumWindows.argtypes = [callback_type, w.LPARAM]
        user32.EnumWindows(callback, 0)
        if not windows:
            raise ValueError('Fusion 窗口尚未就绪，无法正常关闭。')
        for window in windows:
            user32.PostMessageW(window, 0x0010, 0, 0)
        deadline = time.monotonic() + 5
        while self.fusion_state():
            if time.monotonic() >= deadline:
                raise ValueError('Fusion 尚未退出，请查看显示日志。')
            time.sleep(.05)
        return None

    def status(self):
        active = self.active()
        if not active:
            try:
                last = json.loads(self.state_path.read_text(encoding='utf-8-sig'))
                error = tail(last.get('stderr_path', ''))
                exit_code = self.child.poll() if self.child and self.child.pid == last['process_id'] else None
                started = 'initialized; entering controller loop' in startup_output(last.get('stdout_path', ''))
                if '[NativeRuntime][Error]' in error or exit_code not in (None, 0) or not started:
                    return {'phase': 'failed', 'record': None, 'last_record': last,
                            'error': error or f'程序在初始化完成前退出，退出码：{exit_code if exit_code is not None else "未知"}。'}
            except (OSError, ValueError, KeyError):
                pass
            if self.child:
                self.child.poll()
            return {'phase': 'stopped', 'record': None}
        output, error = tail(active['stdout_path']), tail(active['stderr_path'])
        startup = startup_output(active['stdout_path'])
        initialized = 'initialized; entering controller loop' in startup
        device = ''
        for line in startup.splitlines():
            if 'physical input backend=SDL' in line and 'name="' in line:
                device = line.split('name="', 1)[1].rsplit('"', 1)[0]
        connected = bool(device) or ('| connected' in startup)
        phase = 'stopping' if self.stopping_pid == active['process_id'] else (
            'running' if initialized and connected else 'waiting_device' if initialized else 'starting')
        return {'phase': phase, 'record': active, 'device': device or ('XInput 手柄' if connected else '未检测到手柄'),
                'virtual_connected': 'virtual DualShock 4 gamepad is online' in startup,
                'initialized': initialized, 'error': error, 'output': output}
