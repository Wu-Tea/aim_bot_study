from dataclasses import dataclass

from .state import GamepadFrame, GamepadOutput


@dataclass(slots=True, frozen=True)
class RecoilCompensationConfig:
    amount: float = 0.30


class RecoilCompensationPlugin:
    def __init__(self, config: RecoilCompensationConfig | None = None):
        self.config = config or RecoilCompensationConfig()

    def reset(self) -> None:
        return None

    def apply(self, frame: GamepadFrame, output: GamepadOutput) -> None:
        if output.auto_fire_active and self.config.amount != 0.0:
            output.right_y -= int(self.config.amount * 32767)
