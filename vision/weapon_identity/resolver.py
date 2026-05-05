from __future__ import annotations

from dataclasses import dataclass
from typing import Any
from typing import Iterable
from typing import Mapping

from .adapters import WeaponIdentityAdapter
from .models import RecognitionEvent
from .models import WeaponIdentityRecord
from .runtime_state import ResolverRuntimeState
from .signatures import SignatureMatch

_CONFIDENCE_THRESHOLD = 0.80
_IMAGE_MARGIN_THRESHOLD = 0.08
_AGREEMENT_BONUS = 0.05
_DEGRADED_CONFIDENCE = 0.55
_TEXT_CONFIDENCE_BY_GAME = {
    "cod20": 0.84,
    "cod21": 0.93,
    "cod22": 0.72,
}
_CARRY_FORWARD_CONFIDENCE_BY_GAME = {
    "cod20": 0.78,
    "cod21": 0.86,
    "cod22": 0.76,
}
_IMAGE_PENALTY_BY_GAME = {
    "cod20": 0.00,
    "cod21": 0.12,
    "cod22": 0.00,
}


@dataclass(slots=True, frozen=True)
class ResolverResult:
    event: RecognitionEvent | None
    runtime_state: ResolverRuntimeState

    def __post_init__(self) -> None:
        if self.event is not None and not isinstance(self.event, RecognitionEvent):
            raise ValueError("ResolverResult.event must be a RecognitionEvent or None")
        if not isinstance(self.runtime_state, ResolverRuntimeState):
            raise ValueError("ResolverResult.runtime_state must be a ResolverRuntimeState")


def resolve_weapon(
    *,
    adapter: WeaponIdentityAdapter,
    identity_records: Mapping[str, WeaponIdentityRecord] | Iterable[WeaponIdentityRecord],
    ranked_image_matches: Iterable[SignatureMatch] = (),
    text_candidates: Iterable[str] = (),
    runtime_state: ResolverRuntimeState | None = None,
    switch_suspected: bool = False,
    timestamp: str,
) -> ResolverResult:
    adapter = _require_adapter(adapter)
    state = _coerce_runtime_state(runtime_state).begin_frame(
        switch_suspected=_require_bool(switch_suspected, "switch_suspected"),
        text_window_frames=adapter.switch_hints.text_window_frames,
    )
    timestamp = _require_non_empty_str(timestamp, "timestamp")
    records = _coerce_identity_records(identity_records)
    image_signal = _pick_image_signal(ranked_image_matches, adapter.game_id)
    text_signal = _pick_text_signal(
        text_candidates,
        records,
        game_id=adapter.game_id,
        text_window_active=state.text_window_active,
    )
    previous_weapon_id = state.confirmed_weapon_id

    if image_signal and text_signal and image_signal.canonical_weapon_id != text_signal.canonical_weapon_id:
        return _carry_forward(previous_weapon_id, state, adapter.game_id, timestamp, degraded=True)

    if image_signal and text_signal and image_signal.canonical_weapon_id == text_signal.canonical_weapon_id:
        confidence = _clamp_confidence(max(image_signal.confidence, text_signal.confidence) + _AGREEMENT_BONUS)
        if confidence >= _CONFIDENCE_THRESHOLD:
            return _confirmed_result(
                game_id=adapter.game_id,
                canonical_weapon_id=image_signal.canonical_weapon_id,
                confidence=confidence,
                source="image+text",
                timestamp=timestamp,
                matched_name=text_signal.matched_name,
                runtime_state=state,
            )
        return _carry_forward(previous_weapon_id, state, adapter.game_id, timestamp, degraded=True)

    if text_signal and text_signal.confidence >= _CONFIDENCE_THRESHOLD:
        return _confirmed_result(
            game_id=adapter.game_id,
            canonical_weapon_id=text_signal.canonical_weapon_id,
            confidence=text_signal.confidence,
            source="text",
            timestamp=timestamp,
            matched_name=text_signal.matched_name,
            runtime_state=state,
        )

    if (
        previous_weapon_id
        and adapter.game_id == "cod21"
        and not state.switch_suspected
        and image_signal is not None
        and text_signal is None
    ):
        return _carry_forward(previous_weapon_id, state, adapter.game_id, timestamp, degraded=True)

    if image_signal and image_signal.confidence >= _CONFIDENCE_THRESHOLD:
        return _confirmed_result(
            game_id=adapter.game_id,
            canonical_weapon_id=image_signal.canonical_weapon_id,
            confidence=image_signal.confidence,
            source="image",
            timestamp=timestamp,
            matched_name=None,
            runtime_state=state,
        )

    if (
        previous_weapon_id
        and adapter.switch_hints.cache_weapon_until_switch
        and not state.switch_suspected
        and image_signal is None
        and text_signal is None
    ):
        return _carry_forward(previous_weapon_id, state, adapter.game_id, timestamp, degraded=False)

    if previous_weapon_id:
        return _carry_forward(previous_weapon_id, state, adapter.game_id, timestamp, degraded=True)

    return ResolverResult(event=None, runtime_state=state)


@dataclass(slots=True, frozen=True)
class _ResolvedTextSignal:
    canonical_weapon_id: str
    confidence: float
    matched_name: str


@dataclass(slots=True, frozen=True)
class _ResolvedImageSignal:
    canonical_weapon_id: str
    confidence: float


def _pick_text_signal(
    text_candidates: Iterable[str],
    identity_records: tuple[WeaponIdentityRecord, ...],
    *,
    game_id: str,
    text_window_active: bool,
) -> _ResolvedTextSignal | None:
    if game_id == "cod20" and not text_window_active:
        return None

    for candidate_name in text_candidates:
        normalized_name = _require_non_empty_str(candidate_name, "text_candidates[]")
        canonical_weapon_id = _resolve_name(normalized_name, identity_records)
        if canonical_weapon_id is None:
            continue
        confidence = _TEXT_CONFIDENCE_BY_GAME.get(game_id, 0.75)
        if game_id == "cod21" and not text_window_active:
            confidence = 0.70
        return _ResolvedTextSignal(
            canonical_weapon_id=canonical_weapon_id,
            confidence=confidence,
            matched_name=normalized_name,
        )
    return None


def _pick_image_signal(ranked_image_matches: Iterable[SignatureMatch], game_id: str) -> _ResolvedImageSignal | None:
    matches = tuple(_require_signature_match(match, "ranked_image_matches[]") for match in ranked_image_matches)
    if not matches:
        return None

    top_match = matches[0]
    second_score = matches[1].score if len(matches) > 1 else 0.0
    confidence = top_match.score - _IMAGE_PENALTY_BY_GAME.get(game_id, 0.0)
    if len(matches) > 1 and (top_match.score - second_score) < _IMAGE_MARGIN_THRESHOLD:
        confidence -= 0.15
    return _ResolvedImageSignal(
        canonical_weapon_id=top_match.canonical_weapon_id,
        confidence=_clamp_confidence(confidence),
    )


def _confirmed_result(
    *,
    game_id: str,
    canonical_weapon_id: str,
    confidence: float,
    source: str,
    timestamp: str,
    matched_name: str | None,
    runtime_state: ResolverRuntimeState,
) -> ResolverResult:
    event = RecognitionEvent(
        game=game_id,
        canonical_weapon_id=canonical_weapon_id,
        confidence=confidence,
        source=source,
        timestamp=timestamp,
        degraded=False,
        matched_name=matched_name,
    )
    return ResolverResult(
        event=event,
        runtime_state=runtime_state.with_confirmed_weapon(canonical_weapon_id),
    )


def _carry_forward(
    previous_weapon_id: str | None,
    runtime_state: ResolverRuntimeState,
    game_id: str,
    timestamp: str,
    *,
    degraded: bool,
) -> ResolverResult:
    if previous_weapon_id is None:
        return ResolverResult(event=None, runtime_state=runtime_state)

    confidence = _DEGRADED_CONFIDENCE if degraded else _CARRY_FORWARD_CONFIDENCE_BY_GAME.get(game_id, 0.82)
    event = RecognitionEvent(
        game=game_id,
        canonical_weapon_id=previous_weapon_id,
        confidence=confidence,
        source="carry_forward",
        timestamp=timestamp,
        degraded=degraded,
        matched_name=None,
    )
    return ResolverResult(
        event=event,
        runtime_state=runtime_state.with_confirmed_weapon(previous_weapon_id),
    )


def _resolve_name(candidate_name: str, identity_records: tuple[WeaponIdentityRecord, ...]) -> str | None:
    for record in identity_records:
        canonical_weapon_id = record.resolve_name(candidate_name)
        if canonical_weapon_id is not None:
            return canonical_weapon_id
    return None


def _coerce_identity_records(
    identity_records: Mapping[str, WeaponIdentityRecord] | Iterable[WeaponIdentityRecord],
) -> tuple[WeaponIdentityRecord, ...]:
    if isinstance(identity_records, Mapping):
        records = tuple(identity_records.values())
    else:
        records = tuple(identity_records)
    return tuple(_require_identity_record(record, "identity_records[]") for record in records)


def _coerce_runtime_state(runtime_state: ResolverRuntimeState | None) -> ResolverRuntimeState:
    if runtime_state is None:
        return ResolverRuntimeState()
    if not isinstance(runtime_state, ResolverRuntimeState):
        raise ValueError("runtime_state must be a ResolverRuntimeState")
    return runtime_state


def _require_adapter(value: Any) -> WeaponIdentityAdapter:
    if not isinstance(value, WeaponIdentityAdapter):
        raise ValueError("adapter must be a WeaponIdentityAdapter")
    return value


def _require_identity_record(value: Any, label: str) -> WeaponIdentityRecord:
    if not isinstance(value, WeaponIdentityRecord):
        raise ValueError(f"{label} must contain WeaponIdentityRecord values")
    return value


def _require_signature_match(value: Any, label: str) -> SignatureMatch:
    if not isinstance(value, SignatureMatch):
        raise ValueError(f"{label} must contain SignatureMatch values")
    return value


def _require_non_empty_str(value: Any, label: str) -> str:
    if type(value) is not str:
        raise ValueError(f"{label} must be a string")
    result = value.strip()
    if not result:
        raise ValueError(f"{label} must be a non-empty string")
    return result


def _require_bool(value: Any, label: str) -> bool:
    if type(value) is not bool:
        raise ValueError(f"{label} must be a boolean")
    return value


def _clamp_confidence(value: float) -> float:
    return min(1.0, max(0.0, float(value)))


__all__ = ["ResolverResult", "resolve_weapon"]
