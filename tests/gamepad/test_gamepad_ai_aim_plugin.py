import unittest

from controllers.base_controller import ControllerTarget
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
    target=None,
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
        target=target,
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


def _target(
    *,
    aim_point_x,
    aim_point_y,
    screen_center_x=320.0,
    screen_center_y=256.0,
    body_box=None,
):
    return ControllerTarget(
        aim_point_x=aim_point_x,
        aim_point_y=aim_point_y,
        screen_center_x=screen_center_x,
        screen_center_y=screen_center_y,
        body_box=body_box,
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

    def test_default_plugin_only_snaps_once_per_ads_session(self):
        plugin = AIAimPlugin(
            AIAimConfig(
                smoothing=0.0,
                deadzone_inner=0.0,
                deadzone_outer=1.0,
                x_deadzone_outer=1.0,
                ai_delta_gain=1.0,
            )
        )
        target = _target(
            aim_point_x=380.0,
            aim_point_y=220.0,
            body_box=(360.0, 120.0, 420.0, 320.0),
        )

        first = _frame(
            aiming=True,
            target_dx=60.0,
            target_dy=-36.0,
            timestamp=1.00,
            target_revision=1,
            target=target,
        )
        second = _frame(
            aiming=True,
            target_dx=55.0,
            target_dy=-36.0,
            timestamp=1.04,
            target_revision=2,
            target=target,
        )
        third = _frame(
            aiming=True,
            target_dx=50.0,
            target_dy=-36.0,
            timestamp=1.14,
            target_revision=3,
            target=target,
        )
        fourth = _frame(
            aiming=False,
            target_dx=0.0,
            target_dy=0.0,
            timestamp=1.20,
            target_revision=4,
        )
        fifth = _frame(
            aiming=True,
            target_dx=55.0,
            target_dy=-36.0,
            timestamp=1.24,
            target_revision=5,
            target=target,
        )

        first_output = _output(first)
        second_output = _output(second)
        third_output = _output(third)
        fifth_output = _output(fifth)

        plugin.apply(first, first_output)
        plugin.apply(second, second_output)
        plugin.apply(third, third_output)
        plugin.apply(fourth, _output(fourth))
        plugin.apply(fifth, fifth_output)

        self.assertGreater(first_output.right_x, 0)
        self.assertGreater(second_output.right_x, 0)
        self.assertEqual(third_output.right_x, 0)
        self.assertGreater(fifth_output.right_x, 0)

    def test_default_plugin_cancels_ads_snap_when_target_switches_during_snap_window(self):
        plugin = AIAimPlugin(
            AIAimConfig(
                smoothing=0.0,
                deadzone_inner=0.0,
                deadzone_outer=1.0,
                x_deadzone_outer=1.0,
                ai_delta_gain=1.0,
            )
        )
        first_target = _target(
            aim_point_x=440.0,
            aim_point_y=256.0,
            body_box=(398.0, 188.0, 482.0, 368.0),
        )
        switched_target = _target(
            aim_point_x=180.0,
            aim_point_y=256.0,
            body_box=(138.0, 188.0, 222.0, 368.0),
        )

        first = _frame(
            aiming=True,
            target_dx=120.0,
            target_dy=0.0,
            timestamp=1.00,
            target_revision=1,
            target=first_target,
        )
        second = _frame(
            aiming=True,
            target_dx=120.0,
            target_dy=0.0,
            timestamp=1.00 + (1.0 / 60.0),
            target_revision=2,
            target=first_target,
        )
        third = _frame(
            aiming=True,
            target_dx=-140.0,
            target_dy=0.0,
            timestamp=1.00 + (2.0 / 60.0),
            target_revision=3,
            target=switched_target,
        )

        first_output = _output(first)
        second_output = _output(second)
        third_output = _output(third)

        plugin.apply(first, first_output)
        plugin.apply(second, second_output)
        plugin.apply(third, third_output)

        self.assertGreater(first_output.right_x, 0)
        self.assertGreater(second_output.right_x, 0)
        self.assertEqual(third_output.right_x, 0)

    def test_default_plugin_body_lock_requires_near_target_region_and_center_activation_window(self):
        lock_plugin = AIAimPlugin(
            AIAimConfig(
                smoothing=0.0,
                deadzone_inner=0.0,
                deadzone_outer=1.0,
                x_deadzone_outer=1.0,
                ai_delta_gain=1.0,
            )
        )
        outside_plugin = AIAimPlugin(
            AIAimConfig(
                smoothing=0.0,
                deadzone_inner=0.0,
                deadzone_outer=1.0,
                x_deadzone_outer=1.0,
                ai_delta_gain=1.0,
            )
        )
        lock_target = _target(
            aim_point_x=350.0,
            aim_point_y=220.0,
            body_box=(290.0, 150.0, 370.0, 320.0),
        )
        outside_activation_target = _target(
            aim_point_x=440.0,
            aim_point_y=220.0,
            body_box=(300.0, 150.0, 560.0, 320.0),
        )

        lock_frame = _frame(
            aiming=True,
            target_dx=30.0,
            target_dy=-36.0,
            timestamp=2.00,
            target_revision=1,
            target=lock_target,
        )
        outside_frame = _frame(
            aiming=True,
            target_dx=120.0,
            target_dy=-36.0,
            timestamp=3.20,
            target_revision=2,
            target=outside_activation_target,
        )
        warmup_frame = _frame(
            aiming=True,
            target_dx=120.0,
            target_dy=-36.0,
            timestamp=3.00,
            target_revision=1,
            target=outside_activation_target,
        )
        lock_output = _output(lock_frame)
        outside_output = _output(outside_frame)

        lock_plugin.apply(lock_frame, lock_output)
        outside_plugin.apply(warmup_frame, _output(warmup_frame))
        outside_plugin.apply(outside_frame, outside_output)

        self.assertNotEqual((lock_output.right_x, lock_output.right_y), (0, 0))
        self.assertEqual((outside_output.right_x, outside_output.right_y), (0, 0))

    def test_default_plugin_can_lead_a_matched_body_lock_target_after_multiple_frames(self):
        lead_plugin = AIAimPlugin(
            AIAimConfig(
                smoothing=0.0,
                deadzone_inner=0.0,
                deadzone_outer=1.0,
                x_deadzone_outer=1.0,
                ai_delta_gain=1.0,
                body_lock_lead_frames=2,
                body_lock_lead_seconds=0.05,
                body_lock_lead_max_px=16.0,
            )
        )
        no_lead_plugin = AIAimPlugin(
            AIAimConfig(
                smoothing=0.0,
                deadzone_inner=0.0,
                deadzone_outer=1.0,
                x_deadzone_outer=1.0,
                ai_delta_gain=1.0,
                body_lock_lead_frames=99,
                body_lock_lead_seconds=0.0,
                body_lock_lead_max_px=0.0,
            )
        )
        sequence = (
            _frame(
                aiming=True,
                target_dx=0.0,
                target_dy=-36.0,
                timestamp=4.00,
                target_revision=1,
                target=_target(
                    aim_point_x=320.0,
                    aim_point_y=220.0,
                    body_box=(280.0, 150.0, 360.0, 320.0),
                ),
            ),
            _frame(
                aiming=True,
                target_dx=10.0,
                target_dy=-36.0,
                timestamp=4.02,
                target_revision=2,
                target=_target(
                    aim_point_x=330.0,
                    aim_point_y=220.0,
                    body_box=(290.0, 150.0, 370.0, 320.0),
                ),
            ),
            _frame(
                aiming=True,
                target_dx=20.0,
                target_dy=-36.0,
                timestamp=4.04,
                target_revision=3,
                target=_target(
                    aim_point_x=340.0,
                    aim_point_y=220.0,
                    body_box=(300.0, 150.0, 380.0, 320.0),
                ),
            ),
        )

        lead_output = None
        no_lead_output = None
        for frame in sequence:
            lead_output = _output(frame)
            no_lead_output = _output(frame)
            lead_plugin.apply(frame, lead_output)
            no_lead_plugin.apply(frame, no_lead_output)

        self.assertIsNotNone(lead_output)
        self.assertIsNotNone(no_lead_output)
        self.assertGreater(lead_output.right_x, no_lead_output.right_x)

    def test_default_plugin_body_lock_ignores_tiny_vertical_error_near_lock_point(self):
        plugin = AIAimPlugin(
            AIAimConfig(
                smoothing=0.0,
                deadzone_inner=0.0,
                deadzone_outer=1.0,
                x_deadzone_outer=1.0,
                ai_delta_gain=1.0,
            )
        )
        frame = _frame(
            aiming=True,
            target_dx=10.0,
            target_dy=-2.5,
            timestamp=5.00,
            target_revision=1,
            target=_target(
                aim_point_x=330.0,
                aim_point_y=253.5,
                body_box=(290.0, 185.1, 370.0, 365.1),
            ),
        )
        output = _output(frame)

        plugin.apply(frame, output)

        self.assertGreater(output.right_x, 0)
        self.assertEqual(output.right_y, 0)

    def test_default_plugin_restores_some_vertical_tail_help_after_motion_history(self):
        plugin = AIAimPlugin(
            AIAimConfig(
                smoothing=0.0,
                deadzone_inner=0.0,
                deadzone_outer=1.0,
                x_deadzone_outer=1.0,
                ai_delta_gain=1.0,
            )
        )
        frame = _frame(
            aiming=True,
            target_dx=10.0,
            target_dy=-4.0,
            timestamp=5.00,
            target_revision=1,
            target=_target(
                aim_point_x=330.0,
                aim_point_y=252.0,
                body_box=(290.0, 183.6, 370.0, 363.6),
            ),
        )
        first_output = _output(frame)
        plugin.apply(frame, first_output)

        follow_up = _frame(
            aiming=True,
            target_dx=10.0,
            target_dy=-4.0,
            timestamp=5.00 + (1.0 / 60.0),
            target_revision=2,
            target=_target(
                aim_point_x=330.0,
                aim_point_y=252.0,
                body_box=(290.0, 183.6, 370.0, 363.6),
            ),
        )
        second_output = _output(follow_up)
        plugin.apply(follow_up, second_output)

        self.assertEqual(first_output.right_y, 0)
        self.assertNotEqual(second_output.right_y, 0)


if __name__ == "__main__":
    unittest.main()
