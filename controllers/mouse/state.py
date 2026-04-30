from dataclasses import dataclass

from ..base_controller import ControllerTarget


@dataclass(slots=True, frozen=True)
class MouseFrame:
    timestamp: float
    manual_dx: float
    manual_dy: float
    is_aiming: bool
    target_dx: float
    target_dy: float
    auto_fire_requested: bool
    manual_left_pressed: bool = False
    manual_override_active: bool = False
    target: ControllerTarget | None = None
    input_session_id: int = 0
    target_revision: int = 0
    target_timestamp: float | None = None


@dataclass(slots=True)
class MouseOutput:
    move_dx: float = 0.0
    move_dy: float = 0.0
    left_click: bool = False
    auto_fire_active: bool = False
