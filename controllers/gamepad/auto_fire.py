from dataclasses import dataclass
from typing import Literal

from .state import GamepadFrame, GamepadOutput


@dataclass(slots=True, frozen=True)
class AutoFireConfig:
    fire_output: Literal["RB", "RT"] = "RB"
    aim_only: bool = True


class AutoFirePlugin:
    def __init__(self, config: AutoFireConfig | None = None):
        self.config = config or AutoFireConfig()

    def reset(self) -> None:
        return None

    def apply(self, frame: GamepadFrame, output: GamepadOutput) -> None:
        should_fire = frame.auto_fire_requested
        if self.config.aim_only:
            should_fire = should_fire and frame.is_aiming

        output.auto_fire_active = should_fire
        if self.config.fire_output == "RB":
            output.buttons["rb"] = bool(output.buttons.get("rb", False) or should_fire)
            return

        if should_fire:
            output.right_trigger = 255
