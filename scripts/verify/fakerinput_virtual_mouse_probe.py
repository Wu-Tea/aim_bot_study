"""Explicit virtual HID output experiment; never installs drivers or captures input.

The default only connects to the existing FakerInput device. --send-motion sends
64 tiny round-trip relative moves with no clicks or wheel movement, recorded by
two independent instances of the existing passive Raw Input monitor. This is
not a physical exclusion, performance, or game acceptance test.
"""
from __future__ import annotations

import argparse
import csv
import ctypes as c
import hashlib
import json
from pathlib import Path
import queue
import subprocess
import threading
import time

ROOT = Path(__file__).resolve().parents[2]
RESEARCH = ROOT / 'artifacts/mouse_link/fakerinput-research-20260908'
PATTERN = [(1, 0), (-1, 0), (0, 1), (0, -1)] * 16


def start_receiver(listener: Path, directory: Path) -> subprocess.Popen:
    process = subprocess.Popen(
        [str(listener), '--seconds', '4', '--out', str(directory)], cwd=ROOT,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        creationflags=subprocess.CREATE_NO_WINDOW)
    ready: queue.Queue = queue.Queue()
    threading.Thread(target=lambda: ready.put(process.stdout.readline()), daemon=True).start()
    try:
        line = ready.get(timeout=3)
        if not line.startswith(b'Listening:'):
            raise RuntimeError(f'Listener not ready: {line!r}')
    except BaseException:
        if process.poll() is None:
            process.terminate()  # Owned passive child only.
        process.wait(timeout=3)
        raise
    return process


def measure(directory: Path, expected_path: str) -> dict:
    with (directory / 'devices.csv').open(encoding='utf-8', newline='') as handle:
        devices = list(csv.DictReader(handle))
    candidates = [d for d in devices if d['name_query_succeeded'] == '1'
                  and d['device_path'].casefold() == expected_path.casefold()]
    if len(candidates) != 1:
        raise RuntimeError('Expected exactly one previously identified virtual Raw Input device')
    selected = candidates[0]
    with (directory / 'events.csv').open(encoding='utf-8', newline='') as handle:
        events = list(csv.DictReader(handle))
    raw = [r for r in events if r['stream'] == 'raw'
           and r['device_handle'] == selected['device_handle']]
    motion = [r for r in raw if int(r['x']) or int(r['y'])]
    actual = [(int(r['x']), int(r['y'])) for r in motion]
    health = json.loads((directory / 'summary.json').read_text(encoding='utf-8'))
    return {
        'device_path': selected['device_path'], 'device_handle': selected['device_handle'],
        'motion_packets': len(actual), 'sequence_matches': actual == PATTERN,
        'net_x': sum(x for x, y in actual), 'net_y': sum(y for x, y in actual),
        'absolute_x': sum(abs(x) for x, y in actual),
        'absolute_y': sum(abs(y) for x, y in actual),
        'all_motion_relative': all(r['coordinate_kind'] == 'relative_counts' for r in motion),
        'button_or_wheel_packets': sum(int(r['buttons']) != 0 for r in raw),
        'dropped': health['dropped'], 'read_errors': health['read_errors'],
        'mouse_devices_enumerated': len(devices),
    }


def matches(result: dict) -> bool:
    return (result['sequence_matches'] and result['all_motion_relative']
            and result['button_or_wheel_packets'] == 0
            and result['dropped'] == 0 and result['read_errors'] == 0)


def run(args) -> int:
    args.out.mkdir(parents=True, exist_ok=False)
    library = c.CDLL(str(args.dll.resolve()), use_last_error=True)
    signatures = {
        'alloc': ([], c.c_void_p), 'free': ([c.c_void_p], None),
        'connect': ([c.c_void_p], c.c_bool), 'disconnect': ([c.c_void_p], None),
        'versionAPINumber': ([c.c_void_p], c.c_uint32),
        'driverVersionNumber': ([c.c_void_p], c.c_uint32),
        'update_relative_mouse': ([c.c_void_p, c.c_ubyte, c.c_short, c.c_short,
                                   c.c_ubyte, c.c_ubyte], c.c_bool),
    }
    for name, (arguments, result) in signatures.items():
        function = getattr(library, 'fakerinput_' + name)
        function.argtypes, function.restype = arguments, result
    client = library.fakerinput_alloc()
    if not client:
        raise MemoryError('FakerInput client allocation failed')
    connected = False
    processes = []
    report = {
        'dll_sha256': hashlib.sha256(args.dll.read_bytes()).hexdigest(),
        'listener_sha256': hashlib.sha256(args.listener.read_bytes()).hexdigest(),
        'connected': False, 'sent': [], 'receivers': [], 'error': None,
        'virtual_output_verified': False, 'physical_exclusion_verified': False,
        'game_receipt_verified': False, 'rate_1000hz_verified': False,
        'driver_install_performed': False,
    }
    try:
        connected = library.fakerinput_connect(client)
        report['connected'] = connected
        if not connected:
            raise RuntimeError(f'FakerInput connect failed: {c.get_last_error()}')
        report['api_version'] = library.fakerinput_versionAPINumber(client)
        report['driver_hid_version'] = library.fakerinput_driverVersionNumber(client)
        if report['api_version'] != 1:
            raise RuntimeError('Unsupported FakerInput API version')
        if args.send_motion:
            if not args.expected_raw_path:
                raise RuntimeError('--expected-raw-path is required and must come from PnP/Raw device inspection')
            for index in range(2):
                processes.append(start_receiver(args.listener, args.out / f'receiver_{index}'))
            for x, y in PATTERN:
                before = time.perf_counter_ns()
                accepted = library.fakerinput_update_relative_mouse(client, 0, x, y, 0, 0)
                report['sent'].append({'x': x, 'y': y, 'accepted': accepted,
                                       'call_ns': time.perf_counter_ns() - before})
                if not accepted:
                    raise RuntimeError(f'Virtual mouse write failed: {c.get_last_error()}')
                time.sleep(0.008)  # Slow explicit stimulus, not a rate measurement.
    except Exception as error:
        report['error'] = str(error)
    finally:
        if connected:
            library.fakerinput_disconnect(client)
        # A failed upstream connect handles its own partial cleanup. Do not
        # disconnect an uninitialized allocation after an early open failure.
        library.fakerinput_free(client)
        for index, process in enumerate(processes):
            try:
                output, _ = process.communicate(timeout=7)
                (args.out / f'receiver_{index}.log').write_bytes(output)
                if process.returncode:
                    raise RuntimeError(f'Receiver {index} exited {process.returncode}')
                report['receivers'].append(measure(args.out / f'receiver_{index}', args.expected_raw_path))
            except Exception as error:
                report['error'] = str(error)
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=3)
        report['virtual_output_verified'] = (
            report['error'] is None and len(report['sent']) == len(PATTERN)
            and len(report['receivers']) == 2 and all(matches(r) for r in report['receivers']))
        (args.out / 'summary.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps({k: v for k, v in report.items() if k != 'sent'}, indent=2))
    return 0 if report['error'] is None and (not args.send_motion or report['virtual_output_verified']) else 2


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dll', type=Path, default=RESEARCH / 'client-build/FakerInputDll.dll')
    parser.add_argument('--listener', type=Path, default=RESEARCH / 'monitor-build/Release/mouse_input_monitor.exe')
    parser.add_argument('--out', type=Path, required=True, help='New output directory')
    parser.add_argument('--send-motion', action='store_true')
    parser.add_argument('--expected-raw-path', help='Exact virtual mouse path, correlated with PnP parent/collection')
    raise SystemExit(run(parser.parse_args()))
