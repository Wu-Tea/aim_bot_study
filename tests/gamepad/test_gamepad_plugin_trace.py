import unittest

from controllers.gamepad.plugin import apply_plugins_with_trace
from controllers.gamepad.state import GamepadFrame, GamepadOutput


def _frame():
    return GamepadFrame(
        timestamp=1.0,
        left_x=0,
        left_y=0,
        manual_right_x=0,
        manual_right_y=0,
        left_trigger=255,
        right_trigger=0,
        buttons={"rb": False},
        is_aiming=True,
        target_dx=0.0,
        target_dy=0.0,
        auto_fire_requested=False,
    )


def _output():
    return GamepadOutput(
        left_x=0,
        left_y=0,
        right_x=0,
        right_y=0,
        left_trigger=255,
        right_trigger=0,
        buttons={"rb": False},
    )


class _TracePlugin:
    def __init__(self, name, *, delta_right_y=0, auto_fire_active=None):
        self.name = name
        self.delta_right_y = delta_right_y
        self.auto_fire_active = auto_fire_active

    def reset(self):
        return None

    def apply(self, frame, output):
        output.right_y += self.delta_right_y
        if self.auto_fire_active is not None:
            output.auto_fire_active = self.auto_fire_active


class PluginTraceTests(unittest.TestCase):
    def test_apply_plugins_with_trace_records_per_plugin_right_y_and_auto_fire_changes(self):
        plugins = [
            _TracePlugin("aim", delta_right_y=-1200),
            _TracePlugin("fire", auto_fire_active=True),
            _TracePlugin("recoil", delta_right_y=-9830),
        ]
        output = _output()

        traces = apply_plugins_with_trace(plugins, _frame(), output)

        self.assertEqual([trace.plugin_name for trace in traces], ["aim", "fire", "recoil"])
        self.assertEqual(traces[0].before_right_y, 0)
        self.assertEqual(traces[0].after_right_y, -1200)
        self.assertEqual(traces[0].delta_right_y, -1200)
        self.assertFalse(traces[0].after_auto_fire_active)
        self.assertEqual(traces[1].delta_right_y, 0)
        self.assertFalse(traces[1].before_auto_fire_active)
        self.assertTrue(traces[1].after_auto_fire_active)
        self.assertEqual(traces[2].before_right_y, -1200)
        self.assertEqual(traces[2].after_right_y, -11030)
        self.assertEqual(output.right_y, -11030)
        self.assertTrue(output.auto_fire_active)


if __name__ == "__main__":
    unittest.main()
