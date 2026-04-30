import tomllib
from dataclasses import dataclass, replace
from pathlib import Path
from typing import Any, Mapping

from controllers.gamepad.adaptive_delta_gain import AdaptiveDeltaGainConfig
from controllers.gamepad.ai_aim import AIAimConfig as GamepadAIAimConfig
from controllers.mouse.ai_aim import AIAimConfig as MouseAIAimConfig


PROJECT_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_CONFIG_PATH = PROJECT_ROOT / "config.toml"


GAMEPAD_AI_AIM_KEYS = frozenset(
    {
        "smoothing",
        "max_pixels",
        "max_ai_force",
        "max_ai_force_y",
        "ai_delta_gain",
        "piecewise_mid_pixels_y",
        "piecewise_max_pixels_y",
        "piecewise_mid_ratio_y",
        "ads_snap_window_ms",
        "ads_snap_smoothing",
        "ads_snap_max_ai_force",
        "ads_snap_max_ai_force_y",
        "ads_snap_max_target_dy_px",
        "body_lock_smoothing",
        "body_lock_max_ai_force",
        "body_lock_max_ai_force_y",
        "body_lock_box_tolerance_px",
        "body_lock_activation_box_px",
        "body_lock_confidence_frames",
        "body_lock_confidence_min_strong",
        "body_lock_opposing_suppression_max",
        "body_lock_orthogonal_suppression_max",
        "body_lock_helpful_preservation_floor",
        "body_lock_near_lock_error_px",
        "body_lock_vertical_orthogonal_bias",
        "body_lock_vertical_deadzone_px",
        "body_lock_vertical_tail_inner_px",
        "body_lock_vertical_tail_speed_threshold_px_per_sec",
        "body_lock_upper_body_ratio",
        "body_lock_lead_frames",
        "body_lock_lead_seconds",
        "body_lock_vertical_lead_scale",
        "body_lock_lead_max_px",
        "body_lock_target_match_iou",
        "body_lock_target_match_center_px",
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
    }
)


@dataclass(slots=True, frozen=True)
class TuningConfig:
    gamepad_ai_aim: GamepadAIAimConfig
    adaptive_delta_gain: AdaptiveDeltaGainConfig
    mouse_ai_aim: MouseAIAimConfig


def _filter(section: Mapping[str, Any] | None, allowed: frozenset[str]) -> dict[str, Any]:
    if not section:
        return {}
    return {key: value for key, value in section.items() if key in allowed}


def _read_toml(path: Path) -> dict[str, Any]:
    if not path.is_file():
        return {}
    with open(path, "rb") as handle:
        return tomllib.load(handle)


def load_tuning_config(path: Path | None = None) -> TuningConfig:
    resolved_path = path if path is not None else DEFAULT_CONFIG_PATH
    data = _read_toml(resolved_path)

    gamepad_section = data.get("gamepad", {}) or {}
    mouse_section = data.get("mouse", {}) or {}

    gamepad_ai_aim = replace(
        GamepadAIAimConfig(),
        **_filter(gamepad_section.get("ai_aim"), GAMEPAD_AI_AIM_KEYS),
    )
    adaptive = replace(
        AdaptiveDeltaGainConfig(),
        **_filter(gamepad_section.get("adaptive_delta_gain"), ADAPTIVE_DELTA_GAIN_KEYS),
    )
    mouse_ai_aim = replace(
        MouseAIAimConfig(),
        **_filter(mouse_section.get("ai_aim"), MOUSE_AI_AIM_KEYS),
    )

    return TuningConfig(
        gamepad_ai_aim=gamepad_ai_aim,
        adaptive_delta_gain=adaptive,
        mouse_ai_aim=mouse_ai_aim,
    )
