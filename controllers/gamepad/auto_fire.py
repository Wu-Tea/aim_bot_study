from dataclasses import dataclass
from typing import Literal

from .state import GamepadFrame, GamepadOutput


_MANUAL_FIRE_TRIGGER_THRESHOLD = 10


@dataclass(slots=True, frozen=True)
class AutoFireConfig:
    fire_output: Literal["RB", "RT"] = "RB"
    aim_only: bool = True
    manual_takeover_release_seconds: float = 0.035
    manual_takeover_resume_delay_seconds: float = 0.085


class AutoFirePlugin:
    def __init__(self, config: AutoFireConfig | None = None):
        self.config = config or AutoFireConfig()
        self._manual_fire_was_pressed = False
        self._auto_fire_was_active = False
        self._manual_takeover_started_at: float | None = None

    def reset(self) -> None:
        self._manual_fire_was_pressed = False
        self._auto_fire_was_active = False
        self._manual_takeover_started_at = None

    def apply(self, frame: GamepadFrame, output: GamepadOutput) -> None:
        should_fire = frame.auto_fire_requested
        if self.config.aim_only:
            should_fire = should_fire and frame.is_aiming

        manual_fire_pressed = self._manual_fire_pressed(frame)
        manual_fire_started = manual_fire_pressed and not self._manual_fire_was_pressed
        auto_fire_was_active = self._auto_fire_was_active
        if manual_fire_started and (should_fire or auto_fire_was_active):
            self._manual_takeover_started_at = frame.timestamp

        takeover_elapsed = self._manual_takeover_elapsed(frame.timestamp)
        in_takeover_release = (
            takeover_elapsed is not None
            and takeover_elapsed < self._manual_takeover_release_seconds()
        )
        in_takeover_guard = (
            takeover_elapsed is not None
            and takeover_elapsed < self._manual_takeover_total_seconds()
        )
        if takeover_elapsed is not None and not in_takeover_guard:
            self._manual_takeover_started_at = None
            in_takeover_release = False
            in_takeover_guard = False

        self._manual_fire_was_pressed = manual_fire_pressed
        if manual_fire_pressed:
            output.auto_fire_active = False
            self._auto_fire_was_active = False
            if in_takeover_release:
                self._release_fire_output(output)
            return

        if in_takeover_guard:
            output.auto_fire_active = False
            self._auto_fire_was_active = False
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

    def _manual_takeover_elapsed(self, timestamp: float) -> float | None:
        if self._manual_takeover_started_at is None:
            return None
        return max(0.0, float(timestamp) - self._manual_takeover_started_at)

    def _manual_takeover_release_seconds(self) -> float:
        return max(0.0, float(self.config.manual_takeover_release_seconds))

    def _manual_takeover_total_seconds(self) -> float:
        return self._manual_takeover_release_seconds() + max(
            0.0,
            float(self.config.manual_takeover_resume_delay_seconds),
        )
