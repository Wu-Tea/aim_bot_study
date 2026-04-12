from .ai_aim import AIAimConfig, AIAimPlugin
from .auto_fire import AutoFireConfig, AutoFirePlugin
from .horizontal_assist import (
    HorizontalAimAssist,
    HorizontalAimAssistConfig,
    compute_axis_soft_strengths,
)
from .overshoot_guard import OvershootGuard, OvershootGuardConfig
from .plugin import GamepadPlugin, apply_plugins, reset_plugins
from .recoil_compensation import RecoilCompensationConfig, RecoilCompensationPlugin
from .state import GamepadFrame, GamepadOutput

__all__ = [
    "AIAimConfig",
    "AIAimPlugin",
    "AutoFireConfig",
    "AutoFirePlugin",
    "HorizontalAimAssist",
    "HorizontalAimAssistConfig",
    "compute_axis_soft_strengths",
    "OvershootGuard",
    "OvershootGuardConfig",
    "GamepadPlugin",
    "apply_plugins",
    "reset_plugins",
    "RecoilCompensationConfig",
    "RecoilCompensationPlugin",
    "GamepadFrame",
    "GamepadOutput",
]
