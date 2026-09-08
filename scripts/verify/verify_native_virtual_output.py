"""Explicitly send 64 tiny HID movements through the relay's C++ backend.

Two passive receiver processes independently check the exact output sequence.
This never captures physical input, presses buttons, or installs drivers.
"""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from fakerinput_virtual_mouse_probe import ROOT, start_receiver, measure, matches


def run(args):
    args.out.mkdir(parents=True, exist_ok=False)
    processes = []
    result = {'native_sender_sha256': hashlib.sha256(args.sender.read_bytes()).hexdigest(),
              'error': None, 'receivers': [], 'virtual_output_verified': False,
              'physical_exclusion_verified': False}
    try:
        for index in range(2):
            processes.append(start_receiver(args.listener, args.out / f'receiver_{index}'))
        sender = subprocess.run([str(args.sender.resolve()), '--send-motion'], cwd=ROOT,
                                capture_output=True, timeout=8, creationflags=subprocess.CREATE_NO_WINDOW)
        (args.out / 'sender.log').write_bytes(sender.stdout + sender.stderr)
        result['sender_exit'] = sender.returncode
        if sender.returncode:
            raise RuntimeError(f'Native output sender failed: {sender.returncode}')
    except Exception as error:
        result['error'] = str(error)
    finally:
        for index, process in enumerate(processes):
            try:
                data, _ = process.communicate(timeout=7)
                (args.out / f'receiver_{index}.log').write_bytes(data)
                if process.returncode:
                    raise RuntimeError(f'Receiver failed: {process.returncode}')
                result['receivers'].append(measure(args.out / f'receiver_{index}', args.expected_raw_path))
            except Exception as error:
                result['error'] = str(error)
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=3)
        result['virtual_output_verified'] = (result['error'] is None and len(result['receivers']) == 2
                                             and all(matches(r) for r in result['receivers']))
        (args.out / 'summary.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result, indent=2))
    return 0 if result['virtual_output_verified'] else 2


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    build = ROOT / 'artifacts/mouse_link/virtual-build/Release'
    parser.add_argument('--sender', type=Path, default=build / 'mouse_virtual_output_probe.exe')
    parser.add_argument('--listener', type=Path, default=build / 'mouse_input_monitor.exe')
    parser.add_argument('--expected-raw-path', required=True)
    parser.add_argument('--out', type=Path, required=True)
    raise SystemExit(run(parser.parse_args()))
