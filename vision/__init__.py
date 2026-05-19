from __future__ import annotations

from importlib import import_module
from typing import Any

__all__ = ["NativeVisionDebugOverlay", "VisionConfig", "process_native_vision", "process_vision"]


def __getattr__(name: str) -> Any:
    if name in {"NativeVisionDebugOverlay", "process_native_vision"}:
        native_runner = import_module(".native_runner", __name__)
        return getattr(native_runner, name)
    if name in {"VisionConfig", "process_vision"}:
        runner = import_module(".runner", __name__)
        return getattr(runner, name)
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
