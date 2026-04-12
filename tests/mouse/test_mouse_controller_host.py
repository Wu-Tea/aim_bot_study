import threading
import unittest

from controllers.mouse_controller import MouseController
from controllers.mouse.state import MouseOutput


class _FakePlugin:
    def __init__(self):
        self.reset_calls = 0
        self.apply_calls = 0
        self.last_frame = None

    def reset(self):
        self.reset_calls += 1

    def apply(self, frame, output):
        self.apply_calls += 1
        self.last_frame = frame


class MouseControllerHostTests(unittest.TestCase):
    def _make_controller(self, plugins):
        """Create a MouseController without starting the thread or listeners."""
        ctrl = MouseController.__new__(MouseController)
        ctrl.lock = threading.Lock()
        ctrl.running = False
        ctrl.ready = True
        ctrl.target_dx = 0.0
        ctrl.target_dy = 0.0
        ctrl.target_revision = 0
        ctrl.target_timestamp = None
        ctrl._is_aiming = False
        ctrl._auto_fire_requested = False
        ctrl._acc_dx = 0.0
        ctrl._acc_dy = 0.0
        ctrl._left_click_held = False
        ctrl.plugins = list(plugins)
        return ctrl

    def test_update_stores_vision_signals(self):
        ctrl = self._make_controller([])
        ctrl.update(12.5, -8.0)
        self.assertAlmostEqual(ctrl.target_dx, 12.5)
        self.assertAlmostEqual(ctrl.target_dy, -8.0)

    def test_reset_clears_target_and_resets_plugins(self):
        p = _FakePlugin()
        ctrl = self._make_controller([p])
        ctrl.target_dx = 10.0
        ctrl.target_dy = 5.0
        ctrl.reset()
        self.assertAlmostEqual(ctrl.target_dx, 0.0)
        self.assertAlmostEqual(ctrl.target_dy, 0.0)
        self.assertEqual(p.reset_calls, 1)

    def test_set_auto_fire_stores_flag(self):
        ctrl = self._make_controller([])
        ctrl.set_auto_fire(True)
        self.assertTrue(ctrl._auto_fire_requested)
        ctrl.set_auto_fire(False)
        self.assertFalse(ctrl._auto_fire_requested)

    def test_set_auto_rb_is_alias(self):
        ctrl = self._make_controller([])
        ctrl.set_auto_rb(True)
        self.assertTrue(ctrl._auto_fire_requested)

    def test_build_frame_captures_state(self):
        p = _FakePlugin()
        ctrl = self._make_controller([p])
        ctrl._is_aiming = True
        ctrl.target_dx = 7.0
        ctrl.target_dy = -3.0
        ctrl._auto_fire_requested = True
        ctrl._acc_dx = 2.0
        ctrl._acc_dy = 1.0
        ctrl.target_revision = 5
        ctrl.target_timestamp = 99.0

        frame = ctrl._build_frame(timestamp=100.0)
        self.assertTrue(frame.is_aiming)
        self.assertAlmostEqual(frame.target_dx, 7.0)
        self.assertAlmostEqual(frame.target_dy, -3.0)
        self.assertTrue(frame.auto_fire_requested)
        self.assertAlmostEqual(frame.manual_dx, 2.0)
        self.assertAlmostEqual(frame.manual_dy, 1.0)
        self.assertEqual(frame.target_revision, 5)
        self.assertEqual(frame.target_timestamp, 99.0)
        # Accumulators should be consumed
        self.assertAlmostEqual(ctrl._acc_dx, 0.0)
        self.assertAlmostEqual(ctrl._acc_dy, 0.0)


if __name__ == "__main__":
    unittest.main()
