from dataclasses import dataclass


@dataclass(slots=True, frozen=True)
class AdaptiveDeltaGainConfig:
    min_error_px: float = 6.0
    convergence_epsilon_px: float = 0.5
    trigger_frames: int = 3
    gain_per_update: float = 0.08
    decay_per_update: float = 0.12
    max_bonus: float = 0.6
    opposing_input_threshold: int = 4500
    stale_seconds: float = 0.15


@dataclass(slots=True, frozen=True)
class AdaptiveDeltaGainAdjustment:
    target_dx_multiplier: float = 1.0
    target_dy_multiplier: float = 1.0


class AdaptiveDeltaGain:
    def __init__(self, config: AdaptiveDeltaGainConfig | None = None):
        self.config = config or AdaptiveDeltaGainConfig()
        self.reset()

    def reset(self) -> None:
        self._bonus_x = 0.0
        self._bonus_y = 0.0
        self._prev_abs_x: float | None = None
        self._prev_abs_y: float | None = None
        self._nonconverge_x = 0
        self._nonconverge_y = 0
        self._last_obs_time: float | None = None
        self._last_sign_x = 0.0
        self._last_sign_y = 0.0

    def observe_target(
        self,
        *,
        target_dx: float,
        target_dy: float,
        is_aiming: bool,
        timestamp: float,
    ) -> None:
        if not is_aiming:
            self.reset()
            return

        cfg = self.config
        if (
            self._last_obs_time is not None
            and (timestamp - self._last_obs_time) > cfg.stale_seconds
        ):
            self._prev_abs_x = None
            self._prev_abs_y = None
            self._nonconverge_x = 0
            self._nonconverge_y = 0

        self._last_obs_time = timestamp
        self._last_sign_x = _sign(target_dx)
        self._last_sign_y = _sign(target_dy)

        self._bonus_x, self._prev_abs_x, self._nonconverge_x = self._update_axis(
            abs(target_dx), self._prev_abs_x, self._bonus_x, self._nonconverge_x
        )
        self._bonus_y, self._prev_abs_y, self._nonconverge_y = self._update_axis(
            abs(target_dy), self._prev_abs_y, self._bonus_y, self._nonconverge_y
        )

    def _update_axis(
        self,
        cur: float,
        prev: float | None,
        bonus: float,
        nonconverge: int,
    ) -> tuple[float, float, int]:
        cfg = self.config
        if cur < cfg.min_error_px:
            bonus = max(0.0, bonus - cfg.decay_per_update)
            return bonus, cur, 0

        if prev is None:
            return bonus, cur, 0

        if cur >= prev - cfg.convergence_epsilon_px:
            nonconverge += 1
            if nonconverge >= cfg.trigger_frames:
                bonus = min(cfg.max_bonus, bonus + cfg.gain_per_update)
        else:
            nonconverge = 0
            bonus = max(0.0, bonus - cfg.decay_per_update)

        return bonus, cur, nonconverge

    def compute_adjustment(self, manual_rx: int, manual_ry: int) -> AdaptiveDeltaGainAdjustment:
        bonus_x = self._bonus_x
        bonus_y = self._bonus_y

        if self._opposes(manual_rx, self._last_sign_x):
            bonus_x = 0.0
        if self._opposes(manual_ry, self._last_sign_y):
            bonus_y = 0.0

        return AdaptiveDeltaGainAdjustment(
            target_dx_multiplier=1.0 + bonus_x,
            target_dy_multiplier=1.0 + bonus_y,
        )

    def _opposes(self, manual_axis: int, target_sign: float) -> bool:
        if abs(manual_axis) < self.config.opposing_input_threshold or target_sign == 0.0:
            return False
        return (manual_axis > 0 and target_sign < 0) or (manual_axis < 0 and target_sign > 0)


def _sign(x: float) -> float:
    if x > 0.0:
        return 1.0
    if x < 0.0:
        return -1.0
    return 0.0
