"""Opt-in diagnostic session attachment for the normal desktop launcher."""
from datetime import datetime, timezone
import json
from pathlib import Path
import shutil
import subprocess
import time


def _process_running(pid, created=None):
    """Query Windows, distinguishing absence from access/query failures and PID reuse."""
    import ctypes
    from ctypes import wintypes as w
    from .runtime import kernel
    handle = kernel.OpenProcess(0x1000, False, int(pid))
    if not handle:
        error = ctypes.get_last_error()
        if error == 87:  # ERROR_INVALID_PARAMETER: PID no longer exists.
            return False
        raise ctypes.WinError(error)
    try:
        code = w.DWORD()
        if not kernel.GetExitCodeProcess(handle, ctypes.byref(code)):
            raise ctypes.WinError(ctypes.get_last_error())
        if code.value != 259:
            return False
        if created is not None:
            times = [w.FILETIME() for _ in range(4)]
            if not kernel.GetProcessTimes(handle, *[ctypes.byref(value) for value in times]):
                raise ctypes.WinError(ctypes.get_last_error())
            actual = (times[0].dwHighDateTime << 32) | times[0].dwLowDateTime
            if actual != int(created):
                return False
        return True
    finally:
        kernel.CloseHandle(handle)


def collectors_running(base, health):
    """True/False from process identities; None means old evidence is insufficient.

    Old sessions recorded PIDs without creation times. A live reused PID in those
    sessions is conservatively treated as running; it is never terminated here.
    """
    identities = health.get('processes')
    if identities is not None:
        if set(identities) != {'monitor', 'gpu', 'presentmon'}:
            raise ValueError('采集进程身份记录不完整。')
        return any(_process_running(item['pid'], item['created'])
                   for item in identities.values() if item is not None)
    environment = base / 'environment.json'
    if not health.get('pid') or not environment.is_file():
        return None
    info = json.loads(environment.read_text(encoding='utf-8-sig'))
    if not info.get('nvidia_pid') or not info.get('presentmon_pid'):
        return None
    return any(_process_running(pid) for pid in
               (health['pid'], info['nvidia_pid'], info['presentmon_pid']))


def _clean_completion(health):
    return health.get('complete') and not health.get('gpu_running') and not health.get('presentmon_running')


def capture_status(root, record=None, now=None):
    """Report collector readiness without breaking the runtime status RPC on bad logs."""
    result = dict(enabled=False, state='off', message='已关闭', directory=None)
    marker = Path(root) / 'runs/desktop/long-capture.json'
    if not marker.is_file():
        return result
    try:
        info = json.loads(marker.read_text(encoding='utf-8-sig'))
        base = Path(info['directory']).resolve()
        if not base.is_relative_to((Path(root) / 'output').resolve()):
            raise ValueError('采集目录必须位于项目 output 内。')
        result['directory'] = str(base)
        now = now or datetime.now(timezone.utc)
        heartbeat = base / 'monitor-health.json'
        if not info.get('enabled') or (base / 'stop').exists():
            health = json.loads(heartbeat.read_text(encoding='utf-8-sig')) if heartbeat.is_file() else {}
            if _clean_completion(health):
                return result
            running = collectors_running(base, health)
            if running is False:
                return dict(result, message='已关闭；上次采集异常结束，已有日志保留')
            if running is True:
                return dict(result, state='stopping', message='正在停止系统监测')
            return dict(result, state='error', message='已禁止后续采集，但缺少进程记录，无法确认系统监测是否停止')
        if datetime.fromisoformat(info['expires_utc']) <= now:
            return dict(result, state='expired', message='已到期；如需继续，请重新开启')
        result['enabled'] = True
        if not heartbeat.is_file() or now.timestamp() - heartbeat.stat().st_mtime > 30:
            raise ValueError('采集监测无响应；请关闭后重新开启')
        health = json.loads(heartbeat.read_text(encoding='utf-8-sig'))
        if health.get('failed') or health.get('complete') or not health.get('gpu_running') or not health.get('presentmon_running'):
            raise ValueError('采集异常：' + (health.get('reason') or '系统监测未运行'))
        if collectors_running(base, health) is False:
            raise ValueError('采集进程已退出；请关闭后重新开启')
        attached = record and record.get('capture_directory')
        if attached and Path(attached).resolve().is_relative_to(base / 'runtime'):
            return dict(result, state='recording', message='正在采集')
        if record:
            return dict(result, state='unattached', message='系统监测已开启；当前助手未接入，请停止后重新启动助手')
        return dict(result, state='armed', message='已开启，等待启动助手')
    except (OSError, ValueError, KeyError, TypeError) as error:
        return dict(result, state='error', message=str(error))


def _capture_script(root, name):
    shell = shutil.which('pwsh') or shutil.which('powershell')
    if not shell:
        raise ValueError('找不到 PowerShell，无法控制性能采集。')
    log = Path(root) / 'runs/desktop/capture-control.log'
    log.parent.mkdir(parents=True, exist_ok=True)
    # Start-Process descendants inherit handles on Windows. A pipe would keep
    # communicate() waiting for the 12-hour monitor even after arm.ps1 exits.
    with log.open('a+b') as output:
        start = output.tell()
        result = subprocess.run([shell, '-NoProfile', '-NonInteractive', '-File',
            str(Path(root) / 'tools/long_capture' / name)], cwd=root,
            stdin=subprocess.DEVNULL, stdout=output, stderr=subprocess.STDOUT,
            timeout=45, creationflags=subprocess.CREATE_NO_WINDOW)
        if result.returncode:
            output.seek(start)
            raise ValueError('性能采集操作失败：' + output.read().decode('utf-8', errors='replace').strip())


def set_capture_enabled(root, enabled):
    """Caller holds the runtime launch lock and has verified the assistant is stopped."""
    state = capture_status(root)
    if enabled and state['state'] == 'armed':
        return state
    marker = Path(root) / 'runs/desktop/long-capture.json'
    if marker.is_file():
        _capture_script(root, 'stop.ps1')
        # Completion is either an explicit terminal acknowledgement or all owned
        # processes having exited. A crashed monitor cannot update its heartbeat.
        health_path = Path(state['directory']) / 'monitor-health.json' if state['directory'] else None
        until = time.monotonic() + 15
        while health_path and health_path.is_file():
            health = json.loads(health_path.read_text(encoding='utf-8-sig'))
            if _clean_completion(health) or collectors_running(health_path.parent, health) is False:
                break
            if time.monotonic() >= until:
                raise ValueError('已关闭后续采集，但系统监测尚未确认停止。请检查采集目录。')
            time.sleep(.2)
    if enabled:
        _capture_script(root, 'arm.ps1')
        state = capture_status(root)
        if state['state'] != 'armed':
            raise ValueError(state['message'])
        return state
    return capture_status(root)


def attach_capture(root, session, config_path, env, now=None):
    marker = Path(root) / 'runs/desktop/long-capture.json'
    if not marker.is_file():
        return None
    info = json.loads(marker.read_text(encoding='utf-8-sig'))
    if not info.get('enabled'):
        return None
    now = now or datetime.now(timezone.utc)
    if datetime.fromisoformat(info['expires_utc']) <= now:
        return None
    base = Path(info['directory']).resolve()
    allowed = (Path(root) / 'output').resolve()
    if not base.is_relative_to(allowed):
        raise ValueError('采集目录必须位于项目 output 内。')
    if (base / 'stop').exists():
        return None
    heartbeat = base / 'monitor-health.json'
    if not heartbeat.is_file() or now.timestamp() - heartbeat.stat().st_mtime > 30:
        raise ValueError('长时采集监测进程未就绪，无法保证日志完整。请重新启动长时采集。')
    health = json.loads(heartbeat.read_text(encoding='utf-8-sig'))
    if health.get('failed') or not health.get('gpu_running') or not health.get('presentmon_running'):
        raise ValueError('长时采集器异常，请检查采集目录的 monitor-health.json。')
    if collectors_running(base, health) is False:
        raise ValueError('长时采集进程已退出，请关闭后重新开启性能采集。')
    folder = base / 'runtime' / session
    folder.mkdir(parents=True, exist_ok=False)
    shutil.copy2(config_path, folder / 'source-config.toml')
    env['AIMLAB_LIVE_TRACE_DIR'] = str(folder / 'trace')
    env['AIMLAB_DIAGNOSTIC_SESSION_DIR'] = str(folder)
    (folder / 'launch.json').write_text(json.dumps({
        'started_utc': now.isoformat(), 'source_config': str(config_path),
        'desktop_session': session, 'schema': 'long-capture-v2',
    }, ensure_ascii=False, indent=2), encoding='utf-8')
    executable = Path(root) / 'native/build/Release/cod_native_runtime.exe'
    if executable.is_file():
        stat = executable.stat()
        (folder / 'build.json').write_text(json.dumps({'path': str(executable),
            'bytes': stat.st_size, 'modified_ns': stat.st_mtime_ns}), encoding='utf-8')
    return folder
