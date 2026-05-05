from __future__ import annotations

from bisect import bisect_left
from dataclasses import dataclass
from statistics import fmean
from statistics import median
from statistics import pstdev

from vision.recoil_collection.models import RecoilBurstSampleSeries
from vision.recoil_collection.models import RecoilCollectionSession
from vision.recoil_collection.models import RecoilProfileRecord


@dataclass(slots=True, frozen=True)
class RecoilExtractionConfig:
    sample_interval_ms: int
    min_clean_bursts: int = 3
    target_clean_bursts: int = 4
    outlier_mad_multiplier: float = 6.0
    outlier_distance_floor: float = 1.25
    variance_soft_limit: float = 0.75

    def __post_init__(self) -> None:
        if type(self.sample_interval_ms) is not int or self.sample_interval_ms <= 0:
            raise ValueError("RecoilExtractionConfig.sample_interval_ms must be a positive integer")
        if type(self.min_clean_bursts) is not int or self.min_clean_bursts <= 0:
            raise ValueError("RecoilExtractionConfig.min_clean_bursts must be a positive integer")
        if type(self.target_clean_bursts) is not int or self.target_clean_bursts <= 0:
            raise ValueError("RecoilExtractionConfig.target_clean_bursts must be a positive integer")
        if self.target_clean_bursts < self.min_clean_bursts:
            raise ValueError("RecoilExtractionConfig.target_clean_bursts must be >= min_clean_bursts")
        if type(self.outlier_mad_multiplier) not in {int, float} or float(self.outlier_mad_multiplier) < 0.0:
            raise ValueError("RecoilExtractionConfig.outlier_mad_multiplier must be a non-negative number")
        if type(self.outlier_distance_floor) not in {int, float} or float(self.outlier_distance_floor) < 0.0:
            raise ValueError("RecoilExtractionConfig.outlier_distance_floor must be a non-negative number")
        if type(self.variance_soft_limit) not in {int, float} or float(self.variance_soft_limit) <= 0.0:
            raise ValueError("RecoilExtractionConfig.variance_soft_limit must be a positive number")
        object.__setattr__(self, "outlier_mad_multiplier", float(self.outlier_mad_multiplier))
        object.__setattr__(self, "outlier_distance_floor", float(self.outlier_distance_floor))
        object.__setattr__(self, "variance_soft_limit", float(self.variance_soft_limit))


@dataclass(slots=True, frozen=True)
class ExtractedRecoilProfile:
    profile: RecoilProfileRecord
    accepted_burst_ids: tuple[str, ...]
    rejected_burst_ids: tuple[str, ...]


@dataclass(slots=True, frozen=True)
class _AlignedBurstCurve:
    burst_id: str
    sample_interval_ms: int
    offsets_ms: tuple[int, ...]
    samples_x: tuple[float, ...]
    samples_y: tuple[float, ...]


def extract_recoil_profile(
    *,
    session: RecoilCollectionSession,
    bursts: tuple[RecoilBurstSampleSeries, ...] | list[RecoilBurstSampleSeries],
    profile_id: str,
    created_at: str,
    config: RecoilExtractionConfig,
) -> ExtractedRecoilProfile:
    normalized_bursts = tuple(bursts)
    if not normalized_bursts:
        raise ValueError("extract_recoil_profile requires at least one burst")
    for burst in normalized_bursts:
        if burst.session_id != session.session_id:
            raise ValueError("All bursts must belong to the provided session")

    aligned_bursts = tuple(_align_and_resample_burst(burst=burst, config=config) for burst in normalized_bursts)
    clean_bursts, rejected_burst_ids = _reject_outliers(aligned_bursts=aligned_bursts, config=config)
    profile_samples_x = tuple(
        fmean(burst.samples_x[index] for burst in clean_bursts)
        for index in range(len(clean_bursts[0].samples_x))
    )
    profile_samples_y = tuple(
        fmean(burst.samples_y[index] for burst in clean_bursts)
        for index in range(len(clean_bursts[0].samples_y))
    )
    variance_summary = _summarize_variance(clean_bursts)
    confidence = _compute_confidence(
        total_burst_count=len(aligned_bursts),
        clean_burst_count=len(clean_bursts),
        variance_summary=variance_summary,
        config=config,
    )

    profile = RecoilProfileRecord(
        profile_id=profile_id,
        canonical_weapon_id=session.canonical_weapon_id,
        game=session.game,
        stance=session.stance,
        aim_mode=session.aim_mode,
        sample_interval_ms=config.sample_interval_ms,
        duration_ms=len(profile_samples_x) * config.sample_interval_ms,
        initial_delay_ms=0,
        samples_x=profile_samples_x,
        samples_y=profile_samples_y,
        sample_count=len(profile_samples_x),
        burst_count=len(clean_bursts),
        variance_summary=variance_summary,
        confidence=confidence,
        capture_resolution=session.capture_resolution,
        capture_fps=session.capture_fps,
        collector_version=session.collector_version,
        created_at=created_at,
    )
    return ExtractedRecoilProfile(
        profile=profile,
        accepted_burst_ids=tuple(burst.burst_id for burst in clean_bursts),
        rejected_burst_ids=rejected_burst_ids,
    )


def _align_and_resample_burst(
    *,
    burst: RecoilBurstSampleSeries,
    config: RecoilExtractionConfig,
) -> _AlignedBurstCurve:
    first_sample = burst.samples[0]
    aligned_offsets_ms = tuple(sample.offset_ms - first_sample.offset_ms for sample in burst.samples)
    aligned_samples_x = tuple(sample.x - first_sample.x for sample in burst.samples)
    aligned_samples_y = tuple(sample.y - first_sample.y for sample in burst.samples)
    return _AlignedBurstCurve(
        burst_id=burst.burst_id,
        sample_interval_ms=burst.sample_interval_ms,
        offsets_ms=aligned_offsets_ms,
        samples_x=aligned_samples_x,
        samples_y=aligned_samples_y,
    )


def _reject_outliers(
    *,
    aligned_bursts: tuple[_AlignedBurstCurve, ...],
    config: RecoilExtractionConfig,
) -> tuple[tuple[_AlignedBurstCurve, ...], tuple[str, ...]]:
    target_offsets_ms = _common_target_offsets(aligned_bursts=aligned_bursts, config=config)
    resampled_bursts = tuple(
        _resample_aligned_curve(
            curve=curve,
            target_offsets_ms=target_offsets_ms,
            target_sample_interval_ms=config.sample_interval_ms,
        )
        for curve in aligned_bursts
    )
    if len(resampled_bursts) < 3:
        return resampled_bursts, ()

    median_curve_x = tuple(
        median(burst.samples_x[index] for burst in resampled_bursts)
        for index in range(len(target_offsets_ms))
    )
    median_curve_y = tuple(
        median(burst.samples_y[index] for burst in resampled_bursts)
        for index in range(len(target_offsets_ms))
    )
    distances = tuple(
        _curve_distance(
            burst=burst,
            reference_x=median_curve_x,
            reference_y=median_curve_y,
        )
        for burst in resampled_bursts
    )
    median_distance = median(distances)
    median_absolute_deviation = median(abs(distance - median_distance) for distance in distances)
    threshold = median_distance + max(
        config.outlier_distance_floor,
        config.outlier_mad_multiplier * median_absolute_deviation,
    )

    clean_bursts = tuple(
        burst
        for burst, distance in zip(resampled_bursts, distances)
        if distance <= threshold
    )
    rejected_burst_ids = tuple(
        burst.burst_id
        for burst, distance in zip(resampled_bursts, distances)
        if distance > threshold
    )
    if clean_bursts:
        return clean_bursts, rejected_burst_ids

    best_burst_index = min(range(len(resampled_bursts)), key=lambda index: distances[index])
    return (resampled_bursts[best_burst_index],), tuple(
        burst.burst_id
        for index, burst in enumerate(resampled_bursts)
        if index != best_burst_index
    )


def _common_target_offsets(
    *,
    aligned_bursts: tuple[_AlignedBurstCurve, ...],
    config: RecoilExtractionConfig,
) -> tuple[int, ...]:
    common_last_offset_ms = min(curve.offsets_ms[-1] for curve in aligned_bursts)
    return tuple(range(0, common_last_offset_ms + 1, config.sample_interval_ms))


def _resample_aligned_curve(
    *,
    curve: _AlignedBurstCurve,
    target_offsets_ms: tuple[int, ...],
    target_sample_interval_ms: int,
) -> _AlignedBurstCurve:
    return _AlignedBurstCurve(
        burst_id=curve.burst_id,
        sample_interval_ms=target_sample_interval_ms,
        offsets_ms=target_offsets_ms,
        samples_x=tuple(_interpolate(curve.offsets_ms, curve.samples_x, offset_ms) for offset_ms in target_offsets_ms),
        samples_y=tuple(_interpolate(curve.offsets_ms, curve.samples_y, offset_ms) for offset_ms in target_offsets_ms),
    )


def _interpolate(offsets_ms: tuple[int, ...], values: tuple[float, ...], target_offset_ms: int) -> float:
    if target_offset_ms <= offsets_ms[0]:
        return values[0]
    if target_offset_ms >= offsets_ms[-1]:
        return values[-1]

    right_index = bisect_left(offsets_ms, target_offset_ms)
    if offsets_ms[right_index] == target_offset_ms:
        return values[right_index]

    left_index = right_index - 1
    left_offset_ms = offsets_ms[left_index]
    right_offset_ms = offsets_ms[right_index]
    left_value = values[left_index]
    right_value = values[right_index]
    interval_ms = right_offset_ms - left_offset_ms
    if interval_ms <= 0:
        return left_value
    progress = (target_offset_ms - left_offset_ms) / interval_ms
    return left_value + ((right_value - left_value) * progress)


def _curve_distance(
    *,
    burst: _AlignedBurstCurve,
    reference_x: tuple[float, ...],
    reference_y: tuple[float, ...],
) -> float:
    total_squared_error = 0.0
    for sample_x, sample_y, expected_x, expected_y in zip(
        burst.samples_x,
        burst.samples_y,
        reference_x,
        reference_y,
    ):
        total_squared_error += (sample_x - expected_x) ** 2
        total_squared_error += (sample_y - expected_y) ** 2
    return (total_squared_error / max(1, len(reference_x))) ** 0.5


def _summarize_variance(clean_bursts: tuple[_AlignedBurstCurve, ...]) -> dict[str, float]:
    pointwise_horizontal_stddev = tuple(
        pstdev([burst.samples_x[index] for burst in clean_bursts])
        for index in range(len(clean_bursts[0].samples_x))
    )
    pointwise_vertical_stddev = tuple(
        pstdev([burst.samples_y[index] for burst in clean_bursts])
        for index in range(len(clean_bursts[0].samples_y))
    )
    return {
        "horizontal_stddev": fmean(pointwise_horizontal_stddev),
        "vertical_stddev": fmean(pointwise_vertical_stddev),
        "max_horizontal_stddev": max(pointwise_horizontal_stddev),
        "max_vertical_stddev": max(pointwise_vertical_stddev),
    }


def _compute_confidence(
    *,
    total_burst_count: int,
    clean_burst_count: int,
    variance_summary: dict[str, float],
    config: RecoilExtractionConfig,
) -> float:
    burst_factor = min(1.0, clean_burst_count / config.target_clean_bursts)
    retention_factor = clean_burst_count / max(1, total_burst_count)
    disagreement_scale = max(
        variance_summary["horizontal_stddev"],
        variance_summary["vertical_stddev"],
    )
    variance_factor = 1.0 / (1.0 + (disagreement_scale / config.variance_soft_limit))
    confidence = burst_factor * retention_factor * variance_factor
    if clean_burst_count < config.min_clean_bursts:
        confidence *= clean_burst_count / config.min_clean_bursts
    return max(0.0, min(1.0, confidence))


__all__ = [
    "ExtractedRecoilProfile",
    "RecoilExtractionConfig",
    "extract_recoil_profile",
]
