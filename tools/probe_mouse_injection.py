import argparse
import sys
import time
from pathlib import Path

import win32api

PROJECT_ROOT = Path(__file__).resolve().parent.parent
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from controllers.mouse_controller import (
    MOUSEEVENTF_MOVE,
    _SendInputInjector,
    _Win32MouseEventInjector,
)


def _injector_for_backend(backend: str):
    normalized = backend.strip().lower()
    if normalized in {"sendinput", "send_input"}:
        return _SendInputInjector()
    if normalized in {"mouse_event", "mouseevent"}:
        return _Win32MouseEventInjector()
    raise ValueError(f"unknown backend: {backend}")


def probe_backend(backend: str, *, dx: int, dy: int, settle_seconds: float) -> dict:
    injector = _injector_for_backend(backend)
    start = win32api.GetCursorPos()
    injector.send(MOUSEEVENTF_MOVE, dx, dy)
    time.sleep(settle_seconds)
    after = win32api.GetCursorPos()
    injector.send(MOUSEEVENTF_MOVE, -dx, -dy)
    time.sleep(settle_seconds)
    end = win32api.GetCursorPos()
    observed_dx = after[0] - start[0]
    observed_dy = after[1] - start[1]
    accepted = _same_direction(dx, observed_dx) or _same_direction(dy, observed_dy)
    return {
        "backend": injector.name,
        "start": start,
        "after": after,
        "end": end,
        "requested_dx": dx,
        "requested_dy": dy,
        "observed_dx": observed_dx,
        "observed_dy": observed_dy,
        "accepted": accepted,
    }


def _same_direction(requested: int, observed: int) -> bool:
    if requested == 0:
        return False
    if observed == 0:
        return False
    return (requested > 0 and observed > 0) or (requested < 0 and observed < 0)


def _format_result(result: dict) -> str:
    return (
        f"backend={result['backend']} "
        f"requested=({result['requested_dx']},{result['requested_dy']}) "
        f"observed=({result['observed_dx']},{result['observed_dy']}) "
        f"start={result['start']} after={result['after']} end={result['end']} "
        f"accepted={result['accepted']}"
    )


def _parse_args():
    parser = argparse.ArgumentParser(
        description="Probe whether a mouse injection backend moves the Windows cursor."
    )
    parser.add_argument(
        "--backend",
        choices=("sendinput", "mouse_event", "both"),
        default="both",
        help="Injection backend to probe.",
    )
    parser.add_argument("--dx", type=int, default=24)
    parser.add_argument("--dy", type=int, default=0)
    parser.add_argument("--settle", type=float, default=0.050)
    return parser.parse_args()


def main() -> int:
    args = _parse_args()
    backends = ("sendinput", "mouse_event") if args.backend == "both" else (args.backend,)
    results = [
        probe_backend(backend, dx=args.dx, dy=args.dy, settle_seconds=args.settle)
        for backend in backends
    ]
    for result in results:
        print(_format_result(result))
    return 0 if all(result["accepted"] for result in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
