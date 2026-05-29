from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass

from vision.recoil_collection.calibration import RecoilControlCalibration
from vision.recoil_collection.calibration import stick_from_pixels
from vision.recoil_collection.models import RecoilProfileRecord

from .state import GamepadFrame, GamepadOutput


_MANUAL_FIRE_TRIGGER_THRESHOLD = 10


@dataclass(slots=True, frozen=True)
class RecoilCompensationConfig:
    profile_amount: float = 1.0
    feedback_amount: float = 0.30
    profile_x_amount: float = 1.0
    profile_lead_ms: int = 0
    profile_velocity_reference_ms: int = 10
    piecewise_mid_pixels_y: float = 45.0
    piecewise_max_pixels_y: float = 180.0
    piecewise_mid_ratio_y: float = 0.65


class RecoilCompensationPlugin:
    def __init__(
        self,
        config: RecoilCompensationConfig | None = None,
        *,
        profile_provider: Callable[
            [GamepadFrame],
            RecoilProfileRecord | tuple[RecoilProfileRecord | None, RecoilControlCalibration | None] | None,
        ]
        | None = None,
        profile_selection_logger: Callable[[str], None] | None = None,
    ):
        self.config = config or RecoilCompensationConfig()
        self._profile_provider = profile_provider
        self._profile_selection_logger = profile_selection_logger
        self._active_profile_id: str | None = None
        self._active_fire_started_at: float | None = None
        self._last_logged_selection_key: tuple[str, str | None] | None = None

    def reset(self) -> None:
        self._active_profile_id = None
        self._active_fire_started_at = None

    def apply(self, frame: GamepadFrame, output: GamepadOutput) -> None:
        fire_active = output.auto_fire_active or _manual_fire_pressed(frame)
        if self._profile_provider is None:
            if fire_active and self.config.feedback_amount != 0.0:
                output.right_y -= int(self.config.feedback_amount * 32767)
            return

        profile, calibration = _coerce_profile_result(self._profile_provider(frame))
        if profile is None:
            self.reset()
            if fire_active:
                self._log_profile_selection(frame, None)
            if fire_active and self.config.feedback_amount != 0.0:
                output.right_y -= int(self.config.feedback_amount * 32767)
            return

        if self._active_profile_id != profile.profile_id:
            self.reset()
            self._active_profile_id = profile.profile_id

        if not fire_active:
            self._active_fire_started_at = None
            return

        self._log_profile_selection(frame, profile)
        if self._active_fire_started_at is None:
            self._active_fire_started_at = float(frame.timestamp)

        elapsed_ms = max(0, int(round((float(frame.timestamp) - self._active_fire_started_at) * 1000.0)))
        profile_elapsed_ms = elapsed_ms + max(0, int(self.config.profile_lead_ms))
        cumulative_pixels_x, cumulative_pixels_y = _cumulative_profile_values(
            profile,
            elapsed_ms=profile_elapsed_ms,
        )
        profile_scale = max(0.0, float(self.config.profile_amount))
        profile_x_scale = profile_scale * float(self.config.profile_x_amount)
        previous_pixels_x, previous_pixels_y = _cumulative_profile_values(
            profile,
            elapsed_ms=max(0, profile_elapsed_ms - profile.sample_interval_ms),
        )
        delta_x = cumulative_pixels_x - previous_pixels_x
        delta_y = cumulative_pixels_y - previous_pixels_y
        if calibration is not None:
            anti_recoil_stick_y = (
                stick_from_pixels(
                    -delta_y,
                    axis="y",
                    duration_ms=profile.sample_interval_ms,
                    calibration=calibration,
                )
                * profile_scale
            )
            profile_stick_x = (
                stick_from_pixels(
                    -delta_x,
                    axis="x",
                    duration_ms=profile.sample_interval_ms,
                    calibration=calibration,
                )
                * profile_x_scale
            )
        else:
            mapped_delta_y = _normalize_delta_for_uncalibrated_mapping(
                delta_y,
                sample_interval_ms=profile.sample_interval_ms,
                config=self.config,
            )
            mapped_delta_x = _normalize_delta_for_uncalibrated_mapping(
                delta_x,
                sample_interval_ms=profile.sample_interval_ms,
                config=self.config,
            )
            anti_recoil_stick_y = (
                _map_pixels_to_stick(-mapped_delta_y, config=self.config)
                * profile_scale
            )
            profile_stick_x = _map_pixels_to_stick(-mapped_delta_x, config=self.config) * profile_x_scale
        if profile_stick_x:
            output.right_x += int(round(profile_stick_x))
        if anti_recoil_stick_y:
            output.right_y += int(round(anti_recoil_stick_y))

    def _log_profile_selection(self, frame: GamepadFrame, profile: RecoilProfileRecord | None) -> None:
        logger = self._profile_selection_logger
        if logger is None:
            return

        aim_mode = "ads" if frame.is_aiming else "hipfire"
        profile_id = None if profile is None else profile.profile_id
        key = (aim_mode, profile_id)
        if self._last_logged_selection_key == key:
            return
        self._last_logged_selection_key = key

        if profile is None:
            logger(f"[Recoil] active_profile aim={aim_mode} profile=none fallback={self.config.feedback_amount:.0%}")
            return
        logger(
            f"[Recoil] active_profile aim={aim_mode} profile={profile.profile_id} "
            f"confidence={profile.confidence:.3f} profile_amount={self.config.profile_amount:.2f} "
            f"profile_x={self.config.profile_x_amount:.2f} feedback={self.config.feedback_amount:.2f} "
            f"lead_ms={max(0, int(self.config.profile_lead_ms))} "
            f"velocity_ref_ms={max(1, int(self.config.profile_velocity_reference_ms))}"
        )


def _coerce_profile_result(
    value: RecoilProfileRecord | tuple[RecoilProfileRecord | None, RecoilControlCalibration | None] | None,
) -> tuple[RecoilProfileRecord | None, RecoilControlCalibration | None]:
    if value is None:
        return None, None
    if isinstance(value, tuple) and len(value) == 2:
        return value[0], value[1]
    return value, None


def _cumulative_profile_values(profile: RecoilProfileRecord, *, elapsed_ms: int) -> tuple[float, float]:
    if elapsed_ms < profile.initial_delay_ms:
        return 0.0, 0.0

    sample_index = (elapsed_ms - profile.initial_delay_ms) // profile.sample_interval_ms
    sample_index = max(0, min(sample_index, profile.sample_count - 1))
    return float(profile.samples_x[sample_index]), float(profile.samples_y[sample_index])


def _manual_fire_pressed(frame: GamepadFrame) -> bool:
    return bool(frame.buttons.get("rb", False) or frame.right_trigger > _MANUAL_FIRE_TRIGGER_THRESHOLD)


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


def _normalize_delta_for_uncalibrated_mapping(
    delta: float,
    *,
    sample_interval_ms: int,
    config: RecoilCompensationConfig,
) -> float:
    interval_ms = max(1, int(sample_interval_ms))
    reference_ms = max(1, int(config.profile_velocity_reference_ms))
    return float(delta) * (reference_ms / interval_ms)
