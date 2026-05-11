from dataclasses import dataclass
from typing import Literal

from .state import GamepadFrame, GamepadOutput


_MANUAL_FIRE_TRIGGER_THRESHOLD = 10


@dataclass(slots=True, frozen=True)
class AutoFireConfig:
    fire_output: Literal["RB", "RT"] = "RB"
    aim_only: bool = True


class AutoFirePlugin:
    def __init__(self, config: AutoFireConfig | None = None):
        self.config = config or AutoFireConfig()
        self._manual_fire_was_pressed = False
        self._auto_fire_was_active = False

    def reset(self) -> None:
        self._manual_fire_was_pressed = False
        self._auto_fire_was_active = False

    def apply(self, frame: GamepadFrame, output: GamepadOutput) -> None:
        should_fire = frame.auto_fire_requested
        if self.config.aim_only:
            should_fire = should_fire and frame.is_aiming

        manual_fire_pressed = self._manual_fire_pressed(frame)
        manual_fire_started = manual_fire_pressed and not self._manual_fire_was_pressed
        auto_fire_was_active = self._auto_fire_was_active
        self._manual_fire_was_pressed = manual_fire_pressed
        if manual_fire_pressed:
            output.auto_fire_active = False
            self._auto_fire_was_active = False
            if manual_fire_started and (should_fire or auto_fire_was_active):
                self._release_fire_output(output)
            return

        output.auto_fire_active = should_fire
        self._auto_fire_was_active = should_fire
        if self.config.fire_output == "RB":
            output.buttons["rb"] = bool(output.buttons.get("rb", False) or should_fire)
            return

        if should_fire:
            output.right_trigger = 255

    def _manual_fire_pressed(self, frame: GamepadFrame) -> bool:
        return bool(frame.buttons.get("rb", False) or frame.right_trigger > _MANUAL_FIRE_TRIGGER_THRESHOLD)

    def _release_fire_output(self, output: GamepadOutput) -> None:
        output.buttons["rb"] = False
        output.right_trigger = 0
