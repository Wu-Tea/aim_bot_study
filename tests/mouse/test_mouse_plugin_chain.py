import unittest

from controllers.mouse.plugin import apply_plugins, reset_plugins
from controllers.mouse.state import MouseFrame, MouseOutput


def _frame():
    return MouseFrame(
        timestamp=1.0,
        manual_dx=0.0,
        manual_dy=0.0,
        is_aiming=True,
        target_dx=0.0,
        target_dy=0.0,
        auto_fire_requested=False,
    )


class _RecordingPlugin:
    def __init__(self, name, calls):
        self.name = name
        self.calls = calls

    def reset(self):
        self.calls.append(f"reset:{self.name}")

    def apply(self, frame, output):
        self.calls.append(f"apply:{self.name}")


class MousePluginChainTests(unittest.TestCase):
    def test_apply_plugins_runs_in_declared_order(self):
        calls = []
        plugins = [
            _RecordingPlugin("aim", calls),
            _RecordingPlugin("fire", calls),
            _RecordingPlugin("recoil", calls),
        ]
        output = MouseOutput()
        apply_plugins(plugins, _frame(), output)
        self.assertEqual(calls, ["apply:aim", "apply:fire", "apply:recoil"])

    def test_reset_plugins_broadcasts_to_every_plugin(self):
        calls = []
        plugins = [_RecordingPlugin("aim", calls), _RecordingPlugin("fire", calls)]
        reset_plugins(plugins)
        self.assertEqual(calls, ["reset:aim", "reset:fire"])

    def test_apply_with_empty_list_is_noop(self):
        output = MouseOutput()
        apply_plugins([], _frame(), output)
        self.assertEqual(output.move_dx, 0.0)
        self.assertEqual(output.move_dy, 0.0)


if __name__ == "__main__":
    unittest.main()
