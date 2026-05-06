import unittest

from controllers.gamepad.recoil_compensation import (
    RecoilCompensationConfig,
    RecoilCompensationPlugin,
)
from controllers.gamepad.state import GamepadFrame, GamepadOutput
from vision.recoil_collection.models import RecoilProfileRecord


def _frame(*, timestamp: float = 1.0):
    return GamepadFrame(
        timestamp=timestamp,
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


def _profile(
    *,
    profile_id: str = "profile-cod22-m4-ads-standing-v1",
    canonical_weapon_id: str = "cod22-m4",
    aim_mode: str = "ads",
    samples_y: tuple[float, ...] = (0.0, -120.0, -260.0),
) -> RecoilProfileRecord:
    return RecoilProfileRecord(
        profile_id=profile_id,
        canonical_weapon_id=canonical_weapon_id,
        game="cod22",
        stance="standing",
        aim_mode=aim_mode,
        sample_interval_ms=10,
        duration_ms=len(samples_y) * 10,
        initial_delay_ms=0,
        samples_x=tuple(0.0 for _ in samples_y),
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
        plugin = RecoilCompensationPlugin(RecoilCompensationConfig(amount=0.30))
        frame = _frame()
        output = GamepadOutput(right_y=0, auto_fire_active=True)

        plugin.apply(frame, output)

        self.assertLess(output.right_y, 0)

    def test_recoil_is_skipped_when_auto_fire_is_inactive(self):
        plugin = RecoilCompensationPlugin(RecoilCompensationConfig(amount=0.30))
        frame = _frame()
        output = GamepadOutput(right_y=0, auto_fire_active=False)

        plugin.apply(frame, output)

        self.assertEqual(output.right_y, 0)

    def test_profile_driven_playback_uses_incremental_curve_deltas(self):
        plugin = RecoilCompensationPlugin(
            RecoilCompensationConfig(),
            profile_provider=lambda _frame: _profile(samples_y=(0.0, -120.0, -260.0)),
        )

        first = GamepadOutput(right_y=0, auto_fire_active=True)
        plugin.apply(_frame(timestamp=1.00), first)

        second = GamepadOutput(right_y=0, auto_fire_active=True)
        plugin.apply(_frame(timestamp=1.01), second)

        third = GamepadOutput(right_y=0, auto_fire_active=True)
        plugin.apply(_frame(timestamp=1.02), third)

        self.assertEqual(first.right_y, 0)
        self.assertEqual(second.right_y, -120)
        self.assertEqual(third.right_y, -140)

    def test_stop_firing_resets_profile_playback_to_the_start(self):
        plugin = RecoilCompensationPlugin(
            RecoilCompensationConfig(),
            profile_provider=lambda _frame: _profile(samples_y=(0.0, -90.0, -180.0)),
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

        self.assertEqual(active.right_y, -90)
        self.assertEqual(stopped.right_y, 0)
        self.assertEqual(restarted.right_y, 0)
        self.assertEqual(resumed.right_y, -90)

    def test_weapon_change_resets_curve_before_applying_new_profile(self):
        active_profile = _profile(
            profile_id="profile-cod22-m4-ads-standing-v1",
            canonical_weapon_id="cod22-m4",
            samples_y=(0.0, -120.0, -260.0),
        )
        swapped_profile = _profile(
            profile_id="profile-cod22-kastov-ads-standing-v1",
            canonical_weapon_id="cod22-kastov-762",
            samples_y=(0.0, -35.0, -70.0),
        )
        current = {"profile": active_profile}

        plugin = RecoilCompensationPlugin(
            RecoilCompensationConfig(),
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

        self.assertEqual(before_swap.right_y, -120)
        self.assertEqual(swap_frame.right_y, 0)
        self.assertEqual(after_swap.right_y, -35)

    def test_missing_or_degraded_profile_fallback_clears_active_curve(self):
        ready_profile = _profile(samples_y=(0.0, -80.0, -160.0))
        current = {"profile": ready_profile}

        plugin = RecoilCompensationPlugin(
            RecoilCompensationConfig(),
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

        self.assertEqual(ready.right_y, -80)
        self.assertEqual(degraded.right_y, 0)
        self.assertEqual(restarted.right_y, 0)


if __name__ == "__main__":
    unittest.main()
