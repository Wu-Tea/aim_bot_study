from dataclasses import dataclass, field
from typing import Mapping


@dataclass(slots=True, frozen=True)
class GamepadFrame:
    timestamp: float
    left_x: int
    left_y: int
    manual_right_x: int
    manual_right_y: int
    left_trigger: int
    right_trigger: int
    buttons: Mapping[str, bool]
    is_aiming: bool
    target_dx: float
    target_dy: float
    auto_fire_requested: bool
    dpad: int = 0
    target_revision: int = 0
    target_timestamp: float | None = None


@dataclass(slots=True)
class GamepadOutput:
    left_x: int = 0
    left_y: int = 0
    right_x: int = 0
    right_y: int = 0
    left_trigger: int = 0
    right_trigger: int = 0
    buttons: dict[str, bool] = field(default_factory=dict)
    dpad: int = 0
    auto_fire_active: bool = False
