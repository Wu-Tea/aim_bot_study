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
    target=_DEFAULT_TARGET,
):
    return MouseFrame(
        timestamp=timestamp,
        manual_dx=manual_dx,
        manual_dy=manual_dy,
        is_aiming=aiming,
        target_dx=target_dx,
        target_dy=target_dy,
        auto_fire_requested=False,
        target=_target() if target is _DEFAULT_TARGET else target,
        target_revision=1,
        target_timestamp=timestamp,
    )


class AIAimSequenceTests(unittest.TestCase):
    def test_manual_entry_can_go_directly_to_stabilize_when_close(self):
        plugin = AIAimPlugin()

        output = MouseOutput()
        plugin.apply(_frame(target_dx=6.0, target_dy=-4.0), output)

        self.assertEqual(plugin._mode, "stabilize")
        self.assertNotEqual((output.move_dx, output.move_dy), (0.0, 0.0))

    def test_manual_entry_uses_acquire_when_target_is_far(self):
        plugin = AIAimPlugin()

        output = MouseOutput()
        plugin.apply(_frame(target_dx=80.0, target_dy=-60.0), output)

        self.assertEqual(plugin._mode, "acquire_far")
        self.assertNotEqual((output.move_dx, output.move_dy), (0.0, 0.0))

    def test_manual_entry_uses_mid_acquire_when_target_is_midrange(self):
        plugin = AIAimPlugin()

        output = MouseOutput()
        plugin.apply(_frame(target_dx=30.0, target_dy=-18.0), output)

        self.assertEqual(plugin._mode, "acquire_mid")
        self.assertNotEqual((output.move_dx, output.move_dy), (0.0, 0.0))

    def test_stabilize_has_hysteresis_before_falling_back_to_acquire(self):
        plugin = AIAimPlugin()
        plugin.apply(
            _frame(
                timestamp=1.0,
                target_dx=6.0,
                target_dy=-4.0,
                target=_target(
                    aim_point_x=332.0,
                    aim_point_y=228.0,
                    body_box=(288.0, 140.0, 368.0, 340.0),
                    source="observed",
                ),
            ),
            MouseOutput(),
        )
        self.assertEqual(plugin._mode, "stabilize")

        output = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.05,
                target_dx=18.0,
                target_dy=-8.0,
                target=_target(source="observed"),
            ),
            output,
        )
        self.assertEqual(plugin._mode, "acquire_mid")
        self.assertNotEqual((output.move_dx, output.move_dy), (0.0, 0.0))

        output = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.10,
                target_dx=42.0,
                target_dy=-10.0,
                target=_target(source="observed"),
            ),
            output,
        )
        self.assertEqual(plugin._mode, "acquire_mid")
        self.assertNotEqual((output.move_dx, output.move_dy), (0.0, 0.0))

    def test_stabilize_falls_back_to_acquire_when_error_grows_sharply(self):
        plugin = AIAimPlugin()
        plugin.apply(
            _frame(
                timestamp=1.0,
                target_dx=6.0,
                target_dy=-4.0,
                target=_target(source="observed"),
            ),
            MouseOutput(),
        )
        self.assertEqual(plugin._mode, "stabilize")

        output = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.05,
                target_dx=20.0,
                target_dy=-6.0,
                target=_target(
                    aim_point_x=344.0,
                    aim_point_y=230.0,
                    body_box=(300.0, 142.0, 380.0, 342.0),
                    source="observed",
                ),
            ),
            output,
        )

        self.assertEqual(plugin._mode, "acquire_mid")
        self.assertNotEqual((output.move_dx, output.move_dy), (0.0, 0.0))

    def test_close_moving_target_does_not_ping_pong_between_stabilize_and_mid_acquire(self):
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
                    source="observed",
                ),
            ),
            MouseOutput(),
        )
        self.assertEqual(plugin._mode, "stabilize")

        output = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.05,
                target_dx=0.0,
                target_dy=-1.0,
                target=_target(
                    aim_point_x=320.0,
                    aim_point_y=255.0,
                    body_box=(280.0, 175.0, 360.0, 335.0),
                    source="observed",
                ),
            ),
            output,
        )
        self.assertEqual(plugin._mode, "stabilize")
        self.assertNotEqual((output.move_dx, output.move_dy), (0.0, 0.0))

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
                    source="observed",
                ),
            ),
            output,
        )
        self.assertEqual(plugin._mode, "stabilize")
        self.assertNotEqual((output.move_dx, output.move_dy), (0.0, 0.0))

    def test_reacquire_mode_is_short_lived_before_falling_back_to_acquire_mid(self):
        plugin = AIAimPlugin()
        plugin.apply(
            _frame(
                timestamp=1.0,
                target_dx=6.0,
                target_dy=-4.0,
                target=_target(source="observed"),
            ),
            MouseOutput(),
        )
        self.assertEqual(plugin._mode, "stabilize")

        plugin.apply(
            _frame(timestamp=1.04, target_dx=0.0, target_dy=0.0, target=None),
            MouseOutput(),
        )

        plugin.apply(
            _frame(
                timestamp=1.08,
                target_dx=30.0,
                target_dy=-18.0,
                target=_target(source="observed"),
            ),
            MouseOutput(),
        )
        self.assertEqual(plugin._mode, "reacquire")

        output = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.20,
                target_dx=32.0,
                target_dy=-20.0,
                target=_target(source="observed"),
            ),
            output,
        )

        self.assertEqual(plugin._mode, "acquire_mid")
        self.assertNotEqual((output.move_dx, output.move_dy), (0.0, 0.0))

    def test_reacquire_mode_persists_within_reacquire_window(self):
        plugin = AIAimPlugin()
        plugin.apply(
            _frame(
                timestamp=1.0,
                target_dx=6.0,
                target_dy=-4.0,
                target=_target(source="observed"),
            ),
            MouseOutput(),
        )
        self.assertEqual(plugin._mode, "stabilize")

        plugin.apply(
            _frame(timestamp=1.04, target_dx=0.0, target_dy=0.0, target=None),
            MouseOutput(),
        )

        plugin.apply(
            _frame(
                timestamp=1.08,
                target_dx=30.0,
                target_dy=-18.0,
                target=_target(source="observed"),
            ),
            MouseOutput(),
        )
        self.assertEqual(plugin._mode, "reacquire")

        output = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.12,
                target_dx=28.0,
                target_dy=-16.0,
                target=_target(
                    aim_point_x=348.0,
                    aim_point_y=240.0,
                    body_box=(308.0, 160.0, 388.0, 340.0),
                    source="observed",
                ),
            ),
            output,
        )

        self.assertEqual(plugin._mode, "reacquire")
        self.assertNotEqual((output.move_dx, output.move_dy), (0.0, 0.0))

    def test_midrange_target_that_keeps_pulling_away_stays_in_far_acquire(self):
        plugin = AIAimPlugin(
            AIAimConfig(
                mid_acquire_enter_px=48.0,
                mid_acquire_exit_px=64.0,
                chase_hold_projection_px_per_sec=90.0,
                chase_hold_min_radius_px=24.0,
            )
        )
        plugin.apply(
            _frame(
                timestamp=1.0,
                target_dx=-44.0,
                target_dy=0.0,
                target=_target(
                    aim_point_x=276.0,
                    aim_point_y=256.0,
                    body_box=(236.0, 180.0, 316.0, 340.0),
                    source="observed",
                ),
            ),
            MouseOutput(),
        )
        self.assertEqual(plugin._mode, "acquire_mid")

        output = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.05,
                target_dx=-42.0,
                target_dy=0.0,
                target=_target(
                    aim_point_x=270.0,
                    aim_point_y=256.0,
                    body_box=(230.0, 180.0, 310.0, 340.0),
                    source="observed",
                ),
            ),
            output,
        )

        self.assertEqual(plugin._mode, "acquire_far")
        self.assertNotEqual((output.move_dx, output.move_dy), (0.0, 0.0))

    def test_fast_lateral_midrange_target_stays_in_far_acquire(self):
        plugin = AIAimPlugin(
            AIAimConfig(
                mid_acquire_enter_px=48.0,
                mid_acquire_exit_px=64.0,
                chase_hold_projection_px_per_sec=120.0,
                chase_hold_min_radius_px=24.0,
            )
        )
        plugin.apply(
            _frame(
                timestamp=1.0,
                target_dx=-36.0,
                target_dy=0.0,
                target=_target(
                    aim_point_x=284.0,
                    aim_point_y=256.0,
                    body_box=(244.0, 180.0, 324.0, 340.0),
                    source="observed",
                ),
            ),
            MouseOutput(),
        )
        self.assertEqual(plugin._mode, "acquire_mid")

        output = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.05,
                target_dx=-24.0,
                target_dy=0.0,
                target=_target(
                    aim_point_x=296.0,
                    aim_point_y=256.0,
                    body_box=(256.0, 180.0, 336.0, 340.0),
                    source="observed",
                ),
            ),
            output,
        )

        self.assertEqual(plugin._mode, "acquire_far")
        self.assertNotEqual((output.move_dx, output.move_dy), (0.0, 0.0))

    def test_different_family_target_is_switch_guarded_before_acquire(self):
        plugin = AIAimPlugin(
            AIAimConfig(
                switch_guard_ms=100,
                switch_guard_commit_radius_px=24.0,
            )
        )
        plugin.apply(
            _frame(
                timestamp=1.00,
                target_dx=6.0,
                target_dy=-4.0,
                target=_target(
                    aim_point_x=326.0,
                    aim_point_y=252.0,
                    body_box=(286.0, 172.0, 366.0, 332.0),
                    source="observed",
                ),
            ),
            MouseOutput(),
        )
        self.assertEqual(plugin._mode, "stabilize")

        guarded = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.04,
                target_dx=42.0,
                target_dy=-2.0,
                target=_target(
                    aim_point_x=362.0,
                    aim_point_y=254.0,
                    body_box=(322.0, 174.0, 402.0, 334.0),
                    source="observed",
                ),
            ),
            guarded,
        )

        self.assertEqual(plugin._mode, "manual")
        self.assertEqual((guarded.move_dx, guarded.move_dy), (0.0, 0.0))

        switched = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.16,
                target_dx=42.0,
                target_dy=-2.0,
                target=_target(
                    aim_point_x=362.0,
                    aim_point_y=254.0,
                    body_box=(322.0, 174.0, 402.0, 334.0),
                    source="observed",
                ),
            ),
            switched,
        )

        self.assertEqual(plugin._mode, "acquire_mid")
        self.assertNotEqual((switched.move_dx, switched.move_dy), (0.0, 0.0))

    def test_switch_guard_does_not_block_target_return_after_gap(self):
        plugin = AIAimPlugin(
            AIAimConfig(
                switch_guard_ms=100,
                switch_guard_commit_radius_px=24.0,
            )
        )
        plugin.apply(
            _frame(
                timestamp=1.00,
                target_dx=6.0,
                target_dy=-4.0,
                target=_target(
                    aim_point_x=326.0,
                    aim_point_y=252.0,
                    body_box=(286.0, 172.0, 366.0, 332.0),
                    source="observed",
                ),
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
                timestamp=1.16,
                target_dx=46.0,
                target_dy=0.0,
                target=_target(
                    aim_point_x=372.0,
                    aim_point_y=252.0,
                    body_box=(332.0, 172.0, 412.0, 332.0),
                    source="observed",
                ),
            ),
            output,
        )

        self.assertNotEqual(plugin._mode, "manual")
        self.assertNotEqual((output.move_dx, output.move_dy), (0.0, 0.0))

    def test_confirmed_target_switch_uses_soft_acquire_instead_of_far_yank(self):
        plugin = AIAimPlugin(
            AIAimConfig(
                switch_guard_ms=100,
                switch_guard_commit_radius_px=24.0,
            )
        )
        plugin.apply(
            _frame(
                timestamp=1.00,
                target_dx=6.0,
                target_dy=-4.0,
                target=_target(
                    aim_point_x=326.0,
                    aim_point_y=252.0,
                    body_box=(286.0, 172.0, 366.0, 332.0),
                    source="observed",
                ),
            ),
            MouseOutput(),
        )
        self.assertEqual(plugin._mode, "stabilize")

        plugin.apply(
            _frame(
                timestamp=1.04,
                target_dx=84.0,
                target_dy=-2.0,
                target=_target(
                    aim_point_x=410.0,
                    aim_point_y=254.0,
                    body_box=(370.0, 174.0, 450.0, 334.0),
                    source="observed",
                ),
            ),
            MouseOutput(),
        )
        self.assertEqual(plugin._mode, "manual")

        output = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.16,
                target_dx=84.0,
                target_dy=-2.0,
                target=_target(
                    aim_point_x=410.0,
                    aim_point_y=254.0,
                    body_box=(370.0, 174.0, 450.0, 334.0),
                    source="observed",
                ),
            ),
            output,
        )

        self.assertEqual(plugin._mode, "acquire_mid")
        self.assertLess(
            (output.move_dx ** 2 + output.move_dy ** 2) ** 0.5,
            40.0,
        )

    def test_stabilize_uses_predicted_same_target_continuity(self):
        plugin = AIAimPlugin()
        plugin.apply(
            _frame(
                timestamp=1.0,
                target_dx=6.0,
                target_dy=-4.0,
                target=_target(
                    aim_point_x=332.0,
                    aim_point_y=228.0,
                    body_box=(288.0, 140.0, 368.0, 340.0),
                    source="observed",
                ),
            ),
            MouseOutput(),
        )
        self.assertEqual(plugin._mode, "stabilize")

        output = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.05,
                target_dx=10.0,
                target_dy=-6.0,
                target=_target(
                    aim_point_x=340.0,
                    aim_point_y=230.0,
                    body_box=(296.0, 142.0, 376.0, 342.0),
                    source="predicted",
                ),
            ),
            output,
        )

        self.assertEqual(plugin._mode, "stabilize")
        self.assertNotEqual((output.move_dx, output.move_dy), (0.0, 0.0))

    def test_predicted_target_without_committed_context_stays_manual(self):
        plugin = AIAimPlugin()

        output = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.0,
                target_dx=8.0,
                target_dy=-6.0,
                target=_target(source="predicted"),
            ),
            output,
        )

        self.assertEqual(plugin._mode, "manual")
        self.assertEqual((output.move_dx, output.move_dy), (0.0, 0.0))

    def test_no_target_gap_preserves_same_target_predicted_stabilize(self):
        plugin = AIAimPlugin()
        plugin.apply(
            _frame(
                timestamp=1.0,
                target_dx=6.0,
                target_dy=-4.0,
                target=_target(source="observed"),
            ),
            MouseOutput(),
        )
        stabilize_until = plugin._stabilize_until
        last_target = plugin._last_target

        gap_output = MouseOutput()
        plugin.apply(
            _frame(timestamp=1.04, target_dx=0.0, target_dy=0.0, target=None),
            gap_output,
        )

        self.assertEqual(plugin._mode, "manual")
        self.assertLess(
            (gap_output.move_dx ** 2 + gap_output.move_dy ** 2) ** 0.5,
            0.5,
        )
        self.assertIs(plugin._last_target, last_target)
        self.assertEqual(plugin._stabilize_until, stabilize_until)

        output = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.07,
                target_dx=9.0,
                target_dy=-5.0,
                target=_target(
                    aim_point_x=338.0,
                    aim_point_y=230.0,
                    body_box=(294.0, 142.0, 374.0, 342.0),
                    source="predicted",
                ),
            ),
            output,
        )

        self.assertEqual(plugin._mode, "stabilize")
        self.assertNotEqual((output.move_dx, output.move_dy), (0.0, 0.0))

    def test_different_family_predicted_target_does_not_stabilize(self):
        plugin = AIAimPlugin()
        plugin.apply(
            _frame(
                timestamp=1.0,
                target_dx=6.0,
                target_dy=-4.0,
                target=_target(
                    aim_point_x=332.0,
                    aim_point_y=228.0,
                    body_box=(288.0, 140.0, 368.0, 340.0),
                    source="observed",
                ),
            ),
            MouseOutput(),
        )

        output = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.05,
                target_dx=8.0,
                target_dy=-5.0,
                target=_target(
                    aim_point_x=420.0,
                    aim_point_y=240.0,
                    body_box=(396.0, 120.0, 468.0, 352.0),
                    source="predicted",
                ),
            ),
            output,
        )

        self.assertEqual(plugin._mode, "manual")
        self.assertEqual((output.move_dx, output.move_dy), (0.0, 0.0))

    def test_different_family_observed_target_falls_back_to_acquire(self):
        plugin = AIAimPlugin(AIAimConfig(switch_guard_ms=0))
        plugin.apply(
            _frame(
                timestamp=1.0,
                target_dx=6.0,
                target_dy=-4.0,
                target=_target(
                    aim_point_x=332.0,
                    aim_point_y=228.0,
                    body_box=(288.0, 140.0, 368.0, 340.0),
                    source="observed",
                ),
            ),
            MouseOutput(),
        )

        output = MouseOutput()
        plugin.apply(
            _frame(
                timestamp=1.05,
                target_dx=44.0,
                target_dy=-12.0,
                target=_target(
                    aim_point_x=416.0,
                    aim_point_y=234.0,
                    body_box=(392.0, 136.0, 468.0, 344.0),
                    source="observed",
                ),
            ),
            output,
        )

        self.assertEqual(plugin._mode, "acquire_mid")
        self.assertNotEqual((output.move_dx, output.move_dy), (0.0, 0.0))


if __name__ == "__main__":
    unittest.main()
