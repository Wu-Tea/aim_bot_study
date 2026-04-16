import math
import unittest

from tests.gamepad.benchmark_scenarios import (
    DecelEvent,
    InitialState,
    ResumeEvent,
    ScenarioManifest,
    TurnEvent,
    expand_manifest,
    generate_phase1_manifests,
)


class ScenarioExpansionTests(unittest.TestCase):
    def test_expand_manifest_applies_turn_decel_and_resume_events(self):
        manifest = ScenarioManifest(
            scenario_key="replay-s00",
            kind="turn_then_decel",
            initial_state=InitialState(
                initial_dx=0.0,
                initial_dy=0.0,
                initial_speed_px_per_sec=120.0,
                initial_heading_deg=0.0,
            ),
            turn_events=(TurnEvent(frame=2, delta_heading_deg=90.0, speed_scale=0.5),),
            decel_events=(DecelEvent(frame=4, duration_frames=2, target_speed_scale=0.0, hard_stop=True),),
            resume_events=(ResumeEvent(frame=6, duration_frames=2, target_speed_scale=1.0),),
        )

        states = expand_manifest(manifest, frame_dt=1.0 / 60.0, sim_frames=8)

        self.assertEqual(len(states), 8)
        self.assertGreater(states[1].target_x, states[0].target_x)
        self.assertAlmostEqual(states[2].heading_deg, 90.0, places=3)
        self.assertLess(states[3].speed_px_per_sec, 120.0)
        self.assertAlmostEqual(states[5].speed_px_per_sec, 0.0, places=3)
        self.assertGreater(states[7].speed_px_per_sec, 0.0)


class ScenarioManifestGenerationTests(unittest.TestCase):
    def test_generate_phase1_manifests_ignores_run_key_for_content(self):
        alpha = generate_phase1_manifests("run-alpha", 12345)
        bravo = generate_phase1_manifests("run-bravo", 12345)

        def normalize(manifest):
            payload = manifest.to_dict()
            payload["scenario_key"] = "<normalized>"
            return payload

        self.assertEqual([m.scenario_key for m in alpha], [f"run-alpha-s{i:02d}" for i in range(24)])
        self.assertEqual([m.scenario_key for m in bravo], [f"run-bravo-s{i:02d}" for i in range(24)])
        self.assertEqual([normalize(m) for m in alpha], [normalize(m) for m in bravo])

    def test_scenario_manifest_from_dict_rejects_malformed_payloads(self):
        manifest = generate_phase1_manifests("run-alpha", 12345)[0].to_dict()

        with self.assertRaises(ValueError):
            ScenarioManifest.from_dict(
                {
                    key: value
                    for key, value in manifest.items()
                    if key != "resume_events"
                }
            )

        with self.assertRaises(ValueError):
            ScenarioManifest.from_dict({**manifest, "unexpected": True})

    def test_scenario_manifest_from_dict_rejects_nested_type_drift(self):
        manifest = generate_phase1_manifests("run-alpha", 12345)[0].to_dict()
        decel_manifest = generate_phase1_manifests("run-alpha", 12345)[8].to_dict()

        with self.assertRaises(ValueError):
            ScenarioManifest.from_dict({**manifest, "scenario_key": 123})

        with self.assertRaises(ValueError):
            ScenarioManifest.from_dict({**manifest, "kind": 456})

        with self.assertRaises(ValueError):
            ScenarioManifest.from_dict(
                {
                    **manifest,
                    "initial_state": {
                        **manifest["initial_state"],
                        "initial_dx": "23.8",
                    },
                }
            )

        with self.assertRaises(ValueError):
            ScenarioManifest.from_dict(
                {
                    **manifest,
                    "turn_events": [
                        {
                            **manifest["turn_events"][0],
                            "frame": "69",
                        }
                    ],
                }
            )

        with self.assertRaises(ValueError):
            ScenarioManifest.from_dict(
                {
                    **decel_manifest,
                    "decel_events": [
                        {
                            **decel_manifest["decel_events"][0],
                            "hard_stop": "false",
                        }
                    ],
                }
            )

        with self.assertRaises(ValueError):
            ScenarioManifest.from_dict(
                {
                    **manifest,
                    "resume_events": [
                        {
                            "frame": 1,
                            "duration_frames": 2,
                            "target_speed_scale": "0.7",
                        }
                    ],
                }
            )

    def test_scenario_manifest_from_dict_rejects_invalid_phase1_semantics(self):
        steady_turns = generate_phase1_manifests("run-alpha", 12345)[0].to_dict()
        turn_then_decel = generate_phase1_manifests("run-alpha", 12345)[8].to_dict()
        decel_resume = generate_phase1_manifests("run-alpha", 12345)[16].to_dict()

        with self.assertRaises(ValueError):
            ScenarioManifest.from_dict({**steady_turns, "kind": "invalid_kind"})

        with self.assertRaises(ValueError):
            ScenarioManifest.from_dict(
                {
                    **steady_turns,
                    "turn_events": [
                        {
                            **steady_turns["turn_events"][0],
                            "frame": -1,
                        }
                    ],
                }
            )

        with self.assertRaises(ValueError):
            ScenarioManifest.from_dict(
                {
                    **steady_turns,
                    "turn_events": [
                        {
                            **steady_turns["turn_events"][0],
                            "frame": 0,
                        }
                    ],
                    "decel_events": [
                        {
                            "frame": 5,
                            "duration_frames": 3,
                            "target_speed_scale": 0.5,
                            "hard_stop": False,
                        }
                    ],
                }
            )

        with self.assertRaises(ValueError):
            ScenarioManifest.from_dict(
                {
                    **turn_then_decel,
                    "decel_events": [
                        {
                            **turn_then_decel["decel_events"][0],
                            "frame": turn_then_decel["turn_events"][0]["frame"] - 1,
                        }
                    ],
                }
            )

        with self.assertRaises(ValueError):
            ScenarioManifest.from_dict(
                {
                    **turn_then_decel,
                    "turn_events": [],
                }
            )

        with self.assertRaises(ValueError):
            ScenarioManifest.from_dict(
                {
                    **turn_then_decel,
                    "decel_events": [],
                }
            )

        with self.assertRaises(ValueError):
            ScenarioManifest.from_dict(
                {
                    **decel_resume,
                    "resume_events": [
                        {
                            **decel_resume["resume_events"][0],
                            "frame": decel_resume["decel_events"][0]["frame"] + decel_resume["decel_events"][0]["duration_frames"] - 1,
                        }
                    ],
                }
            )

        with self.assertRaises(ValueError):
            ScenarioManifest.from_dict(
                {
                    **decel_resume,
                    "decel_events": [
                        {
                            **decel_resume["decel_events"][0],
                            "duration_frames": 0,
                        }
                    ],
                }
            )

    def test_generate_phase1_manifests_is_deterministic_for_same_inputs(self):
        first = generate_phase1_manifests("run-alpha", 12345)
        second = generate_phase1_manifests("run-alpha", 12345)

        self.assertEqual(len(first), 24)
        self.assertEqual([manifest.to_dict() for manifest in first], [manifest.to_dict() for manifest in second])
        self.assertEqual(
            [manifest.scenario_key for manifest in first],
            [f"run-alpha-s{i:02d}" for i in range(24)],
        )
        self.assertEqual(
            [manifest.kind for manifest in first],
            ["steady_turns"] * 8 + ["turn_then_decel"] * 8 + ["decel_resume"] * 8,
        )
        self.assertEqual(
            first[0].to_dict(),
            {
                "scenario_key": "run-alpha-s00",
                "kind": "steady_turns",
                "initial_state": {
                    "initial_dx": -31.121898018627185,
                    "initial_dy": 24.436948670871345,
                    "initial_speed_px_per_sec": 295.63890612808274,
                    "initial_heading_deg": -94.3198525579494,
                },
                "turn_events": [
                    {
                        "frame": 19,
                        "delta_heading_deg": -97.95359267090777,
                        "speed_scale": 0.9291163019747569,
                    },
                    {
                        "frame": 70,
                        "delta_heading_deg": 12.11145750005953,
                        "speed_scale": 0.9206401015982615,
                    },
                ],
                "decel_events": [],
                "resume_events": [],
            },
        )
        self.assertEqual(
            first[8].to_dict(),
            {
                "scenario_key": "run-alpha-s08",
                "kind": "turn_then_decel",
                "initial_state": {
                    "initial_dx": -20.63879057556563,
                    "initial_dy": -17.39923878768377,
                    "initial_speed_px_per_sec": 386.76565102693115,
                    "initial_heading_deg": 100.42816905145872,
                },
                "turn_events": [
                    {
                        "frame": 27,
                        "delta_heading_deg": 14.725051171851305,
                        "speed_scale": 0.9151600578597532,
                    }
                ],
                "decel_events": [
                    {
                        "frame": 98,
                        "duration_frames": 25,
                        "target_speed_scale": 0.0,
                        "hard_stop": True,
                    }
                ],
                "resume_events": [],
            },
        )
        self.assertEqual(
            first[16].to_dict(),
            {
                "scenario_key": "run-alpha-s16",
                "kind": "decel_resume",
                "initial_state": {
                    "initial_dx": -39.04699244352808,
                    "initial_dy": -4.62568686522507,
                    "initial_speed_px_per_sec": 366.295328448036,
                    "initial_heading_deg": -160.7871144406381,
                },
                "turn_events": [],
                "decel_events": [
                    {
                        "frame": 71,
                        "duration_frames": 30,
                        "target_speed_scale": 0.30250985912652373,
                        "hard_stop": False,
                    }
                ],
                "resume_events": [
                    {
                        "frame": 132,
                        "duration_frames": 32,
                        "target_speed_scale": 1.0426749730701081,
                    }
                ],
            },
        )
        self.assertEqual(
            first[-1].to_dict(),
            {
                "scenario_key": "run-alpha-s23",
                "kind": "decel_resume",
                "initial_state": {
                    "initial_dx": 22.869586594271766,
                    "initial_dy": -12.205975646953938,
                    "initial_speed_px_per_sec": 322.5600503549402,
                    "initial_heading_deg": 66.78120141025897,
                },
                "turn_events": [],
                "decel_events": [
                    {
                        "frame": 72,
                        "duration_frames": 20,
                        "target_speed_scale": 0.43014009950198945,
                        "hard_stop": False,
                    }
                ],
                "resume_events": [
                    {
                        "frame": 152,
                        "duration_frames": 28,
                        "target_speed_scale": 1.0403447212522097,
                    }
                ],
            },
        )

        round_tripped = [type(manifest).from_dict(manifest.to_dict()) for manifest in first]
        self.assertEqual([manifest.to_dict() for manifest in first], [manifest.to_dict() for manifest in round_tripped])

    def test_generate_phase1_manifests_has_expected_family_counts(self):
        manifests = generate_phase1_manifests("run-beta", 98765)

        family_counts = {}
        for manifest in manifests:
            family_counts[manifest.kind] = family_counts.get(manifest.kind, 0) + 1

        self.assertEqual(len(manifests), 24)
        self.assertEqual(
            family_counts,
            {
                "steady_turns": 8,
                "turn_then_decel": 8,
                "decel_resume": 8,
            },
        )

    def test_initial_state_records_offsets_separately_from_heading_and_speed(self):
        manifest = generate_phase1_manifests("run-alpha", 12345)[0]

        derived_dx = manifest.initial_state.initial_speed_px_per_sec * math.cos(
            math.radians(manifest.initial_state.initial_heading_deg)
        )
        derived_dy = manifest.initial_state.initial_speed_px_per_sec * math.sin(
            math.radians(manifest.initial_state.initial_heading_deg)
        )

        self.assertNotAlmostEqual(manifest.initial_state.initial_dx, derived_dx)
        self.assertNotAlmostEqual(manifest.initial_state.initial_dy, derived_dy)


if __name__ == "__main__":
    unittest.main()
