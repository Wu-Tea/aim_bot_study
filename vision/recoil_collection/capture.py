from __future__ import annotations

from dataclasses import dataclass
from math import hypot
import time
from typing import Any
from typing import Callable
from typing import Iterable

import cv2
import numpy as np

from vision.recoil_collection.extraction import ExtractedRecoilProfile
from vision.recoil_collection.extraction import RecoilExtractionConfig
from vision.recoil_collection.extraction import extract_recoil_profile as default_extract_recoil_profile
from vision.recoil_collection.models import RecoilBurstSampleSeries
from vision.recoil_collection.models import RecoilBurstWindow
from vision.recoil_collection.models import RecoilCollectionSession
from vision.recoil_collection.models import RecoilProfileSummary
from vision.recoil_collection.models import RecoilSample
from vision.recoil_collection.segmentation import BurstSegmentationConfig
from vision.recoil_collection.segmentation import BurstSegmentationSample
from vision.recoil_collection.segmentation import segment_standing_fire_bursts as default_segment_bursts
from vision.weapon_identity.models import RecognitionEvent

_MIN_VALID_PHASE_CORRELATION_RESPONSE = 0.05


class RecoilCollectionError(ValueError):
    """Raised when the recoil collector cannot safely produce a profile."""


@dataclass(slots=True, frozen=True)
class MotionTraceSample:
    offset_ms: int
    x: float
    y: float
    center_motion: float

    def __post_init__(self) -> None:
        object.__setattr__(self, "offset_ms", _require_non_negative_int(self.offset_ms, "MotionTraceSample.offset_ms"))
        object.__setattr__(self, "x", _require_number(self.x, "MotionTraceSample.x"))
        object.__setattr__(self, "y", _require_number(self.y, "MotionTraceSample.y"))
        center_motion = _require_number(self.center_motion, "MotionTraceSample.center_motion")
        if center_motion < 0.0:
            raise ValueError("MotionTraceSample.center_motion must be non-negative")
        object.__setattr__(self, "center_motion", center_motion)


@dataclass(slots=True, frozen=True)
class RecoilCollectorConfig:
    capture_width: int = 640
    capture_height: int = 640
    capture_fps: int = 100
    max_capture_seconds: float = 8.0
    motion_start_threshold: float = 0.6
    motion_end_threshold: float = 0.2
    motion_confirm_frames: int = 2
    settle_frames: int = 2
    min_clean_bursts: int = 3
    target_clean_bursts: int = 4
    collector_version: str = "collector-0.1.0"

    def __post_init__(self) -> None:
        object.__setattr__(self, "capture_width", _require_positive_int(self.capture_width, "RecoilCollectorConfig.capture_width"))
        object.__setattr__(self, "capture_height", _require_positive_int(self.capture_height, "RecoilCollectorConfig.capture_height"))
        object.__setattr__(self, "capture_fps", _require_positive_int(self.capture_fps, "RecoilCollectorConfig.capture_fps"))
        if type(self.max_capture_seconds) not in {int, float} or float(self.max_capture_seconds) <= 0.0:
            raise ValueError("RecoilCollectorConfig.max_capture_seconds must be positive")
        object.__setattr__(self, "max_capture_seconds", float(self.max_capture_seconds))
        if type(self.motion_start_threshold) not in {int, float} or float(self.motion_start_threshold) <= 0.0:
            raise ValueError("RecoilCollectorConfig.motion_start_threshold must be positive")
        if type(self.motion_end_threshold) not in {int, float} or float(self.motion_end_threshold) < 0.0:
            raise ValueError("RecoilCollectorConfig.motion_end_threshold must be non-negative")
        object.__setattr__(self, "motion_start_threshold", float(self.motion_start_threshold))
        object.__setattr__(self, "motion_end_threshold", float(self.motion_end_threshold))
        object.__setattr__(self, "motion_confirm_frames", _require_positive_int(self.motion_confirm_frames, "RecoilCollectorConfig.motion_confirm_frames"))
        object.__setattr__(self, "settle_frames", _require_positive_int(self.settle_frames, "RecoilCollectorConfig.settle_frames"))
        object.__setattr__(self, "min_clean_bursts", _require_positive_int(self.min_clean_bursts, "RecoilCollectorConfig.min_clean_bursts"))
        object.__setattr__(self, "target_clean_bursts", _require_positive_int(self.target_clean_bursts, "RecoilCollectorConfig.target_clean_bursts"))
        if self.target_clean_bursts < self.min_clean_bursts:
            raise ValueError("RecoilCollectorConfig.target_clean_bursts must be >= min_clean_bursts")
        object.__setattr__(self, "collector_version", _require_non_empty_str(self.collector_version, "RecoilCollectorConfig.collector_version"))

    @property
    def sample_interval_ms(self) -> int:
        return max(1, int(round(1000.0 / self.capture_fps)))

    @property
    def capture_resolution(self) -> str:
        return f"{self.capture_width}x{self.capture_height}"

    def segmentation_config(self) -> BurstSegmentationConfig:
        return BurstSegmentationConfig(
            motion_start_threshold=self.motion_start_threshold,
            motion_end_threshold=self.motion_end_threshold,
            motion_confirm_frames=self.motion_confirm_frames,
            settle_frames=self.settle_frames,
        )

    def extraction_config(self) -> RecoilExtractionConfig:
        return RecoilExtractionConfig(
            sample_interval_ms=self.sample_interval_ms,
            min_clean_bursts=self.min_clean_bursts,
            target_clean_bursts=self.target_clean_bursts,
        )


@dataclass(slots=True, frozen=True)
class RecoilCollectionResult:
    recognition_event: RecognitionEvent
    session: RecoilCollectionSession
    motion_samples: tuple[MotionTraceSample, ...]
    burst_windows: tuple[RecoilBurstWindow, ...]
    burst_series: tuple[RecoilBurstSampleSeries, ...]
    extracted_profile: ExtractedRecoilProfile
    profile_summary: RecoilProfileSummary


def collect_recoil_profile(
    *,
    game: str,
    aim_mode: str,
    standing_only: bool,
    recognizer: Any,
    weapon_frame_source: Any,
    motion_sampler: Callable[[], Iterable[MotionTraceSample]],
    config: RecoilCollectorConfig | None = None,
    timestamp_fn: Callable[[], str] | None = None,
    segmenter: Callable[..., tuple[RecoilBurstWindow, ...]] = default_segment_bursts,
    extractor: Callable[..., ExtractedRecoilProfile] = default_extract_recoil_profile,
) -> RecoilCollectionResult:
    collector_config = config or RecoilCollectorConfig()
    timestamp_fn = timestamp_fn or _utc_timestamp
    game = _require_non_empty_str(game, "game")
    aim_mode = _require_non_empty_str(aim_mode, "aim_mode")
    if aim_mode not in {"hipfire", "ads"}:
        raise RecoilCollectionError("aim_mode must be one of ['ads', 'hipfire']")
    if type(standing_only) is not bool:
        raise RecoilCollectionError("standing_only must be a boolean")
    if not standing_only:
        raise RecoilCollectionError("V1 recoil collection requires --standing-only")

    recognition_event = _confirm_weapon_once(
        recognizer=recognizer,
        frame_source=weapon_frame_source,
        game=game,
    )
    started_at = _require_non_empty_str(timestamp_fn(), "timestamp")
    session = RecoilCollectionSession(
        session_id=_build_session_id(recognition_event.canonical_weapon_id, aim_mode, started_at),
        canonical_weapon_id=recognition_event.canonical_weapon_id,
        game=game,
        stance="standing",
        aim_mode=aim_mode,
        capture_resolution=collector_config.capture_resolution,
        capture_fps=float(collector_config.capture_fps),
        collector_version=collector_config.collector_version,
        started_at=started_at,
    )
    motion_samples = _coerce_motion_samples(motion_sampler())
    segmentation_samples = tuple(
        BurstSegmentationSample(
            offset_ms=sample.offset_ms,
            center_motion=sample.center_motion,
            ammo=None,
            manual_marker=None,
        )
        for sample in motion_samples
    )
    burst_windows = segmenter(
        session=session,
        samples=segmentation_samples,
        config=collector_config.segmentation_config(),
    )
    if not burst_windows:
        raise RecoilCollectionError("No firing bursts detected from captured motion")

    burst_series = build_burst_sample_series(
        session=session,
        motion_samples=motion_samples,
        windows=burst_windows,
        sample_interval_ms=collector_config.sample_interval_ms,
    )
    if not burst_series:
        raise RecoilCollectionError("No recoil burst sample series could be extracted")

    extracted_profile = extractor(
        session=session,
        bursts=burst_series,
        profile_id=_build_profile_id(recognition_event.canonical_weapon_id, aim_mode, started_at),
        created_at=started_at,
        config=collector_config.extraction_config(),
    )
    if extracted_profile.profile.burst_count < collector_config.min_clean_bursts:
        raise RecoilCollectionError(
            "Insufficient repeated bursts for a reliable recoil profile: "
            f"need at least {collector_config.min_clean_bursts} clean bursts, "
            f"got {extracted_profile.profile.burst_count}"
        )
    profile_summary = _build_profile_summary(extracted_profile.profile)
    return RecoilCollectionResult(
        recognition_event=recognition_event,
        session=session,
        motion_samples=motion_samples,
        burst_windows=burst_windows,
        burst_series=burst_series,
        extracted_profile=extracted_profile,
        profile_summary=profile_summary,
    )


def build_burst_sample_series(
    *,
    session: RecoilCollectionSession,
    motion_samples: tuple[MotionTraceSample, ...] | list[MotionTraceSample],
    windows: tuple[RecoilBurstWindow, ...] | list[RecoilBurstWindow],
    sample_interval_ms: int,
) -> tuple[RecoilBurstSampleSeries, ...]:
    normalized_motion_samples = _coerce_motion_samples(motion_samples)
    normalized_windows = tuple(windows)
    result: list[RecoilBurstSampleSeries] = []
    for window in normalized_windows:
        burst_samples = tuple(
            RecoilSample(
                offset_ms=sample.offset_ms,
                x=sample.x,
                y=sample.y,
            )
            for sample in normalized_motion_samples
            if window.start_offset_ms <= sample.offset_ms < window.end_offset_ms
        )
        if not burst_samples:
            continue
        result.append(
            RecoilBurstSampleSeries(
                burst_id=window.burst_id,
                session_id=session.session_id,
                sample_interval_ms=sample_interval_ms,
                samples=burst_samples,
                sample_count=len(burst_samples),
            )
        )
    return tuple(result)


def build_full_screen_frame_grabber() -> Any:
    import win32api

    from vision.dxgi_capture import create_capture_backend

    screen_width = win32api.GetSystemMetrics(0)
    screen_height = win32api.GetSystemMetrics(1)
    return _BackendFrameGrabber(
        create_capture_backend(
            region=(0, 0, int(screen_width), int(screen_height)),
            output_color="RGB",
        )
    )


def build_live_motion_sampler(config: RecoilCollectorConfig) -> Callable[[], tuple[MotionTraceSample, ...]]:
    def _sample_motion_trace() -> tuple[MotionTraceSample, ...]:
        from vision.capture import ScreenCaptureThread

        capture_thread = ScreenCaptureThread(
            target_fps=config.capture_fps,
            crop_width=config.capture_width,
            crop_height=config.capture_height,
        )
        capture_thread.start()
        try:
            return _collect_motion_trace_from_thread(capture_thread=capture_thread, config=config)
        finally:
            capture_thread.stop()
            join = getattr(capture_thread, "join", None)
            if callable(join):
                join(timeout=1.0)

    return _sample_motion_trace


class _BackendFrameGrabber:
    def __init__(self, backend: Any):
        self._backend = backend

    def grab(self) -> Any:
        frame = self._backend.grab()
        if frame is None:
            raise RecoilCollectionError("Unable to capture a HUD frame for weapon confirmation")
        return frame

    def close(self) -> None:
        self._backend.close()


def _confirm_weapon_once(*, recognizer: Any, frame_source: Any, game: str) -> RecognitionEvent:
    frame = None
    try:
        frame = _grab_frame(frame_source)
        event = recognizer.process_frame(frame, frame_id=1, captured_at=0.0)
    finally:
        _close_if_present(frame_source)
    if event is None or not isinstance(event, RecognitionEvent) or event.degraded:
        raise RecoilCollectionError("Unable to confirm current weapon from the HUD without degraded recognition")
    if event.game != game:
        raise RecoilCollectionError(f"Recognizer returned {event.game!r} while collector is running for {game!r}")
    return event


def _grab_frame(frame_source: Any) -> Any:
    if callable(frame_source):
        return frame_source()
    grab = getattr(frame_source, "grab", None)
    if callable(grab):
        return grab()
    raise RecoilCollectionError("weapon_frame_source must be callable or expose a grab() method")


def _close_if_present(frame_source: Any) -> None:
    close = getattr(frame_source, "close", None)
    if callable(close):
        close()


def _coerce_motion_samples(samples: Iterable[MotionTraceSample]) -> tuple[MotionTraceSample, ...]:
    normalized_samples = tuple(samples)
    if not normalized_samples:
        raise RecoilCollectionError("No motion samples were captured")
    previous_offset_ms = None
    for index, sample in enumerate(normalized_samples):
        if not isinstance(sample, MotionTraceSample):
            raise RecoilCollectionError(f"motion_samples[{index}] must be a MotionTraceSample")
        if previous_offset_ms is not None and sample.offset_ms <= previous_offset_ms:
            raise RecoilCollectionError("Motion trace samples must be strictly increasing by offset_ms")
        previous_offset_ms = sample.offset_ms
    return normalized_samples


def _build_profile_summary(profile) -> RecoilProfileSummary:
    return RecoilProfileSummary(
        profile_id=profile.profile_id,
        canonical_weapon_id=profile.canonical_weapon_id,
        game=profile.game,
        stance=profile.stance,
        aim_mode=profile.aim_mode,
        sample_count=profile.sample_count,
        burst_count=profile.burst_count,
        confidence=profile.confidence,
        peak_abs_x=max(abs(sample_x) for sample_x in profile.samples_x),
        peak_abs_y=max(abs(sample_y) for sample_y in profile.samples_y),
        created_at=profile.created_at,
    )


def _build_session_id(canonical_weapon_id: str, aim_mode: str, started_at: str) -> str:
    return f"session-{canonical_weapon_id}-{aim_mode}-standing-{_compact_timestamp(started_at)}"


def _build_profile_id(canonical_weapon_id: str, aim_mode: str, started_at: str) -> str:
    return f"profile-{canonical_weapon_id}-{aim_mode}-standing-{_compact_timestamp(started_at)}"


def _compact_timestamp(timestamp: str) -> str:
    return _require_non_empty_str(timestamp, "timestamp").replace("-", "").replace(":", "").lower()


def _collect_motion_trace_from_thread(*, capture_thread: Any, config: RecoilCollectorConfig) -> tuple[MotionTraceSample, ...]:
    deadline = time.perf_counter() + config.max_capture_seconds
    last_seen_id = 0
    previous_gray = None
    first_captured_at = None
    previous_offset_ms = -1
    cumulative_x = 0.0
    cumulative_y = 0.0
    samples: list[MotionTraceSample] = []

    while time.perf_counter() < deadline:
        captured_frame, last_seen_id = capture_thread.get_latest_frame(last_seen_id=last_seen_id, timeout=0.25)
        if captured_frame is None:
            continue

        current_gray = _to_gray_float32(captured_frame.frame)
        if previous_gray is None:
            first_captured_at = float(captured_frame.captured_at)
            samples.append(MotionTraceSample(offset_ms=0, x=0.0, y=0.0, center_motion=0.0))
            previous_gray = current_gray
            previous_offset_ms = 0
            continue

        delta_x, delta_y = _estimate_phase_shift(previous_gray, current_gray)
        cumulative_x += delta_x
        cumulative_y += delta_y
        raw_offset_ms = int(round((float(captured_frame.captured_at) - float(first_captured_at)) * 1000.0))
        offset_ms = max(previous_offset_ms + 1, raw_offset_ms)
        samples.append(
            MotionTraceSample(
                offset_ms=offset_ms,
                x=cumulative_x,
                y=cumulative_y,
                center_motion=hypot(delta_x, delta_y),
            )
        )
        previous_gray = current_gray
        previous_offset_ms = offset_ms

    return _coerce_motion_samples(samples)


def _estimate_phase_shift(previous_gray: np.ndarray, current_gray: np.ndarray) -> tuple[float, float]:
    shift, response = cv2.phaseCorrelate(previous_gray, current_gray)
    delta_x = float(shift[0]) if np.isfinite(shift[0]) else 0.0
    delta_y = float(shift[1]) if np.isfinite(shift[1]) else 0.0
    if not np.isfinite(response) or response < _MIN_VALID_PHASE_CORRELATION_RESPONSE:
        return 0.0, 0.0
    return delta_x, delta_y


def _to_gray_float32(frame: Any) -> np.ndarray:
    array = np.asarray(frame)
    if array.size == 0:
        raise RecoilCollectionError("Captured motion frame must not be empty")
    if array.ndim == 2:
        gray = array
    elif array.ndim == 3 and array.shape[2] == 1:
        gray = array[:, :, 0]
    elif array.ndim == 3 and array.shape[2] >= 3:
        gray = cv2.cvtColor(array[:, :, :3], cv2.COLOR_RGB2GRAY)
    else:
        raise RecoilCollectionError("Captured motion frame must be grayscale or RGB-like")
    return gray.astype(np.float32)


def _utc_timestamp() -> str:
    from datetime import datetime
    from datetime import timezone

    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def _require_positive_int(value: Any, label: str) -> int:
    integer = _require_non_negative_int(value, label)
    if integer <= 0:
        raise ValueError(f"{label} must be positive")
    return integer


def _require_non_negative_int(value: Any, label: str) -> int:
    if type(value) is not int:
        raise ValueError(f"{label} must be an integer")
    if value < 0:
        raise ValueError(f"{label} must be non-negative")
    return value


def _require_number(value: Any, label: str) -> float:
    if type(value) not in {int, float}:
        raise ValueError(f"{label} must be a number")
    return float(value)


def _require_non_empty_str(value: Any, label: str) -> str:
    if type(value) is not str:
        raise ValueError(f"{label} must be a string")
    text = value.strip()
    if not text:
        raise ValueError(f"{label} must be a non-empty string")
    return text


__all__ = [
    "MotionTraceSample",
    "RecoilCollectionError",
    "RecoilCollectionResult",
    "RecoilCollectorConfig",
    "build_burst_sample_series",
    "build_full_screen_frame_grabber",
    "build_live_motion_sampler",
    "collect_recoil_profile",
]
