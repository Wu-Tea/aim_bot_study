import unittest

from vision.runner import AdsAutoFireGate


class AdsAutoFireGateTests(unittest.TestCase):
    def test_blocks_autofire_for_120ms_after_ads_starts(self):
        gate = AdsAutoFireGate(delay_seconds=0.12)

        gate.on_aiming(True, timestamp=1.00)

        self.assertFalse(gate.allow_auto_fire(True, timestamp=1.05))
        self.assertFalse(gate.allow_auto_fire(True, timestamp=1.119))
        self.assertTrue(gate.allow_auto_fire(True, timestamp=1.12))

    def test_reset_on_ads_release_requires_new_delay(self):
        gate = AdsAutoFireGate(delay_seconds=0.12)

        gate.on_aiming(True, timestamp=1.00)
        self.assertTrue(gate.allow_auto_fire(True, timestamp=1.20))

        gate.on_aiming(False, timestamp=1.30)
        gate.on_aiming(True, timestamp=2.00)

        self.assertFalse(gate.allow_auto_fire(True, timestamp=2.05))
        self.assertTrue(gate.allow_auto_fire(True, timestamp=2.12))

    def test_false_raw_autofire_stays_false_after_delay(self):
        gate = AdsAutoFireGate(delay_seconds=0.12)

        gate.on_aiming(True, timestamp=1.00)

        self.assertFalse(gate.allow_auto_fire(False, timestamp=1.20))


if __name__ == "__main__":
    unittest.main()
