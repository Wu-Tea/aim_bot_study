import os
import unittest
from unittest.mock import patch

from controllers.base_controller import BaseController
from vision.runner import VisionConfig


class VisionRunnerTests(unittest.TestCase):
    def test_from_env_uses_dataclass_defaults_when_env_is_unset(self):
        with patch.dict(os.environ, {}, clear=True):
            config = VisionConfig.from_env()

        self.assertEqual(config.crop_size, 640)
        self.assertEqual(config.target_fps, 90)

    def test_from_env_allows_runtime_overrides(self):
        with patch.dict(
            os.environ,
            {
                "VISION_CROP_SIZE": "512",
                "VISION_TARGET_FPS": "144",
            },
            clear=True,
        ):
            config = VisionConfig.from_env()

        self.assertEqual(config.crop_size, 512)
        self.assertEqual(config.target_fps, 144)


class _AliasController(BaseController):
    def __init__(self):
        self.auto_fire_values = []

    def update(self, dx, dy):
        return None

    def reset(self):
        return None

    def is_aiming(self):
        return False

    def set_auto_fire(self, pressed: bool):
        self.auto_fire_values.append(bool(pressed))


class BaseControllerAliasTests(unittest.TestCase):
    def test_set_auto_rb_forwards_to_set_auto_fire(self):
        controller = _AliasController()

        controller.set_auto_rb(True)

        self.assertEqual(controller.auto_fire_values, [True])


if __name__ == "__main__":
    unittest.main()
