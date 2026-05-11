from __future__ import annotations

from importlib import import_module
from typing import Any

__all__ = [
    "GamepadRecoilBridge",
    "IdentityStore",
    "RecoilAppConfig",
    "RecoilProfileStore",
    "RecoilRuntime",
]


def __getattr__(name: str) -> Any:
    if name not in __all__:
        raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
    runtime_module = import_module(".runtime", __name__)
    return getattr(runtime_module, name)
