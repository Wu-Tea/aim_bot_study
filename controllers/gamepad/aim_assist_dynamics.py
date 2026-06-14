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
    manual_curve_straighten_enabled: bool = True
    manual_curve_straighten_strength: float = 0.30
    manual_curve_straighten_min_manual: float = 1600.0
    manual_curve_straighten_min_assist: float = 900.0


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

        raw_assist_x = float(int(output.right_x) - int(frame.manual_right_x))
        raw_assist_y = float(int(output.right_y) - int(frame.manual_right_y))
        manual_x, manual_y = self._straighten_manual_curve(
            float(frame.manual_right_x),
            float(frame.manual_right_y),
            raw_assist_x,
            raw_assist_y,
        )
        guarded_assist_x = raw_assist_x
        guarded_assist_y = raw_assist_y
        if self._recoil_active(frame, output):
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
        else:
            self.reset()
        output.right_x = _clamp_stick(int(round(manual_x + guarded_assist_x)))
        output.right_y = _clamp_stick(int(round(manual_y + guarded_assist_y)))

    def _straighten_manual_curve(
        self,
        manual_x: float,
        manual_y: float,
        assist_x: float,
        assist_y: float,
    ) -> tuple[float, float]:
        if not self.config.manual_curve_straighten_enabled:
            return manual_x, manual_y
        assist_mag = (assist_x * assist_x + assist_y * assist_y) ** 0.5
        manual_mag = (manual_x * manual_x + manual_y * manual_y) ** 0.5
        if (
            assist_mag < max(0.0, float(self.config.manual_curve_straighten_min_assist))
            or manual_mag < max(0.0, float(self.config.manual_curve_straighten_min_manual))
            or assist_mag <= 1e-6
        ):
            return manual_x, manual_y
        strength = max(0.0, min(0.85, float(self.config.manual_curve_straighten_strength)))
        if strength <= 0.0:
            return manual_x, manual_y
        ux = assist_x / assist_mag
        uy = assist_y / assist_mag
        parallel = manual_x * ux + manual_y * uy
        parallel_x = ux * parallel
        parallel_y = uy * parallel
        keep_orthogonal = 1.0 - strength
        return (
            parallel_x + ((manual_x - parallel_x) * keep_orthogonal),
            parallel_y + ((manual_y - parallel_y) * keep_orthogonal),
        )

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
