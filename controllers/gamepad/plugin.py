from typing import Iterable, Protocol

from .state import GamepadFrame, GamepadOutput


class GamepadPlugin(Protocol):
    def reset(self) -> None:
        ...

    def apply(self, frame: GamepadFrame, output: GamepadOutput) -> None:
        ...


def apply_plugins(
    plugins: Iterable[GamepadPlugin],
    frame: GamepadFrame,
    output: GamepadOutput,
) -> None:
    for plugin in plugins:
        plugin.apply(frame, output)


def reset_plugins(plugins: Iterable[GamepadPlugin]) -> None:
    for plugin in plugins:
        plugin.reset()
