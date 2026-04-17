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
        self.assertEqual(config.gamepad_ai_aim.body_lock_activation_box_px, 150.0)
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
            ads_snap_window_ms = 120
            body_lock_activation_box_px = 220
            body_lock_confidence_frames = 6
            body_lock_confidence_min_strong = 0.72
            body_lock_opposing_suppression_max = 0.94
            body_lock_orthogonal_suppression_max = 0.81
            body_lock_helpful_preservation_floor = 0.77
            body_lock_near_lock_error_px = 20.0
            body_lock_vertical_orthogonal_bias = 1.25
            body_lock_vertical_deadzone_px = 4.5
            body_lock_vertical_tail_inner_px = 1.5
            body_lock_vertical_tail_speed_threshold_px_per_sec = 80.0
            body_lock_vertical_lead_scale = 0.8
            body_lock_lead_frames = 6

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
        self.assertEqual(config.gamepad_ai_aim.ads_snap_window_ms, 120)
        self.assertEqual(config.gamepad_ai_aim.body_lock_activation_box_px, 220)
        self.assertEqual(config.gamepad_ai_aim.body_lock_confidence_frames, 6)
        self.assertEqual(config.gamepad_ai_aim.body_lock_confidence_min_strong, 0.72)
        self.assertEqual(config.gamepad_ai_aim.body_lock_opposing_suppression_max, 0.94)
        self.assertEqual(
            config.gamepad_ai_aim.body_lock_orthogonal_suppression_max,
            0.81,
        )
        self.assertEqual(
            config.gamepad_ai_aim.body_lock_helpful_preservation_floor,
            0.77,
        )
        self.assertEqual(config.gamepad_ai_aim.body_lock_near_lock_error_px, 20.0)
        self.assertEqual(
            config.gamepad_ai_aim.body_lock_vertical_orthogonal_bias,
            1.25,
        )
        self.assertEqual(config.gamepad_ai_aim.body_lock_vertical_deadzone_px, 4.5)
        self.assertEqual(config.gamepad_ai_aim.body_lock_vertical_tail_inner_px, 1.5)
        self.assertEqual(
            config.gamepad_ai_aim.body_lock_vertical_tail_speed_threshold_px_per_sec,
            80.0,
        )
        self.assertEqual(config.gamepad_ai_aim.body_lock_vertical_lead_scale, 0.8)
        self.assertEqual(config.gamepad_ai_aim.body_lock_lead_frames, 6)
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
