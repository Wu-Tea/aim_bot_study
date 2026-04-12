from dataclasses import dataclass

from .state import MouseFrame, MouseOutput


@dataclass(slots=True, frozen=True)
class AutoFireConfig:
    aim_only: bool = True


class AutoFirePlugin:
    def __init__(self, config: AutoFireConfig | None = None):
        self.config = config or AutoFireConfig()

    def reset(self) -> None:
        return None

    def apply(self, frame: MouseFrame, output: MouseOutput) -> None:
        should_fire = frame.auto_fire_requested
        if self.config.aim_only:
            should_fire = should_fire and frame.is_aiming
        output.left_click = should_fire
        output.auto_fire_active = should_fire
