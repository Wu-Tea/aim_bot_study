import unittest

from controllers.base_controller import ControllerTarget
from controllers.mouse.ai_aim import AIAimConfig, AIAimPlugin
from controllers.mouse.state import MouseFrame, MouseOutput


_DEFAULT_TARGET = object()


def _target(
    *,
    aim_point_x=332.0,
    aim_point_y=228.0,
    body_box=(288.0, 140.0, 368.0, 340.0),
    source="observed",
):
    return ControllerTarget(
        aim_point_x=aim_point_x,
        aim_point_y=aim_point_y,
        screen_center_x=320.0,
        screen_center_y=256.0,
        body_box=body_box,
        target_source=source,
    )


def _frame(
    *,
    timestamp=1.0,
    aiming=True,
    target_dx=12.0,
    target_dy=-6.0,
    manual_dx=0.0,
    manual_dy=0.0,
    manual_override_active=False,
    target=_DEFAULT_TARGET,
    target_revision=1,
    target_timestamp=None,
):
    return MouseFrame(
        timestamp=timestamp,
        manual_dx=manual_dx,
        manual_dy=manual_dy,
        is_aiming=aiming,
        target_dx=target_dx,
        target_dy=target_dy,
        auto_fire_requested=False,
        manual_override_active=manual_override_active,
        target=_target() if target is _DEFAULT_TARGET else target,
        target_revision=target_revision,
        target_timestamp=timestamp if target_timestamp is None else target_timestamp,
    )


def _magnitude(output: MouseOutput) -> float:
    return (output.move_dx ** 2 + output.move_dy ** 2) ** 0.5


class AIAimPluginTests(unittest.TestCase):
    def test_frame_stores_target_metadata(self):
        frame = _frame(target=_target(source="reconstructed"))
        self.assertEqual(frame.target.target_source, "reconstructed")

    def test_no_correction_when_not_aiming(self):
        plugin = AIAimPlugin()
        output = MouseOutput()
        plugin.apply(
            _frame(target_dx=50.0, target_dy=30.0, aiming=False, target=_target()),
            output,
        )
        self.assertAlmostEqual(output.move_dx, 0.0)
        self.assertAlmostEqual(output.move_dy, 0.0)
        self.assertEqual(plugin._mode, "manual")

    def test_no_correction_without_target_object(self):
        plugin = AIAimPlugin()
        output = MouseOutput()
        plugin.apply(_frame(target_dx=12.0, target_dy=-6.0, target=None), output)
        self.assertAlmostEqual(output.move_dx, 0.0)
        self.assertAlmostEqual(output.move_dy, 0.0)
        self.assertEqual(plugin._mode, "manual")

    def test_observed_target_starts_aggressive_acquire(self):
        plugin = AIAimPlugin()
        output = MouseOutput()
        plugin.apply(
            _frame(target_dx=73.5, target_dy=-119.9, target=_target()),
            output,
        )
        self.assertEqual(plugin._mode, "acquire_far")
        self.assertGreater(_magnitude(output), 4.0)

    def test_observed_target_after_long_ads_still_acquires(self):
        plugin = AIAimPlugin()
        plugin.apply(_frame(timestamp=1.0, target=None), MouseOutput())

        output = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.40,
                target_dx=73.5,
                target_dy=-119.9,
                target=_target(),
            ),
            output,
        )
        self.assertEqual(plugin._mode, "acquire_far")
        self.assertGreater(_magnitude(output), 4.0)

    def test_reconstructed_target_can_start_acquire_from_manual(self):
        plugin = AIAimPlugin()
        output = MouseOutput()
        plugin.apply(
            _frame(
                target_dx=68.0,
                target_dy=-82.0,
                target=_target(source="reconstructed"),
            ),
            output,
        )
        self.assertEqual(plugin._mode, "acquire_far")
        self.assertGreater(_magnitude(output), 4.0)

    def test_midrange_observed_target_uses_mid_acquire(self):
        plugin = AIAimPlugin()
        output = MouseOutput()
        plugin.apply(
            _frame(target_dx=34.0, target_dy=-18.0, target=_target()),
            output,
        )
        self.assertEqual(plugin._mode, "acquire_mid")
        self.assertEqual(plugin._last_follow_profile_name, "chase")
        self.assertGreater(_magnitude(output), 1.5)
        self.assertLess(
            _magnitude(output),
            plugin.config.acquire_max_move_px + 0.01,
        )

    def test_predicted_target_does_not_start_fresh_acquire(self):
        plugin = AIAimPlugin()
        output = MouseOutput()
        plugin.apply(
            _frame(
                target_dx=68.0,
                target_dy=-82.0,
                target=_target(source="predicted"),
            ),
            output,
        )
        self.assertEqual(plugin._mode, "manual")
        self.assertEqual((output.move_dx, output.move_dy), (0.0, 0.0))

    def test_close_observed_target_enters_stabilize(self):
        plugin = AIAimPlugin()
        output = MouseOutput()
        plugin.apply(_frame(target_dx=6.0, target_dy=-4.0, target=_target()), output)
        self.assertEqual(plugin._mode, "stabilize")
        self.assertEqual(plugin._last_follow_profile_name, "control")
        self.assertNotEqual((output.move_dx, output.move_dy), (0.0, 0.0))
        self.assertLessEqual(_magnitude(output), plugin.config.stabilize_max_move_px + 0.01)

    def test_midrange_target_does_not_enter_stabilize_too_early(self):
        plugin = AIAimPlugin()
        output = MouseOutput()
        plugin.apply(_frame(target_dx=15.0, target_dy=-8.0, target=_target()), output)
        self.assertEqual(plugin._mode, "acquire_mid")
        self.assertGreater(_magnitude(output), plugin.config.stabilize_max_move_px)

    def test_stabilize_keeps_helping_inside_inner_release_band(self):
        plugin = AIAimPlugin()
        plugin.apply(_frame(target_dx=6.0, target_dy=-4.0, target=_target()), MouseOutput())
        self.assertEqual(plugin._mode, "stabilize")

        output = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.05,
                target_dx=1.2,
                target_dy=0.4,
                target=_target(),
            ),
            output,
        )

        self.assertEqual(plugin._mode, "stabilize")
        self.assertNotEqual((output.move_dx, output.move_dy), (0.0, 0.0))

    def test_outer_stabilize_uses_balanced_follow_profile(self):
        plugin = AIAimPlugin(
            AIAimConfig(
                stabilize_enter_px=18.0,
                stabilize_exit_px=18.0,
            )
        )

        output = MouseOutput()
        plugin.apply(_frame(target_dx=14.0, target_dy=-4.0, target=_target()), output)

        self.assertEqual(plugin._mode, "stabilize")
        self.assertEqual(plugin._last_follow_profile_name, "balanced")
        self.assertNotEqual((output.move_dx, output.move_dy), (0.0, 0.0))

    def test_balanced_follow_profile_can_boost_outer_stabilize_output(self):
        neutral = AIAimPlugin(
            AIAimConfig(
                stabilize_enter_px=18.0,
                stabilize_exit_px=18.0,
                stabilize_gain=0.20,
                stabilize_max_move_px=10.0,
                stabilize_response_horizon_s=0.020,
                response_accel_multiplier=5.0,
                stabilize_error_rate_gain=0.0,
                follow_control_radius_px=8.0,
                follow_chase_radius_px=30.0,
                follow_balanced_gain_scale=1.0,
                follow_balanced_max_move_scale=1.0,
                follow_balanced_horizon_scale=1.0,
                follow_balanced_accel_scale=1.0,
                follow_balanced_error_rate_scale=1.0,
            )
        )
        boosted = AIAimPlugin(
            AIAimConfig(
                stabilize_enter_px=18.0,
                stabilize_exit_px=18.0,
                stabilize_gain=0.20,
                stabilize_max_move_px=10.0,
                stabilize_response_horizon_s=0.020,
                response_accel_multiplier=5.0,
                stabilize_error_rate_gain=0.0,
                follow_control_radius_px=8.0,
                follow_chase_radius_px=30.0,
                follow_balanced_gain_scale=1.35,
                follow_balanced_max_move_scale=1.20,
                follow_balanced_horizon_scale=0.75,
                follow_balanced_accel_scale=1.30,
                follow_balanced_error_rate_scale=1.0,
            )
        )

        neutral_out = MouseOutput()
        boosted_out = MouseOutput()
        neutral.apply(_frame(target_dx=14.0, target_dy=0.0, target=_target()), neutral_out)
        boosted.apply(_frame(target_dx=14.0, target_dy=0.0, target=_target()), boosted_out)

        self.assertGreater(boosted_out.move_dx, neutral_out.move_dx)

    def test_close_moving_target_gets_stronger_stabilize_than_static_hold(self):
        plugin = AIAimPlugin()
        plugin.apply(
            _frame(
                timestamp=1.00,
                target_dx=4.0,
                target_dy=-1.0,
                target=_target(
                    aim_point_x=324.0,
                    aim_point_y=255.0,
                    body_box=(284.0, 175.0, 364.0, 335.0),
                ),
            ),
            MouseOutput(),
        )
        self.assertEqual(plugin._mode, "stabilize")

        plugin.apply(
            _frame(
                timestamp=1.05,
                target_dx=0.0,
                target_dy=-1.0,
                target=_target(
                    aim_point_x=320.0,
                    aim_point_y=255.0,
                    body_box=(280.0, 175.0, 360.0, 335.0),
                ),
            ),
            MouseOutput(),
        )
        self.assertEqual(plugin._mode, "stabilize")

        output = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.10,
                target_dx=2.0,
                target_dy=-1.0,
                target=_target(
                    aim_point_x=322.0,
                    aim_point_y=255.0,
                    body_box=(282.0, 175.0, 362.0, 335.0),
                ),
            ),
            output,
        )

        self.assertEqual(plugin._mode, "stabilize")
        self.assertGreater(_magnitude(output), 0.9)

    def test_motion_boost_still_respects_inner_release_scaling(self):
        plugin = AIAimPlugin()
        plugin.apply(
            _frame(
                timestamp=1.00,
                target_dx=4.0,
                target_dy=-1.0,
                target=_target(
                    aim_point_x=324.0,
                    aim_point_y=255.0,
                    body_box=(284.0, 175.0, 364.0, 335.0),
                ),
            ),
            MouseOutput(),
        )
        plugin.apply(
            _frame(
                timestamp=1.05,
                target_dx=0.0,
                target_dy=-1.0,
                target=_target(
                    aim_point_x=320.0,
                    aim_point_y=255.0,
                    body_box=(280.0, 175.0, 360.0, 335.0),
                ),
            ),
            MouseOutput(),
        )

        output = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.10,
                target_dx=2.0,
                target_dy=-1.0,
                target=_target(
                    aim_point_x=322.0,
                    aim_point_y=255.0,
                    body_box=(282.0, 175.0, 362.0, 335.0),
                ),
            ),
            output,
        )

        self.assertEqual(plugin._mode, "stabilize")
        self.assertLess(_magnitude(output), 2.5)

    def test_motion_boost_does_not_apply_after_visibility_gap(self):
        plugin = AIAimPlugin()
        plugin.apply(
            _frame(
                timestamp=1.00,
                target_dx=4.0,
                target_dy=-1.0,
                target=_target(
                    aim_point_x=324.0,
                    aim_point_y=255.0,
                    body_box=(284.0, 175.0, 364.0, 335.0),
                ),
            ),
            MouseOutput(),
        )
        plugin.apply(
            _frame(timestamp=1.04, target_dx=0.0, target_dy=0.0, target=None),
            MouseOutput(),
        )

        output = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.16,
                target_dx=2.0,
                target_dy=-1.0,
                target=_target(
                    aim_point_x=322.0,
                    aim_point_y=255.0,
                    body_box=(282.0, 175.0, 362.0, 335.0),
                ),
            ),
            output,
        )

        self.assertEqual(plugin._mode, "stabilize")
        self.assertLess(_magnitude(output), 0.5)

    def test_motion_boost_is_reserved_for_axis_dominant_tracking(self):
        plugin = AIAimPlugin()
        plugin.apply(
            _frame(
                timestamp=1.00,
                target_dx=4.0,
                target_dy=-4.0,
                target=_target(
                    aim_point_x=324.0,
                    aim_point_y=252.0,
                    body_box=(284.0, 172.0, 364.0, 332.0),
                ),
            ),
            MouseOutput(),
        )

        output = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.05,
                target_dx=2.0,
                target_dy=-2.0,
                target=_target(
                    aim_point_x=322.0,
                    aim_point_y=254.0,
                    body_box=(282.0, 174.0, 362.0, 334.0),
                ),
            ),
            output,
        )

        self.assertEqual(plugin._mode, "stabilize")
        self.assertLess(_magnitude(output), 0.85)

    def test_acquire_ignores_orthogonal_manual_wobble(self):
        plugin = AIAimPlugin()
        output = MouseOutput()
        plugin.apply(
            _frame(
                target_dx=73.5,
                target_dy=-119.9,
                manual_dx=8.0,
                manual_dy=8.0,
                target=_target(),
            ),
            output,
        )
        self.assertEqual(plugin._mode, "acquire_far")
        self.assertGreater(_magnitude(output), 4.0)

    def test_strong_breakaway_returns_to_manual(self):
        plugin = AIAimPlugin()
        plugin.apply(_frame(target_dx=6.0, target_dy=-4.0), MouseOutput())
        self.assertEqual(plugin._mode, "stabilize")

        output = MouseOutput()
        plugin.apply(
            _frame(target_dx=5.0, target_dy=0.0, manual_dx=-30.0, target=_target()),
            output,
        )
        self.assertEqual(plugin._mode, "manual")
        self.assertEqual((output.move_dx, output.move_dy), (0.0, 0.0))

    def test_manual_override_forces_manual_even_when_drag_matches_target_direction(self):
        plugin = AIAimPlugin()
        plugin.apply(_frame(target_dx=6.0, target_dy=-4.0), MouseOutput())
        self.assertEqual(plugin._mode, "stabilize")

        output = MouseOutput()
        plugin.apply(
            _frame(
                target_dx=5.0,
                target_dy=0.0,
                manual_dx=30.0,
                manual_override_active=True,
                target=_target(),
            ),
            output,
        )

        self.assertEqual(plugin._mode, "manual")
        self.assertEqual((output.move_dx, output.move_dy), (0.0, 0.0))

    def test_reset_clears_mode_and_session_state(self):
        plugin = AIAimPlugin()
        plugin.apply(_frame(target_dx=6.0, target_dy=-4.0), MouseOutput())
        self.assertEqual(plugin._mode, "stabilize")

        plugin.reset()

        self.assertEqual(plugin._mode, "manual")
        self.assertIsNone(plugin._stabilize_until)
        self.assertIsNone(plugin._last_target)

    def test_same_target_reacquire_uses_short_boosted_reacquire_mode(self):
        plugin = AIAimPlugin()
        plugin.apply(
            _frame(
                timestamp=1.0,
                target_dx=6.0,
                target_dy=-4.0,
                target=_target(),
            ),
            MouseOutput(),
        )
        self.assertEqual(plugin._mode, "stabilize")

        plugin.apply(
            _frame(timestamp=1.04, target_dx=0.0, target_dy=0.0, target=None),
            MouseOutput(),
        )

        output = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.08,
                target_dx=44.0,
                target_dy=-22.0,
                target=_target(),
            ),
            output,
        )

        self.assertEqual(plugin._mode, "reacquire")
        self.assertGreater(_magnitude(output), 3.0)

    def test_mid_acquire_gap_can_reenter_reacquire_without_prior_stabilize(self):
        plugin = AIAimPlugin()
        plugin.apply(
            _frame(
                timestamp=1.00,
                target_dx=34.0,
                target_dy=-18.0,
                target=_target(
                    aim_point_x=354.0,
                    aim_point_y=238.0,
                    body_box=(314.0, 158.0, 394.0, 338.0),
                ),
            ),
            MouseOutput(),
        )
        self.assertEqual(plugin._mode, "acquire_mid")

        plugin.apply(
            _frame(
                timestamp=1.05,
                target_dx=0.0,
                target_dy=0.0,
                target=None,
            ),
            MouseOutput(),
        )

        output = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.10,
                target_dx=42.0,
                target_dy=-20.0,
                target=_target(
                    aim_point_x=362.0,
                    aim_point_y=236.0,
                    body_box=(322.0, 156.0, 402.0, 336.0),
                ),
            ),
            output,
        )

        self.assertEqual(plugin._mode, "reacquire")
        self.assertGreater(_magnitude(output), 3.0)

    def test_same_target_reacquire_survives_benchmark_sized_gap(self):
        plugin = AIAimPlugin()
        plugin.apply(
            _frame(
                timestamp=1.00,
                target_dx=34.0,
                target_dy=-18.0,
                target=_target(
                    aim_point_x=354.0,
                    aim_point_y=238.0,
                    body_box=(314.0, 158.0, 394.0, 338.0),
                ),
            ),
            MouseOutput(),
        )
        self.assertEqual(plugin._mode, "acquire_mid")

        plugin.apply(
            _frame(
                timestamp=1.06,
                target_dx=0.0,
                target_dy=0.0,
                target=None,
            ),
            MouseOutput(),
        )

        output = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.18,
                target_dx=46.0,
                target_dy=-22.0,
                target=_target(
                    aim_point_x=366.0,
                    aim_point_y=234.0,
                    body_box=(326.0, 154.0, 406.0, 334.0),
                ),
            ),
            output,
        )

        self.assertEqual(plugin._mode, "reacquire")
        self.assertGreater(_magnitude(output), 3.0)

    def test_far_acquire_adds_motion_lead_for_same_target_family(self):
        plugin = AIAimPlugin(
            AIAimConfig(
                acquire_gain=1.0,
                acquire_max_move_px=100.0,
                acquire_lead_seconds=0.03,
                acquire_lead_max_px=20.0,
            )
        )
        plugin.apply(
            _frame(
                timestamp=1.00,
                target_dx=50.0,
                target_dy=0.0,
                target=_target(
                    aim_point_x=370.0,
                    aim_point_y=256.0,
                    body_box=(330.0, 180.0, 410.0, 340.0),
                ),
            ),
            MouseOutput(),
        )

        output = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.05,
                target_dx=54.0,
                target_dy=0.0,
                target=_target(
                    aim_point_x=376.0,
                    aim_point_y=256.0,
                    body_box=(336.0, 180.0, 416.0, 340.0),
                ),
            ),
            output,
        )

        self.assertEqual(plugin._mode, "acquire_far")
        self.assertGreater(output.move_dx, 54.0)

    def test_same_target_revision_continues_with_rate_limited_trajectory(self):
        plugin = AIAimPlugin()
        moves = []

        for tick in range(7):
            output = MouseOutput()
            plugin.apply(
                _frame(
                    timestamp=1.000 + tick * 0.001,
                    target_dx=80.0,
                    target_dy=0.0,
                    target_revision=7,
                    target_timestamp=1.000,
                    target=_target(
                        aim_point_x=400.0,
                        aim_point_y=256.0,
                        body_box=(360.0, 176.0, 440.0, 336.0),
                    ),
                ),
                output,
            )
            moves.append(output.move_dx)

        self.assertEqual(plugin._mode, "acquire_far")
        self.assertGreater(sum(moves), moves[0])
        self.assertLess(sum(moves), 60.0)
        self.assertLess(max(moves), 10.0)
        self.assertTrue(all(move >= 0.0 for move in moves))

    def test_new_target_revision_can_emit_new_mouse_move(self):
        plugin = AIAimPlugin()

        plugin.apply(
            _frame(
                timestamp=1.00,
                target_dx=80.0,
                target_dy=0.0,
                target_revision=7,
                target_timestamp=1.00,
                target=_target(
                    aim_point_x=400.0,
                    aim_point_y=256.0,
                    body_box=(360.0, 176.0, 440.0, 336.0),
                ),
            ),
            MouseOutput(),
        )

        output = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.008,
                target_dx=64.0,
                target_dy=0.0,
                target_revision=8,
                target_timestamp=1.008,
                target=_target(
                    aim_point_x=384.0,
                    aim_point_y=256.0,
                    body_box=(344.0, 176.0, 424.0, 336.0),
                ),
            ),
            output,
        )

        self.assertEqual(plugin._mode, "acquire_far")
        self.assertGreater(output.move_dx, 0.0)

    def test_mid_acquire_stall_builds_extra_push_when_error_stops_shrinking(self):
        plugin = AIAimPlugin(
            AIAimConfig(
                mid_acquire_gain=0.5,
                mid_acquire_max_move_px=20.0,
                acquire_stall_trigger_frames=1,
                acquire_stall_gain_per_frame=0.25,
                acquire_stall_decay_per_frame=0.0,
                acquire_stall_max_bonus=0.5,
            )
        )
        plugin.apply(
            _frame(
                timestamp=1.00,
                target_dx=36.0,
                target_dy=0.0,
                target=_target(
                    aim_point_x=356.0,
                    aim_point_y=256.0,
                    body_box=(316.0, 180.0, 396.0, 340.0),
                ),
            ),
            MouseOutput(),
        )

        first = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.05,
                target_dx=35.8,
                target_dy=0.0,
                target=_target(
                    aim_point_x=355.8,
                    aim_point_y=256.0,
                    body_box=(315.8, 180.0, 395.8, 340.0),
                ),
            ),
            first,
        )

        second = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.10,
                target_dx=35.7,
                target_dy=0.0,
                target=_target(
                    aim_point_x=355.7,
                    aim_point_y=256.0,
                    body_box=(315.7, 180.0, 395.7, 340.0),
                ),
            ),
            second,
        )

        self.assertEqual(plugin._mode, "acquire_mid")
        self.assertGreater(second.move_dx, first.move_dx)

    def test_target_gap_clears_acquire_stall_bonus_before_reacquire(self):
        plugin = AIAimPlugin(
            AIAimConfig(
                mid_acquire_gain=0.5,
                mid_acquire_max_move_px=20.0,
                reacquire_gain=1.0,
                reacquire_max_move_px=100.0,
                acquire_stall_trigger_frames=1,
                acquire_stall_gain_per_frame=0.25,
                acquire_stall_decay_per_frame=0.0,
                acquire_stall_max_bonus=0.5,
            )
        )
        plugin.apply(
            _frame(
                timestamp=1.00,
                target_dx=36.0,
                target_dy=0.0,
                target=_target(
                    aim_point_x=356.0,
                    aim_point_y=256.0,
                    body_box=(316.0, 180.0, 396.0, 340.0),
                ),
            ),
            MouseOutput(),
        )
        plugin.apply(
            _frame(
                timestamp=1.05,
                target_dx=35.8,
                target_dy=0.0,
                target=_target(
                    aim_point_x=355.8,
                    aim_point_y=256.0,
                    body_box=(315.8, 180.0, 395.8, 340.0),
                ),
            ),
            MouseOutput(),
        )
        self.assertGreater(plugin._acquire_bonus, 0.0)

        plugin.apply(
            _frame(timestamp=1.08, target_dx=0.0, target_dy=0.0, target=None),
            MouseOutput(),
        )

        self.assertEqual(plugin._acquire_bonus, 0.0)
        self.assertIsNone(plugin._last_acquire_radius)


if __name__ == "__main__":
    unittest.main()
