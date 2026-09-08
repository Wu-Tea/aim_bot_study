"""Bounded native relay diagnosis, defaulting to no-capture preflight.

Persists the actual shared lifecycle state independently of a stalled UI.
Only --capture opts into a two-second physical passthrough experiment.
"""
import argparse
import ctypes as c
import json
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


class State(c.Structure):
    _fields_ = [('magic', c.c_int32), ('source', c.c_int32), ('parent', c.c_uint32),
                ('simulate', c.c_bool), ('hang', c.c_bool), ('preflight', c.c_bool),
                ('hardware', c.c_wchar * 512)] + [
                    (name, c.c_int32) for name in
                    ('stop', 'ready', 'active', 'mode', 'error', 'phase', 'written', 'diagnostics_truncated')
                ] + [(name, c.c_int64) for name in ('heartbeat', 'parent_heartbeat', 'armed_at', 'released_at')]


def run(args):
    args.out.mkdir(parents=True, exist_ok=False)
    session = args.out / 'session'
    command = [str(args.exe.resolve()), '--source', str(args.source), '--out', str(session)]
    command += ['--seconds', '2'] if args.capture else ['--preflight-only']
    k = c.WinDLL('kernel32', use_last_error=True)
    k.OpenFileMappingW.argtypes = [c.c_uint32, c.c_int, c.c_wchar_p]
    k.OpenFileMappingW.restype = c.c_void_p
    k.MapViewOfFile.argtypes = [c.c_void_p, c.c_uint32, c.c_uint32, c.c_uint32, c.c_size_t]
    k.MapViewOfFile.restype = c.c_void_p
    k.UnmapViewOfFile.argtypes = [c.c_void_p]
    k.CloseHandle.argtypes = [c.c_void_p]
    handle = pointer = None
    snapshots = []
    with (args.out / 'console.log').open('wb') as log:
        process = subprocess.Popen(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        started = time.monotonic()
        try:
            while process.poll() is None and time.monotonic() - started < 7:
                mapping_name = session / 'mapping-name.txt'
                if not handle and mapping_name.exists():
                    name = mapping_name.read_text(encoding='utf-8')
                    if name:
                        handle = k.OpenFileMappingW(4, False, name)
                        if handle:
                            pointer = k.MapViewOfFile(handle, 4, 0, 0, c.sizeof(State))
                if pointer:
                    state = State.from_address(pointer)
                    snapshot = {name: getattr(state, name) for name in
                                ('phase', 'stop', 'ready', 'active', 'error', 'written', 'heartbeat',
                                 'parent_heartbeat', 'armed_at', 'released_at')}
                    snapshot['elapsed'] = round(time.monotonic() - started, 3)
                    snapshots.append(snapshot)
                time.sleep(.05)
            if process.poll() is None:
                worker_file = session / 'worker-pid.txt'
                pids = [process.pid] + ([int(worker_file.read_text())] if worker_file.exists() else [])
                debugger = Path(r'C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe')
                for pid in pids:
                    try:
                        result = subprocess.run([str(debugger), '-pv', '-p', str(pid), '-c', '~* kb;qd'],
                                                capture_output=True, timeout=4,
                                                creationflags=subprocess.CREATE_NO_WINDOW)
                        (args.out / f'stack-{pid}.txt').write_bytes(result.stdout + result.stderr)
                    except subprocess.TimeoutExpired:
                        (args.out / f'stack-{pid}.txt').write_text('Noninvasive stack query timed out.')
                for pid in reversed(pids):
                    # Only the parent created here and its recorded child.
                    subprocess.run(['taskkill', '/PID', str(pid), '/F'], capture_output=True, timeout=4,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
                process.wait(timeout=4)
        finally:
            if process.poll() is None:
                process.terminate()
            if pointer:
                k.UnmapViewOfFile(pointer)
            if handle:
                k.CloseHandle(handle)
    result = {'capture_requested': args.capture, 'parent_exit': process.returncode, 'snapshots': snapshots}
    (args.out / 'diagnosis.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps({'capture_requested': args.capture, 'parent_exit': process.returncode,
                      'last_state': snapshots[-1] if snapshots else None}, indent=2))
    print((args.out / 'console.log').read_text(encoding='utf-8', errors='replace'))
    return 0 if process.returncode == 0 else 2


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=int, default=13)
    parser.add_argument('--capture', action='store_true')
    parser.add_argument('--exe', type=Path, default=ROOT / 'artifacts/mouse_link/virtual-diagnostic-build/RelWithDebInfo/mouse_virtual_relay.exe')
    parser.add_argument('--out', type=Path, required=True)
    raise SystemExit(run(parser.parse_args()))
