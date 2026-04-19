import math
import random
import unittest

from controllers.gamepad.ai_aim import AIAimConfig
from controllers.gamepad.legacy_ai_aim import (
    AdaptiveDeltaGainSubPlugin,
    HorizontalAssistSubPlugin,
    LegacyAIAimPlugin,
    OvershootGuardSubPlugin,
)
from controllers.gamepad.state import GamepadFrame, GamepadOutput


FRAME_DT = 1.0 / 60.0
SIM_FRAMES = 120
MEASURE_FROM_FRAME = 60
MAX_RETICLE_SPEED_PPS = 1500.0
STICK_MAX = 32767


def _build_plugin(with_adaptive: bool) -> LegacyAIAimPlugin:
    subs = []
    if with_adaptive:
        subs.append(AdaptiveDeltaGainSubPlugin())
    subs.append(HorizontalAssistSubPlugin())
    subs.append(OvershootGuardSubPlugin())
    return LegacyAIAimPlugin(AIAimConfig(), sub_plugins=tuple(subs))


def _run_scenario(plugin, *, target_vx, target_vy, initial_dx, initial_dy):
    reticle_x = 0.0
    reticle_y = 0.0
    target_x = initial_dx
    target_y = initial_dy

    errors = []
    for frame_idx in range(SIM_FRAMES):
        timestamp = frame_idx * FRAME_DT
        error_x = target_x - reticle_x
        error_y = target_y - reticle_y

        frame = GamepadFrame(
            timestamp=timestamp,
            left_x=0,
            left_y=0,
            manual_right_x=0,
            manual_right_y=0,
            left_trigger=255,
            right_trigger=0,
            buttons={},
            is_aiming=True,
            target_dx=error_x,
            target_dy=error_y,
            auto_fire_requested=False,
            target_revision=frame_idx + 1,
            target_timestamp=timestamp,
        )
        output = GamepadOutput(right_x=0, right_y=0, left_trigger=255)
        plugin.apply(frame, output)

        stick_x = max(-STICK_MAX, min(STICK_MAX, output.right_x))
        stick_y = max(-STICK_MAX, min(STICK_MAX, output.right_y))
        reticle_x += (stick_x / STICK_MAX) * MAX_RETICLE_SPEED_PPS * FRAME_DT
        reticle_y += (-stick_y / STICK_MAX) * MAX_RETICLE_SPEED_PPS * FRAME_DT

        target_x += target_vx * FRAME_DT
        target_y += target_vy * FRAME_DT

        if frame_idx >= MEASURE_FROM_FRAME:
            errors.append(math.hypot(error_x, error_y))

    return sum(errors) / len(errors)


class AdaptiveDeltaGainSimulationTests(unittest.TestCase):
    def test_adaptive_reduces_mean_tracking_error_on_randomised_targets(self):
        rng = random.Random(20260413)
        trials = 30

        baseline_errors = []
        adaptive_errors = []

        for _ in range(trials):
            speed = rng.uniform(280.0, 520.0)
            angle = rng.uniform(0.0, 2.0 * math.pi)
            target_vx = speed * math.cos(angle)
            target_vy = speed * math.sin(angle)
            initial_dx = rng.uniform(-40.0, 40.0)
            initial_dy = rng.uniform(-25.0, 25.0)

            baseline = _build_plugin(with_adaptive=False)
            adaptive = _build_plugin(with_adaptive=True)

            baseline_errors.append(
                _run_scenario(
                    baseline,
                    target_vx=target_vx,
                    target_vy=target_vy,
                    initial_dx=initial_dx,
                    initial_dy=initial_dy,
                )
            )
            adaptive_errors.append(
                _run_scenario(
                    adaptive,
                    target_vx=target_vx,
                    target_vy=target_vy,
                    initial_dx=initial_dx,
                    initial_dy=initial_dy,
                )
            )

        baseline_mean = sum(baseline_errors) / trials
        adaptive_mean = sum(adaptive_errors) / trials
        adaptive_wins = sum(1 for a, b in zip(adaptive_errors, baseline_errors) if a < b)

        self.assertLess(
            adaptive_mean,
            baseline_mean,
            msg=f"adaptive_mean={adaptive_mean:.2f} baseline_mean={baseline_mean:.2f}",
        )
        self.assertGreaterEqual(
            adaptive_wins,
            int(trials * 0.7),
            msg=f"adaptive only won {adaptive_wins}/{trials} trials",
        )

    def test_adaptive_no_worse_on_slow_targets(self):
        rng = random.Random(20260414)
        trials = 20

        for _ in range(trials):
            speed = rng.uniform(80.0, 180.0)
            angle = rng.uniform(0.0, 2.0 * math.pi)
            target_vx = speed * math.cos(angle)
            target_vy = speed * math.sin(angle)

            baseline = _build_plugin(with_adaptive=False)
            adaptive = _build_plugin(with_adaptive=True)

            baseline_err = _run_scenario(
                baseline, target_vx=target_vx, target_vy=target_vy,
                initial_dx=0.0, initial_dy=0.0,
            )
            adaptive_err = _run_scenario(
                adaptive, target_vx=target_vx, target_vy=target_vy,
                initial_dx=0.0, initial_dy=0.0,
            )

            self.assertLessEqual(
                adaptive_err,
                baseline_err + 3.0,
                msg=f"adaptive regressed on slow target: adaptive={adaptive_err:.2f} baseline={baseline_err:.2f}",
            )


if __name__ == "__main__":
    unittest.main()
