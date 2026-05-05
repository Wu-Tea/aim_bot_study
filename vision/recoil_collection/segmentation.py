from __future__ import annotations

from dataclasses import dataclass

from vision.recoil_collection.models import RecoilBurstWindow
from vision.recoil_collection.models import RecoilCollectionSession


@dataclass(slots=True, frozen=True)
class BurstSegmentationSample:
    offset_ms: int
    center_motion: float
    ammo: int | None = None
    manual_marker: str | None = None

    def __post_init__(self) -> None:
        if type(self.offset_ms) is not int or self.offset_ms < 0:
            raise ValueError("BurstSegmentationSample.offset_ms must be a non-negative integer")
        if type(self.center_motion) not in {int, float} or float(self.center_motion) < 0.0:
            raise ValueError("BurstSegmentationSample.center_motion must be a non-negative number")
        object.__setattr__(self, "center_motion", float(self.center_motion))
        if self.ammo is not None and (type(self.ammo) is not int or self.ammo < 0):
            raise ValueError("BurstSegmentationSample.ammo must be a non-negative integer or None")
        if self.manual_marker not in {None, "start", "stop"}:
            raise ValueError("BurstSegmentationSample.manual_marker must be one of [None, 'start', 'stop']")


@dataclass(slots=True, frozen=True)
class BurstSegmentationConfig:
    motion_start_threshold: float
    motion_end_threshold: float
    motion_confirm_frames: int
    settle_frames: int

    def __post_init__(self) -> None:
        if type(self.motion_start_threshold) not in {int, float} or float(self.motion_start_threshold) <= 0.0:
            raise ValueError("BurstSegmentationConfig.motion_start_threshold must be positive")
        if type(self.motion_end_threshold) not in {int, float} or float(self.motion_end_threshold) < 0.0:
            raise ValueError("BurstSegmentationConfig.motion_end_threshold must be non-negative")
        object.__setattr__(self, "motion_start_threshold", float(self.motion_start_threshold))
        object.__setattr__(self, "motion_end_threshold", float(self.motion_end_threshold))
        if self.motion_end_threshold > self.motion_start_threshold:
            raise ValueError("BurstSegmentationConfig.motion_end_threshold must not exceed motion_start_threshold")
        if type(self.motion_confirm_frames) is not int or self.motion_confirm_frames <= 0:
            raise ValueError("BurstSegmentationConfig.motion_confirm_frames must be positive")
        if type(self.settle_frames) is not int or self.settle_frames <= 0:
            raise ValueError("BurstSegmentationConfig.settle_frames must be positive")


def segment_standing_fire_bursts(
    *,
    session: RecoilCollectionSession,
    samples: tuple[BurstSegmentationSample, ...] | list[BurstSegmentationSample],
    config: BurstSegmentationConfig,
) -> tuple[RecoilBurstWindow, ...]:
    normalized_samples = tuple(samples)
    _require_monotonic_samples(normalized_samples)

    windows: list[RecoilBurstWindow] = []
    last_visible_ammo: int | None = None
    motion_streak = 0
    motion_streak_start_offset_ms: int | None = None

    active_start_offset_ms: int | None = None
    active_start_reason: str | None = None
    last_active_offset_ms: int | None = None
    low_motion_streak = 0

    for sample in normalized_samples:
        ammo_drop = False
        if sample.ammo is not None:
            ammo_drop = last_visible_ammo is not None and sample.ammo < last_visible_ammo
            last_visible_ammo = sample.ammo

        if active_start_offset_ms is None:
            if sample.manual_marker == "start":
                active_start_offset_ms = sample.offset_ms
                active_start_reason = "manual"
                last_active_offset_ms = sample.offset_ms
                motion_streak = 0
                motion_streak_start_offset_ms = None
                low_motion_streak = 0
                continue

            if sample.center_motion >= config.motion_start_threshold:
                motion_streak += 1
                if motion_streak_start_offset_ms is None:
                    motion_streak_start_offset_ms = sample.offset_ms
            else:
                motion_streak = 0
                motion_streak_start_offset_ms = None

            if motion_streak >= config.motion_confirm_frames:
                active_start_offset_ms = motion_streak_start_offset_ms
                active_start_reason = "motion"
                last_active_offset_ms = sample.offset_ms
                low_motion_streak = 0
                motion_streak = 0
                motion_streak_start_offset_ms = None
                continue

            if ammo_drop:
                active_start_offset_ms = sample.offset_ms
                active_start_reason = "ammo"
                last_active_offset_ms = sample.offset_ms
                low_motion_streak = 0
            continue

        if sample.manual_marker == "stop":
            windows.append(
                _build_window(
                    session=session,
                    burst_index=len(windows) + 1,
                    start_offset_ms=active_start_offset_ms,
                    end_offset_ms=sample.offset_ms,
                    start_reason=active_start_reason,
                    end_reason="manual",
                )
            )
            active_start_offset_ms = None
            active_start_reason = None
            last_active_offset_ms = None
            low_motion_streak = 0
            motion_streak = 0
            motion_streak_start_offset_ms = None
            continue

        if active_start_reason == "manual":
            last_active_offset_ms = sample.offset_ms
            continue

        if sample.center_motion <= config.motion_end_threshold:
            low_motion_streak += 1
            if low_motion_streak >= config.settle_frames:
                end_offset_ms = last_active_offset_ms if last_active_offset_ms is not None else sample.offset_ms
                windows.append(
                    _build_window(
                        session=session,
                        burst_index=len(windows) + 1,
                        start_offset_ms=active_start_offset_ms,
                        end_offset_ms=end_offset_ms,
                        start_reason=active_start_reason,
                        end_reason="motion_settled",
                    )
                )
                active_start_offset_ms = None
                active_start_reason = None
                last_active_offset_ms = None
                low_motion_streak = 0
                motion_streak = 0
                motion_streak_start_offset_ms = None
        else:
            low_motion_streak = 0
            last_active_offset_ms = sample.offset_ms

    if active_start_offset_ms is not None:
        final_offset_ms = normalized_samples[-1].offset_ms if normalized_samples else active_start_offset_ms
        windows.append(
            _build_window(
                session=session,
                burst_index=len(windows) + 1,
                start_offset_ms=active_start_offset_ms,
                end_offset_ms=final_offset_ms,
                start_reason=active_start_reason,
                end_reason="end_of_samples",
            )
        )

    return tuple(windows)


def _require_monotonic_samples(samples: tuple[BurstSegmentationSample, ...]) -> None:
    previous_offset_ms = None
    for sample in samples:
        if previous_offset_ms is not None and sample.offset_ms <= previous_offset_ms:
            raise ValueError("Burst segmentation samples must be strictly increasing by offset_ms")
        previous_offset_ms = sample.offset_ms


def _build_window(
    *,
    session: RecoilCollectionSession,
    burst_index: int,
    start_offset_ms: int,
    end_offset_ms: int,
    start_reason: str | None,
    end_reason: str,
) -> RecoilBurstWindow:
    return RecoilBurstWindow(
        burst_id=f"{session.session_id}-burst-{burst_index:03d}",
        session_id=session.session_id,
        start_offset_ms=start_offset_ms,
        end_offset_ms=end_offset_ms,
        start_reason="manual" if start_reason is None else start_reason,
        end_reason=end_reason,
    )
