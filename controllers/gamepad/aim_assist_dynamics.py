from __future__ import annotations

from dataclasses import dataclass

from .state import GamepadFrame, GamepadOutput


_STICK_MIN = -32768
_STICK_MAX = 32767


@dataclass(slots=True, frozen=True)
class AimAssistDynamicsConfig:
    enabled: bool = True
    recoil_jitter_guard_enabled: bool = True
    recoil_jitter_assist_threshold: float = 1400.0
    recoil_jitter_flip_scale: float = 0.20
    recoil_jitter_memory_seconds: float = 0.050


class AimAssistDynamicsPlugin:
    name = "AimAssistDynamicsPlugin"

    def __init__(self, config: AimAssistDynamicsConfig | None = None):
        self.config = config or AimAssistDynamicsConfig()
        self._last_raw_assist_x: float | None = None
        self._last_raw_assist_y: float | None = None
        self._last_timestamp: float | None = None

    def reset(self) -> None:
        self._last_raw_assist_x = None
        self._last_raw_assist_y = None
        self._last_timestamp = None

    def apply(self, frame: GamepadFrame, output: GamepadOutput) -> None:
        if not self.config.enabled:
            return
        if not self._recoil_active(frame, output):
            self.reset()
            return

        raw_assist_x = float(int(output.right_x) - int(frame.manual_right_x))
        raw_assist_y = float(int(output.right_y) - int(frame.manual_right_y))
        guarded_assist_x = self._guard_recoil_axis_jitter(
            frame,
            raw_assist_x,
            previous=self._last_raw_assist_x,
        )
        guarded_assist_y = self._guard_recoil_axis_jitter(
            frame,
            raw_assist_y,
            previous=self._last_raw_assist_y,
        )
        self._last_raw_assist_x = raw_assist_x
        self._last_raw_assist_y = raw_assist_y
        self._last_timestamp = float(frame.timestamp)
        output.right_x = _clamp_stick(int(round(int(frame.manual_right_x) + guarded_assist_x)))
        output.right_y = _clamp_stick(int(round(int(frame.manual_right_y) + guarded_assist_y)))

    def _guard_recoil_axis_jitter(
        self,
        frame: GamepadFrame,
        raw_assist: float,
        *,
        previous: float | None,
    ) -> float:
        if not self.config.recoil_jitter_guard_enabled:
            return raw_assist
        if previous is None or not self._within_memory_window(frame):
            return raw_assist
        threshold = max(0.0, float(self.config.recoil_jitter_assist_threshold))
        if abs(raw_assist) > threshold or abs(previous) > threshold:
            return raw_assist
        if raw_assist == 0.0 or previous == 0.0:
            return raw_assist
        if (raw_assist > 0.0) == (previous > 0.0):
            return raw_assist
        scale = max(0.0, min(1.0, float(self.config.recoil_jitter_flip_scale)))
        return raw_assist * scale

    def _within_memory_window(self, frame: GamepadFrame) -> bool:
        if self._last_timestamp is None:
            return False
        elapsed = float(frame.timestamp) - self._last_timestamp
        return 0.0 <= elapsed <= max(0.0, float(self.config.recoil_jitter_memory_seconds))

    def _recoil_active(self, frame: GamepadFrame, output: GamepadOutput) -> bool:
        return bool(output.auto_fire_active or frame.buttons.get("rb", False) or frame.right_trigger > 10)


def _clamp_stick(value: int) -> int:
    return max(_STICK_MIN, min(_STICK_MAX, value))
