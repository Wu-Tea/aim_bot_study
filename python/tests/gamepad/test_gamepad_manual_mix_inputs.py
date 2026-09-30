import unittest

from tests.gamepad.benchmark_scenarios import generate_phase1_manifests
from tests.gamepad.manual_mix_inputs import (
    HIGH_INTENSITY_MANUAL_MIX_SEEDS,
    ManualMixInputConfig,
    generate_manual_mix_frames,
    high_intensity_manual_mix_config,
)


class ManualMixInputTests(unittest.TestCase):
    def test_same_manifest_and_seed_generate_identical_manual_frames(self):
        manifest = generate_phase1_manifests("mix-suite", 12345)[0]
        config = ManualMixInputConfig()

        first = generate_manual_mix_frames(
            manifest,
            manual_seed=7,
            config=config,
            sim_frames=60,
        )
        second = generate_manual_mix_frames(
            manifest,
            manual_seed=7,
            config=config,
            sim_frames=60,
        )

        self.assertEqual(first, second)

    def test_different_manual_seeds_change_the_generated_sequence(self):
        manifest = generate_phase1_manifests("mix-suite", 12345)[0]
        config = ManualMixInputConfig()

        first = generate_manual_mix_frames(
            manifest,
            manual_seed=7,
            config=config,
            sim_frames=60,
        )
        second = generate_manual_mix_frames(
            manifest,
            manual_seed=8,
            config=config,
            sim_frames=60,
        )

        self.assertNotEqual(first, second)

    def test_generator_emits_at_least_one_opposing_burst_annotation(self):
        manifest = generate_phase1_manifests("mix-suite", 12345)[8]
        config = ManualMixInputConfig()

        frames = generate_manual_mix_frames(
            manifest,
            manual_seed=3,
            config=config,
            sim_frames=120,
        )

        self.assertTrue(any(frame.in_opposing_burst for frame in frames))
        self.assertTrue(any(frame.mode == "opposing_burst" for frame in frames))

    def test_high_intensity_profile_uses_more_pressure_and_seed_coverage(self):
        manifest = generate_phase1_manifests("mix-suite", 12345)[8]
        standard_frames = generate_manual_mix_frames(
            manifest,
            manual_seed=3,
            config=ManualMixInputConfig(),
            sim_frames=120,
        )
        intense_frames = generate_manual_mix_frames(
            manifest,
            manual_seed=3,
            config=high_intensity_manual_mix_config(),
            sim_frames=120,
        )

        standard_pressure = sum(abs(frame.manual_right_x) for frame in standard_frames) / len(standard_frames)
        intense_pressure = sum(abs(frame.manual_right_x) for frame in intense_frames) / len(intense_frames)

        self.assertEqual(HIGH_INTENSITY_MANUAL_MIX_SEEDS, (1, 2, 3, 4, 5))
        self.assertGreater(intense_pressure, standard_pressure)
        self.assertGreater(
            high_intensity_manual_mix_config().event_window_frames,
            ManualMixInputConfig().event_window_frames,
        )


if __name__ == "__main__":
    unittest.main()
