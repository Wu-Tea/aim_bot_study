from __future__ import annotations

import json
from os import PathLike
from pathlib import Path
from typing import Any
from typing import Mapping
from typing import TextIO

from vision.recoil_collection.models import RecoilProfileRecord

from .models import ActiveProfilePayload
from .models import RecognizerState
from .models import SidecarRuntimeContext

RecognizerStateSource = RecognizerState | Mapping[str, Any] | TextIO | str | PathLike[str] | None

_READY_IDENTITY_THRESHOLD = 0.80


class RecoilSidecarService:
    def __init__(
        self,
        *,
        profile_dir: str | PathLike[str],
        recognizer_state_path: str | PathLike[str] | None = None,
        ready_identity_threshold: float = _READY_IDENTITY_THRESHOLD,
    ) -> None:
        self.profile_dir = Path(profile_dir)
        self.recognizer_state_path = Path(recognizer_state_path) if recognizer_state_path is not None else None
        self.ready_identity_threshold = _require_confidence(
            ready_identity_threshold,
            "RecoilSidecarService.ready_identity_threshold",
        )

    def read_recognizer_state(self, source: RecognizerStateSource = None) -> RecognizerState | None:
        if isinstance(source, RecognizerState):
            return source

        if source is None:
            if self.recognizer_state_path is None or not self.recognizer_state_path.exists():
                return None
            source = self.recognizer_state_path

        if isinstance(source, Mapping):
            return RecognizerState.from_dict(dict(source))

        if hasattr(source, "read"):
            payload = _load_json_text(getattr(source, "read")())
            return RecognizerState.from_dict(payload)

        state_path = Path(source)
        if not state_path.exists():
            return None
        payload = _load_json_text(state_path.read_text(encoding="utf-8"))
        return RecognizerState.from_dict(payload)

    def load_matching_profiles(
        self,
        recognizer_state: RecognizerState,
        *,
        context: SidecarRuntimeContext | Mapping[str, Any] | None = None,
    ) -> tuple[RecoilProfileRecord, ...]:
        state = _coerce_recognizer_state(recognizer_state)
        runtime_context = _coerce_runtime_context(context)
        all_profiles = self._load_profile_store()
        candidate_profiles = self._narrow_profiles_by_hint(all_profiles, state.profile_ids)

        matches = [
            profile
            for profile in candidate_profiles
            if profile.game == state.game
            and profile.canonical_weapon_id == state.canonical_weapon_id
            and profile.stance == runtime_context.stance
            and (runtime_context.aim_mode is None or profile.aim_mode == runtime_context.aim_mode)
        ]
        return tuple(sorted(matches, key=lambda profile: (-profile.confidence, profile.profile_id)))

    def publish_active_profile(
        self,
        source: RecognizerStateSource = None,
        *,
        context: SidecarRuntimeContext | Mapping[str, Any] | None = None,
    ) -> ActiveProfilePayload:
        runtime_context = _coerce_runtime_context(context)
        recognizer_state = self.read_recognizer_state(source)
        if recognizer_state is None:
            return ActiveProfilePayload(
                canonical_weapon_id=None,
                profile_id=None,
                game=None,
                stance=runtime_context.stance,
                aim_mode=runtime_context.aim_mode,
                profile_confidence=None,
                identity_confidence=None,
                updated_at=None,
                status="unknown",
            )

        matches = self.load_matching_profiles(recognizer_state, context=runtime_context)
        selected_profile = _select_best_profile(matches)
        if selected_profile is None:
            return ActiveProfilePayload(
                canonical_weapon_id=recognizer_state.canonical_weapon_id,
                profile_id=None,
                game=recognizer_state.game,
                stance=runtime_context.stance,
                aim_mode=runtime_context.aim_mode,
                profile_confidence=None,
                identity_confidence=recognizer_state.confidence,
                updated_at=recognizer_state.timestamp,
                status="unknown",
            )

        status = "ready"
        if recognizer_state.degraded or recognizer_state.confidence < self.ready_identity_threshold:
            status = "degraded"

        return ActiveProfilePayload(
            canonical_weapon_id=recognizer_state.canonical_weapon_id,
            profile_id=selected_profile.profile_id,
            game=selected_profile.game,
            stance=selected_profile.stance,
            aim_mode=selected_profile.aim_mode,
            profile_confidence=selected_profile.confidence,
            identity_confidence=recognizer_state.confidence,
            updated_at=recognizer_state.timestamp,
            status=status,
        )

    def _load_profile_store(self) -> tuple[RecoilProfileRecord, ...]:
        if not self.profile_dir.exists() or not self.profile_dir.is_dir():
            return ()

        records: list[RecoilProfileRecord] = []
        for path in sorted(self.profile_dir.glob("*.json")):
            try:
                payload = _load_json_text(path.read_text(encoding="utf-8"))
                records.append(RecoilProfileRecord.from_dict(payload))
            except (OSError, UnicodeDecodeError, ValueError):
                continue
        return tuple(records)

    def _narrow_profiles_by_hint(
        self,
        profiles: tuple[RecoilProfileRecord, ...],
        profile_ids: tuple[str, ...],
    ) -> tuple[RecoilProfileRecord, ...]:
        if not profile_ids:
            return profiles
        hinted_profiles = tuple(profile for profile in profiles if profile.profile_id in profile_ids)
        return hinted_profiles or profiles


def _coerce_recognizer_state(value: RecognizerState) -> RecognizerState:
    if not isinstance(value, RecognizerState):
        raise ValueError("recognizer_state must be a RecognizerState")
    return value


def _coerce_runtime_context(
    value: SidecarRuntimeContext | Mapping[str, Any] | None,
) -> SidecarRuntimeContext:
    if value is None:
        return SidecarRuntimeContext()
    if isinstance(value, SidecarRuntimeContext):
        return value
    if isinstance(value, Mapping):
        return SidecarRuntimeContext.from_dict(dict(value))
    raise ValueError("context must be a SidecarRuntimeContext, mapping, or None")


def _load_json_text(value: Any) -> dict[str, Any]:
    if isinstance(value, bytes):
        text = value.decode("utf-8")
    elif isinstance(value, str):
        text = value
    else:
        raise ValueError("Recognizer state source must provide text or bytes")

    payload = json.loads(text)
    if not isinstance(payload, dict):
        raise ValueError("Recognizer state payload must be a JSON object")
    return payload


def _select_best_profile(matches: tuple[RecoilProfileRecord, ...]) -> RecoilProfileRecord | None:
    if not matches:
        return None
    return matches[0]


def _require_confidence(value: Any, label: str) -> float:
    if type(value) not in {int, float}:
        raise ValueError(f"{label} must be a number")
    confidence = float(value)
    if confidence < 0.0 or confidence > 1.0:
        raise ValueError(f"{label} must be between 0.0 and 1.0")
    return confidence


__all__ = ["RecoilSidecarService"]
