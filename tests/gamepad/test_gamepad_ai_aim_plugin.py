import unittest

from controllers.gamepad.ai_aim import (
    AIAimConfig,
    AIAimPlugin,
    ManualIntentGuardSubPlugin,
)
from controllers.gamepad.manual_intent_guard import ManualIntentGuardConfig
from controllers.gamepad.state import GamepadFrame, GamepadOutput


def _frame(
    *,
    aiming=True,
    target_dx=0.0,
    target_dy=0.0,
    manual_rx=0,
    manual_ry=0,
    timestamp=1.0,
    target_revision=0,
):
    return GamepadFrame(
        timestamp=timestamp,
        left_x=0,
        left_y=0,
        manual_right_x=manual_rx,
        manual_right_y=manual_ry,
        left_trigger=255 if aiming else 0,
        right_trigger=0,
        buttons={"rb": False},
        is_aiming=aiming,
        target_dx=target_dx,
        target_dy=target_dy,
        auto_fire_requested=False,
        target_revision=target_revision,
        target_timestamp=timestamp,
    )


def _output(frame):
    return GamepadOutput(
        left_x=frame.left_x,
        left_y=frame.left_y,
        right_x=frame.manual_right_x,
        right_y=frame.manual_right_y,
        left_trigger=frame.left_trigger,
        right_trigger=frame.right_trigger,
        buttons=dict(frame.buttons),
    )


class _AddDxPlugin:
    def reset(self):
        return None

    def observe_target(self, *, target_dx: float, target_dy: float, is_aiming: bool, timestamp: float):
        return None

    def apply(self, context):
        context.assist_dx += 5.0


class _ScalePlugin:
    def reset(self):
        return None

    def observe_target(self, *, target_dx: float, target_dy: float, is_aiming: bool, timestamp: float):
        return None

    def apply(self, context):
        context.x_desired_scale *= 0.5


class AIAimPluginTests(unittest.TestCase):
    def make_manual_intent_plugin(self):
        return ManualIntentGuardSubPlugin(
            ManualIntentGuardConfig(
                min_error_px=8.0,
                stable_history=3,
                opposing_input_threshold=4000,
                opposed_output_scale=0.4,
                opposed_ai_fade_scale=0.0,
            )
        )

    def test_piecewise_mapping_hits_mid_ratio_at_first_breakpoint(self):
        plugin = AIAimPlugin(
            AIAimConfig(
                smoothing=0.0,
                ai_delta_gain=0.7,
                piecewise_mid_pixels=80,
                piecewise_max_pixels=230,
                piecewise_mid_ratio=0.5,
            ),
            sub_plugins=(),
        )

        mapped = plugin._map_pixel_to_stick(80.0 * plugin.config.ai_delta_gain)

        self.assertAlmostEqual(mapped, 32767 * 0.5, delta=1.0)

    def test_piecewise_mapping_boosts_mid_range_before_max_speed(self):
        plugin = AIAimPlugin(
            AIAimConfig(
                smoothing=0.0,
                ai_delta_gain=0.7,
                piecewise_mid_pixels=80,
                piecewise_max_pixels=230,
                piecewise_mid_ratio=0.5,
            ),
            sub_plugins=(),
        )

        mapped = plugin._map_pixel_to_stick(100.0 * plugin.config.ai_delta_gain)

        expected_ratio = 0.5 + (0.5 * ((100.0 - 80.0) / (230.0 - 80.0)))
        self.assertAlmostEqual(mapped, 32767 * expected_ratio, delta=1.0)

    def test_piecewise_mapping_hits_full_scale_at_second_breakpoint(self):
        plugin = AIAimPlugin(
            AIAimConfig(
                smoothing=0.0,
                ai_delta_gain=0.7,
                piecewise_mid_pixels=80,
                piecewise_max_pixels=230,
                piecewise_mid_ratio=0.5,
            ),
            sub_plugins=(),
        )

        mapped = plugin._map_pixel_to_stick(230.0 * plugin.config.ai_delta_gain)

        self.assertAlmostEqual(mapped, 32767.0, delta=1.0)

    def test_ai_aim_adds_right_stick_correction_when_aiming(self):
        plugin = AIAimPlugin(
            AIAimConfig(
                smoothing=0.0,
                max_pixels=150,
                deadzone_inner=0.0,
                deadzone_outer=1.0,
                x_deadzone_outer=1.0,
                ai_delta_gain=1.0,
            ),
            sub_plugins=(),
        )
        frame = _frame(aiming=True, target_dx=30.0, target_dy=-15.0, manual_rx=0, manual_ry=0)
        output = _output(frame)

        plugin.apply(frame, output)

        self.assertGreater(output.right_x, 0)
        self.assertGreater(output.right_y, 0)

    def test_ai_aim_uses_stronger_vertical_mapping_for_large_vertical_error(self):
        plugin = AIAimPlugin(
            AIAimConfig(
                smoothing=0.0,
                deadzone_inner=0.0,
                deadzone_outer=1.0,
                x_deadzone_outer=1.0,
                ai_delta_gain=1.0,
                max_ai_force=1.0,
                max_ai_force_y=1.0,
                piecewise_mid_pixels=80,
                piecewise_max_pixels=230,
                piecewise_mid_ratio=0.5,
                piecewise_mid_pixels_y=45,
                piecewise_max_pixels_y=180,
                piecewise_mid_ratio_y=0.65,
            ),
            sub_plugins=(),
        )
        frame = _frame(aiming=True, target_dx=0.0, target_dy=-80.0, manual_rx=0, manual_ry=0)
        output = _output(frame)

        plugin.apply(frame, output)

        self.assertGreater(output.right_y, 8000)

    def test_ai_aim_keeps_manual_passthrough_when_not_aiming(self):
        plugin = AIAimPlugin(AIAimConfig(smoothing=0.0), sub_plugins=())
        frame = _frame(aiming=False, target_dx=50.0, target_dy=20.0, manual_rx=4000, manual_ry=-3000)
        output = _output(frame)

        plugin.apply(frame, output)

        self.assertEqual(output.right_x, 4000)
        self.assertEqual(output.right_y, -3000)

    def test_ai_aim_applies_sub_plugins_in_declared_order(self):
        plugin = AIAimPlugin(
            AIAimConfig(
                smoothing=0.0,
                deadzone_inner=0.0,
                deadzone_outer=1.0,
                x_deadzone_outer=1.0,
                ai_delta_gain=1.0,
            ),
            sub_plugins=(_AddDxPlugin(), _ScalePlugin()),
        )
        frame = _frame(aiming=True, target_dx=20.0, target_dy=0.0, manual_rx=0, manual_ry=0)
        output = _output(frame)

        plugin.apply(frame, output)

        self.assertGreater(output.right_x, 0)
        self.assertLess(output.right_x, 32767)

    def test_opposed_manual_input_keeps_ai_correction_when_target_history_is_stable(self):
        plugin = AIAimPlugin(
            AIAimConfig(
                smoothing=0.0,
                max_pixels=100,
                piecewise_mid_pixels=0.0,
                deadzone_inner=0.0,
                deadzone_outer=1.0,
                x_deadzone_outer=1.0,
                ai_delta_gain=1.0,
                max_ai_force=1.0,
                ai_fade_full=8000,
            ),
            sub_plugins=(self.make_manual_intent_plugin(),),
        )

        for i, timestamp in enumerate((0.00, 0.02, 0.04), start=1):
            frame = _frame(
                aiming=True,
                target_dx=20.0,
                manual_rx=0,
                timestamp=timestamp,
                target_revision=i,
            )
            plugin.apply(frame, _output(frame))

        frame = _frame(
            aiming=True,
            target_dx=20.0,
            manual_rx=-8000,
            timestamp=0.06,
            target_revision=4,
        )
        output = _output(frame)

        plugin.apply(frame, output)

        self.assertGreater(output.right_x, 0)

    def test_aligned_manual_input_keeps_normal_full_fade_behavior(self):
        plugin = AIAimPlugin(
            AIAimConfig(
                smoothing=0.0,
                max_pixels=100,
                piecewise_mid_pixels=0.0,
                deadzone_inner=0.0,
                deadzone_outer=1.0,
                x_deadzone_outer=1.0,
                ai_delta_gain=1.0,
                max_ai_force=1.0,
                ai_fade_full=8000,
            ),
            sub_plugins=(self.make_manual_intent_plugin(),),
        )

        for i, timestamp in enumerate((0.00, 0.02, 0.04), start=1):
            frame = _frame(
                aiming=True,
                target_dx=20.0,
                manual_rx=0,
                timestamp=timestamp,
                target_revision=i,
            )
            plugin.apply(frame, _output(frame))

        frame = _frame(
            aiming=True,
            target_dx=20.0,
            manual_rx=8000,
            timestamp=0.06,
            target_revision=4,
        )
        output = _output(frame)

        plugin.apply(frame, output)

        self.assertEqual(output.right_x, 8000)


if __name__ == "__main__":
    unittest.main()
