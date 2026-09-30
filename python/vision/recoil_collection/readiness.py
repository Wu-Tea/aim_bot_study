from __future__ import annotations

from typing import Any

from vision.recoil_collection.models import RecoilProfileRecord

DEFAULT_MIN_ACTIVE_PROFILE_CONFIDENCE = 0.70
DEFAULT_MIN_MAGAZINE_SUPPORT = 2
DEFAULT_MAX_VERTICAL_RECOVERY_RATIO = 0.65
DEFAULT_MIN_VERTICAL_RECOVERY_PEAK_PIXELS = 80.0
DEFAULT_MIN_VERTICAL_RECOVERY_DROP_PIXELS = 80.0
DEFAULT_MAX_HORIZONTAL_FINAL_RANGE_PIXELS = 120.0


def is_profile_ready_for_compensation(
    profile: RecoilProfileRecord,
    *,
    min_confidence: float = DEFAULT_MIN_ACTIVE_PROFILE_CONFIDENCE,
    min_magazine_support: int = DEFAULT_MIN_MAGAZINE_SUPPORT,
) -> bool:
    return profile_readiness_reason(
        profile,
        min_confidence=min_confidence,
        min_magazine_support=min_magazine_support,
    ) is None


def profile_readiness_reason(
    profile: RecoilProfileRecord,
    *,
    min_confidence: float = DEFAULT_MIN_ACTIVE_PROFILE_CONFIDENCE,
    min_magazine_support: int = DEFAULT_MIN_MAGAZINE_SUPPORT,
) -> str | None:
    if profile.profile_type != "magazine_curve_v1":
        if profile.confidence < float(min_confidence):
            return "confidence_below_min"
        return None

    required_support = max(1, int(min_magazine_support))
    accepted_episodes = _float_from_mapping(
        profile.fit_summary,
        "accepted_episode_count",
        fallback=float(profile.burst_count),
    )
    if accepted_episodes < float(required_support):
        return "accepted_episodes_below_min"
    if profile.support_counts and min(profile.support_counts) < required_support:
        return "support_below_min"
    if _float_from_mapping(profile.fit_summary, "vertical_direction_reversal", fallback=0.0) >= 1.0:
        return "vertical_direction_reversal"
    if _float_from_mapping(
        profile.fit_summary,
        "horizontal_final_range",
        fallback=0.0,
    ) >= DEFAULT_MAX_HORIZONTAL_FINAL_RANGE_PIXELS:
        return "horizontal_episode_disagreement"
    return None


def has_vertical_recovery_tail(profile: RecoilProfileRecord) -> bool:
    if profile.profile_type != "magazine_curve_v1":
        return False
    if _float_from_mapping(profile.fit_summary, "vertical_recovery_tail", fallback=0.0) >= 1.0:
        return True
    return _has_vertical_recovery_tail(profile.samples_y)


def _float_from_mapping(mapping: Any, key: str, *, fallback: float) -> float:
    try:
        value = mapping.get(key, fallback)
    except AttributeError:
        return fallback
    if type(value) not in {int, float}:
        return fallback
    return float(value)


def _has_vertical_recovery_tail(samples_y: tuple[float, ...]) -> bool:
    if len(samples_y) < 4:
        return False
    min_y = min(samples_y)
    max_y = max(samples_y)
    if abs(max_y) >= abs(min_y):
        peak_abs = float(abs(max_y))
        final_abs = float(samples_y[-1])
    else:
        peak_abs = float(abs(min_y))
        final_abs = float(abs(samples_y[-1]))
    if peak_abs < DEFAULT_MIN_VERTICAL_RECOVERY_PEAK_PIXELS:
        return False
    recovery_abs = peak_abs - max(0.0, final_abs)
    if recovery_abs < DEFAULT_MIN_VERTICAL_RECOVERY_DROP_PIXELS:
        return False
    recovery_ratio = max(0.0, final_abs) / peak_abs
    return recovery_ratio <= DEFAULT_MAX_VERTICAL_RECOVERY_RATIO


__all__ = [
    "DEFAULT_MIN_ACTIVE_PROFILE_CONFIDENCE",
    "DEFAULT_MIN_MAGAZINE_SUPPORT",
    "has_vertical_recovery_tail",
    "is_profile_ready_for_compensation",
    "profile_readiness_reason",
]
