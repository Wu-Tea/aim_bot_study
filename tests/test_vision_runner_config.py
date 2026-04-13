import unittest
from pathlib import Path

from vision.runner import DEFAULT_MODEL_PATH, VisionConfig


class VisionRunnerConfigTests(unittest.TestCase):
    def test_default_model_switches_to_detect_task_and_weights(self):
        config = VisionConfig()

        self.assertEqual(config.model_task, "detect")
        self.assertEqual(Path(config.model_path).name, "yolo26n.engine")
        self.assertEqual(Path(DEFAULT_MODEL_PATH).name, "yolo26n.engine")
        self.assertEqual(config.capture_width, 896)
        self.assertEqual(config.capture_height, 512)
        self.assertEqual(config.capture_fps, 70)
        self.assertEqual(config.image_size, (512, 896))


if __name__ == "__main__":
    unittest.main()
