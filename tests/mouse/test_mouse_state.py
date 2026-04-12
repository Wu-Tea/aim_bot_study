import unittest

from controllers.mouse.state import MouseFrame, MouseOutput


class MouseFrameTests(unittest.TestCase):
    def test_frame_is_frozen(self):
        frame = MouseFrame(
            timestamp=1.0,
            manual_dx=5.0,
            manual_dy=-3.0,
            is_aiming=True,
            target_dx=10.0,
            target_dy=-4.0,
            auto_fire_requested=False,
        )
        with self.assertRaises(AttributeError):
            frame.target_dx = 99.0

    def test_frame_stores_all_fields(self):
        frame = MouseFrame(
            timestamp=2.0,
            manual_dx=1.0,
            manual_dy=2.0,
            is_aiming=False,
            target_dx=3.0,
            target_dy=4.0,
            auto_fire_requested=True,
            target_revision=7,
            target_timestamp=1.5,
        )
        self.assertEqual(frame.timestamp, 2.0)
        self.assertEqual(frame.manual_dx, 1.0)
        self.assertEqual(frame.manual_dy, 2.0)
        self.assertFalse(frame.is_aiming)
        self.assertEqual(frame.target_dx, 3.0)
        self.assertEqual(frame.target_dy, 4.0)
        self.assertTrue(frame.auto_fire_requested)
        self.assertEqual(frame.target_revision, 7)
        self.assertEqual(frame.target_timestamp, 1.5)


class MouseOutputTests(unittest.TestCase):
    def test_output_is_mutable(self):
        output = MouseOutput()
        output.move_dx = 5.0
        output.move_dy = -3.0
        output.left_click = True
        output.auto_fire_active = True
        self.assertEqual(output.move_dx, 5.0)
        self.assertEqual(output.move_dy, -3.0)
        self.assertTrue(output.left_click)
        self.assertTrue(output.auto_fire_active)

    def test_output_defaults(self):
        output = MouseOutput()
        self.assertEqual(output.move_dx, 0.0)
        self.assertEqual(output.move_dy, 0.0)
        self.assertFalse(output.left_click)
        self.assertFalse(output.auto_fire_active)


if __name__ == "__main__":
    unittest.main()
