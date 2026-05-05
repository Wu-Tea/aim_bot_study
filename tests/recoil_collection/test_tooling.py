import contextlib
import importlib
import io
import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

import numpy as np

from vision.weapon_identity.models import RecognitionEvent
from vision.weapon_identity.models import VisualSignatureRecord
from vision.weapon_identity.models import WeaponIdentityRecord
from vision.weapon_identity.signatures import SignatureMatch


def _load_tool_module():
    try:
        return importlib.import_module("tools.weapon_recognizer")
    except ModuleNotFoundError as exc:
        raise AssertionError(f"Missing weapon recognizer tool: {exc}") from exc


def _load_recoil_tool_module():
    try:
        return importlib.import_module("tools.recoil_collector")
    except ModuleNotFoundError as exc:
        raise AssertionError(f"Missing recoil collector tool: {exc}") from exc


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

    def test_parse_args_requires_task6_data_and_capture_inputs(self):
        tool = _load_tool_module()
        required_pairs = [
            ("--profile-dir", "artifacts/recoil_profiles"),
            ("--signature-dir", "artifacts/weapon_signatures"),
            ("--capture-width", "960"),
            ("--capture-height", "540"),
            ("--fps", "60"),
        ]

        for omitted_flag, _ in required_pairs:
            argv = ["--game", "cod22"]
            for flag, value in required_pairs:
                if flag == omitted_flag:
                    continue
                argv.extend([flag, value])
            with self.subTest(omitted_flag=omitted_flag):
                with contextlib.redirect_stderr(io.StringIO()):
                    with self.assertRaises(SystemExit):
                        tool.parse_args(argv)

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

    def test_cod21_changed_image_evidence_does_not_emit_stale_carry_forward_weapon(self):
        tool = _load_tool_module()
        recognizer = tool.SignatureWeaponRecognizer(
            game="cod21",
            identity_records=(
                _weapon_record(canonical_weapon_id="cod21-krig-c", game="cod21", display_name="Krig C"),
                _weapon_record(canonical_weapon_id="cod21-xm4", game="cod21", display_name="XM4"),
            ),
            signature_records=(
                _signature_record(signature_id="sig-krig-c", canonical_weapon_id="cod21-krig-c", game="cod21"),
                _signature_record(signature_id="sig-xm4", canonical_weapon_id="cod21-xm4", game="cod21"),
            ),
            profile_index={},
        )
        frame = np.zeros((640, 640, 3), dtype=np.uint8)

        with patch.object(
            tool,
            "score_candidates",
            side_effect=[
                [_match("sig-krig-c", "cod21-krig-c", 0.95)],
                [_match("sig-xm4", "cod21-xm4", 0.97)],
            ],
        ):
            first_event = recognizer.process_frame(frame, frame_id=1, captured_at=1.0)
            second_event = recognizer.process_frame(frame, frame_id=2, captured_at=2.0)

        self.assertIsNotNone(first_event)
        self.assertEqual(first_event.canonical_weapon_id, "cod21-krig-c")
        self.assertNotEqual(getattr(second_event, "canonical_weapon_id", None), "cod21-krig-c")
        self.assertNotEqual(getattr(second_event, "source", None), "carry_forward")

    def test_build_default_recognizer_rejects_malformed_identity_payloads_in_signature_dir(self):
        tool = _load_tool_module()

        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            profile_dir = temp_path / "profiles"
            signature_dir = temp_path / "signatures"
            profile_dir.mkdir()
            signature_dir.mkdir()
            (signature_dir / "signature.json").write_text(
                json.dumps(
                    _signature_record(
                        signature_id="sig-primary",
                        canonical_weapon_id="cod22-m4",
                        game="cod22",
                    ).to_dict(),
                    indent=2,
                    sort_keys=True,
                )
                + "\n",
                encoding="utf-8",
            )
            malformed_identity_payload = {
                "canonical_weapon_id": "cod22-m4",
                "game": "cod22",
                "weapon_family": "assault_rifle",
                "display_name": "M4",
                "alias_names": [],
                "blueprint_names": ["Blackcell Ember"],
                "signature_refs": ["sig-primary"],
                "notes": "",
                "created_at": "2026-05-05T12:00:00Z",
            }
            (signature_dir / "identity.json").write_text(
                json.dumps(malformed_identity_payload, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )

            with self.assertRaisesRegex(ValueError, "updated_at|identity"):
                tool._build_default_recognizer(
                    SimpleNamespace(
                        game="cod22",
                        profile_dir=profile_dir,
                        signature_dir=signature_dir,
                    )
                )


class RecoilCollectorToolTests(unittest.TestCase):
    def test_parse_args_accepts_task9_cli_options(self):
        tool = _load_recoil_tool_module()

        args = tool.parse_args(
            [
                "--game",
                "cod22",
                "--mode",
                "ads",
                "--standing-only",
                "--profile-dir",
                "artifacts/recoil_profiles",
                "--signature-dir",
                "artifacts/weapon_signatures",
                "--output",
                "artifacts/recoil_profiles/latest-summary.json",
            ]
        )

        self.assertEqual(args.game, "cod22")
        self.assertEqual(args.mode, "ads")
        self.assertTrue(args.standing_only)
        self.assertEqual(args.profile_dir, Path("artifacts/recoil_profiles"))
        self.assertEqual(args.signature_dir, Path("artifacts/weapon_signatures"))
        self.assertEqual(args.output, Path("artifacts/recoil_profiles/latest-summary.json"))

    def test_main_collects_profile_and_writes_profile_and_summary_outputs(self):
        tool = _load_recoil_tool_module()
        capture = importlib.import_module("vision.recoil_collection.capture")
        event = RecognitionEvent(
            game="cod22",
            canonical_weapon_id="cod22-m4",
            confidence=0.93,
            source="image",
            timestamp="2026-05-06T14:00:00Z",
            degraded=False,
            matched_name=None,
        )

        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            profile_dir = temp_path / "profiles"
            signature_dir = temp_path / "signatures"
            output_path = temp_path / "summary.json"
            signature_dir.mkdir()
            stdout = io.StringIO()
            stderr = io.StringIO()
            recognizer = _StubCollectorRecognizer(event)

            exit_code = tool.main(
                argv=[
                    "--game",
                    "cod22",
                    "--mode",
                    "ads",
                    "--standing-only",
                    "--profile-dir",
                    str(profile_dir),
                    "--signature-dir",
                    str(signature_dir),
                    "--output",
                    str(output_path),
                ],
                stdout=stdout,
                stderr=stderr,
                recognizer_factory=lambda args: recognizer,
                weapon_frame_grabber_factory=lambda args: _StubFrameGrabber(np.zeros((8, 8, 3), dtype=np.uint8)),
                motion_sampler_factory=lambda args, config: (lambda: _collector_motion_trace(capture)),
                timestamp_fn=lambda: "2026-05-06T14:05:00Z",
            )

            self.assertEqual(exit_code, 0)
            self.assertEqual(recognizer.frames_seen, [{"frame_id": 1, "captured_at": 0.0, "shape": (8, 8, 3)}])

            output_lines = [line for line in stdout.getvalue().splitlines() if line.strip()]
            self.assertEqual(len(output_lines), 1)
            payload = json.loads(output_lines[0])
            self.assertEqual(payload["type"], "recoil_collection_summary")
            self.assertEqual(payload["recognized_weapon"]["canonical_weapon_id"], "cod22-m4")
            self.assertEqual(payload["profile_summary"]["game"], "cod22")
            self.assertEqual(payload["profile_summary"]["aim_mode"], "ads")
            self.assertEqual(payload["profile_summary"]["burst_count"], 3)
            self.assertEqual(payload["profile_summary"]["sample_count"], 4)
            self.assertEqual(payload["accepted_burst_ids"], ["session-cod22-m4-ads-standing-20260506t140500z-burst-001", "session-cod22-m4-ads-standing-20260506t140500z-burst-002", "session-cod22-m4-ads-standing-20260506t140500z-burst-003"])
            self.assertEqual(payload["rejected_burst_ids"], [])

            profile_path = Path(payload["profile_path"])
            profile_summary_path = Path(payload["profile_summary_path"])
            self.assertTrue(profile_path.exists())
            self.assertTrue(profile_summary_path.exists())
            self.assertTrue(output_path.exists())
            self.assertEqual(json.loads(output_path.read_text(encoding="utf-8")), payload)

            saved_profile = json.loads(profile_path.read_text(encoding="utf-8"))
            saved_profile_summary = json.loads(profile_summary_path.read_text(encoding="utf-8"))
            self.assertEqual(saved_profile["canonical_weapon_id"], "cod22-m4")
            self.assertEqual(saved_profile["sample_count"], 4)
            self.assertEqual(saved_profile_summary["profile_id"], payload["profile_summary"]["profile_id"])
            self.assertEqual(saved_profile_summary["burst_count"], 3)
            self.assertIn("profile_path", payload)
            self.assertEqual(stderr.getvalue(), "")

    def test_main_fails_conservatively_when_weapon_confirmation_is_missing_or_degraded(self):
        tool = _load_recoil_tool_module()

        for event in (
            None,
            RecognitionEvent(
                game="cod22",
                canonical_weapon_id="cod22-m4",
                confidence=0.55,
                source="carry_forward",
                timestamp="2026-05-06T14:00:00Z",
                degraded=True,
                matched_name=None,
            ),
        ):
            with self.subTest(event=event):
                with tempfile.TemporaryDirectory() as temp_dir:
                    temp_path = Path(temp_dir)
                    profile_dir = temp_path / "profiles"
                    signature_dir = temp_path / "signatures"
                    output_path = temp_path / "summary.json"
                    signature_dir.mkdir()
                    stdout = io.StringIO()
                    stderr = io.StringIO()

                    exit_code = tool.main(
                        argv=[
                            "--game",
                            "cod22",
                            "--mode",
                            "hipfire",
                            "--standing-only",
                            "--profile-dir",
                            str(profile_dir),
                            "--signature-dir",
                            str(signature_dir),
                            "--output",
                            str(output_path),
                        ],
                        stdout=stdout,
                        stderr=stderr,
                        recognizer_factory=lambda args, event=event: _StubCollectorRecognizer(event),
                        weapon_frame_grabber_factory=lambda args: _StubFrameGrabber(np.zeros((8, 8, 3), dtype=np.uint8)),
                        motion_sampler_factory=lambda args, config: (lambda: ()),
                        timestamp_fn=lambda: "2026-05-06T14:05:00Z",
                    )

                    self.assertEqual(exit_code, 1)
                    self.assertEqual(stdout.getvalue(), "")
                    self.assertFalse(output_path.exists())
                    if profile_dir.exists():
                        self.assertEqual(list(profile_dir.glob("*.json")), [])
                    self.assertIn("Unable to confirm current weapon", stderr.getvalue())


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


class _StubCollectorRecognizer:
    def __init__(self, event):
        self._event = event
        self.frames_seen = []

    def process_frame(self, frame, *, frame_id, captured_at):
        self.frames_seen.append(
            {
                "frame_id": frame_id,
                "captured_at": captured_at,
                "shape": tuple(frame.shape),
            }
        )
        return self._event


class _StubFrameGrabber:
    def __init__(self, frame):
        self._frame = frame
        self.closed = False

    def grab(self):
        return self._frame

    def close(self):
        self.closed = True


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


def _weapon_record(*, canonical_weapon_id, game, display_name):
    return WeaponIdentityRecord(
        canonical_weapon_id=canonical_weapon_id,
        game=game,
        weapon_family="assault_rifle",
        display_name=display_name,
        alias_names=(),
        blueprint_names=(),
        signature_refs=(),
        notes="",
        created_at="2026-05-05T12:00:00Z",
        updated_at="2026-05-05T12:00:00Z",
    )


def _signature_record(*, signature_id, canonical_weapon_id, game):
    return VisualSignatureRecord(
        signature_id=signature_id,
        canonical_weapon_id=canonical_weapon_id,
        game=game,
        region_type="weapon_icon",
        resolution_bucket="1080p",
        ui_scale_bucket="default",
        feature_type="classical_signature_v1",
        feature_payload={"template": [], "edge_map": [], "perceptual_hash": "0"},
        captured_from="hud-crop",
        confidence=0.95,
    )


def _match(signature_id, canonical_weapon_id, score):
    return SignatureMatch(
        signature_id=signature_id,
        canonical_weapon_id=canonical_weapon_id,
        score=score,
        template_score=score,
        edge_score=score,
        hash_score=score,
        structure_score=score,
    )


def _collector_motion_trace(capture_module):
    MotionTraceSample = capture_module.MotionTraceSample
    return (
        MotionTraceSample(offset_ms=0, x=0.0, y=0.0, center_motion=0.0),
        MotionTraceSample(offset_ms=10, x=0.0, y=0.0, center_motion=0.0),
        MotionTraceSample(offset_ms=20, x=0.0, y=-1.0, center_motion=0.9),
        MotionTraceSample(offset_ms=30, x=0.4, y=-2.0, center_motion=1.0),
        MotionTraceSample(offset_ms=40, x=0.8, y=-3.0, center_motion=0.95),
        MotionTraceSample(offset_ms=50, x=1.2, y=-4.0, center_motion=0.9),
        MotionTraceSample(offset_ms=60, x=1.2, y=-4.0, center_motion=0.05),
        MotionTraceSample(offset_ms=70, x=1.2, y=-4.0, center_motion=0.05),
        MotionTraceSample(offset_ms=80, x=1.2, y=-4.0, center_motion=0.0),
        MotionTraceSample(offset_ms=90, x=1.2, y=-5.0, center_motion=0.9),
        MotionTraceSample(offset_ms=100, x=1.6, y=-6.0, center_motion=1.0),
        MotionTraceSample(offset_ms=110, x=2.0, y=-7.0, center_motion=0.95),
        MotionTraceSample(offset_ms=120, x=2.4, y=-8.0, center_motion=0.9),
        MotionTraceSample(offset_ms=130, x=2.4, y=-8.0, center_motion=0.05),
        MotionTraceSample(offset_ms=140, x=2.4, y=-8.0, center_motion=0.05),
        MotionTraceSample(offset_ms=150, x=2.4, y=-8.0, center_motion=0.0),
        MotionTraceSample(offset_ms=160, x=2.4, y=-9.0, center_motion=0.9),
        MotionTraceSample(offset_ms=170, x=2.8, y=-10.0, center_motion=1.0),
        MotionTraceSample(offset_ms=180, x=3.2, y=-11.0, center_motion=0.95),
        MotionTraceSample(offset_ms=190, x=3.6, y=-12.0, center_motion=0.9),
        MotionTraceSample(offset_ms=200, x=3.6, y=-12.0, center_motion=0.05),
        MotionTraceSample(offset_ms=210, x=3.6, y=-12.0, center_motion=0.05),
    )


if __name__ == "__main__":
    unittest.main()
