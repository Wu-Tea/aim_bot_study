from dataclasses import dataclass

from .state import MouseFrame, MouseOutput


@dataclass(slots=True, frozen=True)
class AutoFireConfig:
    aim_only: bool = True
    hold_seconds: float = 0.120
    release_seconds: float = 0.030


class AutoFirePlugin:
    def __init__(self, config: AutoFireConfig | None = None):
        self.config = config or AutoFireConfig()
        self._cycle_start: float | None = None

    def reset(self) -> None:
        self._cycle_start = None

    def apply(self, frame: MouseFrame, output: MouseOutput) -> None:
        want_fire = frame.auto_fire_requested
        if self.config.aim_only:
            want_fire = want_fire and frame.is_aiming

        if not want_fire:
            self._cycle_start = None
            output.left_click = False
            output.auto_fire_active = False
            return

        if self._cycle_start is None:
            self._cycle_start = frame.timestamp

        cycle_len = self.config.hold_seconds + self.config.release_seconds
        elapsed = (frame.timestamp - self._cycle_start) % cycle_len
        pressing = elapsed < self.config.hold_seconds

        output.left_click = pressing
        output.auto_fire_active = pressing
