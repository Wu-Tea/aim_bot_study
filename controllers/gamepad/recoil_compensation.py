from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass

from vision.recoil_collection.models import RecoilProfileRecord

from .state import GamepadFrame, GamepadOutput


@dataclass(slots=True, frozen=True)
class RecoilCompensationConfig:
    amount: float = 0.30


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
        self._last_applied_cumulative_y: float = 0.0

    def reset(self) -> None:
        self._active_profile_id = None
        self._active_fire_started_at = None
        self._last_applied_cumulative_y = 0.0

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
            self._last_applied_cumulative_y = 0.0
            return

        if self._active_fire_started_at is None:
            self._active_fire_started_at = float(frame.timestamp)

        cumulative_y = _cumulative_profile_value(
            profile,
            elapsed_ms=max(0, int(round((float(frame.timestamp) - self._active_fire_started_at) * 1000.0))),
        )
        delta_y = cumulative_y - self._last_applied_cumulative_y
        if delta_y:
            output.right_y += int(round(delta_y))
        self._last_applied_cumulative_y = cumulative_y


def _cumulative_profile_value(profile: RecoilProfileRecord, *, elapsed_ms: int) -> float:
    if elapsed_ms < profile.initial_delay_ms:
        return 0.0

    sample_index = (elapsed_ms - profile.initial_delay_ms) // profile.sample_interval_ms
    sample_index = max(0, min(sample_index, profile.sample_count - 1))
    return float(profile.samples_y[sample_index])
