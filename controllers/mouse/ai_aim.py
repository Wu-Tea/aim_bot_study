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
    gain: float = 0.06
    smoothing: float = 0.65
    max_correction_px: float = 1.5
    deadzone_inner_px: float = 3.0
    deadzone_outer_px: float = 8.0
    # Fraction of manual mouse movement to counteract when AI has a target.
    # 0.0 = no dampening, 1.0 = fully cancel user movement.
    manual_dampen: float = 0.4


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
        dampen = 0.0

        if frame.is_aiming:
            target_radial = (frame.target_dx ** 2 + frame.target_dy ** 2) ** 0.5
            strength = _soft_ramp(target_radial, cfg.deadzone_inner_px, cfg.deadzone_outer_px)

            raw_x = frame.target_dx * cfg.gain
            raw_y = frame.target_dy * cfg.gain

            raw_x = max(-cfg.max_correction_px, min(cfg.max_correction_px, raw_x))
            raw_y = max(-cfg.max_correction_px, min(cfg.max_correction_px, raw_y))

            desired_x = raw_x * strength
            desired_y = raw_y * strength

            # Dampen user's physical mouse movement when AI has a target,
            # making the cursor "stickier" near the target.
            dampen = cfg.manual_dampen * strength

        s = cfg.smoothing
        self.carry_x = self.carry_x * s + desired_x * (1.0 - s)
        self.carry_y = self.carry_y * s + desired_y * (1.0 - s)

        output.move_dx += self.carry_x
        output.move_dy += self.carry_y

        # Inject negative delta to counteract part of the user's physical movement.
        if dampen > 0.0:
            output.move_dx -= frame.manual_dx * dampen
            output.move_dy -= frame.manual_dy * dampen
