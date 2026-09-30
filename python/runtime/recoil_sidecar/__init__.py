from importlib import import_module
from typing import Any

from .models import ActiveProfilePayload
from .models import RecognizerState
from .models import SidecarRuntimeContext

__all__ = [
    "ActiveProfilePayload",
    "RecognizerState",
    "RecoilSidecarService",
    "SidecarRuntimeContext",
]


def __getattr__(name: str) -> Any:
    if name != "RecoilSidecarService":
        raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
    service_module = import_module(".service", __name__)
    return service_module.RecoilSidecarService
