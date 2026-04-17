import unittest

from tests.gamepad.benchmark_scenarios import generate_phase1_manifests
from tests.gamepad.manual_mix_inputs import (
    ManualMixInputConfig,
    generate_manual_mix_frames,
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


if __name__ == "__main__":
    unittest.main()
