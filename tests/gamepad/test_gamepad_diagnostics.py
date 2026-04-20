import json
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

from controllers.base_controller import ControllerTarget
from controllers.gamepad.diagnostics import (
    DownwardPullDiagnostics,
    DownwardPullDiagnosticsConfig,
)
from controllers.gamepad.plugin import PluginApplicationTrace
from controllers.gamepad.state import GamepadFrame, GamepadOutput


class _FakeAIAimPlugin:
    def __init__(self):
        self._mode = "body_lock"
        self.ai_stick_x = 0.0
        self.ai_stick_y = -3200.0
        self._last_lock_confidence = 0.91


class _FakePlugin:
    def __init__(self, name):
        self.name = name


def _frame(*, manual_ry=0, target_dy=0.0):
    return GamepadFrame(
        timestamp=12.5,
        left_x=0,
        left_y=0,
        manual_right_x=0,
        manual_right_y=manual_ry,
        left_trigger=255,
        right_trigger=0,
        buttons={"rb": False},
        is_aiming=True,
        target_dx=6.0,
        target_dy=target_dy,
        auto_fire_requested=True,
        target_revision=3,
        target_timestamp=12.4,
        target=ControllerTarget(
            aim_point_x=320.0,
            aim_point_y=210.0,
            screen_center_x=320.0,
            screen_center_y=256.0,
            body_box=(282.0, 128.0, 358.0, 316.0),
        ),
    )


class DownwardPullDiagnosticsTests(unittest.TestCase):
    def test_disabled_diagnostics_do_not_write_any_event(self):
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "downward.jsonl"
            diagnostics = DownwardPullDiagnostics(
                DownwardPullDiagnosticsConfig(
                    enabled=False,
                    output_path=path,
                    downward_delta_threshold=6000,
                )
            )

            wrote = diagnostics.record_if_triggered(
                frame=_frame(),
                output=GamepadOutput(right_y=-9830, auto_fire_active=True),
                plugin_traces=[
                    PluginApplicationTrace(
                        plugin_name="RecoilCompensationPlugin",
                        before_right_y=0,
                        after_right_y=-9830,
                        delta_right_y=-9830,
                        before_auto_fire_active=False,
                        after_auto_fire_active=True,
                    )
                ],
                plugins=[_FakePlugin("RecoilCompensationPlugin")],
            )

            self.assertFalse(wrote)
            self.assertFalse(path.exists())

    def test_writes_json_event_when_downward_pull_exceeds_threshold(self):
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "downward.jsonl"
            diagnostics = DownwardPullDiagnostics(
                DownwardPullDiagnosticsConfig(
                    enabled=True,
                    output_path=path,
                    downward_delta_threshold=6000,
                )
            )
            frame = _frame(manual_ry=0, target_dy=44.0)
            output = GamepadOutput(right_y=-9830, auto_fire_active=True)

            wrote = diagnostics.record_if_triggered(
                frame=frame,
                output=output,
                plugin_traces=[
                    PluginApplicationTrace(
                        plugin_name="AIAimPlugin",
                        before_right_y=0,
                        after_right_y=-2400,
                        delta_right_y=-2400,
                        before_auto_fire_active=False,
                        after_auto_fire_active=False,
                    ),
                    PluginApplicationTrace(
                        plugin_name="AutoFirePlugin",
                        before_right_y=-2400,
                        after_right_y=-2400,
                        delta_right_y=0,
                        before_auto_fire_active=False,
                        after_auto_fire_active=True,
                    ),
                    PluginApplicationTrace(
                        plugin_name="RecoilCompensationPlugin",
                        before_right_y=-2400,
                        after_right_y=-9830,
                        delta_right_y=-7430,
                        before_auto_fire_active=True,
                        after_auto_fire_active=True,
                    ),
                ],
                plugins=[_FakeAIAimPlugin(), _FakePlugin("AutoFirePlugin"), _FakePlugin("RecoilCompensationPlugin")],
            )

            self.assertTrue(wrote)
            payload = json.loads(path.read_text(encoding="utf-8").strip())
            self.assertEqual(payload["manual_right_y"], 0)
            self.assertEqual(payload["final_right_y"], -9830)
            self.assertEqual(payload["system_right_y_delta"], -9830)
            self.assertEqual(payload["largest_downward_plugin"], "RecoilCompensationPlugin")
            self.assertEqual(payload["largest_downward_plugin_delta"], -7430)
            self.assertTrue(payload["auto_fire_active"])
            self.assertEqual(payload["target"]["body_box"], [282.0, 128.0, 358.0, 316.0])
            self.assertEqual(payload["plugin_traces"][0]["plugin_name"], "AIAimPlugin")
            self.assertEqual(payload["plugin_traces"][0]["snapshot"]["mode"], "body_lock")
            self.assertEqual(payload["plugin_traces"][2]["plugin_name"], "RecoilCompensationPlugin")


if __name__ == "__main__":
    unittest.main()
