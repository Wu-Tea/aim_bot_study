import tomllib
from dataclasses import dataclass, replace
from pathlib import Path
from typing import Any, Mapping

from controllers.gamepad.adaptive_delta_gain import AdaptiveDeltaGainConfig
from controllers.gamepad.ai_aim import AIAimConfig as GamepadAIAimConfig
from controllers.gamepad.aim_assist_dynamics import AimAssistDynamicsConfig
from controllers.gamepad.auto_fire import AutoFireConfig as GamepadAutoFireConfig
from controllers.gamepad.recoil_compensation import RecoilCompensationConfig as GamepadRecoilConfig
from controllers.mouse.ai_aim import AIAimConfig as MouseAIAimConfig
from controllers.mouse.auto_fire import AutoFireConfig as MouseAutoFireConfig
from controllers.mouse.recoil_compensation import RecoilCompensationConfig as MouseRecoilConfig


PROJECT_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_CONFIG_PATH = PROJECT_ROOT / "config.toml"


RUNTIME_VISION_KEYS = frozenset(
    {
        "backend",
        "capture_fps",
        "crop_width",
        "crop_height",
        "perf_log",
        "quit_key",
        "native_cue_sidecar",
        "model_path",
        "fallback_model_path",
    }
)
RUNTIME_GAMEPAD_KEYS = frozenset(
    {
        "auto_fire_output",
        "rb_counts_as_aiming",
    }
)
GAMEPAD_AUTO_FIRE_KEYS = frozenset(
    {
        "aim_only",
        "max_source_age_ms",
        "require_aim_ready",
        "manual_takeover_release_seconds",
        "manual_takeover_resume_delay_seconds",
    }
)
GAMEPAD_RECOIL_KEYS = frozenset(
    {
        "profile_amount",
        "profile_x_amount",
        "feedback_amount",
        "profile_lead_ms",
        "profile_velocity_reference_ms",
        "profile_despike_enabled",
        "profile_despike_threshold_px",
        "profile_despike_ratio",
        "target_direction_yield_enabled",
        "selection_log_enabled",
        "piecewise_mid_pixels_y",
        "piecewise_max_pixels_y",
        "piecewise_mid_ratio_y",
    }
)
GAMEPAD_AIM_ASSIST_DYNAMICS_KEYS = frozenset(
    {
        "enabled",
        "recoil_jitter_guard_enabled",
        "recoil_jitter_assist_threshold",
        "recoil_jitter_flip_scale",
        "recoil_jitter_memory_seconds",
    }
)
GAMEPAD_AI_AIM_KEYS = frozenset(
    {
        "smoothing",
        "max_pixels",
        "max_ai_force",
        "max_ai_force_y",
        "ai_delta_gain",
        "target_max_age_ms",
        "target_projection_reticle_speed_px_per_sec",
        "target_projection_velocity_lowpass_alpha",
        "target_projection_max_velocity_px_per_sec",
        "target_projection_weak_velocity_decay",
        "piecewise_mid_pixels_y",
        "piecewise_max_pixels_y",
        "piecewise_mid_ratio_y",
        "ads_snap_window_ms",
        "ads_snap_smoothing",
        "ads_snap_max_ai_force",
        "ads_snap_max_ai_force_y",
        "ads_snap_max_target_dy_px",
        "ads_snap_reticle_speed_px_per_sec",
        "ads_snap_time_to_go_gain",
        "ads_snap_time_to_go_min_remaining_ms",
        "ads_snap_opposing_manual_suppression_max",
        "body_lock_smoothing",
        "body_lock_max_ai_force",
        "body_lock_opposing_boost_max_ai_force",
        "body_lock_max_ai_force_y",
        "body_lock_box_tolerance_px",
        "body_lock_activation_box_px",
        "body_lock_confidence_frames",
        "body_lock_confidence_min_strong",
        "body_lock_opposing_suppression_max",
        "body_lock_orthogonal_suppression_max",
        "body_lock_helpful_preservation_floor",
        "body_lock_manual_overlap_scale",
        "body_lock_near_lock_error_px",
        "body_lock_vertical_orthogonal_bias",
        "body_lock_vertical_deadzone_px",
        "body_lock_vertical_tail_inner_px",
        "body_lock_vertical_tail_speed_threshold_px_per_sec",
        "body_lock_release_tail_scale",
        "body_lock_lateral_motion_min_speed_px_per_sec",
        "body_lock_lateral_motion_lead_seconds",
        "body_lock_lateral_motion_lead_window_px",
        "body_lock_lateral_motion_lead_max_px",
        "body_lock_lateral_motion_tail_scale",
        "body_lock_upper_body_ratio",
        "body_lock_lead_frames",
        "body_lock_lead_seconds",
        "body_lock_vertical_lead_scale",
        "body_lock_lead_max_px",
        "body_lock_target_match_iou",
        "body_lock_target_match_center_px",
        "weak_target_body_lock_force_scale",
        "cue_hold_body_lock_force_scale",
        "auto_fire_ready_error_px",
        "auto_fire_ready_frames",
        "auto_fire_ready_min_ads_ms",
        "auto_fire_ready_max_ai_stick",
    }
)
ADAPTIVE_DELTA_GAIN_KEYS = frozenset(
    {
        "min_error_px",
        "gain_per_update",
        "decay_per_update",
        "max_bonus",
        "trigger_frames",
        "opposing_input_threshold",
        "stale_seconds",
    }
)
MOUSE_AUTO_FIRE_KEYS = frozenset(
    {
        "aim_only",
        "max_source_age_ms",
        "hold_seconds",
        "release_seconds",
    }
)
MOUSE_RECOIL_KEYS = frozenset(
    {
        "amount_px",
    }
)
MOUSE_AI_AIM_KEYS = frozenset(
    {
        "acquire_radius_px",
        "mid_acquire_enter_px",
        "mid_acquire_exit_px",
        "stabilize_enter_px",
        "stabilize_exit_px",
        "inner_release_band_px",
        "stabilize_reacquire_growth_px",
        "stabilize_reacquire_motion_px",
        "acquire_gain",
        "mid_acquire_gain",
        "reacquire_gain",
        "stabilize_gain",
        "predicted_stabilize_gain",
        "moving_stabilize_gain",
        "acquire_max_move_px",
        "mid_acquire_max_move_px",
        "reacquire_max_move_px",
        "stabilize_max_move_px",
        "predicted_stabilize_max_move_px",
        "moving_stabilize_max_move_px",
        "moving_stabilize_motion_px",
        "moving_stabilize_motion_scale",
        "moving_stabilize_max_dt_ms",
        "moving_stabilize_axis_ratio",
        "acquire_lead_seconds",
        "mid_acquire_lead_seconds",
        "reacquire_lead_seconds",
        "acquire_lead_max_px",
        "acquire_response_horizon_s",
        "mid_acquire_response_horizon_s",
        "reacquire_response_horizon_s",
        "stabilize_response_horizon_s",
        "predicted_stabilize_response_horizon_s",
        "response_accel_multiplier",
        "error_rate_lowpass_alpha",
        "follow_control_radius_px",
        "follow_chase_radius_px",
        "follow_balanced_gain_scale",
        "follow_balanced_max_move_scale",
        "follow_balanced_horizon_scale",
        "follow_balanced_accel_scale",
        "follow_balanced_error_rate_scale",
        "follow_chase_gain_scale",
        "follow_chase_max_move_scale",
        "follow_chase_horizon_scale",
        "follow_chase_accel_scale",
        "follow_chase_error_rate_scale",
        "acquire_error_rate_gain",
        "mid_acquire_error_rate_gain",
        "reacquire_error_rate_gain",
        "stabilize_error_rate_gain",
        "predicted_stabilize_error_rate_gain",
        "stabilize_integral_gain",
        "predicted_stabilize_integral_gain",
        "stabilize_integral_limit_px",
        "same_target_grace_ms",
        "reacquire_radius_px",
        "reacquire_window_ms",
        "chase_hold_projection_px_per_sec",
        "chase_hold_speed_px_per_sec",
        "chase_hold_min_radius_px",
        "switch_guard_ms",
        "switch_guard_commit_radius_px",
        "acquire_stall_min_shrink_px",
        "acquire_stall_trigger_frames",
        "acquire_stall_gain_per_frame",
        "acquire_stall_decay_per_frame",
        "acquire_stall_max_bonus",
        "breakaway_speed_px",
        "snap_window_seconds",
        "snap_activation_grace_seconds",
        "snap_gain",
        "snap_max_move_px",
        "snap_response_horizon_s",
        "snap_finish_radius_px",
        "body_lock_enter_px",
        "body_lock_exit_px",
        "body_lock_box_tolerance_px",
        "body_lock_gain",
        "body_lock_max_move_px",
        "body_lock_response_horizon_s",
        "body_lock_deadband_px",
        "body_lock_manual_dampen_speed_px",
        "body_lock_manual_scale",
        "body_lock_jitter_cancel_enabled",
        "body_lock_jitter_cancel_x_inner_radius_px",
        "body_lock_jitter_cancel_x_deadband",
        "body_lock_jitter_cancel_x_soft_px",
        "body_lock_jitter_cancel_x_max_speed",
        "body_lock_jitter_cancel_x_scale",
        "body_lock_jitter_cancel_x_max_move",
        "body_lock_jitter_cancel_x_smoothing",
        "response_adaptive_enabled",
        "response_px_per_input_initial",
        "response_min_px_per_input",
        "response_max_px_per_input",
        "response_sample_alpha",
        "response_min_motion_input",
        "response_stall_factor",
    }
)


@dataclass(slots=True, frozen=True)
class RuntimeVisionConfig:
    backend: str = "native"
    capture_fps: int = 140
    crop_width: int = 640
    crop_height: int = 512
    perf_log: bool = True
    quit_key: str = "0"
    native_cue_sidecar: bool = False
    model_path: str = "models/best.engine"
    fallback_model_path: str = "models/best.pt"


@dataclass(slots=True, frozen=True)
class RuntimeGamepadConfig:
    auto_fire_output: str = "RB"
    rb_counts_as_aiming: bool = False


@dataclass(slots=True, frozen=True)
class RuntimeConfig:
    vision: RuntimeVisionConfig
    gamepad: RuntimeGamepadConfig


@dataclass(slots=True, frozen=True)
class TuningConfig:
    runtime: RuntimeConfig
    gamepad_ai_aim: GamepadAIAimConfig
    gamepad_aim_assist_dynamics: AimAssistDynamicsConfig
    gamepad_auto_fire: GamepadAutoFireConfig
    gamepad_recoil: GamepadRecoilConfig
    adaptive_delta_gain: AdaptiveDeltaGainConfig
    mouse_ai_aim: MouseAIAimConfig
    mouse_auto_fire: MouseAutoFireConfig
    mouse_recoil: MouseRecoilConfig


def _filter(section: Mapping[str, Any] | None, allowed: frozenset[str]) -> dict[str, Any]:
    if not section:
        return {}
    return {key: value for key, value in section.items() if key in allowed}


def _runtime_vision_config(section: Mapping[str, Any] | None) -> RuntimeVisionConfig:
    config = replace(RuntimeVisionConfig(), **_filter(section, RUNTIME_VISION_KEYS))
    if config.backend not in {"python", "native"}:
        config = replace(config, backend=RuntimeVisionConfig().backend)
    return config


def _runtime_gamepad_config(section: Mapping[str, Any] | None) -> RuntimeGamepadConfig:
    config = replace(RuntimeGamepadConfig(), **_filter(section, RUNTIME_GAMEPAD_KEYS))
    if config.auto_fire_output not in {"RB", "RT"}:
        config = replace(config, auto_fire_output=RuntimeGamepadConfig().auto_fire_output)
    return config


def _read_toml(path: Path) -> dict[str, Any]:
    if not path.is_file():
        return {}
    with open(path, "rb") as handle:
        return tomllib.load(handle)


def load_tuning_config(path: Path | None = None) -> TuningConfig:
    resolved_path = path if path is not None else DEFAULT_CONFIG_PATH
    data = _read_toml(resolved_path)

    runtime_section = data.get("runtime", {}) or {}
    gamepad_section = data.get("gamepad", {}) or {}
    mouse_section = data.get("mouse", {}) or {}

    runtime = RuntimeConfig(
        vision=_runtime_vision_config(runtime_section.get("vision")),
        gamepad=_runtime_gamepad_config(runtime_section.get("gamepad")),
    )
    gamepad_ai_aim = replace(
        GamepadAIAimConfig(),
        **_filter(gamepad_section.get("ai_aim"), GAMEPAD_AI_AIM_KEYS),
    )
    gamepad_aim_assist_dynamics = replace(
        AimAssistDynamicsConfig(),
        **_filter(
            gamepad_section.get("aim_assist_dynamics"),
            GAMEPAD_AIM_ASSIST_DYNAMICS_KEYS,
        ),
    )
    gamepad_auto_fire = replace(
        GamepadAutoFireConfig(),
        **_filter(gamepad_section.get("auto_fire"), GAMEPAD_AUTO_FIRE_KEYS),
    )
    gamepad_recoil = replace(
        GamepadRecoilConfig(feedback_amount=0.20),
        **_filter(gamepad_section.get("recoil"), GAMEPAD_RECOIL_KEYS),
    )
    adaptive = replace(
        AdaptiveDeltaGainConfig(),
        **_filter(gamepad_section.get("adaptive_delta_gain"), ADAPTIVE_DELTA_GAIN_KEYS),
    )
    mouse_ai_aim = replace(
        MouseAIAimConfig(),
        **_filter(mouse_section.get("ai_aim"), MOUSE_AI_AIM_KEYS),
    )
    mouse_auto_fire = replace(
        MouseAutoFireConfig(),
        **_filter(mouse_section.get("auto_fire"), MOUSE_AUTO_FIRE_KEYS),
    )
    mouse_recoil = replace(
        MouseRecoilConfig(),
        **_filter(mouse_section.get("recoil"), MOUSE_RECOIL_KEYS),
    )

    return TuningConfig(
        runtime=runtime,
        gamepad_ai_aim=gamepad_ai_aim,
        gamepad_aim_assist_dynamics=gamepad_aim_assist_dynamics,
        gamepad_auto_fire=gamepad_auto_fire,
        gamepad_recoil=gamepad_recoil,
        adaptive_delta_gain=adaptive,
        mouse_ai_aim=mouse_ai_aim,
        mouse_auto_fire=mouse_auto_fire,
        mouse_recoil=mouse_recoil,
    )
