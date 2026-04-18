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
    {"gain", "smoothing", "max_correction_px", "manual_dampen"}
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
