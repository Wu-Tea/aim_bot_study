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
    manual_left_pressed=False,
    manual_override_active=False,
    target=_DEFAULT_TARGET,
    target_revision=1,
    target_timestamp=None,
    response_input_scale=1.0,
):
    return MouseFrame(
        timestamp=timestamp,
        manual_dx=manual_dx,
        manual_dy=manual_dy,
        is_aiming=aiming,
        target_dx=target_dx,
        target_dy=target_dy,
        auto_fire_requested=False,
        manual_left_pressed=manual_left_pressed,
        manual_override_active=manual_override_active,
        target=_target() if target is _DEFAULT_TARGET else target,
        target_revision=target_revision,
        target_timestamp=timestamp if target_timestamp is None else target_timestamp,
        response_input_scale=response_input_scale,
    )


def _magnitude(output: MouseOutput) -> float:
    return (output.move_dx ** 2 + output.move_dy ** 2) ** 0.5


def _sign(value: float) -> int:
    if value > 0.0:
        return 1
    if value < 0.0:
        return -1
    return 0


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

    def test_initial_ads_snap_window_uses_fast_snap_phase(self):
        plugin = AIAimPlugin(
            AIAimConfig(
                snap_window_seconds=0.140,
                snap_gain=1.0,
                snap_max_move_px=22.0,
                snap_response_horizon_s=0.010,
                response_accel_multiplier=3.0,
            )
        )

        output = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.000,
                target_dx=72.0,
                target_dy=0.0,
                target=_target(
                    aim_point_x=392.0,
                    aim_point_y=256.0,
                    body_box=(352.0, 180.0, 432.0, 340.0),
                ),
            ),
            output,
        )

        self.assertEqual(plugin._control_phase, "snap")
        self.assertEqual(plugin._mode, "acquire_far")
        self.assertLessEqual(plugin._last_response_horizon_seconds_value, 0.010)
        self.assertGreater(output.move_dx, 8.0)

    def test_snap_new_observations_seed_servo_from_latest_error(self):
        plugin = AIAimPlugin(
            AIAimConfig(
                snap_window_seconds=0.140,
                snap_gain=1.60,
                snap_max_move_px=160.0,
                snap_response_horizon_s=0.090,
            )
        )

        moves = []
        remaining = []
        for index, error in enumerate((72.0, 52.0, 34.0), start=1):
            output = MouseOutput()
            plugin.apply(
                _frame(
                    timestamp=1.000 + (index - 1) * (1.0 / 120.0),
                    target_dx=error,
                    target_dy=0.0,
                    target_revision=index,
                    target_timestamp=1.000 + (index - 1) * (1.0 / 120.0),
                    target=_target(
                        aim_point_x=320.0 + error,
                        aim_point_y=256.0,
                        body_box=(280.0 + error, 180.0, 360.0 + error, 340.0),
                    ),
                ),
                output,
            )
            moves.append(output.move_dx)
            remaining.append(plugin._snap_remaining_dx)

        self.assertEqual(plugin._control_phase, "snap")
        self.assertGreater(moves[0], 4.0)
        self.assertGreater(remaining[0], remaining[1])
        self.assertGreater(remaining[1], remaining[2])
        self.assertLess(remaining[0], plugin.config.snap_max_move_px)

    def test_snap_continues_servo_between_slow_vision_observations(self):
        plugin = AIAimPlugin(
            AIAimConfig(
                snap_window_seconds=0.140,
                snap_gain=1.60,
                snap_max_move_px=160.0,
                snap_response_horizon_s=0.090,
            )
        )

        first = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.000,
                target_dx=72.0,
                target_dy=0.0,
                target_revision=1,
                target_timestamp=1.000,
                target=_target(
                    aim_point_x=392.0,
                    aim_point_y=256.0,
                    body_box=(352.0, 180.0, 432.0, 340.0),
                ),
            ),
            first,
        )

        repeated_moves = []
        for tick in range(1, 7):
            output = MouseOutput()
            plugin.apply(
                _frame(
                    timestamp=1.000 + tick * 0.001,
                    target_dx=72.0,
                    target_dy=0.0,
                    target_revision=1,
                    target_timestamp=1.000,
                    target=_target(
                        aim_point_x=392.0 - first.move_dx,
                        aim_point_y=256.0,
                        body_box=(
                            352.0 - first.move_dx,
                            180.0,
                            432.0 - first.move_dx,
                            340.0,
                        ),
                    ),
                ),
                output,
            )
            repeated_moves.append(output.move_dx)

        self.assertEqual(plugin._control_phase, "snap")
        self.assertGreater(first.move_dx, 4.0)
        self.assertGreater(sum(repeated_moves), 3.0)
        self.assertTrue(all(move > 0.0 for move in repeated_moves))

    def test_snap_waits_for_first_target_after_ads_animation(self):
        plugin = AIAimPlugin(
            AIAimConfig(
                snap_window_seconds=0.140,
                snap_activation_grace_seconds=0.300,
                snap_gain=1.60,
                snap_max_move_px=160.0,
                snap_response_horizon_s=0.090,
            )
        )
        plugin.apply(_frame(timestamp=1.000, target=None), MouseOutput())

        output = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.200,
                target_dx=72.0,
                target_dy=0.0,
                target_revision=1,
                target_timestamp=1.200,
                target=_target(
                    aim_point_x=392.0,
                    aim_point_y=256.0,
                    body_box=(352.0, 180.0, 432.0, 340.0),
                ),
            ),
            output,
        )

        self.assertEqual(plugin._control_phase, "snap")
        self.assertGreater(output.move_dx, 4.0)

    def test_snap_output_scales_with_measured_mouse_response(self):
        config = AIAimConfig(
            snap_window_seconds=0.140,
            snap_gain=1.0,
            snap_max_move_px=160.0,
            snap_response_horizon_s=1.0 / 140.0,
        )

        def run_once(response_input_scale):
            plugin = AIAimPlugin(config)
            output = MouseOutput()
            plugin.apply(
                _frame(
                    timestamp=1.000,
                    target_dx=72.0,
                    target_dy=0.0,
                    target=_target(
                        aim_point_x=392.0,
                        aim_point_y=256.0,
                        body_box=(352.0, 180.0, 432.0, 340.0),
                    ),
                    response_input_scale=response_input_scale,
                ),
                output,
            )
            return output.move_dx

        unscaled = run_once(1.0)
        scaled = run_once(3.0)

        self.assertGreater(unscaled, 1.0)
        self.assertAlmostEqual(scaled, unscaled * 3.0, delta=0.001)

    def test_manual_left_click_reopens_snap_window_during_existing_ads(self):
        plugin = AIAimPlugin(AIAimConfig(snap_window_seconds=0.140))
        plugin.apply(_frame(timestamp=1.000, target=None), MouseOutput())

        output = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.200,
                manual_left_pressed=True,
                target_dx=72.0,
                target_dy=0.0,
                target=_target(
                    aim_point_x=392.0,
                    aim_point_y=256.0,
                    body_box=(352.0, 180.0, 432.0, 340.0),
                ),
            ),
            output,
        )

        self.assertEqual(plugin._control_phase, "snap")
        self.assertEqual(plugin._mode, "acquire_far")
        self.assertGreater(output.move_dx, 8.0)

    def test_snap_transitions_to_body_lock_when_inside_body_window(self):
        plugin = AIAimPlugin(AIAimConfig(snap_window_seconds=0.140))
        plugin.apply(
            _frame(
                timestamp=1.000,
                target_dx=64.0,
                target_dy=0.0,
                target=_target(
                    aim_point_x=384.0,
                    aim_point_y=256.0,
                    body_box=(344.0, 180.0, 424.0, 340.0),
                ),
            ),
            MouseOutput(),
        )

        output = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.145,
                target_dx=8.0,
                target_dy=0.0,
                target=_target(
                    aim_point_x=328.0,
                    aim_point_y=256.0,
                    body_box=(288.0, 180.0, 368.0, 340.0),
                ),
            ),
            output,
        )

        self.assertEqual(plugin._control_phase, "body_lock")
        self.assertEqual(plugin._mode, "stabilize")
        self.assertNotEqual((output.move_dx, output.move_dy), (0.0, 0.0))

    def test_body_lock_manual_mix_suppresses_small_target_flip_jitter(self):
        plugin = AIAimPlugin(
            AIAimConfig(
                snap_window_seconds=0.0,
                body_lock_deadband_px=2.0,
                body_lock_manual_dampen_speed_px=1.0,
                body_lock_manual_scale=0.25,
            )
        )
        target = _target(
            aim_point_x=326.0,
            aim_point_y=256.0,
            body_box=(286.0, 180.0, 366.0, 340.0),
        )
        plugin.apply(
            _frame(timestamp=1.000, target_dx=6.0, target_dy=0.0, target=target),
            MouseOutput(),
        )

        moves = []
        for index, dx in enumerate((1.0, -1.0, 1.0, -1.0, 1.0, -1.0), start=1):
            output = MouseOutput()
            plugin.apply(
                _frame(
                    timestamp=1.000 + index * 0.008,
                    target_dx=dx,
                    target_dy=0.0,
                    manual_dx=2.5,
                    target=_target(
                        aim_point_x=320.0 + dx,
                        aim_point_y=256.0,
                        body_box=(280.0 + dx, 180.0, 360.0 + dx, 340.0),
                    ),
                    target_revision=1 + index,
                ),
                output,
            )
            moves.append(output.move_dx)

        nonzero_signs = [_sign(move) for move in moves if abs(move) >= 0.05]
        flips = sum(
            1
            for previous, current in zip(nonzero_signs, nonzero_signs[1:])
            if previous != current
        )
        self.assertEqual(plugin._control_phase, "body_lock")
        self.assertEqual(flips, 0)
        self.assertLessEqual(max(abs(move) for move in moves), 0.25)

    def test_body_lock_opposing_manual_keeps_recovery_strength(self):
        plugin = AIAimPlugin(AIAimConfig(snap_window_seconds=0.0))
        target = _target(
            aim_point_x=330.0,
            aim_point_y=256.0,
            body_box=(290.0, 180.0, 370.0, 340.0),
        )
        baseline = MouseOutput()
        plugin.apply(
            _frame(timestamp=1.000, target_dx=10.0, target_dy=0.0, target=target),
            baseline,
        )

        output = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.008,
                target_dx=10.0,
                target_dy=0.0,
                manual_dx=-3.0,
                target_revision=2,
                target=target,
            ),
            output,
        )

        self.assertEqual(plugin._control_phase, "body_lock")
        self.assertGreaterEqual(abs(output.move_dx), abs(baseline.move_dx) * 0.75)

    def test_body_lock_manual_arbitration_distinguishes_opposing_and_helpful_input(self):
        plugin = AIAimPlugin(
            AIAimConfig(
                snap_window_seconds=0.0,
                body_lock_manual_dampen_speed_px=1.0,
                body_lock_manual_scale=0.35,
            )
        )
        plugin._control_phase = "body_lock"

        opposing_x, _ = plugin._apply_body_lock_manual_mix(
            _frame(target_dx=10.0, target_dy=0.0, manual_dx=-3.0),
            move_dx=0.90,
            move_dy=0.0,
        )
        helpful_x, _ = plugin._apply_body_lock_manual_mix(
            _frame(target_dx=10.0, target_dy=0.0, manual_dx=3.0),
            move_dx=0.90,
            move_dy=0.0,
        )
        orthogonal_x, _ = plugin._apply_body_lock_manual_mix(
            _frame(target_dx=10.0, target_dy=0.0, manual_dy=3.0),
            move_dx=0.90,
            move_dy=0.0,
        )

        self.assertAlmostEqual(opposing_x, 0.90)
        self.assertAlmostEqual(orthogonal_x, 0.90)
        self.assertLess(abs(helpful_x), 0.25)

    def test_body_lock_orthogonal_manual_keeps_primary_axis_help(self):
        plugin = AIAimPlugin(AIAimConfig(snap_window_seconds=0.0))
        target = _target(
            aim_point_x=330.0,
            aim_point_y=256.0,
            body_box=(290.0, 180.0, 370.0, 340.0),
        )
        baseline = MouseOutput()
        plugin.apply(
            _frame(timestamp=1.000, target_dx=10.0, target_dy=0.0, target=target),
            baseline,
        )

        output = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.008,
                target_dx=10.0,
                target_dy=0.0,
                manual_dy=3.0,
                target_revision=2,
                target=target,
            ),
            output,
        )

        self.assertEqual(plugin._control_phase, "body_lock")
        self.assertGreaterEqual(abs(output.move_dx), abs(baseline.move_dx) * 0.75)

    def test_body_lock_segmented_x_cancel_offsets_small_manual_jitter(self):
        plugin = AIAimPlugin(
            AIAimConfig(
                snap_window_seconds=0.0,
                body_lock_jitter_cancel_x_smoothing=0.0,
                body_lock_jitter_cancel_x_scale=0.60,
                body_lock_jitter_cancel_x_deadband=0.5,
                body_lock_jitter_cancel_x_soft_px=3.0,
                body_lock_jitter_cancel_x_max_speed=8.0,
                body_lock_jitter_cancel_x_max_move=4.0,
                body_lock_jitter_cancel_x_inner_radius_px=8.0,
            )
        )
        output = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.000,
                target_dx=2.0,
                target_dy=0.0,
                manual_dx=3.0,
                target=_target(
                    aim_point_x=322.0,
                    aim_point_y=256.0,
                    body_box=(282.0, 180.0, 362.0, 340.0),
                ),
            ),
            output,
        )

        self.assertEqual(plugin._control_phase, "body_lock")
        self.assertLess(plugin._last_body_lock_jitter_cancel_x, -1.0)

    def test_body_lock_segmented_x_cancel_ignores_large_manual_turn(self):
        plugin = AIAimPlugin(
            AIAimConfig(
                snap_window_seconds=0.0,
                body_lock_jitter_cancel_x_smoothing=0.0,
                body_lock_jitter_cancel_x_scale=0.60,
                body_lock_jitter_cancel_x_deadband=0.5,
                body_lock_jitter_cancel_x_soft_px=3.0,
                body_lock_jitter_cancel_x_max_speed=8.0,
                body_lock_jitter_cancel_x_max_move=4.0,
                body_lock_jitter_cancel_x_inner_radius_px=8.0,
            )
        )
        output = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.000,
                target_dx=2.0,
                target_dy=0.0,
                manual_dx=12.0,
                target=_target(
                    aim_point_x=322.0,
                    aim_point_y=256.0,
                    body_box=(282.0, 180.0, 362.0, 340.0),
                ),
            ),
            output,
        )

        self.assertEqual(plugin._control_phase, "body_lock")
        self.assertAlmostEqual(plugin._last_body_lock_jitter_cancel_x, 0.0)

    def test_body_lock_segmented_x_cancel_smooths_direction_flip(self):
        plugin = AIAimPlugin(
            AIAimConfig(
                snap_window_seconds=0.0,
                body_lock_jitter_cancel_x_smoothing=0.5,
                body_lock_jitter_cancel_x_scale=0.60,
                body_lock_jitter_cancel_x_deadband=0.5,
                body_lock_jitter_cancel_x_soft_px=3.0,
                body_lock_jitter_cancel_x_max_speed=8.0,
                body_lock_jitter_cancel_x_max_move=4.0,
                body_lock_jitter_cancel_x_inner_radius_px=8.0,
            )
        )
        target = _target(
            aim_point_x=322.0,
            aim_point_y=256.0,
            body_box=(282.0, 180.0, 362.0, 340.0),
        )
        plugin.apply(
            _frame(
                timestamp=1.000,
                target_dx=2.0,
                target_dy=0.0,
                manual_dx=3.0,
                target=target,
            ),
            MouseOutput(),
        )
        first_cancel = plugin._last_body_lock_jitter_cancel_x

        plugin.apply(
            _frame(
                timestamp=1.008,
                target_dx=2.0,
                target_dy=0.0,
                manual_dx=-3.0,
                target=target,
                target_revision=2,
            ),
            MouseOutput(),
        )
        second_cancel = plugin._last_body_lock_jitter_cancel_x

        self.assertLess(first_cancel, 0.0)
        self.assertGreater(second_cancel, first_cancel)
        self.assertLess(abs(second_cancel), 1.0)

    def test_body_lock_segmented_x_cancel_waits_until_near_center(self):
        plugin = AIAimPlugin(
            AIAimConfig(
                snap_window_seconds=0.0,
                body_lock_jitter_cancel_x_smoothing=0.0,
                body_lock_jitter_cancel_x_scale=0.60,
                body_lock_jitter_cancel_x_deadband=0.5,
                body_lock_jitter_cancel_x_soft_px=3.0,
                body_lock_jitter_cancel_x_max_speed=8.0,
                body_lock_jitter_cancel_x_max_move=4.0,
                body_lock_jitter_cancel_x_inner_radius_px=8.0,
            )
        )
        output = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.000,
                target_dx=10.0,
                target_dy=0.0,
                manual_dx=3.0,
                target=_target(
                    aim_point_x=330.0,
                    aim_point_y=256.0,
                    body_box=(290.0, 180.0, 370.0, 340.0),
                ),
            ),
            output,
        )

        self.assertEqual(plugin._control_phase, "body_lock")
        self.assertAlmostEqual(plugin._last_body_lock_jitter_cancel_x, 0.0)

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
        plugin = AIAimPlugin(AIAimConfig(snap_window_seconds=0.0))
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
        plugin = AIAimPlugin(AIAimConfig(snap_window_seconds=0.0))
        output = MouseOutput()
        plugin.apply(_frame(target_dx=24.0, target_dy=-8.0, target=_target()), output)
        self.assertEqual(plugin._mode, "acquire_mid")
        self.assertGreater(_magnitude(output), plugin.config.stabilize_max_move_px)

    def test_repeated_local_error_frame_recalculates_desired_velocity(self):
        plugin = AIAimPlugin(
            AIAimConfig(
                snap_window_seconds=0.0,
                mid_acquire_gain=1.0,
                mid_acquire_max_move_px=100.0,
                mid_acquire_response_horizon_s=0.020,
                follow_balanced_gain_scale=1.0,
                follow_balanced_max_move_scale=1.0,
                follow_balanced_horizon_scale=1.0,
                follow_balanced_accel_scale=1.0,
                follow_balanced_error_rate_scale=1.0,
            )
        )
        target = _target(aim_point_x=335.0, aim_point_y=256.0)
        plugin.apply(
            _frame(
                timestamp=1.000,
                target_dx=20.0,
                target_dy=0.0,
                target=target,
                target_revision=1,
                target_timestamp=1.000,
            ),
            MouseOutput(),
        )
        initial_velocity = plugin._desired_velocity_x

        plugin.apply(
            _frame(
                timestamp=1.001,
                target_dx=16.0,
                target_dy=0.0,
                target=target,
                target_revision=1,
                target_timestamp=1.000,
            ),
            MouseOutput(),
        )

        self.assertEqual(plugin._mode, "acquire_mid")
        self.assertLess(plugin._desired_velocity_x, initial_velocity)

    def test_stabilize_entry_discards_acquire_velocity_carry(self):
        plugin = AIAimPlugin()
        target = _target(aim_point_x=335.0, aim_point_y=256.0)
        plugin.apply(
            _frame(
                timestamp=1.000,
                target_dx=15.0,
                target_dy=0.0,
                target=target,
                target_revision=1,
                target_timestamp=1.000,
            ),
            MouseOutput(),
        )

        output = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.001,
                target_dx=6.0,
                target_dy=0.0,
                target=target,
                target_revision=1,
                target_timestamp=1.000,
            ),
            output,
        )

        self.assertEqual(plugin._mode, "stabilize")
        self.assertLess(abs(output.move_dx), 0.25)

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
        plugin = AIAimPlugin(
            AIAimConfig(
                snap_window_seconds=0.0,
                body_lock_gain=0.12,
                body_lock_max_move_px=0.90,
            )
        )
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
        plugin = AIAimPlugin(
            AIAimConfig(
                snap_window_seconds=0.0,
                body_lock_gain=0.12,
                body_lock_max_move_px=0.90,
            )
        )
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
                snap_window_seconds=0.0,
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
        plugin = AIAimPlugin(AIAimConfig(snap_window_seconds=0.0))
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
        self.assertGreater(plugin._acquire_bonus, 0.0)
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
