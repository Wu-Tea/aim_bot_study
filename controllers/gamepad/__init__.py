from .adaptive_delta_gain import (
    AdaptiveDeltaGain,
    AdaptiveDeltaGainAdjustment,
    AdaptiveDeltaGainConfig,
)
from .ai_aim import AIAimConfig, AIAimPlugin
from .auto_fire import AutoFireConfig, AutoFirePlugin
from .diagnostics import DownwardPullDiagnostics, DownwardPullDiagnosticsConfig
from .horizontal_assist import (
    HorizontalAimAssist,
    HorizontalAimAssistConfig,
    compute_axis_soft_strengths,
)
from .legacy_ai_aim import (
    AdaptiveDeltaGainSubPlugin,
    AIAimContext,
    AIAimSubPlugin,
    HorizontalAssistSubPlugin,
    LegacyAIAimPlugin,
    ManualIntentGuardSubPlugin,
    OvershootGuardSubPlugin,
)
from .manual_intent_guard import (
    ManualIntentAdjustment,
    ManualIntentGuard,
    ManualIntentGuardConfig,
)
from .overshoot_guard import OvershootGuard, OvershootGuardConfig
from .plugin import GamepadPlugin, PluginApplicationTrace, apply_plugins, apply_plugins_with_trace, reset_plugins
from .recoil_compensation import RecoilCompensationConfig, RecoilCompensationPlugin
from .state import GamepadFrame, GamepadOutput

__all__ = [
    "AdaptiveDeltaGain",
    "AdaptiveDeltaGainAdjustment",
    "AdaptiveDeltaGainConfig",
    "AdaptiveDeltaGainSubPlugin",
    "AIAimContext",
    "AIAimConfig",
    "AIAimPlugin",
    "AIAimSubPlugin",
    "HorizontalAssistSubPlugin",
    "LegacyAIAimPlugin",
    "ManualIntentGuardSubPlugin",
    "OvershootGuardSubPlugin",
    "AutoFireConfig",
    "AutoFirePlugin",
    "DownwardPullDiagnostics",
    "DownwardPullDiagnosticsConfig",
    "HorizontalAimAssist",
    "HorizontalAimAssistConfig",
    "compute_axis_soft_strengths",
    "ManualIntentAdjustment",
    "ManualIntentGuard",
    "ManualIntentGuardConfig",
    "OvershootGuard",
    "OvershootGuardConfig",
    "GamepadPlugin",
    "PluginApplicationTrace",
    "apply_plugins",
    "apply_plugins_with_trace",
    "reset_plugins",
    "RecoilCompensationConfig",
    "RecoilCompensationPlugin",
    "GamepadFrame",
    "GamepadOutput",
]
