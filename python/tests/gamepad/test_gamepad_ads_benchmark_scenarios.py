import unittest

from tests.gamepad.ads_benchmark_scenarios import (
    AdsLocalizationEvent,
    AdsScenarioManifest,
    AdsTargetSpec,
    AdsVisibilityGap,
    expand_ads_manifest,
    generate_ads_manifests,
)


class AdsScenarioManifestTests(unittest.TestCase):
    def test_manifest_from_dict_rejects_schema_drift(self):
        manifest = AdsScenarioManifest(
            scenario_key="ads-run-s00",
            family="single_static_offset",
            engagement_target_id="engagement",
            targets=(
                AdsTargetSpec(
                    target_id="engagement",
                    initial_dx=80.0,
                    initial_dy=-32.0,
                    velocity_x=0.0,
                    velocity_y=0.0,
                ),
            ),
        )
        payload = manifest.to_dict()

        with self.assertRaises(ValueError):
            AdsScenarioManifest.from_dict(
                {
                    key: value
                    for key, value in payload.items()
                    if key != "localization_schedule"
                }
            )

        with self.assertRaises(ValueError):
            AdsScenarioManifest.from_dict({**payload, "unexpected": True})

    def test_dual_target_manifest_requires_two_targets(self):
        with self.assertRaises(ValueError):
            AdsScenarioManifest(
                scenario_key="ads-run-s00",
                family="dual_target_disambiguation",
                engagement_target_id="engagement",
                targets=(
                    AdsTargetSpec(
                        target_id="engagement",
                        initial_dx=70.0,
                        initial_dy=20.0,
                        velocity_x=0.0,
                        velocity_y=0.0,
                    ),
                ),
            )

    def test_reacquire_manifest_requires_a_gap(self):
        with self.assertRaises(ValueError):
            AdsScenarioManifest(
                scenario_key="ads-run-s00",
                family="reacquire_after_gap",
                engagement_target_id="engagement",
                targets=(
                    AdsTargetSpec(
                        target_id="engagement",
                        initial_dx=60.0,
                        initial_dy=-20.0,
                        velocity_x=240.0,
                        velocity_y=0.0,
                    ),
                ),
            )


class AdsScenarioExpansionTests(unittest.TestCase):
    def test_expand_manifest_hides_target_during_gap_and_restores_localization_after_gap(self):
        manifest = AdsScenarioManifest(
            scenario_key="ads-run-s00",
            family="reacquire_after_gap",
            engagement_target_id="engagement",
            targets=(
                AdsTargetSpec(
                    target_id="engagement",
                    initial_dx=80.0,
                    initial_dy=-16.0,
                    velocity_x=120.0,
                    velocity_y=0.0,
                    gap_windows=(AdsVisibilityGap(start_frame=2, duration_frames=2),),
                ),
            ),
        )

        frames = expand_ads_manifest(manifest, frame_dt=1.0 / 60.0, sim_frames=5)

        self.assertEqual(len(frames), 5)
        self.assertEqual(frames[0].localized_target_id, "engagement")
        self.assertTrue(frames[1].targets[0].visible)
        self.assertFalse(frames[2].targets[0].visible)
        self.assertIsNone(frames[2].localized_target_id)
        self.assertFalse(frames[3].targets[0].visible)
        self.assertTrue(frames[4].targets[0].visible)
        self.assertEqual(frames[4].localized_target_id, "engagement")
        self.assertGreater(frames[4].targets[0].target_x, frames[1].targets[0].target_x)

    def test_expand_manifest_applies_dual_target_localization_schedule(self):
        manifest = AdsScenarioManifest(
            scenario_key="ads-run-s00",
            family="dual_target_disambiguation",
            engagement_target_id="engagement",
            targets=(
                AdsTargetSpec(
                    target_id="engagement",
                    initial_dx=70.0,
                    initial_dy=20.0,
                    velocity_x=0.0,
                    velocity_y=0.0,
                ),
                AdsTargetSpec(
                    target_id="distractor",
                    initial_dx=98.0,
                    initial_dy=20.0,
                    velocity_x=0.0,
                    velocity_y=0.0,
                ),
            ),
            localization_schedule=(
                AdsLocalizationEvent(frame=0, target_id="distractor"),
                AdsLocalizationEvent(frame=2, target_id="engagement"),
            ),
        )

        frames = expand_ads_manifest(manifest, frame_dt=1.0 / 60.0, sim_frames=4)

        self.assertEqual([frame.localized_target_id for frame in frames], ["distractor", "distractor", "engagement", "engagement"])
        self.assertEqual(frames[0].engagement_target_id, "engagement")
        self.assertEqual({target.target_id for target in frames[0].targets}, {"engagement", "distractor"})

    def test_expand_static_manifest_keeps_position_constant(self):
        manifest = AdsScenarioManifest(
            scenario_key="ads-run-s00",
            family="single_static_offset",
            engagement_target_id="engagement",
            targets=(
                AdsTargetSpec(
                    target_id="engagement",
                    initial_dx=-120.0,
                    initial_dy=48.0,
                    velocity_x=0.0,
                    velocity_y=0.0,
                ),
            ),
        )

        frames = expand_ads_manifest(manifest, frame_dt=1.0 / 60.0, sim_frames=4)

        self.assertEqual([frame.targets[0].target_x for frame in frames], [-120.0, -120.0, -120.0, -120.0])
        self.assertEqual([frame.targets[0].target_y for frame in frames], [48.0, 48.0, 48.0, 48.0])


class AdsScenarioGenerationTests(unittest.TestCase):
    def test_generate_ads_manifests_is_deterministic_and_run_key_only_changes_scenario_key(self):
        alpha = generate_ads_manifests("run-alpha", 12345)
        bravo = generate_ads_manifests("run-bravo", 12345)

        def normalize(manifest):
            payload = manifest.to_dict()
            payload["scenario_key"] = "<normalized>"
            return payload

        self.assertEqual(len(alpha), 36)
        self.assertEqual(len(bravo), 36)
        self.assertEqual([manifest.scenario_key for manifest in alpha], [f"run-alpha-ads-s{i:02d}" for i in range(36)])
        self.assertEqual([manifest.scenario_key for manifest in bravo], [f"run-bravo-ads-s{i:02d}" for i in range(36)])
        self.assertEqual([normalize(manifest) for manifest in alpha], [normalize(manifest) for manifest in bravo])

    def test_generate_ads_manifests_uses_expected_family_counts(self):
        manifests = generate_ads_manifests("run-alpha", 12345)

        families = [manifest.family for manifest in manifests]
        self.assertEqual(families.count("single_static_offset"), 8)
        self.assertEqual(families.count("single_strafe_then_decel"), 8)
        self.assertEqual(families.count("single_diagonal_then_decel"), 8)
        self.assertEqual(families.count("reacquire_after_gap"), 6)
        self.assertEqual(families.count("dual_target_disambiguation"), 6)


if __name__ == "__main__":
    unittest.main()
