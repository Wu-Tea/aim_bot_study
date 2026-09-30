import unittest

from vision.weapon_identity.adapters import ADAPTER_REGISTRY
from vision.weapon_identity.adapters import NormalizedROI
from vision.weapon_identity.adapters import SwitchSuspicionHints
from vision.weapon_identity.adapters import WeaponIdentityAdapter
from vision.weapon_identity.adapters import get_adapter


class AdapterRegistryTests(unittest.TestCase):
    def test_registry_looks_up_supported_game_ids(self):
        self.assertGreaterEqual(set(ADAPTER_REGISTRY), {"cod20", "cod21", "cod22"})

        for game_id in ("cod20", "cod21", "cod22"):
            with self.subTest(game_id=game_id):
                adapter = get_adapter(game_id)
                self.assertIs(adapter, ADAPTER_REGISTRY[game_id])
                self.assertIsInstance(adapter, WeaponIdentityAdapter)
                self.assertEqual(adapter.game_id, game_id)
                self.assertTrue(adapter.adapter_name.strip())
                self.assertTrue(adapter.expected_title_behavior.strip())

    def test_lookup_rejects_unknown_game_id(self):
        with self.assertRaisesRegex(KeyError, "cod19"):
            get_adapter("cod19")

    def test_lookup_is_case_insensitive_and_trims_surrounding_whitespace(self):
        self.assertIs(get_adapter("COD20"), get_adapter("cod20"))
        self.assertIs(get_adapter("  cOd21  "), get_adapter("cod21"))
        self.assertIs(get_adapter("\tCOD22\n"), get_adapter("cod22"))

    def test_lookup_rejects_blank_game_id(self):
        with self.assertRaisesRegex(ValueError, "game_id"):
            get_adapter("   ")


class AdapterPayloadTests(unittest.TestCase):
    def test_all_supported_adapters_expose_required_configuration(self):
        for game_id in ("cod20", "cod21", "cod22"):
            with self.subTest(game_id=game_id):
                adapter = get_adapter(game_id)

                self._assert_roi_payload_shape(adapter.weapon_icon_roi.to_dict())
                self._assert_roi_payload_shape(adapter.weapon_name_text_roi.to_dict())

                hints_payload = adapter.switch_hints.to_dict()
                self.assertIn("slot_rois", hints_payload)
                self.assertIn("switch_signal_names", hints_payload)
                self.assertIn("cache_weapon_until_switch", hints_payload)
                self.assertIn("text_window_frames", hints_payload)
                self.assertIsInstance(hints_payload["slot_rois"], list)
                self.assertIsInstance(hints_payload["switch_signal_names"], list)
                self.assertIsInstance(hints_payload["cache_weapon_until_switch"], bool)
                self.assertIsInstance(hints_payload["text_window_frames"], int)

                for slot_roi in hints_payload["slot_rois"]:
                    self._assert_roi_payload_shape(slot_roi)
                for signal_name in hints_payload["switch_signal_names"]:
                    self.assertIsInstance(signal_name, str)
                    self.assertTrue(signal_name)

    def test_title_specific_behavior_hints_follow_the_approved_design(self):
        cod22 = get_adapter("cod22")
        cod21 = get_adapter("cod21")
        cod20 = get_adapter("cod20")

        self.assertGreater(len(cod22.switch_hints.slot_rois), 0)
        self.assertIn("weapon_name_banner", cod22.switch_hints.switch_signal_names)
        self.assertFalse(cod22.switch_hints.cache_weapon_until_switch)
        self.assertGreater(cod22.switch_hints.text_window_frames, 0)

        self.assertTrue(cod21.switch_hints.cache_weapon_until_switch)
        self.assertIn("weapon_name_banner", cod21.switch_hints.switch_signal_names)
        self.assertGreater(cod21.switch_hints.text_window_frames, 0)

        self.assertGreater(len(cod20.switch_hints.slot_rois), 0)
        self.assertIn("slot_indicator_change", cod20.switch_hints.switch_signal_names)
        self.assertIn("weapon_name_banner", cod20.switch_hints.switch_signal_names)
        self.assertFalse(cod20.switch_hints.cache_weapon_until_switch)

    def _assert_roi_payload_shape(self, payload):
        self.assertIn("left", payload)
        self.assertIn("top", payload)
        self.assertIn("width", payload)
        self.assertIn("height", payload)
        for key in ("left", "top", "width", "height"):
            self.assertIsInstance(payload[key], float)
            self.assertGreater(payload[key], 0.0 if key in {"width", "height"} else -0.0000001)
            self.assertLessEqual(payload[key], 1.0)
        self.assertLessEqual(payload["left"] + payload["width"], 1.0)
        self.assertLessEqual(payload["top"] + payload["height"], 1.0)


class NormalizedROIValidationTests(unittest.TestCase):
    def test_rejects_invalid_roi_bounds(self):
        invalid_rois = (
            ({"left": -0.01, "top": 0.2, "width": 0.1, "height": 0.1}, "left"),
            ({"left": 0.2, "top": 0.2, "width": 0.0, "height": 0.1}, "width"),
            ({"left": 0.95, "top": 0.2, "width": 0.1, "height": 0.1}, "horizontal"),
            ({"left": 0.2, "top": 0.95, "width": 0.1, "height": 0.1}, "vertical"),
        )

        for kwargs, message in invalid_rois:
            with self.subTest(kwargs=kwargs):
                with self.assertRaisesRegex(ValueError, message):
                    NormalizedROI(**kwargs)


class WeaponIdentityAdapterValidationTests(unittest.TestCase):
    def test_rejects_blank_ids_or_names(self):
        invalid_updates = (
            ({"game_id": "   "}, "game_id"),
            ({"adapter_name": " \t "}, "adapter_name"),
            ({"expected_title_behavior": "\n"}, "expected_title_behavior"),
        )

        for updates, message in invalid_updates:
            with self.subTest(updates=updates):
                kwargs = self._build_valid_adapter_kwargs()
                kwargs.update(updates)
                with self.assertRaisesRegex(ValueError, message):
                    WeaponIdentityAdapter(**kwargs)

    def _build_valid_adapter_kwargs(self):
        return {
            "game_id": "cod-test",
            "adapter_name": "Test Adapter",
            "expected_title_behavior": "Uses stable HUD cues.",
            "weapon_icon_roi": NormalizedROI(left=0.1, top=0.1, width=0.2, height=0.2),
            "weapon_name_text_roi": NormalizedROI(left=0.2, top=0.2, width=0.2, height=0.1),
            "switch_hints": SwitchSuspicionHints(),
        }


class SwitchSuspicionHintsValidationTests(unittest.TestCase):
    def test_normalizes_sequence_inputs_to_immutable_tuples(self):
        roi = NormalizedROI(left=0.1, top=0.1, width=0.2, height=0.2)

        hints = SwitchSuspicionHints(
            slot_rois=[roi],
            switch_signal_names=["weapon_name_banner"],
            cache_weapon_until_switch=True,
            text_window_frames=12,
        )

        self.assertEqual(hints.slot_rois, (roi,))
        self.assertEqual(hints.switch_signal_names, ("weapon_name_banner",))

    def test_rejects_invalid_switch_suspicion_inputs(self):
        invalid_cases = (
            ({"slot_rois": ("not-a-roi",)}, "slot_rois"),
            ({"switch_signal_names": "weapon_name_banner"}, "switch_signal_names"),
            ({"switch_signal_names": ("weapon_name_banner", "   ")}, "switch_signal_names"),
            ({"cache_weapon_until_switch": 1}, "cache_weapon_until_switch"),
            ({"text_window_frames": -1}, "text_window_frames"),
        )

        for updates, message in invalid_cases:
            with self.subTest(updates=updates):
                kwargs = {
                    "slot_rois": (),
                    "switch_signal_names": (),
                    "cache_weapon_until_switch": False,
                    "text_window_frames": 0,
                }
                kwargs.update(updates)
                with self.assertRaisesRegex(ValueError, message):
                    SwitchSuspicionHints(**kwargs)


if __name__ == "__main__":
    unittest.main()
