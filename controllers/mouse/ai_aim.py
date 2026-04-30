from dataclasses import dataclass

from ..base_controller import ControllerTarget
from .state import MouseFrame, MouseOutput


ACQUIRE_TARGET_SOURCES = frozenset({"observed", "reconstructed"})
STABILIZE_CONTINUITY_TARGET_SOURCES = frozenset(
    {"observed", "reconstructed", "predicted"}
)
BODY_BOX_CENTER_DELTA_RATIO = 0.35
MIN_BODY_BOX_CENTER_DELTA_PX = 24.0
FALLBACK_AIM_POINT_DISTANCE_PX = 36.0
BREAKAWAY_ALIGNMENT_THRESHOLD = -0.35


@dataclass(slots=True, frozen=True)
class _FollowProfile:
    name: str
    gain_scale: float = 1.0
    max_move_scale: float = 1.0
    horizon_scale: float = 1.0
    accel_scale: float = 1.0
    error_rate_scale: float = 1.0


@dataclass(slots=True, frozen=True)
class AIAimConfig:
    acquire_radius_px: float = 220.0
    mid_acquire_enter_px: float = 48.0
    mid_acquire_exit_px: float = 64.0
    stabilize_enter_px: float = 12.0
    stabilize_exit_px: float = 18.0
    inner_release_band_px: float = 3.0
    stabilize_reacquire_growth_px: float = 1.5
    stabilize_reacquire_motion_px: float = 1.0
    acquire_gain: float = 1.05
    mid_acquire_gain: float = 0.78
    reacquire_gain: float = 0.92
    stabilize_gain: float = 0.12
    predicted_stabilize_gain: float = 0.12
    moving_stabilize_gain: float = 0.55
    acquire_max_move_px: float = 22.0
    mid_acquire_max_move_px: float = 9.0
    reacquire_max_move_px: float = 14.0
    stabilize_max_move_px: float = 0.90
    predicted_stabilize_max_move_px: float = 0.85
    moving_stabilize_max_move_px: float = 2.0
    moving_stabilize_motion_px: float = 1.5
    moving_stabilize_motion_scale: float = 1.25
    moving_stabilize_max_dt_ms: int = 60
    moving_stabilize_axis_ratio: float = 1.75
    acquire_lead_seconds: float = 0.03
    mid_acquire_lead_seconds: float = 0.02
    reacquire_lead_seconds: float = 0.025
    acquire_lead_max_px: float = 14.0
    acquire_response_horizon_s: float = 0.014
    mid_acquire_response_horizon_s: float = 0.015
    reacquire_response_horizon_s: float = 0.013
    stabilize_response_horizon_s: float = 0.018
    predicted_stabilize_response_horizon_s: float = 0.020
    response_accel_multiplier: float = 1.85
    error_rate_lowpass_alpha: float = 0.35
    follow_control_radius_px: float = 10.0
    follow_chase_radius_px: float = 28.0
    follow_balanced_gain_scale: float = 1.06
    follow_balanced_max_move_scale: float = 1.08
    follow_balanced_horizon_scale: float = 0.92
    follow_balanced_accel_scale: float = 1.12
    follow_balanced_error_rate_scale: float = 1.12
    follow_chase_gain_scale: float = 1.14
    follow_chase_max_move_scale: float = 1.14
    follow_chase_horizon_scale: float = 0.82
    follow_chase_accel_scale: float = 1.26
    follow_chase_error_rate_scale: float = 1.26
    acquire_error_rate_gain: float = 0.14
    mid_acquire_error_rate_gain: float = 0.11
    reacquire_error_rate_gain: float = 0.13
    stabilize_error_rate_gain: float = 0.08
    predicted_stabilize_error_rate_gain: float = 0.06
    stabilize_integral_gain: float = 1.4
    predicted_stabilize_integral_gain: float = 1.0
    stabilize_integral_limit_px: float = 6.0
    same_target_grace_ms: int = 200
    reacquire_radius_px: float = 96.0
    reacquire_window_ms: int = 90
    chase_hold_projection_px_per_sec: float = 120.0
    chase_hold_speed_px_per_sec: float = 220.0
    chase_hold_min_radius_px: float = 24.0
    switch_guard_ms: int = 60
    switch_guard_commit_radius_px: float = 24.0
    acquire_stall_min_shrink_px: float = 1.0
    acquire_stall_trigger_frames: int = 2
    acquire_stall_gain_per_frame: float = 0.18
    acquire_stall_decay_per_frame: float = 0.20
    acquire_stall_max_bonus: float = 0.75
    breakaway_speed_px: float = 18.0


class AIAimPlugin:
    def __init__(self, config: AIAimConfig | None = None):
        self.config = config or AIAimConfig()
        self.reset()

    def reset(self) -> None:
        self._mode = "manual"
        self._stabilize_until: float | None = None
        self._continuity_until: float | None = None
        self._reacquire_armed_until: float | None = None
        self._reacquire_until: float | None = None
        self._last_target: ControllerTarget | None = None
        self._last_continuity_target: ControllerTarget | None = None
        self._last_stabilize_radius: float | None = None
        self._last_seen_target: ControllerTarget | None = None
        self._last_seen_timestamp: float | None = None
        self._last_acquire_radius: float | None = None
        self._last_continuity_radius: float | None = None
        self._acquire_stall_frames = 0
        self._acquire_bonus = 0.0
        self._pending_switch_target: ControllerTarget | None = None
        self._pending_switch_since: float | None = None
        self._recent_target_gap_until: float | None = None
        self._last_applied_target_revision: int | None = None
        self._last_applied_target_timestamp: float | None = None
        self._desired_velocity_x = 0.0
        self._desired_velocity_y = 0.0
        self._current_velocity_x = 0.0
        self._current_velocity_y = 0.0
        self._last_apply_timestamp: float | None = None
        self._last_velocity_mode = "manual"
        self._last_follow_profile_name = "control"
        self._last_response_horizon_seconds_value: float | None = None
        self._last_response_accel_scale = 1.0
        self._last_observed_error_x: float | None = None
        self._last_observed_error_y: float | None = None
        self._filtered_error_rate_x = 0.0
        self._filtered_error_rate_y = 0.0
        self._integral_error_x = 0.0
        self._integral_error_y = 0.0

    def apply(self, frame: MouseFrame, output: MouseOutput) -> None:
        if not frame.is_aiming:
            self.reset()
            return
        if frame.manual_override_active:
            self._mode = "manual"
            self._reset_acquire_bonus()
            self._reset_stabilize_integral()
            self._set_desired_velocity(0.0, 0.0, mode="manual")
            self._emit_smooth_move(frame, output, force_stop=True)
            return

        self._expire_stabilize_context(frame.timestamp)
        is_new_observation = not self._is_repeated_observation(frame)
        if frame.target is None:
            if is_new_observation and self._has_continuity_context(frame):
                self._reacquire_armed_until = self._continuity_until
                self._recent_target_gap_until = self._continuity_until
            self._mode = "manual"
            self._last_stabilize_radius = None
            self._reset_acquire_bonus()
            self._reset_stabilize_integral()
            self._set_desired_velocity(0.0, 0.0, mode="manual")
            self._emit_smooth_move(frame, output)
            if is_new_observation:
                self._remember_applied_observation(frame)
            return
        if self._should_release(frame):
            self.reset()
            return
        if self._should_guard_target_switch(frame):
            self._mode = "manual"
            self._reset_acquire_bonus()
            self._reset_stabilize_integral()
            self._set_desired_velocity(0.0, 0.0, mode="manual")
            self._emit_smooth_move(frame, output, force_stop=True)
            if is_new_observation:
                self._remember_applied_observation(frame)
            return

        self._mode = self._choose_mode(frame)
        if is_new_observation or self._mode != self._last_velocity_mode:
            if is_new_observation:
                self._update_observed_error_rate(frame)
            self._update_desired_velocity(frame, self._mode)
            self._remember_target(frame)
            self._remember_seen_target(frame)
            if is_new_observation:
                self._remember_applied_observation(frame)

        self._emit_smooth_move(frame, output)

    def _expire_stabilize_context(self, timestamp: float) -> None:
        if self._stabilize_until is None or timestamp <= self._stabilize_until:
            pass
        else:
            self._stabilize_until = None
            self._last_target = None

        if self._continuity_until is None or timestamp <= self._continuity_until:
            return
        self._continuity_until = None
        self._reacquire_armed_until = None
        self._reacquire_until = None
        self._last_continuity_target = None
        self._last_continuity_radius = None
        self._clear_pending_switch()

    def _choose_mode(self, frame: MouseFrame) -> str:
        if self._can_stabilize(frame):
            return "stabilize"
        if self._should_soft_switch_acquire(frame):
            return "acquire_mid"
        if self._can_reacquire(frame):
            return "reacquire"
        if self._should_hold_far_acquire(frame):
            return "acquire_far"
        if self._can_mid_acquire(frame):
            return "acquire_mid"
        if self._can_acquire(frame):
            return "acquire_far"
        return "manual"

    def _has_stabilize_context(self, frame: MouseFrame) -> bool:
        return (
            self._last_target is not None
            and self._stabilize_until is not None
            and frame.timestamp <= self._stabilize_until
        )

    def _has_continuity_context(self, frame: MouseFrame) -> bool:
        return (
            self._last_continuity_target is not None
            and self._continuity_until is not None
            and frame.timestamp <= self._continuity_until
        )

    def _stabilize_radius_limit(self, frame: MouseFrame) -> float:
        if self._mode == "stabilize":
            return self.config.stabilize_exit_px
        return self.config.stabilize_enter_px

    def _mid_acquire_radius_limit(self) -> float:
        if self._mode in {"acquire_mid", "reacquire"}:
            return self.config.mid_acquire_exit_px
        return self.config.mid_acquire_enter_px

    def _can_stabilize(self, frame: MouseFrame) -> bool:
        if frame.target is None:
            return False
        radius = self._target_radius(frame)
        if radius > self._stabilize_radius_limit(frame):
            return False
        if self._should_hold_far_acquire(frame):
            return False
        if self._should_reacquire_from_stabilize(frame, radius):
            return False

        source = frame.target.target_source
        if source in ACQUIRE_TARGET_SOURCES:
            if not self._has_stabilize_context(frame) or self._last_target is None:
                return True
            return self._same_target_family(frame.target, self._last_target)

        return (
            source == "predicted"
            and self._has_stabilize_context(frame)
            and self._last_target is not None
            and self._same_target_family(frame.target, self._last_target)
        )

    def _can_acquire(self, frame: MouseFrame) -> bool:
        return (
            frame.target is not None
            and frame.target.target_source in ACQUIRE_TARGET_SOURCES
            and self._target_radius(frame) <= self.config.acquire_radius_px
        )

    def _can_mid_acquire(self, frame: MouseFrame) -> bool:
        return (
            frame.target is not None
            and frame.target.target_source in ACQUIRE_TARGET_SOURCES
            and self._target_radius(frame) <= self._mid_acquire_radius_limit()
        )

    def _can_reacquire(self, frame: MouseFrame) -> bool:
        if (
            frame.target is None
            or frame.target.target_source not in ACQUIRE_TARGET_SOURCES
            or not self._has_continuity_context(frame)
            or self._last_continuity_target is None
            or not self._same_target_family(frame.target, self._last_continuity_target)
            or self._target_radius(frame) > self.config.reacquire_radius_px
        ):
            return False

        if (
            self._reacquire_until is not None
            and frame.timestamp <= self._reacquire_until
        ):
            return True

        if (
            self._reacquire_armed_until is None
            or frame.timestamp > self._reacquire_armed_until
        ):
            return False

        self._reacquire_until = (
            frame.timestamp + self.config.reacquire_window_ms / 1000.0
        )
        self._reacquire_armed_until = None
        return True

    def _should_hold_far_acquire(self, frame: MouseFrame) -> bool:
        if (
            frame.target is None
            or frame.target.target_source not in ACQUIRE_TARGET_SOURCES
            or self._target_radius(frame) < self.config.chase_hold_min_radius_px
        ):
            return False

        outward_speed = self._outward_target_motion_px_per_sec(frame)
        if outward_speed >= self.config.chase_hold_projection_px_per_sec:
            return True

        motion_speed = self._same_target_motion_speed_px_per_sec(frame)
        return motion_speed >= self.config.chase_hold_speed_px_per_sec

    def _compute_move(
        self,
        frame: MouseFrame,
        mode: str,
        profile: _FollowProfile,
    ) -> tuple[float, float]:
        if mode == "manual":
            return 0.0, 0.0

        if mode == "stabilize":
            if frame.target is not None and frame.target.target_source == "predicted":
                gain = self.config.predicted_stabilize_gain
                max_move = self.config.predicted_stabilize_max_move_px
            else:
                gain = self.config.stabilize_gain
                max_move = self.config.stabilize_max_move_px
            gain, max_move = self._scale_stabilize_strength(
                frame,
                gain=gain,
                max_move=max_move,
            )
        elif mode == "reacquire":
            gain = self.config.reacquire_gain
            max_move = self.config.reacquire_max_move_px
        elif mode == "acquire_mid":
            gain = self.config.mid_acquire_gain
            max_move = self.config.mid_acquire_max_move_px
        else:
            gain = self.config.acquire_gain
            max_move = self.config.acquire_max_move_px

        if mode in {"acquire_far", "acquire_mid"}:
            bonus = self._update_acquire_bonus(frame)
            gain *= 1.0 + bonus
            max_move *= 1.0 + bonus
        elif mode == "reacquire":
            self._reset_acquire_bonus()
        else:
            self._reset_acquire_bonus()

        gain *= profile.gain_scale
        max_move *= profile.max_move_scale
        lead_dx, lead_dy = self._motion_lead(frame, mode)
        move_dx = (frame.target_dx + lead_dx) * gain
        move_dy = (frame.target_dy + lead_dy) * gain
        return self._clamp_vector(move_dx, move_dy, max_move)

    def _should_reacquire_from_stabilize(
        self, frame: MouseFrame, radius: float
    ) -> bool:
        if (
            self._mode != "stabilize"
            or frame.target is None
            or frame.target.target_source not in ACQUIRE_TARGET_SOURCES
            or self._last_target is None
            or self._last_stabilize_radius is None
        ):
            return False
        if not self._same_target_family(frame.target, self._last_target):
            return False

        radius_growth = radius - self._last_stabilize_radius
        if radius_growth <= self.config.stabilize_reacquire_growth_px:
            return False

        target_motion = self._aim_point_distance(frame.target, self._last_target)
        return target_motion >= self.config.stabilize_reacquire_motion_px

    def _should_release(self, frame: MouseFrame) -> bool:
        manual_speed = self._manual_speed(frame)
        if manual_speed < self.config.breakaway_speed_px:
            return False

        target_radius = self._target_radius(frame)
        if target_radius <= 0.0:
            return True

        alignment = (
            frame.manual_dx * frame.target_dx + frame.manual_dy * frame.target_dy
        ) / (manual_speed * target_radius)
        return alignment <= BREAKAWAY_ALIGNMENT_THRESHOLD

    def _should_guard_target_switch(self, frame: MouseFrame) -> bool:
        if (
            frame.target is None
            or self.config.switch_guard_ms <= 0
            or (
                self._recent_target_gap_until is not None
                and frame.timestamp <= self._recent_target_gap_until
            )
            or not self._has_continuity_context(frame)
            or self._last_continuity_target is None
            or self._last_continuity_radius is None
            or self._last_continuity_radius > self.config.switch_guard_commit_radius_px
            or self._same_target_family(frame.target, self._last_continuity_target)
        ):
            self._clear_pending_switch()
            return False

        if (
            self._pending_switch_target is None
            or not self._same_target_family(frame.target, self._pending_switch_target)
            or self._pending_switch_since is None
        ):
            self._pending_switch_target = frame.target
            self._pending_switch_since = frame.timestamp
            return True

        if (
            frame.timestamp - self._pending_switch_since
            < self.config.switch_guard_ms / 1000.0
        ):
            return True

        return False

    def _should_soft_switch_acquire(self, frame: MouseFrame) -> bool:
        if (
            frame.target is None
            or not self._has_continuity_context(frame)
            or self._last_continuity_target is None
            or self._same_target_family(frame.target, self._last_continuity_target)
            or self._recent_target_gap_until is not None
            and frame.timestamp <= self._recent_target_gap_until
            or self._pending_switch_target is None
            or self._pending_switch_since is None
            or not self._same_target_family(frame.target, self._pending_switch_target)
        ):
            return False

        return (
            frame.timestamp - self._pending_switch_since
            >= self.config.switch_guard_ms / 1000.0
        )

    def _remember_target(self, frame: MouseFrame) -> None:
        observation_timestamp = self._observation_timestamp(frame)
        if (
            self._mode in {"acquire_far", "acquire_mid", "stabilize", "reacquire"}
            and frame.target is not None
            and frame.target.target_source in STABILIZE_CONTINUITY_TARGET_SOURCES
        ):
            self._last_continuity_target = frame.target
            self._last_continuity_radius = self._target_radius(frame)
            self._continuity_until = (
                observation_timestamp + self.config.same_target_grace_ms / 1000.0
            )
            self._clear_pending_switch()
            self._recent_target_gap_until = None

        if (
            self._mode in {"stabilize", "reacquire"}
            and frame.target is not None
            and frame.target.target_source in STABILIZE_CONTINUITY_TARGET_SOURCES
        ):
            self._last_target = frame.target
            self._stabilize_until = (
                observation_timestamp + self.config.same_target_grace_ms / 1000.0
            )
            self._last_stabilize_radius = self._target_radius(frame)
            return

        self._last_stabilize_radius = None

    def _clear_pending_switch(self) -> None:
        self._pending_switch_target = None
        self._pending_switch_since = None

    def _is_repeated_observation(self, frame: MouseFrame) -> bool:
        return (
            frame.target_timestamp is not None
            and frame.target_revision == self._last_applied_target_revision
            and frame.target_timestamp == self._last_applied_target_timestamp
        )

    def _remember_applied_observation(self, frame: MouseFrame) -> None:
        if frame.target_timestamp is None:
            return
        self._last_applied_target_revision = frame.target_revision
        self._last_applied_target_timestamp = frame.target_timestamp

    def _remember_seen_target(self, frame: MouseFrame) -> None:
        if frame.target is None:
            return
        self._last_seen_target = frame.target
        self._last_seen_timestamp = self._observation_timestamp(frame)

    def _update_observed_error_rate(self, frame: MouseFrame) -> None:
        observation_timestamp = self._observation_timestamp(frame)
        if (
            self._last_observed_error_x is None
            or self._last_observed_error_y is None
            or self._last_applied_target_timestamp is None
        ):
            self._last_observed_error_x = frame.target_dx
            self._last_observed_error_y = frame.target_dy
            self._filtered_error_rate_x = 0.0
            self._filtered_error_rate_y = 0.0
            return

        dt = observation_timestamp - self._last_applied_target_timestamp
        if dt <= 0.0:
            return

        raw_rate_x = (frame.target_dx - self._last_observed_error_x) / dt
        raw_rate_y = (frame.target_dy - self._last_observed_error_y) / dt
        alpha = min(1.0, max(0.0, self.config.error_rate_lowpass_alpha))
        self._filtered_error_rate_x = (
            self._filtered_error_rate_x * alpha
            + raw_rate_x * (1.0 - alpha)
        )
        self._filtered_error_rate_y = (
            self._filtered_error_rate_y * alpha
            + raw_rate_y * (1.0 - alpha)
        )
        self._last_observed_error_x = frame.target_dx
        self._last_observed_error_y = frame.target_dy

    def _update_desired_velocity(self, frame: MouseFrame, mode: str) -> None:
        profile = self._follow_profile(frame, mode)
        move_dx, move_dy = self._compute_move(frame, mode, profile)
        horizon_seconds = (
            self._response_horizon_seconds(frame, mode) * profile.horizon_scale
        )
        if horizon_seconds <= 0.0:
            self._set_desired_velocity(0.0, 0.0, mode=mode)
            return

        desired_velocity_x = move_dx / horizon_seconds
        desired_velocity_y = move_dy / horizon_seconds
        desired_velocity_x += (
            self._filtered_error_rate_x
            * self._error_rate_gain(frame, mode)
            * profile.error_rate_scale
        )
        desired_velocity_y += (
            self._filtered_error_rate_y
            * self._error_rate_gain(frame, mode)
            * profile.error_rate_scale
        )

        if mode == "stabilize":
            self._integrate_stabilize_error(frame)
            desired_velocity_x += self._integral_error_x * self.config.stabilize_integral_gain
            desired_velocity_y += self._integral_error_y * self.config.stabilize_integral_gain
        elif (
            mode == "manual"
            or frame.target is None
            or frame.target.target_source != "predicted"
        ):
            self._reset_stabilize_integral()

        if mode == "stabilize" and frame.target is not None and frame.target.target_source == "predicted":
            desired_velocity_x += self._integral_error_x * (
                self.config.predicted_stabilize_integral_gain
                - self.config.stabilize_integral_gain
            )
            desired_velocity_y += self._integral_error_y * (
                self.config.predicted_stabilize_integral_gain
                - self.config.stabilize_integral_gain
            )
        elif mode != "stabilize":
            self._reset_stabilize_integral()

        self._set_desired_velocity(
            desired_velocity_x,
            desired_velocity_y,
            mode=mode,
            horizon_seconds=horizon_seconds,
            accel_scale=profile.accel_scale,
            follow_profile_name=profile.name,
        )

    def _set_desired_velocity(
        self,
        velocity_x: float,
        velocity_y: float,
        *,
        mode: str,
        horizon_seconds: float | None = None,
        accel_scale: float = 1.0,
        follow_profile_name: str = "control",
    ) -> None:
        self._desired_velocity_x = velocity_x
        self._desired_velocity_y = velocity_y
        self._last_velocity_mode = mode
        self._last_follow_profile_name = follow_profile_name
        self._last_response_horizon_seconds_value = (
            horizon_seconds if horizon_seconds is not None and horizon_seconds > 0.0 else None
        )
        self._last_response_accel_scale = accel_scale

    def _emit_smooth_move(
        self,
        frame: MouseFrame,
        output: MouseOutput,
        *,
        force_stop: bool = False,
    ) -> None:
        dt = self._controller_dt(frame.timestamp)
        if dt <= 0.0:
            return

        if force_stop:
            self._desired_velocity_x = 0.0
            self._desired_velocity_y = 0.0
            self._current_velocity_x = 0.0
            self._current_velocity_y = 0.0
            return

        remaining_dt = dt
        while remaining_dt > 0.0:
            step_dt = min(0.001, remaining_dt)
            current_vx = self._current_velocity_x
            current_vy = self._current_velocity_y
            next_vx, next_vy = self._step_velocity_towards_target(
                current_vx,
                current_vy,
                self._desired_velocity_x,
                self._desired_velocity_y,
                dt=step_dt,
            )
            output.move_dx += (current_vx + next_vx) * 0.5 * step_dt
            output.move_dy += (current_vy + next_vy) * 0.5 * step_dt
            self._current_velocity_x = next_vx
            self._current_velocity_y = next_vy
            remaining_dt -= step_dt

    def _controller_dt(self, timestamp: float) -> float:
        if self._last_apply_timestamp is None:
            self._last_apply_timestamp = timestamp
            return 1.0 / 140.0
        dt = timestamp - self._last_apply_timestamp
        self._last_apply_timestamp = timestamp
        if dt <= 0.0:
            return 0.0
        return min(dt, 0.05)

    def _step_velocity_towards_target(
        self,
        current_vx: float,
        current_vy: float,
        target_vx: float,
        target_vy: float,
        *,
        dt: float,
    ) -> tuple[float, float]:
        delta_vx = target_vx - current_vx
        delta_vy = target_vy - current_vy
        max_delta = self._current_accel_limit() * dt
        return self._clamp_delta_vector(
            current_vx,
            current_vy,
            delta_vx,
            delta_vy,
            max_delta=max_delta,
        )

    def _current_accel_limit(self) -> float:
        horizon_seconds = self._last_response_horizon_seconds()
        desired_speed = (
            self._desired_velocity_x ** 2 + self._desired_velocity_y ** 2
        ) ** 0.5
        current_speed = (
            self._current_velocity_x ** 2 + self._current_velocity_y ** 2
        ) ** 0.5
        base_speed = max(desired_speed, current_speed)
        if base_speed <= 0.0 or horizon_seconds <= 0.0:
            return 0.0
        return (
            base_speed
            * self.config.response_accel_multiplier
            * self._last_response_accel_scale
            / horizon_seconds
        )

    def _last_response_horizon_seconds(self) -> float:
        if self._last_response_horizon_seconds_value is not None:
            return self._last_response_horizon_seconds_value
        mode = self._last_velocity_mode
        if mode == "acquire_mid":
            return self.config.mid_acquire_response_horizon_s
        if mode == "reacquire":
            return self.config.reacquire_response_horizon_s
        if mode == "stabilize":
            return self.config.stabilize_response_horizon_s
        if mode == "manual":
            return self.config.stabilize_response_horizon_s
        return self.config.acquire_response_horizon_s

    def _follow_profile(self, frame: MouseFrame, mode: str) -> _FollowProfile:
        if mode not in {"stabilize", "acquire_mid", "reacquire"}:
            return _FollowProfile(name="control")

        radius = self._target_radius(frame)
        control_radius = max(0.0, self.config.follow_control_radius_px)
        chase_radius = max(control_radius, self.config.follow_chase_radius_px)

        if self._is_confirmed_soft_switch_follow(frame, mode):
            radius = min(radius, chase_radius)

        if radius <= control_radius:
            return _FollowProfile(name="control")
        if radius <= chase_radius:
            return _FollowProfile(
                name="balanced",
                gain_scale=self.config.follow_balanced_gain_scale,
                max_move_scale=self.config.follow_balanced_max_move_scale,
                horizon_scale=self.config.follow_balanced_horizon_scale,
                accel_scale=self.config.follow_balanced_accel_scale,
                error_rate_scale=self.config.follow_balanced_error_rate_scale,
            )
        return _FollowProfile(
            name="chase",
            gain_scale=self.config.follow_chase_gain_scale,
            max_move_scale=self.config.follow_chase_max_move_scale,
            horizon_scale=self.config.follow_chase_horizon_scale,
            accel_scale=self.config.follow_chase_accel_scale,
            error_rate_scale=self.config.follow_chase_error_rate_scale,
        )

    def _is_confirmed_soft_switch_follow(self, frame: MouseFrame, mode: str) -> bool:
        return (
            mode == "acquire_mid"
            and frame.target is not None
            and self._last_continuity_target is not None
            and self._pending_switch_target is not None
            and self._pending_switch_since is not None
            and not self._same_target_family(frame.target, self._last_continuity_target)
            and self._same_target_family(frame.target, self._pending_switch_target)
        )

    def _response_horizon_seconds(self, frame: MouseFrame, mode: str) -> float:
        if mode == "acquire_mid":
            return self.config.mid_acquire_response_horizon_s
        if mode == "reacquire":
            return self.config.reacquire_response_horizon_s
        if mode == "stabilize":
            if frame.target is not None and frame.target.target_source == "predicted":
                return self.config.predicted_stabilize_response_horizon_s
            return self.config.stabilize_response_horizon_s
        if mode == "manual":
            return 0.0
        return self.config.acquire_response_horizon_s

    def _error_rate_gain(self, frame: MouseFrame, mode: str) -> float:
        if mode == "acquire_mid":
            return self.config.mid_acquire_error_rate_gain
        if mode == "reacquire":
            return self.config.reacquire_error_rate_gain
        if mode == "stabilize":
            if frame.target is not None and frame.target.target_source == "predicted":
                return self.config.predicted_stabilize_error_rate_gain
            return self.config.stabilize_error_rate_gain
        if mode == "manual":
            return 0.0
        return self.config.acquire_error_rate_gain

    def _integrate_stabilize_error(self, frame: MouseFrame) -> None:
        if frame.target is None:
            self._reset_stabilize_integral()
            return
        dt = self._controller_dt_for_integral(frame)
        if dt <= 0.0:
            return
        limit = max(0.0, self.config.stabilize_integral_limit_px)
        self._integral_error_x = self._clamp_scalar(
            self._integral_error_x + frame.target_dx * dt,
            limit,
        )
        self._integral_error_y = self._clamp_scalar(
            self._integral_error_y + frame.target_dy * dt,
            limit,
        )

    def _controller_dt_for_integral(self, frame: MouseFrame) -> float:
        if self._last_apply_timestamp is None:
            return 1.0 / 140.0
        dt = frame.timestamp - self._last_apply_timestamp
        if dt <= 0.0:
            return 0.0
        return min(dt, 0.05)

    def _reset_stabilize_integral(self) -> None:
        self._integral_error_x = 0.0
        self._integral_error_y = 0.0

    def _observation_timestamp(self, frame: MouseFrame) -> float:
        if frame.target_timestamp is not None:
            return frame.target_timestamp
        return frame.timestamp

    def _motion_lead(self, frame: MouseFrame, mode: str) -> tuple[float, float]:
        if (
            frame.target is None
            or mode not in {"acquire_far", "acquire_mid", "reacquire"}
            or frame.target.target_source not in ACQUIRE_TARGET_SOURCES
            or self._last_seen_target is None
            or self._last_seen_timestamp is None
            or not self._same_target_family(frame.target, self._last_seen_target)
        ):
            return 0.0, 0.0

        dt = self._observation_timestamp(frame) - self._last_seen_timestamp
        if dt <= 0.0:
            return 0.0, 0.0

        if mode == "acquire_mid":
            lead_seconds = self.config.mid_acquire_lead_seconds
        elif mode == "reacquire":
            lead_seconds = self.config.reacquire_lead_seconds
        else:
            lead_seconds = self.config.acquire_lead_seconds

        if lead_seconds <= 0.0:
            return 0.0, 0.0

        motion_dx = frame.target.aim_point_x - self._last_seen_target.aim_point_x
        motion_dy = frame.target.aim_point_y - self._last_seen_target.aim_point_y
        lead_dx = motion_dx * lead_seconds / dt
        lead_dy = motion_dy * lead_seconds / dt
        return self._clamp_vector(
            lead_dx,
            lead_dy,
            self.config.acquire_lead_max_px,
        )

    def _outward_target_motion_px_per_sec(self, frame: MouseFrame) -> float:
        if (
            frame.target is None
            or self._last_seen_target is None
            or self._last_seen_timestamp is None
            or not self._same_target_family(frame.target, self._last_seen_target)
        ):
            return 0.0

        dt = self._observation_timestamp(frame) - self._last_seen_timestamp
        radius = self._target_radius(frame)
        if dt <= 0.0 or radius <= 0.0:
            return 0.0

        motion_dx = frame.target.aim_point_x - self._last_seen_target.aim_point_x
        motion_dy = frame.target.aim_point_y - self._last_seen_target.aim_point_y
        outward_projection_px = (
            motion_dx * frame.target_dx + motion_dy * frame.target_dy
        ) / radius
        return max(0.0, outward_projection_px / dt)

    def _update_acquire_bonus(self, frame: MouseFrame) -> float:
        radius = self._target_radius(frame)
        if (
            frame.target is None
            or self._last_acquire_radius is None
            or self._last_seen_target is None
            or not self._same_target_family(frame.target, self._last_seen_target)
        ):
            self._acquire_stall_frames = 0
            self._acquire_bonus = 0.0
            self._last_acquire_radius = radius
            return self._acquire_bonus

        shrink = self._last_acquire_radius - radius
        if shrink < self.config.acquire_stall_min_shrink_px:
            self._acquire_stall_frames += 1
            if self._acquire_stall_frames >= self.config.acquire_stall_trigger_frames:
                self._acquire_bonus = min(
                    self.config.acquire_stall_max_bonus,
                    self._acquire_bonus + self.config.acquire_stall_gain_per_frame,
                )
        else:
            self._acquire_stall_frames = 0
            self._acquire_bonus = max(
                0.0,
                self._acquire_bonus - self.config.acquire_stall_decay_per_frame,
            )

        self._last_acquire_radius = radius
        return self._acquire_bonus

    def _reset_acquire_bonus(self) -> None:
        self._last_acquire_radius = None
        self._acquire_stall_frames = 0
        self._acquire_bonus = 0.0

    def _scale_stabilize_strength(
        self,
        frame: MouseFrame,
        *,
        gain: float,
        max_move: float,
    ) -> tuple[float, float]:
        if self.config.inner_release_band_px <= 0.0:
            return self._boost_stabilize_for_motion(
                frame,
                gain=gain,
                max_move=max_move,
                attenuation=1.0,
            )

        radius = self._target_radius(frame)
        if radius >= self.config.inner_release_band_px:
            return self._boost_stabilize_for_motion(
                frame,
                gain=gain,
                max_move=max_move,
                attenuation=1.0,
            )

        attenuation = max(0.25, radius / self.config.inner_release_band_px)
        return self._boost_stabilize_for_motion(
            frame,
            gain=gain * attenuation,
            max_move=max_move * attenuation,
            attenuation=attenuation,
        )

    def _boost_stabilize_for_motion(
        self,
        frame: MouseFrame,
        *,
        gain: float,
        max_move: float,
        attenuation: float,
    ) -> tuple[float, float]:
        motion = self._same_target_motion(frame)
        if motion is None:
            return gain, max_move
        motion_dx, motion_dy, _dt = motion
        abs_dx = abs(motion_dx)
        abs_dy = abs(motion_dy)
        dominant_axis = max(abs_dx, abs_dy)
        minor_axis = min(abs_dx, abs_dy)
        motion_px = (motion_dx ** 2 + motion_dy ** 2) ** 0.5
        if motion_px < self.config.moving_stabilize_motion_px:
            return gain, max_move
        if (
            minor_axis > 0.0
            and dominant_axis < minor_axis * self.config.moving_stabilize_axis_ratio
        ):
            return gain, max_move

        boosted_max_move = min(
            self.config.moving_stabilize_max_move_px,
            motion_px * self.config.moving_stabilize_motion_scale,
        )
        return (
            max(gain, self.config.moving_stabilize_gain * attenuation),
            max(max_move, boosted_max_move * attenuation),
        )

    def _same_target_family(
        self, current: ControllerTarget, previous: ControllerTarget
    ) -> bool:
        current_box = current.body_box
        previous_box = previous.body_box
        if current_box is not None and previous_box is not None:
            center_dx, center_dy = self._body_box_center_delta(current_box, previous_box)
            max_dx, max_dy = self._body_box_center_delta_thresholds(
                current_box, previous_box
            )
            return center_dx <= max_dx and center_dy <= max_dy

        return (
            self._aim_point_distance(current, previous) <= FALLBACK_AIM_POINT_DISTANCE_PX
        )

    @staticmethod
    def _target_radius(frame: MouseFrame) -> float:
        return (frame.target_dx ** 2 + frame.target_dy ** 2) ** 0.5

    @staticmethod
    def _manual_speed(frame: MouseFrame) -> float:
        return (frame.manual_dx ** 2 + frame.manual_dy ** 2) ** 0.5

    @staticmethod
    def _aim_point_distance(
        current: ControllerTarget, previous: ControllerTarget
    ) -> float:
        dx = current.aim_point_x - previous.aim_point_x
        dy = current.aim_point_y - previous.aim_point_y
        return (dx ** 2 + dy ** 2) ** 0.5

    def _same_target_motion_px(self, frame: MouseFrame) -> float:
        motion = self._same_target_motion(frame)
        if motion is None:
            return 0.0
        motion_dx, motion_dy, _dt = motion
        return (motion_dx ** 2 + motion_dy ** 2) ** 0.5

    def _same_target_motion_speed_px_per_sec(self, frame: MouseFrame) -> float:
        if (
            frame.target is None
            or self._last_seen_target is None
            or self._last_seen_timestamp is None
            or not self._same_target_family(frame.target, self._last_seen_target)
        ):
            return 0.0

        dt = self._observation_timestamp(frame) - self._last_seen_timestamp
        if dt <= 0.0:
            return 0.0
        return self._aim_point_distance(frame.target, self._last_seen_target) / dt

    def _same_target_motion(
        self, frame: MouseFrame
    ) -> tuple[float, float, float] | None:
        if (
            frame.target is None
            or self._last_seen_target is None
            or self._last_seen_timestamp is None
            or not self._same_target_family(frame.target, self._last_seen_target)
        ):
            return None

        dt = self._observation_timestamp(frame) - self._last_seen_timestamp
        if dt <= 0.0 or dt * 1000.0 > self.config.moving_stabilize_max_dt_ms:
            return None

        motion_dx = frame.target.aim_point_x - self._last_seen_target.aim_point_x
        motion_dy = frame.target.aim_point_y - self._last_seen_target.aim_point_y
        return motion_dx, motion_dy, dt

    @staticmethod
    def _body_box_center_delta(
        first: tuple[float, float, float, float],
        second: tuple[float, float, float, float],
    ) -> tuple[float, float]:
        first_center_x = (first[0] + first[2]) * 0.5
        first_center_y = (first[1] + first[3]) * 0.5
        second_center_x = (second[0] + second[2]) * 0.5
        second_center_y = (second[1] + second[3]) * 0.5
        return abs(first_center_x - second_center_x), abs(
            first_center_y - second_center_y
        )

    @staticmethod
    def _body_box_center_delta_thresholds(
        first: tuple[float, float, float, float],
        second: tuple[float, float, float, float],
    ) -> tuple[float, float]:
        first_width = max(0.0, first[2] - first[0])
        first_height = max(0.0, first[3] - first[1])
        second_width = max(0.0, second[2] - second[0])
        second_height = max(0.0, second[3] - second[1])
        average_width = (first_width + second_width) * 0.5
        average_height = (first_height + second_height) * 0.5
        return (
            max(
                MIN_BODY_BOX_CENTER_DELTA_PX,
                average_width * BODY_BOX_CENTER_DELTA_RATIO,
            ),
            max(
                MIN_BODY_BOX_CENTER_DELTA_PX,
                average_height * BODY_BOX_CENTER_DELTA_RATIO,
            ),
        )

    @staticmethod
    def _clamp_vector(dx: float, dy: float, max_magnitude: float) -> tuple[float, float]:
        magnitude = (dx ** 2 + dy ** 2) ** 0.5
        if max_magnitude <= 0.0 or magnitude <= max_magnitude:
            return dx, dy
        scale = max_magnitude / magnitude
        return dx * scale, dy * scale

    @staticmethod
    def _clamp_delta_vector(
        current_vx: float,
        current_vy: float,
        delta_vx: float,
        delta_vy: float,
        *,
        max_delta: float,
    ) -> tuple[float, float]:
        if max_delta <= 0.0:
            return current_vx, current_vy
        clamped_delta_x, clamped_delta_y = AIAimPlugin._clamp_vector(
            delta_vx,
            delta_vy,
            max_delta,
        )
        return current_vx + clamped_delta_x, current_vy + clamped_delta_y

    @staticmethod
    def _clamp_scalar(value: float, limit: float) -> float:
        if limit <= 0.0:
            return 0.0
        return max(-limit, min(limit, value))
