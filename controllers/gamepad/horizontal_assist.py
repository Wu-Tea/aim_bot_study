from dataclasses import dataclass


def soft_ramp_strength(magnitude: float, inner: float, outer: float) -> float:
    if magnitude <= inner:
        return 0.0
    if magnitude >= outer:
        return 1.0
    if outer <= inner:
        return 1.0
    return (magnitude - inner) / (outer - inner)


def compute_axis_soft_strengths(
    dx: float,
    dy: float,
    inner: float,
    radial_outer: float,
    x_outer: float,
) -> tuple[float, float]:
    radial_strength = soft_ramp_strength((dx * dx + dy * dy) ** 0.5, inner, radial_outer)
    x_strength = max(radial_strength, soft_ramp_strength(abs(dx), inner, x_outer))
    return x_strength, radial_strength


@dataclass(slots=True)
class HorizontalAimAssistConfig:
    min_error_px: float = 5.0
    min_velocity_px_per_sec: float = 60.0
    velocity_filter_alpha: float = 0.35
    feedforward_lead_seconds: float = 0.025
    feedforward_gain: float = 0.7
    max_feedforward_px: float = 10.0
    catchup_trigger_frames: int = 3
    catchup_gain_per_update: float = 0.03
    catchup_max_bonus: float = 0.12
    catchup_decay: float = 0.04
    opposing_input_threshold: int = 4500
    convergence_epsilon_px: float = 0.5


class HorizontalAimAssist:
    def __init__(self, config: HorizontalAimAssistConfig | None = None):
        self.config = config or HorizontalAimAssistConfig()
        self.reset()

    def reset(self):
        self.current_target_dx = 0.0
        self.filtered_dx_velocity = 0.0
        self.catchup_bonus = 0.0
        self._prev_target_dx = None
        self._prev_timestamp = None
        self._nonconverging_updates = 0

    def observe_target(self, target_dx: float, is_aiming: bool, timestamp: float):
        if not is_aiming:
            self.reset()
            return

        self.current_target_dx = float(target_dx)

        if self._prev_target_dx is None or self._prev_timestamp is None:
            self._prev_target_dx = self.current_target_dx
            self._prev_timestamp = float(timestamp)
            return

        dt = max(float(timestamp) - self._prev_timestamp, 1e-6)
        raw_dx_velocity = (self.current_target_dx - self._prev_target_dx) / dt
        alpha = self.config.velocity_filter_alpha
        self.filtered_dx_velocity = (
            (self.filtered_dx_velocity * (1.0 - alpha)) + (raw_dx_velocity * alpha)
        )

        if self._is_nonconverging():
            self._nonconverging_updates += 1
            if self._nonconverging_updates >= self.config.catchup_trigger_frames:
                self.catchup_bonus = min(
                    self.config.catchup_max_bonus,
                    self.catchup_bonus + self.config.catchup_gain_per_update,
                )
        else:
            self._nonconverging_updates = 0
            self.catchup_bonus = max(0.0, self.catchup_bonus - self.config.catchup_decay)

        self._prev_target_dx = self.current_target_dx
        self._prev_timestamp = float(timestamp)

    def compute_adjustment(self, manual_rx: int) -> tuple[float, float]:
        if self._has_opposing_manual_input(manual_rx):
            return 0.0, 0.0

        if not self._is_feedforward_active():
            return 0.0, 0.0

        feedforward_dx = self.filtered_dx_velocity * self.config.feedforward_lead_seconds
        feedforward_dx *= self.config.feedforward_gain
        feedforward_dx = self._clamp(feedforward_dx, self.config.max_feedforward_px)

        if not self._same_direction(feedforward_dx, self.current_target_dx):
            feedforward_dx = 0.0

        return feedforward_dx, self.catchup_bonus

    def _is_nonconverging(self) -> bool:
        if self._prev_target_dx is None:
            return False

        return (
            abs(self.current_target_dx) >= self.config.min_error_px
            and abs(self.filtered_dx_velocity) >= self.config.min_velocity_px_per_sec
            and self._same_direction(self.current_target_dx, self.filtered_dx_velocity)
            and self._same_direction(self.current_target_dx, self._prev_target_dx)
            and abs(self.current_target_dx)
            >= (abs(self._prev_target_dx) - self.config.convergence_epsilon_px)
        )

    def _is_feedforward_active(self) -> bool:
        return (
            abs(self.current_target_dx) >= self.config.min_error_px
            and abs(self.filtered_dx_velocity) >= self.config.min_velocity_px_per_sec
            and self._same_direction(self.current_target_dx, self.filtered_dx_velocity)
        )

    def _has_opposing_manual_input(self, manual_rx: int) -> bool:
        return (
            abs(manual_rx) >= self.config.opposing_input_threshold
            and self._same_direction(float(manual_rx), -self.current_target_dx)
        )

    @staticmethod
    def _same_direction(lhs: float, rhs: float) -> bool:
        return (lhs > 0.0 and rhs > 0.0) or (lhs < 0.0 and rhs < 0.0)

    @staticmethod
    def _clamp(value: float, limit: float) -> float:
        return max(-limit, min(limit, value))
