import importlib
import io
import json
import tempfile
import unittest
from pathlib import Path

import numpy as np

from vision.weapon_identity.models import RecognitionEvent


def _load_tool_module():
    try:
        return importlib.import_module("tools.weapon_recognizer")
    except ModuleNotFoundError as exc:
        raise AssertionError(f"Missing weapon recognizer tool: {exc}") from exc


class WeaponRecognizerToolTests(unittest.TestCase):
    def test_parse_args_accepts_task6_cli_options(self):
        tool = _load_tool_module()

        args = tool.parse_args(
            [
                "--game",
                "cod22",
                "--profile-dir",
                "artifacts/recoil_profiles",
                "--signature-dir",
                "artifacts/weapon_signatures",
                "--capture-width",
                "960",
                "--capture-height",
                "540",
                "--fps",
                "60",
                "--latest-state-file",
                "artifacts/recoil_state/latest.json",
            ]
        )

        self.assertEqual(args.game, "cod22")
        self.assertEqual(args.profile_dir, Path("artifacts/recoil_profiles"))
        self.assertEqual(args.signature_dir, Path("artifacts/weapon_signatures"))
        self.assertEqual(args.capture_width, 960)
        self.assertEqual(args.capture_height, 540)
        self.assertEqual(args.fps, 60)
        self.assertEqual(args.latest_state_file, Path("artifacts/recoil_state/latest.json"))

    def test_main_emits_structured_json_and_writes_latest_state_file(self):
        tool = _load_tool_module()
        event = RecognitionEvent(
            game="cod22",
            canonical_weapon_id="cod22-m4",
            confidence=0.91,
            source="image",
            timestamp="2026-05-05T12:00:00Z",
            degraded=False,
            matched_name=None,
        )

        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            latest_state_path = temp_path / "latest-state.json"
            profile_dir = temp_path / "profiles"
            signature_dir = temp_path / "signatures"
            profile_dir.mkdir()
            signature_dir.mkdir()
            stdout = io.StringIO()
            recognizer = _StubRecognizer([event])
            capture_thread = _StubCaptureThread(
                [
                    _StubCapturedFrame(
                        frame_id=1,
                        captured_at=12.5,
                        frame=np.zeros((4, 4, 3), dtype=np.uint8),
                    )
                ]
            )

            exit_code = tool.main(
                argv=[
                    "--game",
                    "cod22",
                    "--profile-dir",
                    str(profile_dir),
                    "--signature-dir",
                    str(signature_dir),
                    "--capture-width",
                    "640",
                    "--capture-height",
                    "512",
                    "--fps",
                    "75",
                    "--latest-state-file",
                    str(latest_state_path),
                ],
                stdout=stdout,
                recognizer_factory=lambda args: recognizer,
                capture_thread_factory=lambda args: capture_thread,
            )

            self.assertEqual(exit_code, 0)
            self.assertTrue(capture_thread.started)
            self.assertTrue(capture_thread.stopped)
            self.assertTrue(capture_thread.joined)
            self.assertEqual(
                recognizer.frames_seen,
                [
                    {
                        "frame_id": 1,
                        "captured_at": 12.5,
                        "shape": (4, 4, 3),
                    }
                ],
            )

            output_lines = [line for line in stdout.getvalue().splitlines() if line.strip()]
            self.assertEqual(len(output_lines), 1)
            payload = json.loads(output_lines[0])
            expected_payload = {
                "type": "current_weapon",
                "game": "cod22",
                "canonical_weapon_id": "cod22-m4",
                "confidence": 0.91,
                "source": "image",
                "timestamp": "2026-05-05T12:00:00Z",
                "degraded": False,
                "matched_name": None,
                "profile_ids": [],
            }
            self.assertEqual(payload, expected_payload)
            self.assertEqual(
                json.loads(latest_state_path.read_text(encoding="utf-8")),
                expected_payload,
            )


class _StubRecognizer:
    def __init__(self, events):
        self._events = list(events)
        self.frames_seen = []

    def process_frame(self, frame, *, frame_id, captured_at):
        self.frames_seen.append(
            {
                "frame_id": frame_id,
                "captured_at": captured_at,
                "shape": tuple(frame.shape),
            }
        )
        if not self._events:
            return None
        return self._events.pop(0)


class _StubCapturedFrame:
    def __init__(self, *, frame_id, captured_at, frame):
        self.frame_id = frame_id
        self.captured_at = captured_at
        self.frame = frame


class _StubCaptureThread:
    def __init__(self, frames):
        self._frames = list(frames)
        self.running = True
        self.started = False
        self.stopped = False
        self.joined = False

    def start(self):
        self.started = True

    def get_latest_frame(self, last_seen_id=0, timeout=0.1):
        del timeout
        if self._frames:
            frame = self._frames.pop(0)
            if not self._frames:
                self.running = False
            return frame, frame.frame_id
        self.running = False
        return None, last_seen_id

    def stop(self):
        self.stopped = True
        self.running = False

    def join(self, timeout=None):
        del timeout
        self.joined = True


if __name__ == "__main__":
    unittest.main()
