import unittest

from tests.gamepad.ads_benchmark_scenarios import AdsScenarioManifest, AdsTargetSpec
from tests.gamepad.ads_manual_inputs import (
    AdsManualInputConfig,
    AdsManualInputGenerator,
    generate_ads_manual_input_frames,
)


class AdsManualInputTests(unittest.TestCase):
    def setUp(self):
        self.manifest = AdsScenarioManifest(
            scenario_key="ads-run-s00",
            family="single_static_offset",
            engagement_target_id="engagement",
            targets=(
                AdsTargetSpec(
                    target_id="engagement",
                    initial_dx=90.0,
                    initial_dy=-30.0,
                    velocity_x=0.0,
                    velocity_y=0.0,
                ),
            ),
        )

    def test_none_profile_outputs_zero_input(self):
        frames = generate_ads_manual_input_frames(
            self.manifest,
            input_profile="none",
            sim_frames=4,
        )

        self.assertTrue(all(frame.manual_right_x == 0 for frame in frames))
        self.assertTrue(all(frame.manual_right_y == 0 for frame in frames))
        self.assertTrue(all(not frame.in_opposing_burst for frame in frames))

    def test_aligned_follow_points_toward_error_reduction(self):
        generator = AdsManualInputGenerator(self.manifest, input_profile="aligned_follow")

        frame = generator.generate_frame(frame=0, error_x=60.0, error_y=-20.0)

        self.assertGreater(frame.manual_right_x, 0)
        self.assertGreater(frame.manual_right_y, 0)
        self.assertEqual(frame.profile, "aligned_follow")

    def test_opposing_burst_has_early_wrong_direction_window(self):
        frames = generate_ads_manual_input_frames(
            self.manifest,
            input_profile="opposing_burst",
            sim_frames=20,
        )

        burst_frames = [frame for frame in frames if frame.in_opposing_burst]
        self.assertGreaterEqual(len(burst_frames), 2)
        self.assertLessEqual(burst_frames[0].frame, 12)
        self.assertTrue(all(frame.manual_right_x < 0 for frame in burst_frames))

    def test_overshoot_recover_transitions_from_aligned_into_recovery(self):
        frames = generate_ads_manual_input_frames(
            self.manifest,
            input_profile="overshoot_recover",
            sim_frames=12,
        )

        first = frames[0]
        recovery_frames = [frame for frame in frames if frame.in_recovery_window]

        self.assertGreater(first.manual_right_x, 0)
        self.assertGreater(first.manual_right_y, 0)
        self.assertGreaterEqual(len(recovery_frames), 2)
        self.assertTrue(any(frame.manual_right_x < 0 for frame in recovery_frames))

    def test_generation_is_deterministic_for_same_manifest_and_profile(self):
        first = generate_ads_manual_input_frames(
            self.manifest,
            input_profile="opposing_burst",
            sim_frames=18,
            config=AdsManualInputConfig(),
        )
        second = generate_ads_manual_input_frames(
            self.manifest,
            input_profile="opposing_burst",
            sim_frames=18,
            config=AdsManualInputConfig(),
        )

        self.assertEqual(first, second)


if __name__ == "__main__":
    unittest.main()
