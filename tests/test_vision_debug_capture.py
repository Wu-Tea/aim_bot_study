from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

import numpy as np

from vision.debug_capture import DebugFrameCapture


class DebugFrameCaptureTests(unittest.TestCase):
    def test_save_frame_creates_date_based_directory_and_writes_image(self):
        with TemporaryDirectory() as tmp:
            capture = DebugFrameCapture(base_dir=Path(tmp), asynchronous=False)
            frame = np.full((32, 64, 3), 127, dtype=np.uint8)

            saved_path = capture.save_frame(
                frame_bgr=frame,
                detections_count=2,
                has_selected_target=True,
                auto_fire_active=False,
                timestamp_text="2026-04-15_15-52-10_123456",
            )

            self.assertIsNotNone(saved_path)
            self.assertTrue(saved_path.is_file())
            self.assertEqual(saved_path.parent.parent, Path(tmp))
            self.assertEqual(saved_path.parent.name, "2026-04-15")
            self.assertIn("boxes2", saved_path.name)
            self.assertIn("lock1", saved_path.name)
            self.assertIn("fire0", saved_path.name)


if __name__ == "__main__":
    unittest.main()
