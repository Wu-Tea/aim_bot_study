from __future__ import annotations

from typing import Any

from vision.recoil_collection.models import RecoilProfileRecord

DEFAULT_MIN_ACTIVE_PROFILE_CONFIDENCE = 0.70
DEFAULT_MIN_MAGAZINE_SUPPORT = 2


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
    return None


def _float_from_mapping(mapping: Any, key: str, *, fallback: float) -> float:
    try:
        value = mapping.get(key, fallback)
    except AttributeError:
        return fallback
    if type(value) not in {int, float}:
        return fallback
    return float(value)


__all__ = [
    "DEFAULT_MIN_ACTIVE_PROFILE_CONFIDENCE",
    "DEFAULT_MIN_MAGAZINE_SUPPORT",
    "is_profile_ready_for_compensation",
    "profile_readiness_reason",
]
