import math
from dataclasses import dataclass

from .horizontal_assist import (
    compute_axis_soft_strengths,
    soft_ramp_strength,
)
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
    ai_delta_gain: float = 1.0
    target_max_age_ms: float = 50.0
    target_projection_max_age_ms: float = 24.0
    target_projection_reticle_speed_px_per_sec: float = 1500.0
    target_projection_velocity_lowpass_alpha: float = 0.35
    target_projection_max_velocity_px_per_sec: float = 1200.0
    target_projection_weak_velocity_decay: float = 0.70
    ads_snap_window_ms: int = 100
    ads_snap_smoothing: float = 0.0
    ads_snap_max_ai_force: float = 1.0
    ads_snap_max_ai_force_y: float = 1.0
    ads_snap_max_target_dy_px: float = 90.0
    ads_snap_reticle_speed_px_per_sec: float = 1500.0
    ads_snap_time_to_go_gain: float = 1.0
    ads_snap_time_to_go_min_remaining_ms: float = 35.0
    ads_snap_opposing_manual_suppression_max: float = 0.35
    body_lock_smoothing: float = 0.14
    body_lock_max_ai_force: float = 0.30
    body_lock_opposing_boost_max_ai_force: float = 0.42
    body_lock_max_ai_force_y: float = 0.42
    body_lock_box_tolerance_px: float = 18.0
    body_lock_activation_box_px: float = 150.0
    body_lock_confidence_frames: int = 5
    body_lock_confidence_min_strong: float = 0.50
    body_lock_opposing_suppression_max: float = 1.0
    body_lock_orthogonal_suppression_max: float = 0.60
    body_lock_helpful_preservation_floor: float = 1.0
    body_lock_manual_overlap_scale: float = 0.0
    body_lock_near_lock_error_px: float = 32.0
    body_lock_vertical_orthogonal_bias: float = 1.15
    body_lock_vertical_deadzone_px: float = 6.0
    body_lock_vertical_tail_inner_px: float = 2.0
    body_lock_vertical_tail_speed_threshold_px_per_sec: float = 90.0
    body_lock_release_tail_scale: float = 0.20
    body_lock_lateral_motion_min_speed_px_per_sec: float = 120.0
    body_lock_lateral_motion_lead_seconds: float = 0.04
    body_lock_lateral_motion_lead_window_px: float = 8.0
    body_lock_lateral_motion_lead_max_px: float = 7.0
    body_lock_lateral_motion_tail_scale: float = 0.65
    body_lock_upper_body_ratio: float = 0.40
    body_lock_lead_frames: int = 5
    body_lock_lead_seconds: float = 0.0
    body_lock_vertical_lead_scale: float = 0.95
    body_lock_lead_max_px: float = 18.0
    body_lock_target_match_iou: float = 0.10
    body_lock_target_match_center_px: float = 48.0
    weak_target_body_lock_force_scale: float = 0.55
    cue_hold_body_lock_force_scale: float = 0.35
    auto_fire_ready_error_px: float = 16.0
    auto_fire_ready_frames: int = 2
    auto_fire_ready_min_ads_ms: float = 70.0
    auto_fire_ready_max_ai_stick: float = 6000.0


@dataclass(slots=True, frozen=True)
class BodyLockManualResolution:
    sanitized_manual_x: float
    sanitized_manual_y: float
    helpful_preserved_ratio: float
    harmful_suppressed_ratio: float
    orthogonal_suppressed_ratio: float


class AIAimPlugin:
    def __init__(
        self,
        config: AIAimConfig | None = None,
    ):
        self.config = config or AIAimConfig()
        self._reset_runtime_state()

    def reset(self) -> None:
        self._reset_runtime_state()

    def apply(self, frame: GamepadFrame, output: GamepadOutput) -> None:
        self._apply_state_machine(frame, output)

    def _apply_state_machine(self, frame: GamepadFrame, output: GamepadOutput) -> None:
        output.right_x = frame.manual_right_x
        output.right_y = frame.manual_right_y

        if not frame.is_aiming:
            self._reset_runtime_state()
            self._publish_auto_fire_aim_ready(output, False, "not_aiming")
            return

        self._begin_ads_session(frame.timestamp)
        self._observe_target_motion(frame)
        if not self._is_ads_snap_window_open(frame.timestamp):
            self._ads_snap_used = True

        mode = "manual"
        desired_dx = 0.0
        desired_dy = 0.0
        base_lock_dx = 0.0
        base_lock_dy = 0.0
        manual_x = float(frame.manual_right_x)
        manual_y = float(frame.manual_right_y)

        if self._should_body_lock(frame):
            mode = "body_lock"
            base_lock_dx, base_lock_dy = self._body_lock_base_target_delta(frame)
            desired_dx, desired_dy = self._body_lock_target_delta(
                frame,
                base_dx=base_lock_dx,
                base_dy=base_lock_dy,
            )
            self._consume_ads_snap()
        elif self._should_trigger_ads_snap(frame):
            mode = "ads_snap"
            if self._ads_snap_target is None:
                self._ads_snap_target = frame.target
            desired_dx = frame.target_dx * self.config.ai_delta_gain
            desired_dy = self._clamp(
                frame.target_dy * self.config.ai_delta_gain,
                self.config.ads_snap_max_target_dy_px,
            )

        if mode == "manual":
            self.ai_stick_x = 0.0
            self.ai_stick_y = 0.0
            self._reset_body_lock_arbitration_tracking()
            self._record_raw_manual_passthrough(manual_x, manual_y)
        else:
            desired_ai_x, desired_ai_y, smoothing = self._compute_mode_stick(
                mode,
                target_dx=desired_dx,
                target_dy=desired_dy,
                manual_x=manual_x,
                manual_y=manual_y,
                authority_scale=self._target_body_lock_force_scale(frame)
                if mode == "body_lock"
                else 1.0,
                timestamp=frame.timestamp,
            )
            if mode == "body_lock":
                desired_ai_x, desired_ai_y = self._apply_body_lock_axis_guards(
                    desired_ai_x=desired_ai_x,
                    desired_ai_y=desired_ai_y,
                    desired_error_x=base_lock_dx,
                    desired_error_y=base_lock_dy,
                )
                stabilize_ratio = self._body_lock_stabilize_ratio(
                    base_lock_dx,
                    base_lock_dy,
                )
                if stabilize_ratio > 0.0:
                    desired_ai_x *= 1.0 + (0.85 * stabilize_ratio)
                    desired_ai_y *= 1.0 + (0.35 * stabilize_ratio)
                    smoothing *= max(0.15, 1.0 - (0.85 * stabilize_ratio))
            self.ai_stick_x = (self.ai_stick_x * smoothing) + (desired_ai_x * (1.0 - smoothing))
            vertical_scale = self._body_lock_vertical_ai_scale(mode, desired_dy)
            effective_ai_y = 0.0
            if vertical_scale is None:
                self.ai_stick_y = 0.0
            else:
                desired_ai_y *= vertical_scale
                effective_ai_y = desired_ai_y
                self.ai_stick_y = (self.ai_stick_y * smoothing) + (desired_ai_y * (1.0 - smoothing))

            if mode == "ads_snap":
                manual_x = self._resolve_ads_snap_opposing_manual(
                    manual_input=manual_x,
                    planned_ai=self.ai_stick_x,
                    timestamp=frame.timestamp,
                )
                manual_y = self._resolve_ads_snap_opposing_manual(
                    manual_input=manual_y,
                    planned_ai=self.ai_stick_y,
                    timestamp=frame.timestamp,
                )
                # During snap, treat manual same-direction input as part of the
                # intended total correction instead of stacking AI on top of it.
                self.ai_stick_x = self._resolve_ads_snap_manual_overlap(
                    planned_ai=self.ai_stick_x,
                    manual_input=manual_x,
                )
                self.ai_stick_y = self._resolve_ads_snap_manual_overlap(
                    planned_ai=self.ai_stick_y,
                    manual_input=manual_y,
                )

            if mode == "body_lock":
                lock_confidence = self._body_lock_confidence(frame, base_lock_dx, base_lock_dy)
                resolved_manual = self._resolve_body_lock_manual(
                    frame,
                    desired_ai_x=desired_ai_x,
                    desired_ai_y=effective_ai_y,
                    desired_error_x=desired_dx,
                    desired_error_y=desired_dy,
                    lock_confidence=lock_confidence,
                )
                manual_x = resolved_manual.sanitized_manual_x
                manual_y = resolved_manual.sanitized_manual_y
                error_radius = math.hypot(desired_dx, desired_dy)
                self.ai_stick_x = self._resolve_body_lock_manual_overlap(
                    planned_ai=self.ai_stick_x,
                    manual_input=manual_x,
                    error_radius=error_radius,
                )
                self.ai_stick_y = self._resolve_body_lock_manual_overlap(
                    planned_ai=self.ai_stick_y,
                    manual_input=manual_y,
                    error_radius=error_radius,
                )
                self._last_lock_confidence = lock_confidence
                self._last_sanitized_manual_x = manual_x
                self._last_sanitized_manual_y = manual_y
                self._last_helpful_preserved_ratio = (
                    resolved_manual.helpful_preserved_ratio
                )
                self._last_harmful_suppressed_ratio = (
                    resolved_manual.harmful_suppressed_ratio
                )
                self._last_orthogonal_suppressed_ratio = (
                    resolved_manual.orthogonal_suppressed_ratio
                )
            else:
                self._reset_body_lock_arbitration_tracking()
                self._record_raw_manual_passthrough(manual_x, manual_y)

        output.right_x = int(manual_x + self.ai_stick_x)
        output.right_y = int(manual_y + self.ai_stick_y)
        self._update_auto_fire_aim_readiness(
            frame,
            output,
            mode=mode,
            base_lock_dx=base_lock_dx,
            base_lock_dy=base_lock_dy,
        )
        self._mode = mode

    def _begin_ads_session(self, timestamp: float) -> None:
        if self._ads_active:
            return
        self._ads_active = True
        self._ads_started_at = timestamp
        self._ads_snap_used = False
        self._ads_snap_target = None
        self._mode = "manual"
        self.ai_stick_x = 0.0
        self.ai_stick_y = 0.0
        self._reset_motion_tracking()

    def _is_ads_snap_window_open(self, timestamp: float) -> bool:
        if self._ads_started_at is None:
            return False
        return (timestamp - self._ads_started_at) <= (self.config.ads_snap_window_ms / 1000.0)

    def _ads_snap_remaining_seconds(self, timestamp: float) -> float | None:
        if self._ads_started_at is None:
            return None
        window = self.config.ads_snap_window_ms / 1000.0
        if window <= 0.0:
            return 0.0
        elapsed = max(0.0, float(timestamp) - self._ads_started_at)
        return max(0.0, window - elapsed)

    def _ads_snap_progress_ratio(self, timestamp: float) -> float:
        if self._ads_started_at is None:
            return 0.0
        window = self.config.ads_snap_window_ms / 1000.0
        if window <= 0.0:
            return 1.0
        elapsed = max(0.0, float(timestamp) - self._ads_started_at)
        return max(0.0, min(1.0, elapsed / window))

    def _should_trigger_ads_snap(self, frame: GamepadFrame) -> bool:
        if self._ads_snap_used or not self._has_fresh_target(frame):
            return False
        if not self._is_strong_target_authority(frame.target):
            return False
        if not self._is_ads_snap_window_open(frame.timestamp):
            self._consume_ads_snap()
            return False
        if self._ads_snap_target is not None and not self._targets_match(
            self._ads_snap_target,
            frame.target,
        ):
            self._consume_ads_snap()
            return False
        return not self._should_body_lock(frame)

    def _should_body_lock(self, frame: GamepadFrame) -> bool:
        if not self._has_fresh_target(frame) or frame.target.body_box is None:
            return False

        left, top, right, bottom = frame.target.body_box
        tolerance = self.config.body_lock_box_tolerance_px
        screen_x = frame.target.screen_center_x
        screen_y = frame.target.screen_center_y
        if not (
            (left - tolerance) <= screen_x <= (right + tolerance)
            and (top - tolerance) <= screen_y <= (bottom + tolerance)
        ):
            return False

        lock_dx, lock_dy = self._body_lock_base_target_delta(frame)
        activation_half = self.config.body_lock_activation_box_px * 0.5
        return abs(lock_dx) <= activation_half and abs(lock_dy) <= activation_half

    def _body_lock_base_target_delta(self, frame: GamepadFrame) -> tuple[float, float]:
        if frame.target is None:
            return (
                frame.target_dx * self.config.ai_delta_gain,
                frame.target_dy * self.config.ai_delta_gain,
            )

        lock_x, lock_y = self._upper_body_point(frame)
        return (
            (lock_x - frame.target.screen_center_x) * self.config.ai_delta_gain,
            (lock_y - frame.target.screen_center_y) * self.config.ai_delta_gain,
        )

    def _body_lock_target_delta(
        self,
        frame: GamepadFrame,
        *,
        base_dx: float | None = None,
        base_dy: float | None = None,
    ) -> tuple[float, float]:
        if base_dx is None or base_dy is None:
            base_dx, base_dy = self._body_lock_base_target_delta(frame)

        if frame.target is None:
            return (base_dx, base_dy)

        lock_x, lock_y = self._upper_body_point(frame)
        stabilize_ratio = self._body_lock_stabilize_ratio(base_dx, base_dy)
        lead_scale = max(0.0, 1.0 - (1.5 * stabilize_ratio))
        if self._motion_frames >= max(1, int(self.config.body_lock_lead_frames)):
            lock_x += self._clamp(
                self._motion_velocity_x * self.config.body_lock_lead_seconds,
                self.config.body_lock_lead_max_px,
            ) * lead_scale
            lock_y += self._clamp(
                self._motion_velocity_y
                * self.config.body_lock_lead_seconds
                * self.config.body_lock_vertical_lead_scale,
                self.config.body_lock_lead_max_px,
            ) * lead_scale

        dx = (lock_x - frame.target.screen_center_x) * self.config.ai_delta_gain
        dy = (lock_y - frame.target.screen_center_y) * self.config.ai_delta_gain
        return (self._body_lock_lateral_motion_delta(dx), dy)

    def _body_lock_confidence(self, frame: GamepadFrame, lock_dx: float, lock_dy: float) -> float:
        if frame.target is None or frame.target.body_box is None:
            return 0.0

        continuity_frames = self._observe_body_lock_target(frame)
        activation_half = max(1.0, self.config.body_lock_activation_box_px * 0.5)
        activation_distance = max(abs(lock_dx), abs(lock_dy))
        activation_ratio = max(0.0, 1.0 - min(1.0, activation_distance / activation_half))
        continuity = min(
            1.0,
            continuity_frames / max(1, self.config.body_lock_confidence_frames),
        )
        motion_ready = 1.0 if self._motion_frames >= 2 else 0.0
        valid = 1.0 if self._should_body_lock(frame) else 0.0

        confidence = max(
            0.0,
            min(
                1.0,
                (0.40 * continuity)
                + (0.30 * activation_ratio)
                + (0.20 * valid)
                + (0.10 * motion_ready),
            ),
        )
        return confidence * self._target_body_lock_force_scale(frame)

    def _observe_body_lock_target(self, frame: GamepadFrame) -> int:
        target = frame.target
        if target is None:
            self._reset_body_lock_arbitration_tracking()
            return 0

        if self._body_lock_target is None or not self._targets_match(
            self._body_lock_target,
            target,
        ):
            self._body_lock_target = target
            self._body_lock_frames = 1
            return self._body_lock_frames

        self._body_lock_frames += 1
        return self._body_lock_frames

    def _resolve_body_lock_manual(
        self,
        frame: GamepadFrame,
        *,
        desired_ai_x: float,
        desired_ai_y: float,
        desired_error_x: float,
        desired_error_y: float,
        lock_confidence: float,
    ) -> BodyLockManualResolution:
        manual_x = float(frame.manual_right_x)
        manual_y = float(frame.manual_right_y)
        error_radius = math.hypot(desired_error_x, desired_error_y)
        if lock_confidence <= 0.0:
            return BodyLockManualResolution(manual_x, manual_y, 1.0, 0.0, 0.0)

        arbitration_x = desired_ai_x
        arbitration_y = desired_ai_y
        fallback_to_error_direction = False
        desired_norm = math.hypot(arbitration_x, arbitration_y)
        if desired_norm < 1.0 and error_radius > 0.0:
            # Axis guards can intentionally zero the AI output inside the
            # release window; keep arbitrating against the remaining error so
            # same-direction manual input does not immediately punch through.
            arbitration_x = desired_error_x
            arbitration_y = desired_error_y
            desired_norm = error_radius
            fallback_to_error_direction = True
        if desired_norm <= 0.0:
            terminal_manual_scale = max(
                0.10,
                min(
                    1.0,
                    error_radius
                    / max(1.0, self.config.body_lock_near_lock_error_px),
                ),
            )
            return BodyLockManualResolution(
                sanitized_manual_x=manual_x * terminal_manual_scale,
                sanitized_manual_y=manual_y * terminal_manual_scale,
                helpful_preserved_ratio=terminal_manual_scale,
                harmful_suppressed_ratio=0.0,
                orthogonal_suppressed_ratio=0.0,
            )

        ux = arbitration_x / desired_norm
        uy = arbitration_y / desired_norm
        parallel = (manual_x * ux) + (manual_y * uy)
        parallel_x = ux * parallel
        parallel_y = uy * parallel
        orth_x = manual_x - parallel_x
        orth_y = manual_y - parallel_y

        helpful = max(0.0, parallel)
        harmful = max(0.0, -parallel)
        near_lock_ratio = max(
            0.0,
            1.0
            - min(
                1.0,
                error_radius / max(1.0, self.config.body_lock_near_lock_error_px),
            ),
        )
        confidence_ratio = max(
            0.0,
            min(
                1.0,
                (lock_confidence - self.config.body_lock_confidence_min_strong)
                / max(0.001, 1.0 - self.config.body_lock_confidence_min_strong),
            ),
        )

        helpful_scale = min(
            1.0,
            self.config.body_lock_helpful_preservation_floor
            + (
                (1.0 - near_lock_ratio)
                * (1.0 - self.config.body_lock_helpful_preservation_floor)
            ),
        )
        if fallback_to_error_direction:
            helpful_scale = min(
                helpful_scale,
                max(
                    0.10,
                    min(
                        1.0,
                        error_radius
                        / max(1.0, self.config.body_lock_near_lock_error_px),
                    ),
                ),
            )
        harmful_suppression = min(
            self.config.body_lock_opposing_suppression_max,
            self.config.body_lock_opposing_suppression_max * confidence_ratio,
        )
        orthogonal_suppression = min(
            self.config.body_lock_orthogonal_suppression_max,
            self.config.body_lock_orthogonal_suppression_max
            * max(lock_confidence, near_lock_ratio),
        )
        stabilize_ratio = self._body_lock_stabilize_ratio(
            desired_error_x,
            desired_error_y,
        )
        if stabilize_ratio > 0.0:
            helpful_scale *= max(0.75, 1.0 - (0.10 * stabilize_ratio))
            orthogonal_suppression = min(
                1.0,
                orthogonal_suppression + (0.15 * stabilize_ratio),
            )

        sanitized_parallel = (helpful * helpful_scale) - (
            harmful * (1.0 - harmful_suppression)
        )
        sanitized_orth_x = orth_x * (1.0 - orthogonal_suppression)
        sanitized_orth_y = orth_y * (
            1.0
            - min(
                1.0,
                orthogonal_suppression
                * self.config.body_lock_vertical_orthogonal_bias,
            )
        )
        orthogonal_magnitude = math.hypot(orth_x, orth_y)
        return BodyLockManualResolution(
            sanitized_manual_x=(ux * sanitized_parallel) + sanitized_orth_x,
            sanitized_manual_y=(uy * sanitized_parallel) + sanitized_orth_y,
            helpful_preserved_ratio=(
                1.0
                if helpful <= 1.0
                else min(1.0, (helpful * helpful_scale) / helpful)
            ),
            harmful_suppressed_ratio=(
                0.0
                if harmful <= 1.0
                else min(1.0, (harmful * harmful_suppression) / harmful)
            ),
            orthogonal_suppressed_ratio=(
                0.0 if orthogonal_magnitude <= 1.0 else orthogonal_suppression
            ),
        )

    def _apply_body_lock_axis_guards(
        self,
        *,
        desired_ai_x: float,
        desired_ai_y: float,
        desired_error_x: float,
        desired_error_y: float,
    ) -> tuple[float, float]:
        desired_ai_x = self._apply_body_lock_axis_guard(
            axis="x",
            desired_ai=desired_ai_x,
            desired_error=desired_error_x,
        )
        desired_ai_y = self._apply_body_lock_axis_guard(
            axis="y",
            desired_ai=desired_ai_y,
            desired_error=desired_error_y,
        )
        self._last_body_lock_error_x = desired_error_x
        self._last_body_lock_error_y = desired_error_y
        return desired_ai_x, desired_ai_y

    def _apply_body_lock_axis_guard(
        self,
        *,
        axis: str,
        desired_ai: float,
        desired_error: float,
    ) -> float:
        release_threshold = self._body_lock_axis_release_threshold(axis)
        if abs(desired_error) <= release_threshold:
            self._set_body_lock_axis_hold(axis, 0)
            tail_scale = self._body_lock_axis_release_tail_scale(axis)
            previous_error = self._last_body_lock_error_x if axis == "x" else self._last_body_lock_error_y
            if self._is_body_lock_zero_cross(axis, previous_error, desired_error):
                self._clear_body_lock_axis_carry(axis)
            if tail_scale > 0.0:
                return desired_ai * tail_scale
            self._clear_body_lock_axis_carry(axis)
            return 0.0

        if self._body_lock_axis_hold_remaining(axis) > 0:
            self._set_body_lock_axis_hold(
                axis,
                self._body_lock_axis_hold_remaining(axis) - 1,
            )
            self._clear_body_lock_axis_carry(axis)
            return 0.0

        previous_error = self._last_body_lock_error_x if axis == "x" else self._last_body_lock_error_y
        if self._is_body_lock_zero_cross(axis, previous_error, desired_error):
            self._set_body_lock_axis_hold(
                axis,
                self._body_lock_zero_cross_hold_frames(axis),
            )
            self._clear_body_lock_axis_carry(axis)
            return 0.0

        return desired_ai

    def _body_lock_axis_release_threshold(self, axis: str) -> float:
        if axis == "y":
            return max(
                self.config.body_lock_vertical_tail_inner_px + 0.5,
                self.config.deadzone_inner + 1.0,
            )
        return max(
            self.config.deadzone_inner + 1.0,
            self.config.x_deadzone_outer * 0.85,
        )

    def _body_lock_lateral_motion_delta(self, dx: float) -> float:
        if self._motion_frames < 2:
            return dx
        if abs(self._motion_velocity_x) < self.config.body_lock_lateral_motion_min_speed_px_per_sec:
            return dx

        window = max(
            self.config.body_lock_lateral_motion_lead_window_px,
            self._body_lock_axis_release_threshold("x"),
        )
        abs_dx = abs(dx)
        if abs_dx >= window:
            return dx

        near_ratio = 1.0 - min(1.0, abs_dx / max(1.0, window))
        lead_px = self._clamp(
            self._motion_velocity_x * self.config.body_lock_lateral_motion_lead_seconds,
            self.config.body_lock_lateral_motion_lead_max_px,
        )
        return dx + (lead_px * near_ratio)

    def _body_lock_axis_release_tail_scale(self, axis: str) -> float:
        tail_scale = max(0.0, min(1.0, self.config.body_lock_release_tail_scale))
        if axis != "x":
            return tail_scale
        if self._motion_frames < 2:
            return tail_scale
        if abs(self._motion_velocity_x) < self.config.body_lock_lateral_motion_min_speed_px_per_sec:
            return tail_scale
        moving_tail = max(
            0.0,
            min(1.0, self.config.body_lock_lateral_motion_tail_scale),
        )
        return max(tail_scale, moving_tail)

    def _body_lock_zero_cross_hold_frames(self, axis: str) -> int:
        return 1

    def _body_lock_zero_cross_guard_px(self, axis: str) -> float:
        if axis == "y":
            return max(
                self.config.body_lock_vertical_deadzone_px,
                self._body_lock_axis_release_threshold(axis),
            )
        return max(
            self.config.x_deadzone_outer,
            self._body_lock_axis_release_threshold(axis),
        )

    def _is_body_lock_zero_cross(
        self,
        axis: str,
        previous_error: float | None,
        current_error: float,
    ) -> bool:
        if previous_error is None:
            return False
        if previous_error == 0.0 or current_error == 0.0:
            return False
        if (previous_error > 0.0) == (current_error > 0.0):
            return False
        return abs(previous_error) <= self._body_lock_zero_cross_guard_px(axis)

    def _body_lock_axis_hold_remaining(self, axis: str) -> int:
        return self._body_lock_zero_cross_hold_x if axis == "x" else self._body_lock_zero_cross_hold_y

    def _set_body_lock_axis_hold(self, axis: str, value: int) -> None:
        if axis == "x":
            self._body_lock_zero_cross_hold_x = max(0, value)
            return
        self._body_lock_zero_cross_hold_y = max(0, value)

    def _clear_body_lock_axis_carry(self, axis: str) -> None:
        if axis == "x":
            self.ai_stick_x = 0.0
            return
        self.ai_stick_y = 0.0

    def _consume_ads_snap(self) -> None:
        self._ads_snap_used = True
        self._ads_snap_target = None

    def _resolve_ads_snap_manual_overlap(
        self,
        *,
        planned_ai: float,
        manual_input: float,
    ) -> float:
        if planned_ai == 0.0 or manual_input == 0.0:
            return planned_ai
        if planned_ai * manual_input <= 0.0:
            return planned_ai

        remaining = abs(planned_ai) - abs(manual_input)
        if remaining <= 0.0:
            return 0.0
        return math.copysign(remaining, planned_ai)

    def _resolve_ads_snap_opposing_manual(
        self,
        *,
        manual_input: float,
        planned_ai: float,
        timestamp: float,
    ) -> float:
        if manual_input == 0.0 or planned_ai == 0.0:
            return manual_input
        if manual_input * planned_ai >= 0.0:
            return manual_input

        max_suppression = max(
            0.0,
            min(1.0, self.config.ads_snap_opposing_manual_suppression_max),
        )
        if max_suppression <= 0.0:
            return manual_input

        progress = self._ads_snap_progress_ratio(timestamp)
        suppression = max_suppression * (0.35 + (0.65 * progress))
        return manual_input * (1.0 - suppression)

    def _resolve_body_lock_manual_overlap(
        self,
        *,
        planned_ai: float,
        manual_input: float,
        error_radius: float,
    ) -> float:
        if planned_ai == 0.0 or manual_input == 0.0:
            return planned_ai
        if planned_ai * manual_input <= 0.0:
            return planned_ai

        overlap_scale = max(0.0, min(1.0, self.config.body_lock_manual_overlap_scale))
        if overlap_scale <= 0.0:
            return planned_ai

        near_lock_ratio = max(
            0.0,
            1.0
            - min(
                1.0,
                error_radius / max(1.0, self.config.body_lock_near_lock_error_px),
            ),
        )
        manual_credit = abs(manual_input) * overlap_scale * (0.25 + (0.75 * near_lock_ratio))
        remaining = abs(planned_ai) - manual_credit
        if remaining <= 0.0:
            return 0.0
        return math.copysign(remaining, planned_ai)

    def _body_lock_vertical_ai_scale(self, mode: str, desired_dy: float) -> float | None:
        if mode != "body_lock":
            return 1.0

        abs_dy = abs(desired_dy)
        deadzone = self.config.body_lock_vertical_deadzone_px
        if abs_dy > deadzone:
            return 1.0

        if self._motion_frames < 2:
            return None

        motion_speed = math.hypot(self._motion_velocity_x, self._motion_velocity_y)
        if motion_speed > self.config.body_lock_vertical_tail_speed_threshold_px_per_sec:
            return None

        scale = soft_ramp_strength(
            abs_dy,
            self.config.body_lock_vertical_tail_inner_px,
            deadzone,
        )
        settle_speed_threshold = max(
            1.0,
            self.config.body_lock_vertical_tail_speed_threshold_px_per_sec * 0.4,
        )
        if (
            abs_dy > (self.config.body_lock_vertical_tail_inner_px * 0.6)
            and motion_speed <= settle_speed_threshold
        ):
            settle_ratio = max(
                0.0,
                1.0 - min(1.0, motion_speed / settle_speed_threshold),
            )
            scale = max(scale, 0.15 + (0.30 * settle_ratio))

        return scale

    def _body_lock_stabilize_ratio(self, desired_dx: float, desired_dy: float) -> float:
        if self._motion_frames < 2:
            return 0.0

        error_radius = math.hypot(desired_dx, desired_dy)
        near_lock_ratio = max(
            0.0,
            1.0
            - min(
                1.0,
                error_radius / max(1.0, self.config.body_lock_near_lock_error_px),
            ),
        )
        if near_lock_ratio <= 0.0:
            return 0.0

        motion_speed = math.hypot(self._motion_velocity_x, self._motion_velocity_y)
        speed_threshold = max(
            1.0,
            self.config.body_lock_vertical_tail_speed_threshold_px_per_sec,
        )
        low_speed_ratio = max(0.0, 1.0 - min(1.0, motion_speed / speed_threshold))
        return (near_lock_ratio * near_lock_ratio) * (low_speed_ratio * low_speed_ratio)

    def _upper_body_point(self, frame: GamepadFrame) -> tuple[float, float]:
        if frame.target is None or frame.target.body_box is None:
            if frame.target is None:
                return (0.0, 0.0)
            return (frame.target.aim_point_x, frame.target.aim_point_y)

        left, top, right, bottom = frame.target.body_box
        return (
            (left + right) * 0.5,
            top + ((bottom - top) * self.config.body_lock_upper_body_ratio),
        )

    def _observe_target_motion(self, frame: GamepadFrame) -> None:
        if (
            not self._has_fresh_target(frame)
            or frame.target.body_box is None
            or not self._is_strong_target_authority(frame.target)
        ):
            self._reset_motion_tracking()
            return

        current_box = frame.target.body_box
        current_point = self._upper_body_point(frame)
        if (
            self._motion_box is None
            or self._motion_timestamp is None
            or self._motion_point is None
            or not self._boxes_match(self._motion_box, current_box)
        ):
            self._motion_box = current_box
            self._motion_point = current_point
            self._motion_timestamp = frame.timestamp
            self._motion_velocity_x = 0.0
            self._motion_velocity_y = 0.0
            self._motion_frames = 1
            return

        dt = frame.timestamp - self._motion_timestamp
        if dt > 0.0:
            self._motion_velocity_x = (current_point[0] - self._motion_point[0]) / dt
            self._motion_velocity_y = (current_point[1] - self._motion_point[1]) / dt

        self._motion_box = current_box
        self._motion_point = current_point
        self._motion_timestamp = frame.timestamp
        self._motion_frames += 1

    def _has_fresh_target(self, frame: GamepadFrame) -> bool:
        if frame.target is None:
            return False
        if not self._has_aim_authority(frame.target):
            return False
        if frame.target_timestamp is None or self.config.target_max_age_ms <= 0.0:
            return True
        max_age_seconds = self.config.target_max_age_ms / 1000.0
        return (frame.timestamp - frame.target_timestamp) <= max_age_seconds

    def _update_auto_fire_aim_readiness(
        self,
        frame: GamepadFrame,
        output: GamepadOutput,
        *,
        mode: str,
        base_lock_dx: float,
        base_lock_dy: float,
    ) -> None:
        if not frame.auto_fire_requested:
            self._reset_auto_fire_readiness_tracking()
            self._publish_auto_fire_aim_ready(output, True, "")
            return

        if not self._has_fresh_target(frame):
            self._reset_auto_fire_readiness_tracking()
            self._publish_auto_fire_aim_ready(output, False, "stale_target")
            return

        target = frame.target
        if not self._is_fire_authorized_target(target):
            self._reset_auto_fire_readiness_tracking()
            self._publish_auto_fire_aim_ready(output, False, "no_fire_authority")
            return

        min_ads_seconds = max(0.0, self.config.auto_fire_ready_min_ads_ms) / 1000.0
        if self._ads_started_at is None or (frame.timestamp - self._ads_started_at) < min_ads_seconds:
            self._reset_auto_fire_readiness_tracking()
            self._publish_auto_fire_aim_ready(output, False, "min_ads")
            return

        if mode == "body_lock":
            error_px = math.hypot(base_lock_dx, base_lock_dy)
        else:
            error_px = math.hypot(
                frame.target_dx * self.config.ai_delta_gain,
                frame.target_dy * self.config.ai_delta_gain,
            )

        if error_px > max(0.0, self.config.auto_fire_ready_error_px):
            self._reset_auto_fire_readiness_tracking(target)
            reason = "ads_snap" if mode == "ads_snap" else "not_settled"
            self._publish_auto_fire_aim_ready(output, False, reason, error_px=error_px)
            return

        ai_stick_mag = max(abs(self.ai_stick_x), abs(self.ai_stick_y))
        max_ai_stick = max(0.0, self.config.auto_fire_ready_max_ai_stick)
        if max_ai_stick > 0.0 and ai_stick_mag > max_ai_stick:
            self._reset_auto_fire_readiness_tracking(target)
            self._publish_auto_fire_aim_ready(output, False, "ai_stick_high", error_px=error_px)
            return

        if self._auto_fire_ready_target is None or not self._targets_match(
            self._auto_fire_ready_target,
            target,
        ):
            self._auto_fire_ready_frames = 0
            self._auto_fire_ready_target = target

        self._auto_fire_ready_frames += 1
        required_frames = max(1, int(self.config.auto_fire_ready_frames))
        ready = self._auto_fire_ready_frames >= required_frames
        reason = "ready" if ready else "settle_frames"
        self._publish_auto_fire_aim_ready(
            output,
            ready,
            reason,
            error_px=error_px,
            streak=self._auto_fire_ready_frames,
        )

    def _publish_auto_fire_aim_ready(
        self,
        output: GamepadOutput,
        ready: bool,
        reason: str,
        *,
        error_px: float | None = None,
        streak: int | None = None,
    ) -> None:
        output.auto_fire_aim_ready = bool(ready)
        output.auto_fire_aim_ready_reason = reason
        output.auto_fire_settle_error_px = error_px
        output.auto_fire_settle_streak = self._auto_fire_ready_frames if streak is None else streak
        self._last_auto_fire_ready = bool(ready)
        self._last_auto_fire_ready_reason = reason
        self._last_auto_fire_ready_error_px = error_px

    def _reset_auto_fire_readiness_tracking(self, target=None) -> None:
        self._auto_fire_ready_frames = 0
        self._auto_fire_ready_target = target

    def _is_fire_authorized_target(self, target) -> bool:
        if target is None:
            return False
        return bool(getattr(target, "fire_authority", False)) and self._is_strong_target_authority(target)

    def _target_body_lock_force_scale(self, frame: GamepadFrame) -> float:
        if frame.target is None:
            return 0.0
        if self._is_cue_hold_target(frame.target):
            return max(0.0, min(1.0, self.config.cue_hold_body_lock_force_scale))
        if self._is_weak_association_target(frame.target):
            return max(0.0, min(1.0, self.config.weak_target_body_lock_force_scale))
        if self._is_strong_target_authority(frame.target):
            return 1.0
        return 0.0

    def _is_strong_target_authority(self, target) -> bool:
        if not self._has_aim_authority(target):
            return False
        if self._is_weak_association_target(target) or self._is_cue_hold_target(target):
            return False
        return True

    def _has_aim_authority(self, target) -> bool:
        if target is None:
            return False
        if not getattr(target, "aim_authority", True):
            return False
        return not self._is_predicted_target(target)

    @staticmethod
    def _target_token(target, attr: str) -> str:
        value = getattr(target, attr, None)
        if value is None:
            return ""
        return str(value).strip().casefold()

    def _is_weak_association_target(self, target) -> bool:
        source = self._target_token(target, "target_source")
        tier = self._target_token(target, "target_tier")
        return source in {"associated_weak", "weak_observed", "low_score"} or tier in {
            "associated_weak",
            "weak_observed",
        }

    def _is_cue_hold_target(self, target) -> bool:
        source = self._target_token(target, "target_source")
        tier = self._target_token(target, "target_tier")
        return source in {"cue_hold", "yellow_cue"} or tier == "cue_hold"

    def _is_predicted_target(self, target) -> bool:
        source = self._target_token(target, "target_source")
        tier = self._target_token(target, "target_tier")
        return source in {"predicted", "projected", "projection"} or tier in {
            "predicted",
            "projected",
            "projection",
            "none",
            "lost",
        }

    def _targets_match(self, lhs, rhs) -> bool:
        if lhs.body_box is not None and rhs.body_box is not None:
            return self._boxes_match(lhs.body_box, rhs.body_box)

        center_limit = max(
            self.config.body_lock_target_match_center_px,
            self.config.body_lock_activation_box_px * 0.5,
        )
        return (
            abs(lhs.aim_point_x - rhs.aim_point_x) <= center_limit
            and abs(lhs.aim_point_y - rhs.aim_point_y) <= center_limit
        )

    def _boxes_match(
        self,
        lhs: tuple[float, float, float, float],
        rhs: tuple[float, float, float, float],
    ) -> bool:
        if self._box_iou(lhs, rhs) >= self.config.body_lock_target_match_iou:
            return True

        lhs_center_x = (lhs[0] + lhs[2]) * 0.5
        lhs_center_y = (lhs[1] + lhs[3]) * 0.5
        rhs_center_x = (rhs[0] + rhs[2]) * 0.5
        rhs_center_y = (rhs[1] + rhs[3]) * 0.5
        return (
            abs(lhs_center_x - rhs_center_x) <= self.config.body_lock_target_match_center_px
            and abs(lhs_center_y - rhs_center_y) <= self.config.body_lock_target_match_center_px
        )

    @staticmethod
    def _box_iou(
        lhs: tuple[float, float, float, float],
        rhs: tuple[float, float, float, float],
    ) -> float:
        left = max(lhs[0], rhs[0])
        top = max(lhs[1], rhs[1])
        right = min(lhs[2], rhs[2])
        bottom = min(lhs[3], rhs[3])
        inter_w = max(0.0, right - left)
        inter_h = max(0.0, bottom - top)
        inter_area = inter_w * inter_h
        if inter_area <= 0.0:
            return 0.0

        lhs_area = max(0.0, lhs[2] - lhs[0]) * max(0.0, lhs[3] - lhs[1])
        rhs_area = max(0.0, rhs[2] - rhs[0]) * max(0.0, rhs[3] - rhs[1])
        union_area = lhs_area + rhs_area - inter_area
        if union_area <= 0.0:
            return 0.0
        return inter_area / union_area

    def _compute_mode_stick(
        self,
        mode: str,
        *,
        target_dx: float,
        target_dy: float,
        manual_x: float = 0.0,
        manual_y: float = 0.0,
        authority_scale: float = 1.0,
        timestamp: float | None = None,
    ) -> tuple[float, float, float]:
        x_strength, y_strength = compute_axis_soft_strengths(
            dx=target_dx,
            dy=target_dy,
            inner=self.config.deadzone_inner,
            radial_outer=self.config.deadzone_outer,
            x_outer=self.config.x_deadzone_outer,
        )
        if x_strength <= 0.0 and y_strength <= 0.0:
            return (0.0, 0.0, 0.0)

        desired_ai_x = self._map_pixel_to_stick(target_dx) * x_strength
        desired_ai_y = self._map_pixel_to_stick(
            -target_dy,
            mid_pixels=self.config.piecewise_mid_pixels_y,
            max_pixels=self.config.piecewise_max_pixels_y,
            mid_ratio=self.config.piecewise_mid_ratio_y,
        ) * y_strength
        if mode == "ads_snap":
            remaining_seconds = (
                self._ads_snap_remaining_seconds(timestamp)
                if timestamp is not None
                else None
            )
            desired_ai_x = self._prefer_larger_magnitude(
                desired_ai_x,
                self._ads_snap_time_to_go_stick(
                    target_dx,
                    axis_strength=x_strength,
                    remaining_seconds=remaining_seconds,
                ),
            )
            desired_ai_y = self._prefer_larger_magnitude(
                desired_ai_y,
                self._ads_snap_time_to_go_stick(
                    -target_dy,
                    axis_strength=y_strength,
                    remaining_seconds=remaining_seconds,
                ),
            )

        if self.config.invert_x:
            desired_ai_x = -desired_ai_x
        if self.config.invert_y:
            desired_ai_y = -desired_ai_y

        if mode == "ads_snap":
            x_limit = 32767 * self.config.ads_snap_max_ai_force
            y_limit = 32767 * self.config.ads_snap_max_ai_force_y
            smoothing = self.config.ads_snap_smoothing
        else:
            authority_scale = max(0.0, min(1.0, authority_scale))
            desired_ai_x *= authority_scale
            desired_ai_y *= authority_scale
            x_force = self.config.body_lock_max_ai_force
            if manual_x * target_dx < 0.0:
                x_force = max(x_force, self.config.body_lock_opposing_boost_max_ai_force)
            x_limit = 32767 * x_force * authority_scale
            y_limit = 32767 * self.config.body_lock_max_ai_force_y * authority_scale
            smoothing = self.config.body_lock_smoothing

        return (
            self._clamp(desired_ai_x, x_limit),
            self._clamp(desired_ai_y, y_limit),
            smoothing,
        )

    def _ads_snap_time_to_go_stick(
        self,
        delta: float,
        *,
        axis_strength: float,
        remaining_seconds: float | None,
    ) -> float:
        if remaining_seconds is None or axis_strength <= 0.0 or delta == 0.0:
            return 0.0
        reticle_speed = max(1.0, self.config.ads_snap_reticle_speed_px_per_sec)
        min_remaining = max(
            0.001,
            self.config.ads_snap_time_to_go_min_remaining_ms / 1000.0,
        )
        effective_remaining = max(min_remaining, remaining_seconds)
        gain = max(0.0, self.config.ads_snap_time_to_go_gain)
        required_ratio = min(
            1.0,
            (abs(delta) / effective_remaining) * gain / reticle_speed,
        )
        return math.copysign(32767.0 * required_ratio * axis_strength, delta)

    @staticmethod
    def _prefer_larger_magnitude(current: float, candidate: float) -> float:
        if abs(candidate) > abs(current):
            return candidate
        return current

    def _reset_runtime_state(self) -> None:
        self.ai_stick_x = 0.0
        self.ai_stick_y = 0.0
        self._ads_active = False
        self._ads_started_at: float | None = None
        self._ads_snap_used = False
        self._ads_snap_target = None
        self._mode = "manual"
        self._auto_fire_ready_frames = 0
        self._auto_fire_ready_target = None
        self._last_auto_fire_ready = False
        self._last_auto_fire_ready_reason = ""
        self._last_auto_fire_ready_error_px: float | None = None
        self._reset_body_lock_arbitration_tracking()
        self._clear_body_lock_arbitration_debug()
        self._reset_motion_tracking()

    def _reset_body_lock_arbitration_tracking(self) -> None:
        self._body_lock_frames = 0
        self._body_lock_target = None
        self._last_body_lock_error_x = None
        self._last_body_lock_error_y = None
        self._body_lock_zero_cross_hold_x = 0
        self._body_lock_zero_cross_hold_y = 0

    def _clear_body_lock_arbitration_debug(self) -> None:
        self._last_lock_confidence = 0.0
        self._last_sanitized_manual_x = 0.0
        self._last_sanitized_manual_y = 0.0
        self._last_helpful_preserved_ratio = 1.0
        self._last_harmful_suppressed_ratio = 0.0
        self._last_orthogonal_suppressed_ratio = 0.0

    def _record_raw_manual_passthrough(self, manual_x: float, manual_y: float) -> None:
        self._last_lock_confidence = 0.0
        self._last_sanitized_manual_x = manual_x
        self._last_sanitized_manual_y = manual_y
        self._last_helpful_preserved_ratio = 1.0
        self._last_harmful_suppressed_ratio = 0.0
        self._last_orthogonal_suppressed_ratio = 0.0

    def _reset_motion_tracking(self) -> None:
        self._motion_box: tuple[float, float, float, float] | None = None
        self._motion_point: tuple[float, float] | None = None
        self._motion_timestamp: float | None = None
        self._motion_velocity_x = 0.0
        self._motion_velocity_y = 0.0
        self._motion_frames = 0

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
        )
        max_pixels = float(
            self.config.piecewise_max_pixels if max_pixels is None else max_pixels
        )
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

    @staticmethod
    def _clamp(value: float, limit: float) -> float:
        return max(-limit, min(limit, value))
