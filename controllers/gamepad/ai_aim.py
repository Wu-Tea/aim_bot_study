from dataclasses import dataclass
from typing import Iterable, Protocol

from .adaptive_delta_gain import AdaptiveDeltaGain, AdaptiveDeltaGainConfig
from .horizontal_assist import (
    HorizontalAimAssist,
    HorizontalAimAssistConfig,
    compute_axis_soft_strengths,
)
from .manual_intent_guard import ManualIntentGuard, ManualIntentGuardConfig
from .overshoot_guard import OvershootGuard, OvershootGuardConfig
from .state import GamepadFrame, GamepadOutput


@dataclass(slots=True, frozen=True)
class AIAimConfig:
    smoothing: float = 0.62
    max_pixels: int = 130
    piecewise_mid_pixels: float = 60.0
    piecewise_max_pixels: float = 230.0
    piecewise_mid_ratio: float = 0.56
    piecewise_mid_pixels_y: float = 45.0
    piecewise_max_pixels_y: float = 180.0
    piecewise_mid_ratio_y: float = 0.65
    invert_x: bool = False
    invert_y: bool = False
    max_ai_force: float = 0.64
    max_ai_force_y: float = 0.8
    deadzone_inner: float = 1.5
    deadzone_outer: float = 5.0
    x_deadzone_outer: float = 3.0
    ai_fade_full: int = 8000
    ai_delta_gain: float = 0.7


@dataclass(slots=True)
class AIAimContext:
    manual_rx: float
    manual_ry: float
    target_dx: float
    target_dy: float
    timestamp: float
    assist_dx: float
    assist_dy: float
    ai_fade_manual_rx: float = 0.0
    ai_fade_manual_ry: float = 0.0
    x_force_bonus: float = 0.0
    x_desired_scale: float = 1.0
    y_desired_scale: float = 1.0
    x_carry_scale: float = 1.0
    y_carry_scale: float = 1.0


class AIAimSubPlugin(Protocol):
    def reset(self) -> None:
        ...

    def observe_target(
        self,
        *,
        target_dx: float,
        target_dy: float,
        is_aiming: bool,
        timestamp: float,
    ) -> None:
        ...

    def apply(self, context: AIAimContext) -> None:
        ...


class AdaptiveDeltaGainSubPlugin:
    def __init__(self, config: AdaptiveDeltaGainConfig | None = None):
        self.gain = AdaptiveDeltaGain(config)

    def reset(self) -> None:
        self.gain.reset()

    def observe_target(
        self,
        *,
        target_dx: float,
        target_dy: float,
        is_aiming: bool,
        timestamp: float,
    ) -> None:
        self.gain.observe_target(
            target_dx=target_dx,
            target_dy=target_dy,
            is_aiming=is_aiming,
            timestamp=timestamp,
        )

    def apply(self, context: AIAimContext) -> None:
        adjustment = self.gain.compute_adjustment(context.manual_rx, context.manual_ry)
        context.target_dx *= adjustment.target_dx_multiplier
        context.target_dy *= adjustment.target_dy_multiplier
        context.assist_dx *= adjustment.target_dx_multiplier
        context.assist_dy *= adjustment.target_dy_multiplier


class ManualIntentGuardSubPlugin:
    def __init__(self, config: ManualIntentGuardConfig | None = None):
        self.guard = ManualIntentGuard(config)

    def reset(self) -> None:
        self.guard.reset()

    def observe_target(
        self,
        *,
        target_dx: float,
        target_dy: float,
        is_aiming: bool,
        timestamp: float,
    ) -> None:
        self.guard.observe_target(
            target_dx=target_dx,
            is_aiming=is_aiming,
            timestamp=timestamp,
        )

    def apply(self, context: AIAimContext) -> None:
        adjustment = self.guard.compute_adjustment(context.manual_rx)
        context.manual_rx = adjustment.output_manual_rx
        context.ai_fade_manual_rx = adjustment.ai_fade_manual_rx


class HorizontalAssistSubPlugin:
    def __init__(self):
        self.assist = HorizontalAimAssist(
            HorizontalAimAssistConfig(
                min_error_px=4.0,
                min_velocity_px_per_sec=60.0,
                velocity_filter_alpha=0.45,
                feedforward_lead_seconds=0.02,
                feedforward_gain=0.70,
                max_feedforward_px=6.0,
                catchup_trigger_frames=3,
                catchup_gain_per_update=0.025,
                catchup_max_bonus=0.10,
                catchup_decay=0.04,
                opposing_input_threshold=5000,
                convergence_epsilon_px=0.25,
            )
        )

    def reset(self) -> None:
        self.assist.reset()

    def observe_target(
        self,
        *,
        target_dx: float,
        target_dy: float,
        is_aiming: bool,
        timestamp: float,
    ) -> None:
        self.assist.observe_target(
            target_dx=target_dx,
            is_aiming=is_aiming,
            timestamp=timestamp,
        )

    def apply(self, context: AIAimContext) -> None:
        feedforward_dx, x_force_bonus = self.assist.compute_adjustment(context.manual_rx)
        context.assist_dx += feedforward_dx
        context.x_force_bonus += x_force_bonus


class OvershootGuardSubPlugin:
    def __init__(self):
        self.guard = OvershootGuard(
            OvershootGuardConfig(
                manual_input_threshold=3500,
                near_error_px=8.0,
                release_error_px=22.0,
                convergence_epsilon_px=0.25,
                convergence_trigger_frames=2,
                convergence_build_per_update=0.22,
                convergence_max_guard=0.50,
                convergence_decay=0.18,
                zero_cross_arm_px=6.0,
                zero_cross_hold_seconds=0.04,
                zero_cross_guard=0.85,
                carry_damp_gain=1.0,
            )
        )

    def reset(self) -> None:
        self.guard.reset()

    def observe_target(
        self,
        *,
        target_dx: float,
        target_dy: float,
        is_aiming: bool,
        timestamp: float,
    ) -> None:
        self.guard.observe_target(
            target_dx=target_dx,
            target_dy=target_dy,
            is_aiming=is_aiming,
            timestamp=timestamp,
        )

    def apply(self, context: AIAimContext) -> None:
        adjustment = self.guard.compute_adjustment(
            manual_rx=context.manual_rx,
            manual_ry=context.manual_ry,
            timestamp=context.timestamp,
        )
        context.x_desired_scale *= adjustment.x_desired_scale
        context.y_desired_scale *= adjustment.y_desired_scale
        context.x_carry_scale *= adjustment.x_carry_scale
        context.y_carry_scale *= adjustment.y_carry_scale


class AIAimPlugin:
    def __init__(
        self,
        config: AIAimConfig | None = None,
        sub_plugins: Iterable[AIAimSubPlugin] | None = None,
    ):
        self.config = config or AIAimConfig()
        self.sub_plugins = tuple(sub_plugins) if sub_plugins is not None else (
            ManualIntentGuardSubPlugin(),
            AdaptiveDeltaGainSubPlugin(),
            HorizontalAssistSubPlugin(),
            OvershootGuardSubPlugin(),
        )
        self.ai_stick_x = 0.0
        self.ai_stick_y = 0.0
        self._last_target_revision: int | None = None

    def reset(self) -> None:
        for plugin in self.sub_plugins:
            plugin.reset()
        self._last_target_revision = None

    def apply(self, frame: GamepadFrame, output: GamepadOutput) -> None:
        target_dx = frame.target_dx * self.config.ai_delta_gain
        target_dy = frame.target_dy * self.config.ai_delta_gain
        self._observe_if_needed(frame, target_dx, target_dy)

        context = AIAimContext(
            manual_rx=float(frame.manual_right_x),
            manual_ry=float(frame.manual_right_y),
            target_dx=target_dx,
            target_dy=target_dy,
            timestamp=frame.timestamp,
            assist_dx=target_dx,
            assist_dy=target_dy,
            ai_fade_manual_rx=float(frame.manual_right_x),
            ai_fade_manual_ry=float(frame.manual_right_y),
        )
        for plugin in self.sub_plugins:
            plugin.apply(context)

        desired_ai_x = 0.0
        desired_ai_y = 0.0
        if frame.is_aiming:
            x_strength, y_strength = compute_axis_soft_strengths(
                dx=context.assist_dx,
                dy=context.assist_dy,
                inner=self.config.deadzone_inner,
                radial_outer=self.config.deadzone_outer,
                x_outer=self.config.x_deadzone_outer,
            )
            if x_strength > 0.0 or y_strength > 0.0:
                desired_ai_x = self._map_pixel_to_stick(context.assist_dx) * x_strength
                desired_ai_y = self._map_pixel_to_stick(
                    -context.assist_dy,
                    mid_pixels=self.config.piecewise_mid_pixels_y,
                    max_pixels=self.config.piecewise_max_pixels_y,
                    mid_ratio=self.config.piecewise_mid_ratio_y,
                ) * y_strength

                if self.config.invert_x:
                    desired_ai_x = -desired_ai_x
                if self.config.invert_y:
                    desired_ai_y = -desired_ai_y

                x_limit = 32767 * min(1.0, self.config.max_ai_force + context.x_force_bonus)
                y_limit = 32767 * self.config.max_ai_force_y
                desired_ai_x = self._clamp(desired_ai_x, x_limit) * context.x_desired_scale
                desired_ai_y = self._clamp(desired_ai_y, y_limit) * context.y_desired_scale

        self.ai_stick_x = (
            self.ai_stick_x * self.config.smoothing * context.x_carry_scale
        ) + (desired_ai_x * (1.0 - self.config.smoothing))
        self.ai_stick_y = (
            self.ai_stick_y * self.config.smoothing * context.y_carry_scale
        ) + (desired_ai_y * (1.0 - self.config.smoothing))

        scale_x = self._ai_scale_factor(context.ai_fade_manual_rx)
        scale_y = self._ai_scale_factor(context.ai_fade_manual_ry)
        output.right_x = int(context.manual_rx + (self.ai_stick_x * scale_x))
        output.right_y = int(context.manual_ry + (self.ai_stick_y * scale_y))

    def _observe_if_needed(self, frame: GamepadFrame, target_dx: float, target_dy: float) -> None:
        if self._last_target_revision == frame.target_revision:
            return

        observation_time = frame.target_timestamp
        if observation_time is None:
            observation_time = frame.timestamp

        for plugin in self.sub_plugins:
            plugin.observe_target(
                target_dx=target_dx,
                target_dy=target_dy,
                is_aiming=frame.is_aiming,
                timestamp=observation_time,
            )
        self._last_target_revision = frame.target_revision

    def _map_pixel_to_stick(
        self,
        delta: float,
        *,
        mid_pixels: float | None = None,
        max_pixels: float | None = None,
        mid_ratio: float | None = None,
    ) -> float:
        abs_delta = abs(delta)
        sign = -1.0 if delta < 0.0 else 1.0
        piecewise = self._piecewise_map(
            abs_delta,
            mid_pixels=mid_pixels,
            max_pixels=max_pixels,
            mid_ratio=mid_ratio,
        )
        if piecewise is not None:
            return piecewise * sign

        clamped = self._clamp(delta, self.config.max_pixels)
        return (clamped / self.config.max_pixels) * 32767

    def _piecewise_map(
        self,
        abs_delta: float,
        *,
        mid_pixels: float | None = None,
        max_pixels: float | None = None,
        mid_ratio: float | None = None,
    ) -> float | None:
        mid_pixels = float(
            self.config.piecewise_mid_pixels if mid_pixels is None else mid_pixels
        ) * self.config.ai_delta_gain
        max_pixels = float(
            self.config.piecewise_max_pixels if max_pixels is None else max_pixels
        ) * self.config.ai_delta_gain
        mid_ratio = self.config.piecewise_mid_ratio if mid_ratio is None else mid_ratio

        if (
            mid_pixels <= 0.0
            or max_pixels <= mid_pixels
            or mid_ratio <= 0.0
            or mid_ratio >= 1.0
        ):
            return None

        if abs_delta >= max_pixels:
            return 32767.0

        if abs_delta <= mid_pixels:
            progress = abs_delta / mid_pixels
            return 32767.0 * mid_ratio * progress

        progress = (abs_delta - mid_pixels) / (max_pixels - mid_pixels)
        return 32767.0 * (mid_ratio + ((1.0 - mid_ratio) * progress))

    def _ai_scale_factor(self, user_val: float) -> float:
        magnitude = abs(user_val)
        if magnitude >= self.config.ai_fade_full:
            return 0.0
        return 1.0 - (magnitude / self.config.ai_fade_full)

    @staticmethod
    def _clamp(value: float, limit: float) -> float:
        return max(-limit, min(limit, value))
