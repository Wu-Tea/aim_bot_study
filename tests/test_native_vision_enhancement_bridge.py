import unittest

from tests.test_native_vision_targeting_bridge import _load_native_module


CENTER_X = 320.0
CENTER_Y = 320.0


def _process(enhancer, dx, dy, *, timestamp, slow_zone=None, source="observed"):
    return enhancer.process(
        CENTER_X + dx,
        CENTER_Y + dy,
        CENTER_X,
        CENTER_Y,
        slow_zone,
        source,
        timestamp,
    )


class NativeVisionEnhancementBridgeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.module = _load_native_module()

    def test_native_aim_enhancer_is_exposed(self):
        self.assertTrue(
            hasattr(self.module, "NativeAimEnhancer"),
            "vision_native_cpp should expose NativeAimEnhancer for Phase 3B enhancement parity",
        )

    def test_default_pipeline_damps_converging_error_inside_slow_zone_without_reversing(self):
        if not hasattr(self.module, "NativeAimEnhancer"):
            self.fail("NativeAimEnhancer is missing")

        enhancer = self.module.NativeAimEnhancer()
        slow_zone = (300.0, 300.0, 340.0, 340.0)
        frame_dt = 1.0 / 80.0

        _process(enhancer, 12.0, 0.0, slow_zone=slow_zone, timestamp=1.0)
        _process(enhancer, 7.0, 0.0, slow_zone=slow_zone, timestamp=1.0 + frame_dt)
        result = _process(enhancer, 3.0, 0.0, slow_zone=slow_zone, timestamp=1.0 + (frame_dt * 2.0))

        self.assertGreater(result["dx"], 0.0)
        self.assertLessEqual(result["dx"], 3.0)
        self.assertEqual(result["dy"], 0.0)

    def test_source_string_no_longer_disables_velocity_or_lead(self):
        if not hasattr(self.module, "NativeAimEnhancer"):
            self.fail("NativeAimEnhancer is missing")

        enhancer = self.module.NativeAimEnhancer()

        _process(enhancer, 10.0, 0.0, timestamp=1.0)
        observed = _process(enhancer, 16.0, 0.0, timestamp=1.1)
        predicted_one = _process(enhancer, 22.0, 0.0, source="predicted", timestamp=1.2)
        predicted_two = _process(enhancer, 28.0, 0.0, source="predicted", timestamp=1.3)

        self.assertGreaterEqual(observed["dx"], 16.0)
        self.assertGreater(predicted_one["dx"], 22.0)
        self.assertGreater(predicted_two["dx"], 28.0)

    def test_reset_clears_enhancement_state(self):
        if not hasattr(self.module, "NativeAimEnhancer"):
            self.fail("NativeAimEnhancer is missing")

        enhancer = self.module.NativeAimEnhancer()

        _process(enhancer, 10.0, 0.0, timestamp=1.0)
        _process(enhancer, 16.0, 0.0, timestamp=1.1)
        enhancer.reset()
        result = _process(enhancer, 16.0, 0.0, timestamp=2.0)

        self.assertEqual(result["dx"], 16.0)
        self.assertEqual(result["dy"], 0.0)


if __name__ == "__main__":
    unittest.main()
