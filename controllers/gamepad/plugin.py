from dataclasses import dataclass
from typing import Iterable, Protocol

from .state import GamepadFrame, GamepadOutput


class GamepadPlugin(Protocol):
    def reset(self) -> None:
        ...

    def apply(self, frame: GamepadFrame, output: GamepadOutput) -> None:
        ...


@dataclass(slots=True, frozen=True)
class PluginApplicationTrace:
    plugin_name: str
    before_right_y: int
    after_right_y: int
    delta_right_y: int
    before_auto_fire_active: bool
    after_auto_fire_active: bool


def apply_plugins(
    plugins: Iterable[GamepadPlugin],
    frame: GamepadFrame,
    output: GamepadOutput,
) -> None:
    for plugin in plugins:
        plugin.apply(frame, output)


def apply_plugins_with_trace(
    plugins: Iterable[GamepadPlugin],
    frame: GamepadFrame,
    output: GamepadOutput,
) -> list[PluginApplicationTrace]:
    traces: list[PluginApplicationTrace] = []
    for plugin in plugins:
        before_right_y = int(output.right_y)
        before_auto_fire_active = bool(output.auto_fire_active)
        plugin.apply(frame, output)
        after_right_y = int(output.right_y)
        after_auto_fire_active = bool(output.auto_fire_active)
        traces.append(
            PluginApplicationTrace(
                plugin_name=_plugin_name(plugin),
                before_right_y=before_right_y,
                after_right_y=after_right_y,
                delta_right_y=after_right_y - before_right_y,
                before_auto_fire_active=before_auto_fire_active,
                after_auto_fire_active=after_auto_fire_active,
            )
        )
    return traces


def reset_plugins(plugins: Iterable[GamepadPlugin]) -> None:
    for plugin in plugins:
        plugin.reset()


def _plugin_name(plugin: GamepadPlugin) -> str:
    custom_name = getattr(plugin, "name", None)
    if isinstance(custom_name, str) and custom_name:
        return custom_name
    return type(plugin).__name__
