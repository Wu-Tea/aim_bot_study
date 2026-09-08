"""Explicit Windows input experiment; sends only tagged +/-1-count movements.

Two independent instances of the passive listener are the receivers. This is
not a physical-input exclusion test, a driver installer, or a game acceptance
test. No user input is blocked and no button events are generated.
"""
from __future__ import annotations

import argparse
import csv
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import queue
import secrets
import subprocess
import threading
import time

ROOT = Path(__file__).resolve().parents[2]
REPORTS = 64


def write_json(path: Path, data: object) -> None:
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")


def receiver_measurement(directory: Path, tag: int) -> dict:
    with (directory / "events.csv").open(encoding="utf-8", newline="") as stream:
        rows = [r for r in csv.DictReader(stream) if int(r["extra_info"]) == tag]
    raw = [r for r in rows if r["stream"] == "raw"]
    hook = [r for r in rows if r["stream"] == "ll"]
    summary = json.loads((directory / "summary.json").read_text(encoding="utf-8"))
    return {
        "raw_packets": len(raw),
        "raw_x": sum(int(r["x"]) for r in raw),
        "raw_y": sum(int(r["y"]) for r in raw),
        "raw_abs_x": sum(abs(int(r["x"])) for r in raw),
        "raw_abs_y": sum(abs(int(r["y"])) for r in raw),
        "raw_device_associated": sum(int(r["device_handle"]) != 0 for r in raw),
        "raw_relative": sum(r["coordinate_kind"] == "relative_counts" for r in raw),
        "hook_packets": len(hook),
        "hook_injected": sum(bool(int(r["flags"]) & 1) for r in hook),
        "dropped": summary["dropped"],
        "read_errors": summary["read_errors"],
        "no_legacy": summary["no_legacy"],
    }


def receiver_matches(measured: dict, expected: int) -> bool:
    return (
        measured["raw_packets"] == expected
        and measured["hook_packets"] == expected
        and measured["hook_injected"] == expected
        and measured["raw_relative"] == expected
        and measured["raw_x"] == 0 and measured["raw_y"] == 0
        and measured["raw_abs_x"] == expected and measured["raw_abs_y"] == expected
        and measured["dropped"] == 0 and measured["read_errors"] == 0
    )


def run(output: Path, listener: Path) -> int:
    if os.name != "nt":
        raise RuntimeError("Windows desktop required")
    if not listener.is_file():
        raise RuntimeError("Build with scripts/verify/build_mouse_input_monitor.ps1 first")
    output.mkdir(parents=True, exist_ok=False)
    user32 = ctypes.WinDLL("user32", use_last_error=True)

    class MouseInput(ctypes.Structure):
        _fields_ = [("dx", wintypes.LONG), ("dy", wintypes.LONG),
                    ("mouseData", wintypes.DWORD), ("dwFlags", wintypes.DWORD),
                    ("time", wintypes.DWORD), ("dwExtraInfo", ctypes.c_size_t)]

    class InputUnion(ctypes.Union):
        _fields_ = [("mi", MouseInput)]  # MOUSEINPUT is the largest INPUT union member.

    class Input(ctypes.Structure):
        _anonymous_ = ("payload",)
        _fields_ = [("type", wintypes.DWORD), ("payload", InputUnion)]

    class RawDevice(ctypes.Structure):
        _fields_ = [("handle", wintypes.HANDLE), ("type", wintypes.DWORD)]

    user32.SendInput.argtypes = [wintypes.UINT, ctypes.POINTER(Input), ctypes.c_int]
    user32.SendInput.restype = wintypes.UINT
    user32.GetRawInputDeviceList.argtypes = [ctypes.POINTER(RawDevice), ctypes.POINTER(wintypes.UINT), wintypes.UINT]
    user32.GetRawInputDeviceList.restype = wintypes.UINT

    def devices() -> list[int]:
        count = wintypes.UINT()
        if user32.GetRawInputDeviceList(None, ctypes.byref(count), ctypes.sizeof(RawDevice)) == 0xFFFFFFFF:
            raise ctypes.WinError(ctypes.get_last_error())
        values = (RawDevice * count.value)()
        found = user32.GetRawInputDeviceList(values, ctypes.byref(count), ctypes.sizeof(RawDevice))
        if found == 0xFFFFFFFF:
            raise RuntimeError("Device enumeration changed/failed; this measurement is invalid")
        return sorted(int(values[i].handle or 0) for i in range(found) if values[i].type == 0)

    def start_receiver(directory: Path, tag: int, no_legacy: bool) -> subprocess.Popen:
        arguments = [str(listener), "--seconds", "5", "--out", str(directory), "--tag", hex(tag)]
        if no_legacy:
            arguments.append("--no-legacy")
        process = subprocess.Popen(arguments, cwd=ROOT, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
        ready: queue.Queue[bytes] = queue.Queue()
        reader = threading.Thread(target=lambda: ready.put(process.stdout.readline()), daemon=True)
        reader.start()
        try:
            line = ready.get(timeout=4)
            if not line.startswith(b"Listening:"):
                raise RuntimeError(f"Receiver registration failed: {line.decode(errors='replace')}")
        except BaseException:
            if process.poll() is None:
                process.terminate()  # Only this owned passive child, which never captures input.
            process.wait(timeout=4)
            raise
        return process

    phases = []
    for name, receiver_count, first_no_legacy in [
        ("single_receiver", 1, False),
        ("two_receivers", 2, False),
        ("two_receivers_one_no_legacy", 2, True),
    ]:
        phase_dir = output / name
        phase_dir.mkdir()
        tag = 0x60000000 | secrets.randbits(24)
        processes = []
        before = devices()
        try:
            for index in range(receiver_count):
                processes.append(start_receiver(phase_dir / f"receiver_{index}", tag,
                                                first_no_legacy and index == 0))
            registered = devices()
            payload = (Input * REPORTS)()
            for index in range(REPORTS):
                payload[index].type = 0  # INPUT_MOUSE
                payload[index].mi.dx = 1 if index % 2 == 0 else -1
                payload[index].mi.dy = 1 if index % 2 == 0 else -1
                payload[index].mi.dwFlags = 0x0001 | 0x2000  # MOVE | MOVE_NOCOALESCE
                payload[index].mi.dwExtraInfo = tag
            submitted = int(user32.SendInput(REPORTS, payload, ctypes.sizeof(Input)))
            after_send = devices()
            exits = []
            for index, process in enumerate(processes):
                stdout, _ = process.communicate(timeout=9)
                (phase_dir / f"receiver_{index}.log").write_bytes(stdout)
                exits.append(process.returncode)
            after_close = devices()
        finally:
            for process in processes:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=4)
        receivers = [receiver_measurement(phase_dir / f"receiver_{i}", tag)
                     for i in range(receiver_count)]
        stable_devices = before == registered == after_send == after_close
        phase = {
            "name": name, "tag": tag, "submitted": submitted, "receiver_exit_codes": exits,
            "devices_before": before, "devices_registered": registered,
            "devices_after_send": after_send, "devices_after_close": after_close,
            "device_set_unchanged": stable_devices, "receivers": receivers,
            "passed": submitted == REPORTS and all(code == 0 for code in exits)
                      and stable_devices and all(receiver_matches(r, REPORTS) for r in receivers),
        }
        phases.append(phase)
        write_json(phase_dir / "measurement.json", phase)
        print(name, "PASS" if phase["passed"] else "FAIL", "sent=", submitted,
              "raw_received=", [r["raw_packets"] for r in receivers],
              "devices=", len(before), "->", len(after_send), flush=True)

    # Freeze oracles above; prove that a missing receiver or doubled reports would fail.
    control = dict(phases[0]["receivers"][0])
    missing = dict(control, raw_packets=0)
    doubled = dict(control, raw_packets=REPORTS * 2)
    negative_controls = {"missing_receiver_rejected": not receiver_matches(missing, REPORTS),
                         "duplicate_reports_rejected": not receiver_matches(doubled, REPORTS)}
    report = {
        "schema": 1, "stimulus": "controlled_SendInput_not_physical_mouse",
        "listener_sha256": hashlib.sha256(listener.read_bytes()).hexdigest(),
        "runner_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "phases": phases, "negative_controls": negative_controls,
        "experiment_passed": all(p["passed"] for p in phases) and all(negative_controls.values()),
        "physical_exclusion_verified": False, "virtual_mouse_created": False,
        "driver_loaded_by_experiment": False, "game_receipt_verified": False,
    }
    write_json(output / "summary.json", report)
    print("Report:", output / "summary.json", flush=True)
    return 0 if report["experiment_passed"] else 2


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / "runs/mouse_input_monitor" /
                        ("user-mode-feasibility-" + str(time.time_ns())))
    parser.add_argument("--listener", type=Path, default=ROOT /
                        "artifacts/mouse_input_monitor/build/Release/mouse_input_monitor.exe")
    args = parser.parse_args()
    raise SystemExit(run(args.out.resolve(), args.listener.resolve()))
