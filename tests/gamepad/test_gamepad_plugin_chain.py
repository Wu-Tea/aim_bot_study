import unittest

from controllers.gamepad.plugin import apply_plugins, reset_plugins
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


class _RecordingPlugin:
    def __init__(self, name, calls):
        self.name = name
        self.calls = calls

    def reset(self):
        self.calls.append(f"reset:{self.name}")

    def apply(self, frame, output):
        self.calls.append(f"apply:{self.name}")
        output.buttons[self.name] = True


class GamepadPluginChainTests(unittest.TestCase):
    def test_apply_plugins_runs_in_declared_order(self):
        calls = []
        plugins = [
            _RecordingPlugin("aim", calls),
            _RecordingPlugin("fire", calls),
            _RecordingPlugin("recoil", calls),
        ]

        output = _output()
        apply_plugins(plugins, _frame(), output)

        self.assertEqual(calls, ["apply:aim", "apply:fire", "apply:recoil"])
        self.assertTrue(output.buttons["aim"])
        self.assertTrue(output.buttons["fire"])
        self.assertTrue(output.buttons["recoil"])

    def test_reset_plugins_broadcasts_to_every_plugin(self):
        calls = []
        plugins = [_RecordingPlugin("aim", calls), _RecordingPlugin("fire", calls)]

        reset_plugins(plugins)

        self.assertEqual(calls, ["reset:aim", "reset:fire"])


if __name__ == "__main__":
    unittest.main()
