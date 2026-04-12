from dataclasses import dataclass

from .state import MouseFrame, MouseOutput


def _soft_ramp(magnitude: float, inner: float, outer: float) -> float:
    if magnitude <= inner:
        return 0.0
    if outer <= inner or magnitude >= outer:
        return 1.0
    return (magnitude - inner) / (outer - inner)


@dataclass(slots=True, frozen=True)
class AIAimConfig:
    gain: float = 0.5
    smoothing: float = 0.6
    max_correction_px: float = 15.0
    deadzone_inner_px: float = 2.0
    deadzone_outer_px: float = 5.0
    fade_speed_px: float = 50.0


class AIAimPlugin:
    def __init__(self, config: AIAimConfig | None = None):
        self.config = config or AIAimConfig()
        self.carry_x = 0.0
        self.carry_y = 0.0

    def reset(self) -> None:
        self.carry_x = 0.0
        self.carry_y = 0.0

    def apply(self, frame: MouseFrame, output: MouseOutput) -> None:
        cfg = self.config
        desired_x = 0.0
        desired_y = 0.0

        if frame.is_aiming:
            raw_x = frame.target_dx * cfg.gain
            raw_y = frame.target_dy * cfg.gain

            raw_x = max(-cfg.max_correction_px, min(cfg.max_correction_px, raw_x))
            raw_y = max(-cfg.max_correction_px, min(cfg.max_correction_px, raw_y))

            radial = (raw_x * raw_x + raw_y * raw_y) ** 0.5
            strength = _soft_ramp(radial, cfg.deadzone_inner_px, cfg.deadzone_outer_px)

            desired_x = raw_x * strength
            desired_y = raw_y * strength

            manual_speed = (frame.manual_dx ** 2 + frame.manual_dy ** 2) ** 0.5
            fade = max(0.0, 1.0 - manual_speed / cfg.fade_speed_px) if cfg.fade_speed_px > 0 else 1.0
            desired_x *= fade
            desired_y *= fade

        s = cfg.smoothing
        self.carry_x = self.carry_x * s + desired_x * (1.0 - s)
        self.carry_y = self.carry_y * s + desired_y * (1.0 - s)

        output.move_dx += self.carry_x
        output.move_dy += self.carry_y
