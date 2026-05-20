import unittest

from controllers.gamepad.recoil_compensation import (
    RecoilCompensationConfig,
    RecoilCompensationPlugin,
)
from controllers.gamepad.state import GamepadFrame, GamepadOutput
from vision.recoil_collection.models import RecoilProfileRecord


def _frame(*, timestamp: float = 1.0, right_trigger: int = 0):
    return GamepadFrame(
        timestamp=timestamp,
        left_x=0,
        left_y=0,
        manual_right_x=0,
        manual_right_y=0,
        left_trigger=255,
        right_trigger=right_trigger,
        buttons={"rb": False},
        is_aiming=True,
        target_dx=0.0,
        target_dy=0.0,
        auto_fire_requested=False,
    )


def _profile(
    *,
    profile_id: str = "profile-cod22-m4-ads-standing-v1",
    canonical_weapon_id: str = "cod22-m4",
    aim_mode: str = "ads",
    samples_x: tuple[float, ...] | None = None,
    samples_y: tuple[float, ...] = (0.0, -120.0, -260.0),
) -> RecoilProfileRecord:
    if samples_x is None:
        samples_x = tuple(0.0 for _ in samples_y)
    return RecoilProfileRecord(
        profile_id=profile_id,
        canonical_weapon_id=canonical_weapon_id,
        game="cod22",
        stance="standing",
        aim_mode=aim_mode,
        sample_interval_ms=10,
        duration_ms=len(samples_y) * 10,
        initial_delay_ms=0,
        samples_x=samples_x,
        samples_y=samples_y,
        sample_count=len(samples_y),
        burst_count=5,
        variance_summary={"horizontal_stddev": 0.1, "vertical_stddev": 0.2},
        confidence=0.9,
        capture_resolution="2560x1440",
        capture_fps=144.0,
        collector_version="test",
        created_at="2026-05-06T12:00:00Z",
    )


class RecoilCompensationPluginTests(unittest.TestCase):
    def test_recoil_is_applied_only_when_auto_fire_is_active(self):
        plugin = RecoilCompensationPlugin(RecoilCompensationConfig(feedback_amount=0.30))
        frame = _frame()
        output = GamepadOutput(right_y=0, auto_fire_active=True)

        plugin.apply(frame, output)

        self.assertLess(output.right_y, 0)

    def test_recoil_is_skipped_when_auto_fire_is_inactive(self):
        plugin = RecoilCompensationPlugin(RecoilCompensationConfig(feedback_amount=0.30))
        frame = _frame()
        output = GamepadOutput(right_y=0, auto_fire_active=False)

        plugin.apply(frame, output)

        self.assertEqual(output.right_y, 0)

    def test_profile_driven_playback_maps_collector_curve_into_absolute_anti_recoil_stick(self):
        plugin = RecoilCompensationPlugin(
            RecoilCompensationConfig(profile_amount=1.0),
            profile_provider=lambda _frame: _profile(samples_y=(0.0, 1.8, 3.6, 4.5)),
        )

        first = GamepadOutput(right_y=0, auto_fire_active=True)
        plugin.apply(_frame(timestamp=1.00), first)

        second = GamepadOutput(right_y=0, auto_fire_active=True)
        plugin.apply(_frame(timestamp=1.01), second)

        third = GamepadOutput(right_y=0, auto_fire_active=True)
        plugin.apply(_frame(timestamp=1.02), third)

        fourth = GamepadOutput(right_y=0, auto_fire_active=True)
        plugin.apply(_frame(timestamp=1.03), fourth)

        self.assertEqual(first.right_y, 0)
        self.assertEqual(second.right_y, -852)
        self.assertEqual(third.right_y, -1704)
        self.assertEqual(fourth.right_y, -2130)

    def test_profile_playback_outputs_absolute_anti_recoil_stick_from_recorded_curve(self):
        plugin = RecoilCompensationPlugin(
            RecoilCompensationConfig(profile_amount=1.0, profile_x_amount=0.0),
            profile_provider=lambda _frame: _profile(
                samples_x=(0.0, -1.0, -2.0),
                samples_y=(0.0, 1.8, 3.6),
            ),
        )

        first = GamepadOutput(right_x=0, right_y=0, auto_fire_active=True)
        plugin.apply(_frame(timestamp=1.00), first)

        second = GamepadOutput(right_x=0, right_y=0, auto_fire_active=True)
        plugin.apply(_frame(timestamp=1.01), second)

        third = GamepadOutput(right_x=0, right_y=0, auto_fire_active=True)
        plugin.apply(_frame(timestamp=1.02), third)

        self.assertEqual(first.right_x, 0)
        self.assertEqual(first.right_y, 0)
        self.assertEqual(second.right_x, 0)
        self.assertEqual(second.right_y, -852)
        self.assertEqual(third.right_x, 0)
        self.assertEqual(third.right_y, -1704)

    def test_profile_playback_uses_horizontal_sample_delta_not_cumulative_position(self):
        plugin = RecoilCompensationPlugin(
            RecoilCompensationConfig(profile_amount=1.0, profile_x_amount=1.0),
            profile_provider=lambda _frame: _profile(
                samples_x=(0.0, -1.0, -2.0),
                samples_y=(0.0, 1.8, 3.6),
            ),
        )

        first = GamepadOutput(right_x=0, right_y=0, auto_fire_active=True)
        plugin.apply(_frame(timestamp=1.00), first)

        second = GamepadOutput(right_x=0, right_y=0, auto_fire_active=True)
        plugin.apply(_frame(timestamp=1.01), second)

        third = GamepadOutput(right_x=0, right_y=0, auto_fire_active=True)
        plugin.apply(_frame(timestamp=1.02), third)

        self.assertEqual(first.right_x, 0)
        self.assertEqual(first.right_y, 0)
        self.assertEqual(second.right_x, 473)
        self.assertEqual(second.right_y, -852)
        self.assertEqual(third.right_x, 473)
        self.assertEqual(third.right_y, -1704)

    def test_profile_amount_scales_profile_x_and_y_output(self):
        plugin = RecoilCompensationPlugin(
            RecoilCompensationConfig(
                profile_amount=0.5,
                profile_x_amount=1.0,
                feedback_amount=9.0,
            ),
            profile_provider=lambda _frame: _profile(
                samples_x=(0.0, -1.0),
                samples_y=(0.0, 1.8),
            ),
        )

        first = GamepadOutput(right_x=0, right_y=0, auto_fire_active=True)
        plugin.apply(_frame(timestamp=1.00), first)

        second = GamepadOutput(right_x=0, right_y=0, auto_fire_active=True)
        plugin.apply(_frame(timestamp=1.01), second)

        self.assertEqual(first.right_x, 0)
        self.assertEqual(first.right_y, 0)
        self.assertEqual(second.right_x, 237)
        self.assertEqual(second.right_y, -426)

    def test_profile_x_amount_scales_only_horizontal_profile_delta(self):
        plugin = RecoilCompensationPlugin(
            RecoilCompensationConfig(
                profile_amount=1.0,
                profile_x_amount=2.0,
                feedback_amount=9.0,
            ),
            profile_provider=lambda _frame: _profile(
                samples_x=(0.0, -1.0),
                samples_y=(0.0, 1.8),
            ),
        )

        first = GamepadOutput(right_x=0, right_y=0, auto_fire_active=True)
        plugin.apply(_frame(timestamp=1.00), first)

        second = GamepadOutput(right_x=0, right_y=0, auto_fire_active=True)
        plugin.apply(_frame(timestamp=1.01), second)

        self.assertEqual(first.right_x, 0)
        self.assertEqual(first.right_y, 0)
        self.assertEqual(second.right_x, 947)
        self.assertEqual(second.right_y, -852)

    def test_feedback_amount_does_not_affect_active_profile_playback(self):
        plugin = RecoilCompensationPlugin(
            RecoilCompensationConfig(
                profile_amount=1.0,
                profile_x_amount=1.0,
                feedback_amount=2.0,
            ),
            profile_provider=lambda _frame: _profile(
                samples_x=(0.0, -1.0),
                samples_y=(0.0, 1.8),
            ),
        )

        first = GamepadOutput(right_x=0, right_y=0, auto_fire_active=True)
        plugin.apply(_frame(timestamp=1.00), first)

        second = GamepadOutput(right_x=0, right_y=0, auto_fire_active=True)
        plugin.apply(_frame(timestamp=1.01), second)

        self.assertEqual(first.right_x, 0)
        self.assertEqual(first.right_y, 0)
        self.assertEqual(second.right_x, 473)
        self.assertEqual(second.right_y, -852)

    def test_profile_playback_scales_absolute_anti_recoil_by_configured_amount(self):
        plugin = RecoilCompensationPlugin(
            RecoilCompensationConfig(profile_amount=0.25, profile_x_amount=0.0),
            profile_provider=lambda _frame: _profile(
                samples_x=(0.0, -1.0),
                samples_y=(0.0, 1.8),
            ),
        )

        first = GamepadOutput(right_x=0, right_y=0, auto_fire_active=True)
        plugin.apply(_frame(timestamp=1.00), first)

        second = GamepadOutput(right_x=0, right_y=0, auto_fire_active=True)
        plugin.apply(_frame(timestamp=1.01), second)

        self.assertEqual(first.right_x, 0)
        self.assertEqual(first.right_y, 0)
        self.assertEqual(second.right_x, 0)
        self.assertEqual(second.right_y, -213)

    def test_profile_playback_requires_calibration_when_provider_returns_profile_bundle(self):
        from vision.recoil_collection.calibration import RecoilControlCalibration

        calibration = RecoilControlCalibration(
            game="cod22",
            aim_mode="ads",
            stance="standing",
            pixels_per_full_stick_x_per_second=500.0,
            pixels_per_full_stick_y_per_second=1000.0,
            created_at="2026-05-20T00:00:00Z",
        )
        plugin = RecoilCompensationPlugin(
            RecoilCompensationConfig(profile_amount=1.0),
            profile_provider=lambda _frame: (_profile(samples_y=(0.0, 10.0)), calibration),
        )

        first = GamepadOutput(right_y=0, auto_fire_active=True)
        plugin.apply(_frame(timestamp=1.00), first)
        second = GamepadOutput(right_y=0, auto_fire_active=True)
        plugin.apply(_frame(timestamp=1.01), second)

        self.assertEqual(first.right_y, 0)
        self.assertEqual(second.right_y, -32767)

    def test_profile_selection_logger_reports_active_aim_mode_when_firing(self):
        logs = []
        plugin = RecoilCompensationPlugin(
            RecoilCompensationConfig(),
            profile_provider=lambda _frame: _profile(
                profile_id="profile-cod22-m4-ads-standing-current",
                aim_mode="ads",
                samples_y=(0.0, -1.8, -3.6),
            ),
            profile_selection_logger=logs.append,
        )

        plugin.apply(_frame(timestamp=1.00, right_trigger=255), GamepadOutput(auto_fire_active=False))
        plugin.apply(_frame(timestamp=1.01, right_trigger=255), GamepadOutput(auto_fire_active=False))

        self.assertEqual(
            logs,
            [
                "[Recoil] active_profile aim=ads profile=profile-cod22-m4-ads-standing-current confidence=0.900 profile_amount=1.00 profile_x=1.00 feedback=0.30"
            ],
        )

    def test_profile_selection_logger_reports_configured_fallback_amount(self):
        logs = []
        plugin = RecoilCompensationPlugin(
            RecoilCompensationConfig(feedback_amount=0.15),
            profile_provider=lambda _frame: None,
            profile_selection_logger=logs.append,
        )

        plugin.apply(_frame(timestamp=1.00, right_trigger=255), GamepadOutput(auto_fire_active=False))

        self.assertEqual(logs, ["[Recoil] active_profile aim=ads profile=none fallback=15%"])

    def test_stop_firing_resets_profile_playback_to_the_start(self):
        plugin = RecoilCompensationPlugin(
            RecoilCompensationConfig(profile_amount=1.0),
            profile_provider=lambda _frame: _profile(samples_y=(0.0, 1.8, 3.6)),
        )

        plugin.apply(_frame(timestamp=1.00), GamepadOutput(right_y=0, auto_fire_active=True))
        active = GamepadOutput(right_y=0, auto_fire_active=True)
        plugin.apply(_frame(timestamp=1.01), active)

        stopped = GamepadOutput(right_y=0, auto_fire_active=False)
        plugin.apply(_frame(timestamp=1.02), stopped)

        restarted = GamepadOutput(right_y=0, auto_fire_active=True)
        plugin.apply(_frame(timestamp=1.03), restarted)

        resumed = GamepadOutput(right_y=0, auto_fire_active=True)
        plugin.apply(_frame(timestamp=1.04), resumed)

        self.assertEqual(active.right_y, -852)
        self.assertEqual(stopped.right_y, 0)
        self.assertEqual(restarted.right_y, 0)
        self.assertEqual(resumed.right_y, -852)

    def test_manual_fire_uses_profile_playback_and_restarts_on_new_trigger_pull(self):
        plugin = RecoilCompensationPlugin(
            RecoilCompensationConfig(profile_amount=1.0),
            profile_provider=lambda _frame: _profile(samples_y=(0.0, 1.8, 3.6)),
        )

        first_pull = GamepadOutput(right_y=0, auto_fire_active=False)
        plugin.apply(_frame(timestamp=1.00, right_trigger=255), first_pull)

        active = GamepadOutput(right_y=0, auto_fire_active=False)
        plugin.apply(_frame(timestamp=1.01, right_trigger=255), active)

        released = GamepadOutput(right_y=0, auto_fire_active=False)
        plugin.apply(_frame(timestamp=1.02, right_trigger=0), released)

        second_pull = GamepadOutput(right_y=0, auto_fire_active=False)
        plugin.apply(_frame(timestamp=1.03, right_trigger=255), second_pull)

        resumed = GamepadOutput(right_y=0, auto_fire_active=False)
        plugin.apply(_frame(timestamp=1.04, right_trigger=255), resumed)

        self.assertEqual(first_pull.right_y, 0)
        self.assertEqual(active.right_y, -852)
        self.assertEqual(released.right_y, 0)
        self.assertEqual(second_pull.right_y, 0)
        self.assertEqual(resumed.right_y, -852)

    def test_weapon_change_resets_curve_before_applying_new_profile(self):
        active_profile = _profile(
            profile_id="profile-cod22-m4-ads-standing-v1",
            canonical_weapon_id="cod22-m4",
            samples_y=(0.0, 1.8, 3.6),
        )
        swapped_profile = _profile(
            profile_id="profile-cod22-kastov-ads-standing-v1",
            canonical_weapon_id="cod22-kastov-762",
            samples_y=(0.0, 1.2, 2.4),
        )
        current = {"profile": active_profile}

        plugin = RecoilCompensationPlugin(
            RecoilCompensationConfig(profile_amount=1.0),
            profile_provider=lambda _frame: current["profile"],
        )

        plugin.apply(_frame(timestamp=1.00), GamepadOutput(right_y=0, auto_fire_active=True))
        before_swap = GamepadOutput(right_y=0, auto_fire_active=True)
        plugin.apply(_frame(timestamp=1.01), before_swap)

        current["profile"] = swapped_profile
        swap_frame = GamepadOutput(right_y=0, auto_fire_active=True)
        plugin.apply(_frame(timestamp=1.02), swap_frame)

        after_swap = GamepadOutput(right_y=0, auto_fire_active=True)
        plugin.apply(_frame(timestamp=1.03), after_swap)

        self.assertEqual(before_swap.right_y, -852)
        self.assertEqual(swap_frame.right_y, 0)
        self.assertEqual(after_swap.right_y, -568)

    def test_missing_or_degraded_profile_uses_fixed_fallback_and_clears_active_curve(self):
        ready_profile = _profile(samples_y=(0.0, 1.5, 3.0))
        current = {"profile": ready_profile}

        plugin = RecoilCompensationPlugin(
            RecoilCompensationConfig(profile_amount=1.0, feedback_amount=1.0),
            profile_provider=lambda _frame: current["profile"],
        )

        plugin.apply(_frame(timestamp=1.00), GamepadOutput(right_y=0, auto_fire_active=True))
        ready = GamepadOutput(right_y=0, auto_fire_active=True)
        plugin.apply(_frame(timestamp=1.01), ready)

        current["profile"] = None
        degraded = GamepadOutput(right_y=0, auto_fire_active=True)
        plugin.apply(_frame(timestamp=1.02), degraded)

        current["profile"] = ready_profile
        restarted = GamepadOutput(right_y=0, auto_fire_active=True)
        plugin.apply(_frame(timestamp=1.03), restarted)

        self.assertEqual(ready.right_y, -710)
        self.assertLess(degraded.right_y, 0)
        self.assertEqual(restarted.right_y, 0)

    def test_missing_profile_fallback_applies_to_manual_fire_when_provider_is_configured(self):
        plugin = RecoilCompensationPlugin(
            RecoilCompensationConfig(feedback_amount=0.20),
            profile_provider=lambda _frame: None,
        )
        output = GamepadOutput(right_y=0, auto_fire_active=False)

        plugin.apply(_frame(timestamp=1.00, right_trigger=255), output)

        self.assertEqual(output.right_y, -6553)


if __name__ == "__main__":
    unittest.main()
