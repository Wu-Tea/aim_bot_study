from collections import deque
from dataclasses import dataclass


@dataclass(slots=True, frozen=True)
class ManualIntentGuardConfig:
    min_error_px: float = 8.0
    stable_history: int = 3
    opposing_input_threshold: int = 4000
    opposed_output_scale: float = 0.4
    opposed_ai_fade_scale: float = 0.0
    stale_seconds: float = 0.15


@dataclass(slots=True, frozen=True)
class ManualIntentAdjustment:
    output_manual_rx: float
    ai_fade_manual_rx: float


class ManualIntentGuard:
    def __init__(self, config: ManualIntentGuardConfig | None = None):
        self.config = config or ManualIntentGuardConfig()
        self.reset()

    def reset(self) -> None:
        history = max(1, int(self.config.stable_history))
        self._recent_target_dx: deque[float] = deque(maxlen=history)
        self._last_obs_time: float | None = None

    def observe_target(self, *, target_dx: float, is_aiming: bool, timestamp: float) -> None:
        if not is_aiming:
            self.reset()
            return

        if (
            self._last_obs_time is not None
            and (timestamp - self._last_obs_time) > self.config.stale_seconds
        ):
            self._recent_target_dx.clear()

        self._last_obs_time = timestamp
        self._recent_target_dx.append(float(target_dx))

    def compute_adjustment(self, manual_rx: float) -> ManualIntentAdjustment:
        manual_rx = float(manual_rx)
        if abs(manual_rx) < self.config.opposing_input_threshold:
            return ManualIntentAdjustment(
                output_manual_rx=manual_rx,
                ai_fade_manual_rx=manual_rx,
            )

        stable_sign = self._stable_target_sign()
        manual_sign = _sign(manual_rx)
        if stable_sign == 0.0 or manual_sign == 0.0 or manual_sign == stable_sign:
            return ManualIntentAdjustment(
                output_manual_rx=manual_rx,
                ai_fade_manual_rx=manual_rx,
            )

        return ManualIntentAdjustment(
            output_manual_rx=manual_rx * self.config.opposed_output_scale,
            ai_fade_manual_rx=manual_rx * self.config.opposed_ai_fade_scale,
        )

    def _stable_target_sign(self) -> float:
        required = max(1, int(self.config.stable_history))
        if len(self._recent_target_dx) < required:
            return 0.0

        stable_sign = 0.0
        for target_dx in list(self._recent_target_dx)[-required:]:
            if abs(target_dx) < self.config.min_error_px:
                return 0.0

            sign = _sign(target_dx)
            if sign == 0.0:
                return 0.0

            if stable_sign == 0.0:
                stable_sign = sign
                continue

            if sign != stable_sign:
                return 0.0

        return stable_sign


def _sign(value: float) -> float:
    if value > 0.0:
        return 1.0
    if value < 0.0:
        return -1.0
    return 0.0
