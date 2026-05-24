from dataclasses import dataclass, replace

from controllers.base_controller import ControllerTarget

from .state import GamepadOutput


@dataclass(slots=True, frozen=True)
class GamepadTargetTrackerConfig:
    reticle_speed_px_per_sec: float = 1500.0
    stick_max: int = 32767
    max_projection_age_ms: float = 50.0
    velocity_lowpass_alpha: float = 0.35
    max_target_velocity_px_per_sec: float = 1200.0


@dataclass(slots=True, frozen=True)
class GamepadTargetProjection:
    dx: float
    dy: float
    target: ControllerTarget | None
    revision: int
    observed_at: float


class GamepadTargetTracker:
    def __init__(self, config: GamepadTargetTrackerConfig | None = None):
        self.config = config or GamepadTargetTrackerConfig()
        self.reset()

    def reset(self) -> None:
        self._observed_dx = 0.0
        self._observed_dy = 0.0
        self._observed_at: float | None = None
        self._revision = 0
        self._target: ControllerTarget | None = None
        self._target_velocity_x = 0.0
        self._target_velocity_y = 0.0
        self._camera_dx_since_observation = 0.0
        self._camera_dy_since_observation = 0.0

    def update_observation(
        self,
        *,
        dx: float,
        dy: float,
        target: ControllerTarget | None,
        revision: int,
        observed_at: float,
    ) -> None:
        dx = float(dx)
        dy = float(dy)
        observed_at = float(observed_at)
        if (
            self._target is not None
            and target is not None
            and self._observed_at is not None
            and observed_at > self._observed_at
        ):
            dt = observed_at - self._observed_at
            raw_vx = (dx - self._observed_dx + self._camera_dx_since_observation) / dt
            raw_vy = (dy - self._observed_dy + self._camera_dy_since_observation) / dt
            alpha = max(0.0, min(1.0, self.config.velocity_lowpass_alpha))
            self._target_velocity_x = self._clamp_velocity(
                (self._target_velocity_x * alpha) + (raw_vx * (1.0 - alpha))
            )
            self._target_velocity_y = self._clamp_velocity(
                (self._target_velocity_y * alpha) + (raw_vy * (1.0 - alpha))
            )
        else:
            self._target_velocity_x = 0.0
            self._target_velocity_y = 0.0

        self._observed_dx = dx
        self._observed_dy = dy
        self._observed_at = observed_at
        self._revision = int(revision)
        self._target = target
        self._camera_dx_since_observation = 0.0
        self._camera_dy_since_observation = 0.0

    def record_output(self, output: GamepadOutput, *, dt: float) -> None:
        if self._observed_at is None or self.config.stick_max <= 0:
            return
        dt = max(0.0, float(dt))
        if dt <= 0.0:
            return
        right_x = self._clamp_stick(output.right_x)
        right_y = self._clamp_stick(output.right_y)
        pixels_per_stick = self.config.reticle_speed_px_per_sec * dt / self.config.stick_max
        self._camera_dx_since_observation += right_x * pixels_per_stick
        self._camera_dy_since_observation += -right_y * pixels_per_stick

    def project(self, *, timestamp: float) -> GamepadTargetProjection | None:
        if self._observed_at is None:
            return None
        age = max(0.0, float(timestamp) - self._observed_at)
        if age * 1000.0 > self.config.max_projection_age_ms:
            return None
        dx = (
            self._observed_dx
            + (self._target_velocity_x * age)
            - self._camera_dx_since_observation
        )
        dy = (
            self._observed_dy
            + (self._target_velocity_y * age)
            - self._camera_dy_since_observation
        )
        return GamepadTargetProjection(
            dx=dx,
            dy=dy,
            target=self._shift_target(dx - self._observed_dx, dy - self._observed_dy),
            revision=self._revision,
            observed_at=self._observed_at,
        )

    def _shift_target(self, delta_x: float, delta_y: float) -> ControllerTarget | None:
        target = self._target
        if target is None:
            return None
        body_box = target.body_box
        shifted_box = None
        if body_box is not None:
            left, top, right, bottom = body_box
            shifted_box = (
                left + delta_x,
                top + delta_y,
                right + delta_x,
                bottom + delta_y,
            )
        return replace(
            target,
            aim_point_x=target.aim_point_x + delta_x,
            aim_point_y=target.aim_point_y + delta_y,
            body_box=shifted_box,
        )

    def _clamp_velocity(self, value: float) -> float:
        limit = max(0.0, self.config.max_target_velocity_px_per_sec)
        if limit <= 0.0:
            return 0.0
        return max(-limit, min(limit, value))

    def _clamp_stick(self, value: float) -> float:
        limit = float(max(0, self.config.stick_max))
        return max(-limit, min(limit, float(value)))
