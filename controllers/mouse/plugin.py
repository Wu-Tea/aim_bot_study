from typing import Iterable, Protocol

from .state import MouseFrame, MouseOutput


class MousePlugin(Protocol):
    def reset(self) -> None: ...
    def apply(self, frame: MouseFrame, output: MouseOutput) -> None: ...


def apply_plugins(
    plugins: Iterable[MousePlugin],
    frame: MouseFrame,
    output: MouseOutput,
) -> None:
    for plugin in plugins:
        plugin.apply(frame, output)


def reset_plugins(plugins: Iterable[MousePlugin]) -> None:
    for plugin in plugins:
        plugin.reset()
