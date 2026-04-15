import tempfile
import textwrap
import unittest
from pathlib import Path

from config import load_tuning_config
from controllers.gamepad import AdaptiveDeltaGainConfig
from controllers.gamepad import AIAimConfig as GamepadAIAimConfig
from controllers.mouse import AIAimConfig as MouseAIAimConfig


class TuningConfigLoaderTests(unittest.TestCase):
    def test_missing_file_returns_all_dataclass_defaults(self):
        with tempfile.TemporaryDirectory() as tmp:
            config = load_tuning_config(Path(tmp) / "does_not_exist.toml")

        self.assertEqual(config.gamepad_ai_aim, GamepadAIAimConfig())
        self.assertEqual(config.adaptive_delta_gain, AdaptiveDeltaGainConfig())
        self.assertEqual(config.mouse_ai_aim, MouseAIAimConfig())

    def test_overrides_applied_per_section(self):
        toml = textwrap.dedent(
            """
            [gamepad.ai_aim]
            smoothing = 0.42
            max_pixels = 180
            max_ai_force_y = 0.88
            piecewise_mid_pixels_y = 52
            piecewise_max_pixels_y = 172
            piecewise_mid_ratio_y = 0.7

            [gamepad.adaptive_delta_gain]
            max_bonus = 0.9
            trigger_frames = 5

            [mouse.ai_aim]
            gain = 0.12
            manual_dampen = 0.75
            """
        ).strip()

        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "config.toml"
            path.write_text(toml, encoding="utf-8")
            config = load_tuning_config(path)

        self.assertEqual(config.gamepad_ai_aim.smoothing, 0.42)
        self.assertEqual(config.gamepad_ai_aim.max_pixels, 180)
        self.assertEqual(config.gamepad_ai_aim.max_ai_force_y, 0.88)
        self.assertEqual(config.gamepad_ai_aim.piecewise_mid_pixels_y, 52)
        self.assertEqual(config.gamepad_ai_aim.piecewise_max_pixels_y, 172)
        self.assertEqual(config.gamepad_ai_aim.piecewise_mid_ratio_y, 0.7)
        self.assertEqual(
            config.gamepad_ai_aim.ai_delta_gain,
            GamepadAIAimConfig().ai_delta_gain,
        )

        self.assertEqual(config.adaptive_delta_gain.max_bonus, 0.9)
        self.assertEqual(config.adaptive_delta_gain.trigger_frames, 5)
        self.assertEqual(
            config.adaptive_delta_gain.gain_per_update,
            AdaptiveDeltaGainConfig().gain_per_update,
        )

        self.assertEqual(config.mouse_ai_aim.gain, 0.12)
        self.assertEqual(config.mouse_ai_aim.manual_dampen, 0.75)
        self.assertEqual(
            config.mouse_ai_aim.smoothing, MouseAIAimConfig().smoothing
        )

    def test_unknown_keys_are_ignored(self):
        toml = textwrap.dedent(
            """
            [gamepad.ai_aim]
            smoothing = 0.5
            not_a_real_knob = 9.9

            [mouse.ai_aim]
            also_fake = true
            gain = 0.07
            """
        ).strip()

        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "config.toml"
            path.write_text(toml, encoding="utf-8")
            config = load_tuning_config(path)

        self.assertEqual(config.gamepad_ai_aim.smoothing, 0.5)
        self.assertEqual(config.mouse_ai_aim.gain, 0.07)

    def test_empty_file_returns_defaults(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "config.toml"
            path.write_text("", encoding="utf-8")
            config = load_tuning_config(path)

        self.assertEqual(config.gamepad_ai_aim, GamepadAIAimConfig())
        self.assertEqual(config.adaptive_delta_gain, AdaptiveDeltaGainConfig())
        self.assertEqual(config.mouse_ai_aim, MouseAIAimConfig())


if __name__ == "__main__":
    unittest.main()
