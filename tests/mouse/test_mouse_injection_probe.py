import unittest

from tools.probe_mouse_injection import _format_result, _same_direction


class MouseInjectionProbeTests(unittest.TestCase):
    def test_same_direction_accepts_matching_nonzero_signs(self):
        self.assertTrue(_same_direction(24, 10))
        self.assertTrue(_same_direction(-24, -3))
        self.assertFalse(_same_direction(24, -1))
        self.assertFalse(_same_direction(24, 0))
        self.assertFalse(_same_direction(0, 5))

    def test_format_result_includes_backend_and_acceptance(self):
        text = _format_result(
            {
                "backend": "sendinput",
                "requested_dx": 24,
                "requested_dy": 0,
                "observed_dx": 22,
                "observed_dy": 0,
                "start": (10, 10),
                "after": (32, 10),
                "end": (10, 10),
                "accepted": True,
            }
        )

        self.assertIn("backend=sendinput", text)
        self.assertIn("accepted=True", text)


if __name__ == "__main__":
    unittest.main()
