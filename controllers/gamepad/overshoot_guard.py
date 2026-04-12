import math
from dataclasses import dataclass


@dataclass(slots=True, frozen=True)
class OvershootGuardConfig:
    manual_input_threshold: int = 3000
    near_error_px: float = 8.0
    release_error_px: float = 20.0
    convergence_epsilon_px: float = 0.25
    convergence_trigger_frames: int = 2
    convergence_build_per_update: float = 0.20
    convergence_max_guard: float = 0.50
    convergence_decay: float = 0.15
    zero_cross_arm_px: float = 6.0
    zero_cross_hold_seconds: float = 0.04
    zero_cross_guard: float = 0.85
    carry_damp_gain: float = 1.0


@dataclass(slots=True, frozen=True)
class OvershootAdjustment:
    x_desired_scale: float = 1.0
    x_carry_scale: float = 1.0
    y_desired_scale: float = 1.0
    y_carry_scale: float = 1.0


class OvershootGuard:
    def __init__(self, config: OvershootGuardConfig | None = None):
        self.config = config or OvershootGuardConfig()
        self.reset()

    def reset(self):
        self._current_dx = 0.0
        self._current_dy = 0.0
        self._previous_dx = None
        self._previous_dy = None
        self._x_guard = 0.0
        self._y_guard = 0.0
        self._x_convergence_frames = 0
        self._y_convergence_frames = 0
        self._x_cross_hold_until = 0.0
        self._y_cross_hold_until = 0.0

    def observe_target(self, target_dx: float, target_dy: float, is_aiming: bool, timestamp: float):
        if not is_aiming:
            self.reset()
            return

        self._current_dx = float(target_dx)
        self._current_dy = float(target_dy)
        self._x_guard, self._x_convergence_frames, self._x_cross_hold_until = self._update_axis(
            current_error=self._current_dx,
            previous_error=self._previous_dx,
            guard=self._x_guard,
            convergence_frames=self._x_convergence_frames,
            cross_hold_until=self._x_cross_hold_until,
            timestamp=timestamp,
        )
        self._y_guard, self._y_convergence_frames, self._y_cross_hold_until = self._update_axis(
            current_error=self._current_dy,
            previous_error=self._previous_dy,
            guard=self._y_guard,
            convergence_frames=self._y_convergence_frames,
            cross_hold_until=self._y_cross_hold_until,
            timestamp=timestamp,
        )
        self._previous_dx = self._current_dx
        self._previous_dy = self._current_dy

    def _update_axis(
        self,
        current_error: float,
        previous_error: float | None,
        guard: float,
        convergence_frames: int,
        cross_hold_until: float,
        timestamp: float,
    ):
        if previous_error is None:
            return 0.0, 0, cross_hold_until

        epsilon = self.config.convergence_epsilon_px
        current_mag = abs(current_error)
        previous_mag = abs(previous_error)
        same_direction = (
            current_error == 0.0
            or previous_error == 0.0
            or math.copysign(1.0, current_error) == math.copysign(1.0, previous_error)
        )
        zero_crossed = (
            current_error != 0.0
            and previous_error != 0.0
            and math.copysign(1.0, current_error) != math.copysign(1.0, previous_error)
            and min(current_mag, previous_mag) <= self.config.zero_cross_arm_px
        )

        if zero_crossed:
            cross_hold_until = timestamp + self.config.zero_cross_hold_seconds
            guard = max(guard, self.config.zero_cross_guard)
            convergence_frames = 0
            return guard, convergence_frames, cross_hold_until

        if current_mag > self.config.release_error_px:
            return max(0.0, guard - self.config.convergence_decay), 0, cross_hold_until

        converging = same_direction and current_mag < (previous_mag - epsilon)
        if converging and current_mag <= self.config.near_error_px:
            convergence_frames += 1
            if convergence_frames >= self.config.convergence_trigger_frames:
                guard = min(self.config.convergence_max_guard, guard + self.config.convergence_build_per_update)
            return guard, convergence_frames, cross_hold_until

        guard = max(0.0, guard - self.config.convergence_decay)
        return guard, 0, cross_hold_until

    def _manual_same_direction(self, manual_axis: int, error_axis: float):
        if abs(manual_axis) < self.config.manual_input_threshold or error_axis == 0.0:
            return False
        return math.copysign(1.0, float(manual_axis)) == math.copysign(1.0, error_axis)

    def _scales_for_axis(self, manual_axis: int, error_axis: float, guard: float, cross_hold_until: float, timestamp: float):
        desired_scale = 1.0
        carry_scale = 1.0

        if timestamp < cross_hold_until:
            desired_scale = min(desired_scale, 1.0 - self.config.zero_cross_guard)
            carry_scale = min(carry_scale, max(0.0, 1.0 - (self.config.zero_cross_guard * self.config.carry_damp_gain)))

        if self._manual_same_direction(manual_axis, error_axis) and abs(error_axis) <= self.config.near_error_px and guard > 0.0:
            desired_scale = min(desired_scale, 1.0 - guard)
            carry_scale = min(carry_scale, max(0.0, 1.0 - (guard * self.config.carry_damp_gain)))

        return desired_scale, carry_scale

    def compute_adjustment(self, manual_rx: int, manual_ry: int, timestamp: float):
        x_desired_scale, x_carry_scale = self._scales_for_axis(
            manual_axis=manual_rx,
            error_axis=self._current_dx,
            guard=self._x_guard,
            cross_hold_until=self._x_cross_hold_until,
            timestamp=timestamp,
        )
        y_desired_scale, y_carry_scale = self._scales_for_axis(
            manual_axis=manual_ry,
            error_axis=self._current_dy,
            guard=self._y_guard,
            cross_hold_until=self._y_cross_hold_until,
            timestamp=timestamp,
        )
        return OvershootAdjustment(
            x_desired_scale=x_desired_scale,
            x_carry_scale=x_carry_scale,
            y_desired_scale=y_desired_scale,
            y_carry_scale=y_carry_scale,
        )
