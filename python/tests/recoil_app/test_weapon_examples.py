import io
import tempfile
import unittest
from pathlib import Path

import numpy as np

from vision.weapon_identity.text import _load_default_ocr_reader


def _load_runtime_module():
    import importlib

    return importlib.import_module("recoil_app.runtime")


class RecoilWeaponExampleReplayTests(unittest.TestCase):
    def test_weapon_examples_resolve_to_complete_weapon_names(self):
        reader = _load_default_ocr_reader()
        if reader is None:
            self.skipTest("RapidOCR GPU reader is not available in this environment")

        examples_dir = Path("artifacts/weapon_examples")
        if not examples_dir.exists():
            self.skipTest("weapon_examples screenshots are not present")

        runtime = _load_runtime_module()
        examples = {
            "cod20-RAM-9.png": ("cod20", "RAM-9"),
            "cod20-瑞纳提.png": ("cod20", "瑞纳提"),
            "cod21-9毫米PM.png": ("cod21", "9毫米PM"),
            "cod21-DM-10.png": ("cod21", "DM-10"),
            "cod21-干燥大地.png": ("cod21", "干燥大地"),
            "cod22-KT-3勇士.png": ("cod22", "KT-3勇士"),
            "cod22-最后通牒.png": ("cod22", "最后通牒"),
            "cod22-追击者.png": ("cod22", "追击者"),
            "cod22-黑色组织传奇.png": ("cod22", "黑色组织传奇"),
        }

        failures = []
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            stores_by_game = {}
            for game, expected_name in examples.values():
                identity_store, profile_store = stores_by_game.setdefault(
                    game,
                    (
                        runtime.IdentityStore(temp_path / game / "identities"),
                        runtime.RecoilProfileStore(temp_path / game / "profiles"),
                    ),
                )
                identity_store.resolve_or_create(
                    game=game,
                    display_name=expected_name,
                    timestamp="2026-05-19T00:00:00Z",
                )

            for filename, (game, expected_name) in examples.items():
                frame = _load_rgb_frame(examples_dir / filename)
                primary_crop = _crop_default_primary_display_region(
                    frame,
                    runtime.get_adapter(game).weapon_name_text_roi,
                )
                identity_store, profile_store = stores_by_game[game]
                app = runtime.RecoilRuntime(
                    game=game,
                    identity_store=identity_store,
                    profile_store=profile_store,
                    frame_grabber_factory=lambda crop=primary_crop: _FakeFrameGrabber(crop),
                    sleep_fn=lambda seconds: None,
                    timestamp_fn=lambda: "2026-05-19T00:00:01Z",
                    switch_task_runner=lambda slot_index, switch_epoch, task: task(),
                    stdout=io.StringIO(),
                )

                app.handle_switch_pressed()
                state = app.current_state
                actual_name = state.matched_name if state is not None else None
                if actual_name != expected_name:
                    failures.append(f"{filename}: expected={expected_name!r} actual={actual_name!r}")

        self.assertEqual(failures, [])


class _FakeFrameGrabber:
    def __init__(self, frame):
        self._frame = frame

    def grab(self):
        return self._frame

    def close(self):
        return None


def _load_rgb_frame(path: Path):
    try:
        from PIL import Image
    except Exception as exc:
        raise unittest.SkipTest(f"Pillow is not available: {exc}") from exc

    with Image.open(path) as image:
        return np.asarray(image.convert("RGB"))


def _crop_default_primary_display_region(frame, roi, *, padding_x: int = 96, padding_y: int = 40):
    height, width = frame.shape[:2]
    left = int(round(roi.left * width)) - padding_x
    top = int(round(roi.top * height)) - padding_y
    right = int(round((roi.left + roi.width) * width)) + padding_x
    bottom = int(round((roi.top + roi.height) * height)) + padding_y
    left = max(0, min(width - 1, left))
    top = max(0, min(height - 1, top))
    right = max(left + 1, min(width, right))
    bottom = max(top + 1, min(height, bottom))
    return frame[top:bottom, left:right]


if __name__ == "__main__":
    unittest.main()
