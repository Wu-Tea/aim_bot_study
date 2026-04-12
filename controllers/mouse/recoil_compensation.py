from dataclasses import dataclass

from .state import MouseFrame, MouseOutput


@dataclass(slots=True, frozen=True)
class RecoilCompensationConfig:
    amount_px: float = 3.0


class RecoilCompensationPlugin:
    def __init__(self, config: RecoilCompensationConfig | None = None):
        self.config = config or RecoilCompensationConfig()

    def reset(self) -> None:
        return None

    def apply(self, frame: MouseFrame, output: MouseOutput) -> None:
        if output.auto_fire_active and self.config.amount_px != 0.0:
            output.move_dy += self.config.amount_px
