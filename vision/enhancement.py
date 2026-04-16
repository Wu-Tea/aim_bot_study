import math
from dataclasses import dataclass

from .targeting import SelectedTarget


@dataclass(slots=True, frozen=True)
class LeadPredictorConfig:
    lead_seconds: float = 0.05
    gain: float = 0.85
    max_lead_px: float = 10.0
    min_motion_px: float = 2.0
    consistent_frames: int = 2


@dataclass(slots=True, frozen=True)
class CatchupBoostConfig:
    trigger_frames: int = 2
    gain_per_frame: float = 0.12
    max_bonus: float = 0.35
    decay: float = 0.12
    convergence_epsilon_px: float = 0.25


@dataclass(slots=True, frozen=True)
class NearTargetDampingConfig:
    inner_radius: float = 4.0
    outer_radius: float = 28.0
    min_scale: float = 0.65
    convergence_epsilon_px: float = 0.25


@dataclass(slots=True)
class AimEnhancementState:
    target: SelectedTarget
    dt: float
    motion_x: float
    motion_y: float
    velocity_x: float
    velocity_y: float
    previous_dx: float | None
    previous_dy: float | None
    output_dx: float
    output_dy: float


class LeadPredictor:
    def __init__(self, config: LeadPredictorConfig | None = None):
        self.config = config or LeadPredictorConfig()
        self.reset()

    def reset(self):
        self._x_streak = 0
        self._y_streak = 0
        self._x_sign = 0.0
        self._y_sign = 0.0

    def _update_axis_streak(self, motion: float, streak: int, last_sign: float):
        if abs(motion) < self.config.min_motion_px:
            return 0, 0.0

        sign = math.copysign(1.0, motion)
        if sign == last_sign:
            return streak + 1, sign
        return 1, sign

    @staticmethod
    def _clamp_axis_lead(current_error: float, lead: float, max_lead: float):
        if current_error == 0.0 or lead == 0.0:
            return 0.0

        bounded = max(-max_lead, min(max_lead, lead))
        if math.copysign(1.0, bounded) != math.copysign(1.0, current_error):
            return 0.0
        return bounded

    def apply(self, state: AimEnhancementState):
        if state.dt <= 0.0 or self.config.lead_seconds <= 0.0 or self.config.gain <= 0.0:
            return

        self._x_streak, self._x_sign = self._update_axis_streak(state.motion_x, self._x_streak, self._x_sign)
        self._y_streak, self._y_sign = self._update_axis_streak(state.motion_y, self._y_streak, self._y_sign)

        lead_x = 0.0
        lead_y = 0.0
        if self._x_streak >= self.config.consistent_frames:
            lead_x = state.velocity_x * self.config.lead_seconds * self.config.gain
        if self._y_streak >= self.config.consistent_frames:
            lead_y = state.velocity_y * self.config.lead_seconds * self.config.gain

        max_lead = abs(self.config.max_lead_px)
        state.output_dx += self._clamp_axis_lead(state.target.dx, lead_x, max_lead)
        state.output_dy += self._clamp_axis_lead(state.target.dy, lead_y, max_lead)


class CatchupBoost:
    def __init__(self, config: CatchupBoostConfig | None = None):
        self.config = config or CatchupBoostConfig()
        self.reset()

    def reset(self):
        self._x_growth_frames = 0
        self._y_growth_frames = 0
        self._x_bonus = 0.0
        self._y_bonus = 0.0

    def _update_axis(self, current_error: float, previous_error: float | None, growth_frames: int, bonus: float):
        if previous_error is None:
            return 0, max(0.0, bonus - self.config.decay)

        epsilon = self.config.convergence_epsilon_px
        current_mag = abs(current_error)
        previous_mag = abs(previous_error)

        if current_mag <= epsilon:
            return 0, max(0.0, bonus - self.config.decay)

        if current_error == 0.0 or previous_error == 0.0:
            same_direction = current_error == previous_error
        else:
            same_direction = math.copysign(1.0, current_error) == math.copysign(1.0, previous_error)

        if not same_direction:
            return 0, 0.0

        if current_mag > previous_mag + epsilon:
            growth_frames += 1
            if growth_frames >= self.config.trigger_frames:
                bonus = min(self.config.max_bonus, bonus + self.config.gain_per_frame)
            return growth_frames, bonus

        if current_mag < previous_mag - epsilon:
            bonus = max(0.0, bonus - self.config.decay)

        return 0, bonus

    def apply(self, state: AimEnhancementState):
        self._x_growth_frames, self._x_bonus = self._update_axis(
            current_error=state.target.dx,
            previous_error=state.previous_dx,
            growth_frames=self._x_growth_frames,
            bonus=self._x_bonus,
        )
        self._y_growth_frames, self._y_bonus = self._update_axis(
            current_error=state.target.dy,
            previous_error=state.previous_dy,
            growth_frames=self._y_growth_frames,
            bonus=self._y_bonus,
        )

        state.output_dx *= 1.0 + self._x_bonus
        state.output_dy *= 1.0 + self._y_bonus


class NearTargetDamping:
    def __init__(self, config: NearTargetDampingConfig | None = None):
        self.config = config or NearTargetDampingConfig()

    def reset(self):
        return None

    def _scale_for_distance(self, distance: float):
        inner = self.config.inner_radius
        outer = self.config.outer_radius
        min_scale = self.config.min_scale

        if min_scale >= 1.0 or outer <= inner:
            return 1.0
        if distance <= inner:
            return min_scale
        if distance >= outer:
            return 1.0

        progress = (distance - inner) / (outer - inner)
        return min_scale + ((1.0 - min_scale) * progress)

    def _is_converging(self, state: AimEnhancementState):
        if state.previous_dx is None or state.previous_dy is None:
            return False

        current_distance = math.hypot(state.target.dx, state.target.dy)
        previous_distance = math.hypot(state.previous_dx, state.previous_dy)
        return current_distance < (previous_distance - self.config.convergence_epsilon_px)

    def apply(self, state: AimEnhancementState):
        if not state.target.is_crosshair_in_slow_zone():
            return
        if not self._is_converging(state):
            return

        distance = math.hypot(state.target.dx, state.target.dy)
        scale = self._scale_for_distance(distance)
        state.output_dx *= scale
        state.output_dy *= scale


class AimEnhancementPipeline:
    def __init__(
        self,
        lead_predictor: LeadPredictor | None = None,
        catchup_boost: CatchupBoost | None = None,
        near_target_damping: NearTargetDamping | None = None,
        velocity_filter_alpha: float = 0.45,
    ):
        self.lead_predictor = lead_predictor or LeadPredictor()
        self.catchup_boost = catchup_boost or CatchupBoost()
        self.near_target_damping = near_target_damping or NearTargetDamping()
        self.velocity_filter_alpha = min(1.0, max(0.0, velocity_filter_alpha))
        self._plugins = (self.lead_predictor, self.catchup_boost, self.near_target_damping)
        self.reset()

    def reset(self):
        self._previous_target = None
        self._previous_timestamp = None
        self._velocity_x = 0.0
        self._velocity_y = 0.0
        for plugin in self._plugins:
            plugin.reset()

    def process(self, target: SelectedTarget, timestamp: float):
        dt = 0.0
        motion_x = 0.0
        motion_y = 0.0
        previous_dx = None if self._previous_target is None else self._previous_target.dx
        previous_dy = None if self._previous_target is None else self._previous_target.dy

        if self._previous_target is not None and self._previous_timestamp is not None:
            dt = max(0.0, timestamp - self._previous_timestamp)
            motion_x = target.target_x - self._previous_target.target_x
            motion_y = target.target_y - self._previous_target.target_y
            if dt > 0.0:
                raw_velocity_x = motion_x / dt
                raw_velocity_y = motion_y / dt
                alpha = self.velocity_filter_alpha
                self._velocity_x = (alpha * raw_velocity_x) + ((1.0 - alpha) * self._velocity_x)
                self._velocity_y = (alpha * raw_velocity_y) + ((1.0 - alpha) * self._velocity_y)

        state = AimEnhancementState(
            target=target,
            dt=dt,
            motion_x=motion_x,
            motion_y=motion_y,
            velocity_x=self._velocity_x,
            velocity_y=self._velocity_y,
            previous_dx=previous_dx,
            previous_dy=previous_dy,
            output_dx=target.dx,
            output_dy=target.dy,
        )

        for plugin in self._plugins:
            plugin.apply(state)

        self._previous_target = target
        self._previous_timestamp = timestamp
        return state.output_dx, state.output_dy
