from __future__ import annotations

from dataclasses import asdict, dataclass
import json
import os
from pathlib import Path
from typing import Sequence

from .plugin import PluginApplicationTrace
from .state import GamepadFrame, GamepadOutput


PROJECT_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT_PATH = PROJECT_ROOT / "artifacts" / "diagnostics" / "gamepad_downward_pull.jsonl"


@dataclass(slots=True, frozen=True)
class DownwardPullDiagnosticsConfig:
    enabled: bool = False
    output_path: Path = DEFAULT_OUTPUT_PATH
    downward_delta_threshold: int = 6000


class DownwardPullDiagnostics:
    def __init__(self, config: DownwardPullDiagnosticsConfig | None = None):
        self.config = config or DownwardPullDiagnosticsConfig()

    @classmethod
    def from_env(cls) -> "DownwardPullDiagnostics":
        enabled = _env_flag("GAMEPAD_DOWNWARD_DIAGNOSTICS")
        output_path = Path(os.getenv("GAMEPAD_DOWNWARD_DIAGNOSTICS_PATH", str(DEFAULT_OUTPUT_PATH)))
        threshold = int(os.getenv("GAMEPAD_DOWNWARD_DIAGNOSTICS_THRESHOLD", "6000"))
        return cls(
            DownwardPullDiagnosticsConfig(
                enabled=enabled,
                output_path=output_path,
                downward_delta_threshold=max(1, threshold),
            )
        )

    def record_if_triggered(
        self,
        *,
        frame: GamepadFrame,
        output: GamepadOutput,
        plugin_traces: Sequence[PluginApplicationTrace],
        plugins: Sequence[object],
    ) -> bool:
        if not self.config.enabled:
            return False

        system_right_y_delta = int(output.right_y) - int(frame.manual_right_y)
        largest_downward_trace = min(plugin_traces, key=lambda trace: trace.delta_right_y, default=None)
        threshold = -abs(int(self.config.downward_delta_threshold))
        trigger_by_total = system_right_y_delta <= threshold
        trigger_by_plugin = (
            largest_downward_trace is not None
            and largest_downward_trace.delta_right_y <= threshold
        )
        if not trigger_by_total and not trigger_by_plugin:
            return False

        payload = {
            "timestamp": frame.timestamp,
            "manual_right_y": int(frame.manual_right_y),
            "final_right_y": int(output.right_y),
            "system_right_y_delta": system_right_y_delta,
            "auto_fire_active": bool(output.auto_fire_active),
            "is_aiming": bool(frame.is_aiming),
            "auto_fire_requested": bool(frame.auto_fire_requested),
            "target_dx": float(frame.target_dx),
            "target_dy": float(frame.target_dy),
            "target_revision": int(frame.target_revision),
            "target_timestamp": frame.target_timestamp,
            "target": _serialize_target(frame),
            "triggered_by_total_delta": trigger_by_total,
            "triggered_by_plugin_delta": trigger_by_plugin,
            "largest_downward_plugin": (
                largest_downward_trace.plugin_name if largest_downward_trace is not None else None
            ),
            "largest_downward_plugin_delta": (
                largest_downward_trace.delta_right_y if largest_downward_trace is not None else 0
            ),
            "plugin_traces": [
                {
                    **asdict(trace),
                    "snapshot": _plugin_snapshot(plugin),
                }
                for trace, plugin in zip(plugin_traces, plugins)
            ],
        }

        self.config.output_path.parent.mkdir(parents=True, exist_ok=True)
        with self.config.output_path.open("a", encoding="utf-8") as handle:
            handle.write(json.dumps(payload, ensure_ascii=True))
            handle.write("\n")
        return True


def _serialize_target(frame: GamepadFrame) -> dict[str, object] | None:
    if frame.target is None:
        return None
    return {
        "aim_point_x": float(frame.target.aim_point_x),
        "aim_point_y": float(frame.target.aim_point_y),
        "screen_center_x": float(frame.target.screen_center_x),
        "screen_center_y": float(frame.target.screen_center_y),
        "body_box": None if frame.target.body_box is None else list(frame.target.body_box),
    }


def _plugin_snapshot(plugin: object) -> dict[str, object]:
    snapshot: dict[str, object] = {}
    if hasattr(plugin, "_mode"):
        snapshot["mode"] = getattr(plugin, "_mode")
    if hasattr(plugin, "ai_stick_x"):
        snapshot["ai_stick_x"] = float(getattr(plugin, "ai_stick_x"))
    if hasattr(plugin, "ai_stick_y"):
        snapshot["ai_stick_y"] = float(getattr(plugin, "ai_stick_y"))
    if hasattr(plugin, "_last_lock_confidence"):
        snapshot["lock_confidence"] = float(getattr(plugin, "_last_lock_confidence"))
    if hasattr(plugin, "config"):
        snapshot["plugin_class"] = type(plugin).__name__
    return snapshot


def _env_flag(name: str, default: bool = False) -> bool:
    value = os.getenv(name)
    if value is None:
        return default
    return value.strip().lower() in {"1", "true", "yes", "on"}
