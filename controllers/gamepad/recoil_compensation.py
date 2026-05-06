from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass

from vision.recoil_collection.models import RecoilProfileRecord

from .state import GamepadFrame, GamepadOutput


@dataclass(slots=True, frozen=True)
class RecoilCompensationConfig:
    amount: float = 0.30
    piecewise_mid_pixels_y: float = 45.0
    piecewise_max_pixels_y: float = 180.0
    piecewise_mid_ratio_y: float = 0.65


class RecoilCompensationPlugin:
    def __init__(
        self,
        config: RecoilCompensationConfig | None = None,
        *,
        profile_provider: Callable[[GamepadFrame], RecoilProfileRecord | None] | None = None,
    ):
        self.config = config or RecoilCompensationConfig()
        self._profile_provider = profile_provider
        self._active_profile_id: str | None = None
        self._active_fire_started_at: float | None = None
        self._last_applied_stick_y: float = 0.0

    def reset(self) -> None:
        self._active_profile_id = None
        self._active_fire_started_at = None
        self._last_applied_stick_y = 0.0

    def apply(self, frame: GamepadFrame, output: GamepadOutput) -> None:
        if self._profile_provider is None:
            if output.auto_fire_active and self.config.amount != 0.0:
                output.right_y -= int(self.config.amount * 32767)
            return

        profile = self._profile_provider(frame)
        if profile is None:
            self.reset()
            return

        if self._active_profile_id != profile.profile_id:
            self.reset()
            self._active_profile_id = profile.profile_id

        if not output.auto_fire_active:
            self._active_fire_started_at = None
            self._last_applied_stick_y = 0.0
            return

        if self._active_fire_started_at is None:
            self._active_fire_started_at = float(frame.timestamp)

        cumulative_pixels_y = _cumulative_profile_value(
            profile,
            elapsed_ms=max(0, int(round((float(frame.timestamp) - self._active_fire_started_at) * 1000.0))),
        )
        cumulative_stick_y = _map_pixels_to_stick(cumulative_pixels_y, config=self.config)
        delta_y = cumulative_stick_y - self._last_applied_stick_y
        if delta_y:
            output.right_y += int(round(delta_y))
        self._last_applied_stick_y = cumulative_stick_y


def _cumulative_profile_value(profile: RecoilProfileRecord, *, elapsed_ms: int) -> float:
    if elapsed_ms < profile.initial_delay_ms:
        return 0.0

    sample_index = (elapsed_ms - profile.initial_delay_ms) // profile.sample_interval_ms
    sample_index = max(0, min(sample_index, profile.sample_count - 1))
    return float(profile.samples_y[sample_index])


def _map_pixels_to_stick(delta: float, *, config: RecoilCompensationConfig) -> float:
    abs_delta = abs(float(delta))
    sign = -1.0 if delta < 0.0 else 1.0
    mid_pixels = float(config.piecewise_mid_pixels_y)
    max_pixels = float(config.piecewise_max_pixels_y)
    mid_ratio = float(config.piecewise_mid_ratio_y)

    if mid_pixels <= 0.0 or max_pixels <= mid_pixels or mid_ratio <= 0.0 or mid_ratio >= 1.0:
        clamped = max(-max_pixels, min(max_pixels, float(delta)))
        return (clamped / max_pixels) * 32767.0 if max_pixels > 0.0 else 0.0

    if abs_delta >= max_pixels:
        return sign * 32767.0
    if abs_delta <= mid_pixels:
        return sign * 32767.0 * mid_ratio * (abs_delta / mid_pixels)

    progress = (abs_delta - mid_pixels) / (max_pixels - mid_pixels)
    return sign * 32767.0 * (mid_ratio + ((1.0 - mid_ratio) * progress))
