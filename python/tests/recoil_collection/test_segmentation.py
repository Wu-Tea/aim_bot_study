import json
from pathlib import Path
import unittest

from vision.recoil_collection.models import RecoilBurstWindow
from vision.recoil_collection.models import RecoilCollectionSession
from vision.recoil_collection.segmentation import BurstSegmentationConfig
from vision.recoil_collection.segmentation import BurstSegmentationSample
from vision.recoil_collection.segmentation import segment_standing_fire_bursts

_FIXTURES_ROOT = Path(__file__).resolve().parents[1] / "fixtures"


def _session() -> RecoilCollectionSession:
    return RecoilCollectionSession(
        session_id="session-cod22-m4-20260505-110000",
        canonical_weapon_id="cod22-m4",
        game="cod22",
        stance="standing",
        aim_mode="ads",
        capture_resolution="1920x1080",
        capture_fps=240.0,
        collector_version="collector-0.1.0",
        started_at="2026-05-05T11:00:00Z",
    )


def _config() -> BurstSegmentationConfig:
    return BurstSegmentationConfig(
        motion_start_threshold=0.6,
        motion_end_threshold=0.2,
        motion_confirm_frames=2,
        settle_frames=2,
    )


class SegmentStandingFireBurstsTests(unittest.TestCase):
    def test_detects_single_burst_from_motion_and_ammo(self):
        fixture = _load_segmentation_fixture("single_burst_motion_ammo_replay")

        windows = segment_standing_fire_bursts(
            session=fixture["session"],
            samples=fixture["samples"],
            config=fixture["config"],
        )

        self.assertEqual(windows, fixture["expected_windows"])

    def test_detects_multiple_separated_bursts(self):
        fixture = _load_segmentation_fixture("multiple_bursts_replay")

        windows = segment_standing_fire_bursts(
            session=fixture["session"],
            samples=fixture["samples"],
            config=fixture["config"],
        )

        self.assertEqual(windows, fixture["expected_windows"])

    def test_ammo_confirmed_start_backdates_to_pending_motion_onset(self):
        windows = segment_standing_fire_bursts(
            session=_session(),
            samples=(
                BurstSegmentationSample(offset_ms=0, center_motion=0.61, ammo=30),
                BurstSegmentationSample(offset_ms=16, center_motion=0.10, ammo=29),
                BurstSegmentationSample(offset_ms=32, center_motion=0.05, ammo=29),
                BurstSegmentationSample(offset_ms=48, center_motion=0.03, ammo=29),
            ),
            config=_config(),
        )

        self.assertEqual(
            windows,
            (
                RecoilBurstWindow(
                    burst_id="session-cod22-m4-20260505-110000-burst-001",
                    session_id="session-cod22-m4-20260505-110000",
                    start_offset_ms=0,
                    end_offset_ms=32,
                    start_reason="ammo",
                    end_reason="motion_settled",
                ),
            ),
        )

    def test_ammo_drop_does_not_backdate_to_stale_broken_motion_spike(self):
        windows = segment_standing_fire_bursts(
            session=_session(),
            samples=(
                BurstSegmentationSample(offset_ms=0, center_motion=0.61, ammo=30),
                BurstSegmentationSample(offset_ms=16, center_motion=0.10, ammo=30),
                BurstSegmentationSample(offset_ms=32, center_motion=0.10, ammo=30),
                BurstSegmentationSample(offset_ms=48, center_motion=0.10, ammo=30),
                BurstSegmentationSample(offset_ms=64, center_motion=0.10, ammo=29),
                BurstSegmentationSample(offset_ms=80, center_motion=0.05, ammo=29),
                BurstSegmentationSample(offset_ms=96, center_motion=0.04, ammo=29),
            ),
            config=_config(),
        )

        self.assertEqual(
            windows,
            (
                RecoilBurstWindow(
                    burst_id="session-cod22-m4-20260505-110000-burst-001",
                    session_id="session-cod22-m4-20260505-110000",
                    start_offset_ms=64,
                    end_offset_ms=80,
                    start_reason="ammo",
                    end_reason="motion_settled",
                ),
            ),
        )

    def test_returns_no_bursts_when_signals_never_confirm(self):
        windows = segment_standing_fire_bursts(
            session=_session(),
            samples=(
                BurstSegmentationSample(offset_ms=0, center_motion=0.02, ammo=30),
                BurstSegmentationSample(offset_ms=16, center_motion=0.08, ammo=30),
                BurstSegmentationSample(offset_ms=32, center_motion=0.15, ammo=30),
                BurstSegmentationSample(offset_ms=48, center_motion=0.10, ammo=30),
            ),
            config=_config(),
        )

        self.assertEqual(windows, ())

    def test_noisy_motion_does_not_start_a_burst(self):
        windows = segment_standing_fire_bursts(
            session=_session(),
            samples=(
                BurstSegmentationSample(offset_ms=0, center_motion=0.05, ammo=30),
                BurstSegmentationSample(offset_ms=16, center_motion=0.62, ammo=30),
                BurstSegmentationSample(offset_ms=32, center_motion=0.14, ammo=30),
                BurstSegmentationSample(offset_ms=48, center_motion=0.61, ammo=30),
                BurstSegmentationSample(offset_ms=64, center_motion=0.08, ammo=30),
                BurstSegmentationSample(offset_ms=80, center_motion=0.59, ammo=30),
                BurstSegmentationSample(offset_ms=96, center_motion=0.07, ammo=30),
            ),
            config=_config(),
        )

        self.assertEqual(windows, ())

    def test_manual_markers_can_force_start_and_stop(self):
        windows = segment_standing_fire_bursts(
            session=_session(),
            samples=(
                BurstSegmentationSample(offset_ms=0, center_motion=0.02, ammo=None),
                BurstSegmentationSample(offset_ms=16, center_motion=0.03, ammo=None, manual_marker="start"),
                BurstSegmentationSample(offset_ms=32, center_motion=0.04, ammo=None),
                BurstSegmentationSample(offset_ms=48, center_motion=0.05, ammo=None),
                BurstSegmentationSample(offset_ms=64, center_motion=0.03, ammo=None),
                BurstSegmentationSample(offset_ms=80, center_motion=0.02, ammo=None, manual_marker="stop"),
            ),
            config=_config(),
        )

        self.assertEqual(
            windows,
            (
                RecoilBurstWindow(
                    burst_id="session-cod22-m4-20260505-110000-burst-001",
                    session_id="session-cod22-m4-20260505-110000",
                    start_offset_ms=16,
                    end_offset_ms=80,
                    start_reason="manual",
                    end_reason="manual",
                ),
            ),
        )

    def test_terminal_burst_uses_exclusive_end_boundary_from_sample_spacing(self):
        windows = segment_standing_fire_bursts(
            session=_session(),
            samples=(
                BurstSegmentationSample(offset_ms=0, center_motion=0.64, ammo=30),
                BurstSegmentationSample(offset_ms=16, center_motion=0.83, ammo=29),
                BurstSegmentationSample(offset_ms=32, center_motion=0.79, ammo=28),
            ),
            config=_config(),
        )

        self.assertEqual(
            windows,
            (
                RecoilBurstWindow(
                    burst_id="session-cod22-m4-20260505-110000-burst-001",
                    session_id="session-cod22-m4-20260505-110000",
                    start_offset_ms=0,
                    end_offset_ms=48,
                    start_reason="motion",
                    end_reason="end_of_samples",
                ),
            ),
        )


def _load_segmentation_fixture(name: str):
    fixture_path = _FIXTURES_ROOT / "recoil_collection" / "segmentation" / f"{name}.json"
    fixture_payload = json.loads(fixture_path.read_text(encoding="utf-8"))
    return {
        "session": RecoilCollectionSession.from_dict(fixture_payload["session"]),
        "config": BurstSegmentationConfig(**fixture_payload["config"]),
        "samples": tuple(
            BurstSegmentationSample(
                offset_ms=sample_payload["offset_ms"],
                center_motion=sample_payload["center_motion"],
                ammo=sample_payload["ammo"],
                manual_marker=sample_payload["manual_marker"],
            )
            for sample_payload in fixture_payload["samples"]
        ),
        "expected_windows": tuple(
            RecoilBurstWindow.from_dict(window_payload)
            for window_payload in fixture_payload["expected_windows"]
        ),
    }


if __name__ == "__main__":
    unittest.main()
