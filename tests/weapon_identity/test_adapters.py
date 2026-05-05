import unittest

from vision.weapon_identity.adapters import ADAPTER_REGISTRY
from vision.weapon_identity.adapters import COD20WeaponIdentityAdapter
from vision.weapon_identity.adapters import COD21WeaponIdentityAdapter
from vision.weapon_identity.adapters import COD22WeaponIdentityAdapter
from vision.weapon_identity.adapters import get_adapter


class AdapterRegistryTests(unittest.TestCase):
    def test_registry_looks_up_expected_adapter_for_each_supported_game(self):
        expected_types = {
            "cod20": COD20WeaponIdentityAdapter,
            "cod21": COD21WeaponIdentityAdapter,
            "cod22": COD22WeaponIdentityAdapter,
        }

        self.assertEqual(set(ADAPTER_REGISTRY), set(expected_types))

        for game_id, adapter_type in expected_types.items():
            with self.subTest(game_id=game_id):
                adapter = get_adapter(game_id)
                self.assertIs(adapter, ADAPTER_REGISTRY[game_id])
                self.assertIsInstance(adapter, adapter_type)
                self.assertEqual(adapter.game_id, game_id)
                self.assertTrue(adapter.adapter_name)
                self.assertTrue(adapter.expected_title_behavior)

    def test_lookup_rejects_unknown_game_id(self):
        with self.assertRaisesRegex(KeyError, "cod19"):
            get_adapter("cod19")


class AdapterPayloadTests(unittest.TestCase):
    def test_all_supported_adapters_expose_normalized_roi_payloads(self):
        for game_id in ("cod20", "cod21", "cod22"):
            with self.subTest(game_id=game_id):
                adapter = get_adapter(game_id)

                self._assert_roi_payload_shape(adapter.weapon_icon_roi.to_dict())
                self._assert_roi_payload_shape(adapter.weapon_name_text_roi.to_dict())

                hints_payload = adapter.switch_hints.to_dict()
                self.assertEqual(
                    set(hints_payload),
                    {
                        "slot_rois",
                        "switch_signal_names",
                        "cache_weapon_until_switch",
                        "text_window_frames",
                    },
                )
                self.assertIsInstance(hints_payload["slot_rois"], list)
                self.assertIsInstance(hints_payload["switch_signal_names"], list)
                self.assertIsInstance(hints_payload["cache_weapon_until_switch"], bool)

                for slot_roi in hints_payload["slot_rois"]:
                    self._assert_roi_payload_shape(slot_roi)
                for signal_name in hints_payload["switch_signal_names"]:
                    self.assertIsInstance(signal_name, str)
                    self.assertTrue(signal_name)

    def test_title_specific_behavior_hints_follow_the_approved_design(self):
        cod22 = get_adapter("cod22")
        cod21 = get_adapter("cod21")
        cod20 = get_adapter("cod20")

        self.assertIn("blueprint", cod22.expected_title_behavior.casefold())
        self.assertIn("weapon_name_banner", cod22.switch_hints.switch_signal_names)
        self.assertFalse(cod22.switch_hints.cache_weapon_until_switch)

        self.assertIn("switch", cod21.expected_title_behavior.casefold())
        self.assertTrue(cod21.switch_hints.cache_weapon_until_switch)
        self.assertGreater(cod21.switch_hints.text_window_frames, 0)

        self.assertIn("icon", cod20.expected_title_behavior.casefold())
        self.assertIn("text", cod20.expected_title_behavior.casefold())
        self.assertIn("slot_indicator_change", cod20.switch_hints.switch_signal_names)

    def _assert_roi_payload_shape(self, payload):
        self.assertEqual(set(payload), {"left", "top", "width", "height"})
        for key in ("left", "top", "width", "height"):
            self.assertIsInstance(payload[key], float)
            self.assertGreater(payload[key], 0.0 if key in {"width", "height"} else -0.0000001)
            self.assertLessEqual(payload[key], 1.0)
        self.assertLessEqual(payload["left"] + payload["width"], 1.0)
        self.assertLessEqual(payload["top"] + payload["height"], 1.0)


if __name__ == "__main__":
    unittest.main()
