import tempfile
import textwrap
import unittest
from pathlib import Path

from config import load_tuning_config
from config.loader import RuntimeConfig, RuntimeGamepadConfig, RuntimeVisionConfig
from controllers.gamepad import AdaptiveDeltaGainConfig
from controllers.gamepad import AIAimConfig as GamepadAIAimConfig
from controllers.mouse import AIAimConfig as MouseAIAimConfig


class TuningConfigLoaderTests(unittest.TestCase):
    def test_example_config_starts_with_runtime_and_feel_knobs(self):
        path = Path(__file__).resolve().parent.parent / "config.toml.example"
        content = path.read_text(encoding="utf-8")

        self.assertLess(content.index("[runtime.vision]"), content.index("[gamepad.ai_aim]"))
        self.assertLess(content.index("target_max_age_ms"), content.index("smoothing"))
        config = load_tuning_config(path)

        self.assertEqual(config.runtime.vision.backend, "native")
        self.assertEqual(config.runtime.vision.capture_fps, 140)
        self.assertFalse(config.runtime.vision.native_cue_sidecar)

    def test_missing_file_returns_all_dataclass_defaults(self):
        with tempfile.TemporaryDirectory() as tmp:
            config = load_tuning_config(Path(tmp) / "does_not_exist.toml")

        self.assertEqual(config.gamepad_ai_aim, GamepadAIAimConfig())
        self.assertEqual(config.gamepad_ai_aim.body_lock_activation_box_px, 150.0)
        self.assertEqual(config.gamepad_ai_aim.ads_snap_max_target_dy_px, 90.0)
        self.assertEqual(
            config.runtime,
            RuntimeConfig(
                vision=RuntimeVisionConfig(),
                gamepad=RuntimeGamepadConfig(),
            ),
        )
        self.assertEqual(config.adaptive_delta_gain, AdaptiveDeltaGainConfig())
        self.assertEqual(config.mouse_ai_aim, MouseAIAimConfig())

    def test_overrides_applied_per_section(self):
        toml = textwrap.dedent(
            """
            [runtime.vision]
            backend = "python"
            capture_fps = 120
            crop_width = 600
            crop_height = 480
            perf_log = false
            quit_key = "Q"
            native_cue_sidecar = true

            [runtime.gamepad]
            auto_fire_output = "RT"

            [gamepad.ai_aim]
            smoothing = 0.42
            max_pixels = 180
            max_ai_force_y = 0.88
            body_lock_opposing_boost_max_ai_force = 0.67
            target_max_age_ms = 42.0
            piecewise_mid_pixels_y = 52
            piecewise_max_pixels_y = 172
            piecewise_mid_ratio_y = 0.7
            ads_snap_window_ms = 120
            ads_snap_max_target_dy_px = 84
            body_lock_activation_box_px = 220
            body_lock_confidence_frames = 6
            body_lock_confidence_min_strong = 0.72
            body_lock_opposing_suppression_max = 0.94
            body_lock_orthogonal_suppression_max = 0.81
            body_lock_helpful_preservation_floor = 0.77
            body_lock_manual_overlap_scale = 0.66
            body_lock_near_lock_error_px = 20.0
            body_lock_vertical_orthogonal_bias = 1.25
            body_lock_vertical_deadzone_px = 4.5
            body_lock_vertical_tail_inner_px = 1.5
            body_lock_vertical_tail_speed_threshold_px_per_sec = 80.0
            body_lock_release_tail_scale = 0.35
            body_lock_vertical_lead_scale = 0.8
            body_lock_lead_frames = 6

            [gamepad.adaptive_delta_gain]
            max_bonus = 0.9
            trigger_frames = 5

            [mouse.ai_aim]
            acquire_radius_px = 240.0
            mid_acquire_enter_px = 52.0
            mid_acquire_exit_px = 70.0
            stabilize_enter_px = 14.0
            stabilize_exit_px = 22.0
            inner_release_band_px = 2.5
            stabilize_reacquire_growth_px = 2.0
            stabilize_reacquire_motion_px = 1.4
            acquire_gain = 0.98
            mid_acquire_gain = 0.66
            reacquire_gain = 0.88
            stabilize_gain = 0.11
            predicted_stabilize_gain = 0.09
            acquire_max_move_px = 9.4
            mid_acquire_max_move_px = 4.2
            reacquire_max_move_px = 6.8
            stabilize_max_move_px = 0.95
            predicted_stabilize_max_move_px = 0.7
            acquire_lead_seconds = 0.04
            mid_acquire_lead_seconds = 0.025
            reacquire_lead_seconds = 0.03
            acquire_lead_max_px = 12.0
            acquire_response_horizon_s = 0.014
            stabilize_response_horizon_s = 0.024
            response_accel_multiplier = 2.1
            follow_control_radius_px = 9.0
            follow_chase_radius_px = 26.0
            follow_balanced_gain_scale = 1.12
            follow_balanced_horizon_scale = 0.85
            follow_chase_gain_scale = 1.25
            follow_chase_accel_scale = 1.4
            acquire_error_rate_gain = 0.2
            stabilize_integral_gain = 1.8
            stabilize_integral_limit_px = 4.0
            same_target_grace_ms = 110
            reacquire_radius_px = 88.0
            reacquire_window_ms = 75
            chase_hold_projection_px_per_sec = 140.0
            chase_hold_min_radius_px = 28.0
            acquire_stall_min_shrink_px = 1.4
            acquire_stall_trigger_frames = 3
            acquire_stall_gain_per_frame = 0.22
            acquire_stall_decay_per_frame = 0.18
            acquire_stall_max_bonus = 0.8
            breakaway_speed_px = 17.0
            """
        ).strip()

        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "config.toml"
            path.write_text(toml, encoding="utf-8")
            config = load_tuning_config(path)

        self.assertEqual(config.runtime.vision.backend, "python")
        self.assertEqual(config.runtime.vision.capture_fps, 120)
        self.assertEqual(config.runtime.vision.crop_width, 600)
        self.assertEqual(config.runtime.vision.crop_height, 480)
        self.assertFalse(config.runtime.vision.perf_log)
        self.assertEqual(config.runtime.vision.quit_key, "Q")
        self.assertTrue(config.runtime.vision.native_cue_sidecar)
        self.assertEqual(config.runtime.gamepad.auto_fire_output, "RT")

        self.assertEqual(config.gamepad_ai_aim.smoothing, 0.42)
        self.assertEqual(config.gamepad_ai_aim.max_pixels, 180)
        self.assertEqual(config.gamepad_ai_aim.max_ai_force_y, 0.88)
        self.assertEqual(config.gamepad_ai_aim.body_lock_opposing_boost_max_ai_force, 0.67)
        self.assertEqual(config.gamepad_ai_aim.target_max_age_ms, 42.0)
        self.assertEqual(config.gamepad_ai_aim.piecewise_mid_pixels_y, 52)
        self.assertEqual(config.gamepad_ai_aim.piecewise_max_pixels_y, 172)
        self.assertEqual(config.gamepad_ai_aim.piecewise_mid_ratio_y, 0.7)
        self.assertEqual(config.gamepad_ai_aim.ads_snap_window_ms, 120)
        self.assertEqual(config.gamepad_ai_aim.ads_snap_max_target_dy_px, 84)
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
        self.assertEqual(config.gamepad_ai_aim.body_lock_manual_overlap_scale, 0.66)
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
        self.assertEqual(config.gamepad_ai_aim.body_lock_release_tail_scale, 0.35)
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

        self.assertEqual(config.mouse_ai_aim.acquire_radius_px, 240.0)
        self.assertEqual(config.mouse_ai_aim.mid_acquire_enter_px, 52.0)
        self.assertEqual(config.mouse_ai_aim.mid_acquire_exit_px, 70.0)
        self.assertEqual(config.mouse_ai_aim.stabilize_enter_px, 14.0)
        self.assertEqual(config.mouse_ai_aim.stabilize_exit_px, 22.0)
        self.assertEqual(config.mouse_ai_aim.inner_release_band_px, 2.5)
        self.assertEqual(config.mouse_ai_aim.stabilize_reacquire_growth_px, 2.0)
        self.assertEqual(config.mouse_ai_aim.stabilize_reacquire_motion_px, 1.4)
        self.assertEqual(config.mouse_ai_aim.acquire_gain, 0.98)
        self.assertEqual(config.mouse_ai_aim.mid_acquire_gain, 0.66)
        self.assertEqual(config.mouse_ai_aim.reacquire_gain, 0.88)
        self.assertEqual(config.mouse_ai_aim.stabilize_gain, 0.11)
        self.assertEqual(config.mouse_ai_aim.predicted_stabilize_gain, 0.09)
        self.assertEqual(config.mouse_ai_aim.acquire_max_move_px, 9.4)
        self.assertEqual(config.mouse_ai_aim.mid_acquire_max_move_px, 4.2)
        self.assertEqual(config.mouse_ai_aim.reacquire_max_move_px, 6.8)
        self.assertEqual(config.mouse_ai_aim.stabilize_max_move_px, 0.95)
        self.assertEqual(config.mouse_ai_aim.predicted_stabilize_max_move_px, 0.7)
        self.assertEqual(config.mouse_ai_aim.acquire_lead_seconds, 0.04)
        self.assertEqual(config.mouse_ai_aim.mid_acquire_lead_seconds, 0.025)
        self.assertEqual(config.mouse_ai_aim.reacquire_lead_seconds, 0.03)
        self.assertEqual(config.mouse_ai_aim.acquire_lead_max_px, 12.0)
        self.assertEqual(config.mouse_ai_aim.acquire_response_horizon_s, 0.014)
        self.assertEqual(config.mouse_ai_aim.stabilize_response_horizon_s, 0.024)
        self.assertEqual(config.mouse_ai_aim.response_accel_multiplier, 2.1)
        self.assertEqual(config.mouse_ai_aim.follow_control_radius_px, 9.0)
        self.assertEqual(config.mouse_ai_aim.follow_chase_radius_px, 26.0)
        self.assertEqual(config.mouse_ai_aim.follow_balanced_gain_scale, 1.12)
        self.assertEqual(config.mouse_ai_aim.follow_balanced_horizon_scale, 0.85)
        self.assertEqual(config.mouse_ai_aim.follow_chase_gain_scale, 1.25)
        self.assertEqual(config.mouse_ai_aim.follow_chase_accel_scale, 1.4)
        self.assertEqual(config.mouse_ai_aim.acquire_error_rate_gain, 0.2)
        self.assertEqual(config.mouse_ai_aim.stabilize_integral_gain, 1.8)
        self.assertEqual(config.mouse_ai_aim.stabilize_integral_limit_px, 4.0)
        self.assertEqual(config.mouse_ai_aim.same_target_grace_ms, 110)
        self.assertEqual(config.mouse_ai_aim.reacquire_radius_px, 88.0)
        self.assertEqual(config.mouse_ai_aim.reacquire_window_ms, 75)
        self.assertEqual(
            config.mouse_ai_aim.chase_hold_projection_px_per_sec,
            140.0,
        )
        self.assertEqual(config.mouse_ai_aim.chase_hold_min_radius_px, 28.0)
        self.assertEqual(config.mouse_ai_aim.acquire_stall_min_shrink_px, 1.4)
        self.assertEqual(config.mouse_ai_aim.acquire_stall_trigger_frames, 3)
        self.assertEqual(config.mouse_ai_aim.acquire_stall_gain_per_frame, 0.22)
        self.assertEqual(config.mouse_ai_aim.acquire_stall_decay_per_frame, 0.18)
        self.assertEqual(config.mouse_ai_aim.acquire_stall_max_bonus, 0.8)
        self.assertEqual(config.mouse_ai_aim.breakaway_speed_px, 17.0)

    def test_unknown_keys_are_ignored(self):
        toml = textwrap.dedent(
            """
            [runtime.vision]
            backend = "native"
            fake_runtime_key = 123

            [gamepad.ai_aim]
            smoothing = 0.5
            not_a_real_knob = 9.9

            [mouse.ai_aim]
            also_fake = true
            stabilize_gain = 0.07
            """
        ).strip()

        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "config.toml"
            path.write_text(toml, encoding="utf-8")
            config = load_tuning_config(path)

        self.assertEqual(config.gamepad_ai_aim.smoothing, 0.5)
        self.assertEqual(config.runtime.vision.backend, "native")
        self.assertFalse(hasattr(config.runtime.vision, "fake_runtime_key"))
        self.assertEqual(config.mouse_ai_aim.stabilize_gain, 0.07)

    def test_invalid_runtime_choice_falls_back_to_safe_default(self):
        toml = textwrap.dedent(
            """
            [runtime.vision]
            backend = "invalid"

            [runtime.gamepad]
            auto_fire_output = "invalid"
            """
        ).strip()

        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "config.toml"
            path.write_text(toml, encoding="utf-8")
            config = load_tuning_config(path)

        self.assertEqual(config.runtime.vision.backend, RuntimeVisionConfig().backend)
        self.assertEqual(
            config.runtime.gamepad.auto_fire_output,
            RuntimeGamepadConfig().auto_fire_output,
        )

    def test_empty_file_returns_defaults(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "config.toml"
            path.write_text("", encoding="utf-8")
            config = load_tuning_config(path)

        self.assertEqual(config.gamepad_ai_aim, GamepadAIAimConfig())
        self.assertEqual(config.runtime.vision, RuntimeVisionConfig())
        self.assertEqual(config.runtime.gamepad, RuntimeGamepadConfig())
        self.assertEqual(config.adaptive_delta_gain, AdaptiveDeltaGainConfig())
        self.assertEqual(config.mouse_ai_aim, MouseAIAimConfig())


if __name__ == "__main__":
    unittest.main()
