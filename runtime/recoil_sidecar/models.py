from __future__ import annotations

from dataclasses import dataclass
from typing import Any


_ACTIVE_PROFILE_STATUSES = frozenset({"ready", "degraded", "unknown"})
_AIM_MODES = frozenset({"ads", "hipfire"})


@dataclass(slots=True, frozen=True)
class RecognizerState:
    game: str
    canonical_weapon_id: str
    confidence: float
    source: str
    timestamp: str
    degraded: bool = False
    matched_name: str | None = None
    profile_ids: tuple[str, ...] = ()

    def __post_init__(self) -> None:
        object.__setattr__(self, "game", _require_non_empty_str(self.game, "RecognizerState.game"))
        object.__setattr__(
            self,
            "canonical_weapon_id",
            _require_non_empty_str(self.canonical_weapon_id, "RecognizerState.canonical_weapon_id"),
        )
        object.__setattr__(self, "confidence", _require_confidence(self.confidence, "RecognizerState.confidence"))
        object.__setattr__(self, "source", _require_non_empty_str(self.source, "RecognizerState.source"))
        object.__setattr__(self, "timestamp", _require_non_empty_str(self.timestamp, "RecognizerState.timestamp"))
        object.__setattr__(self, "degraded", _require_bool(self.degraded, "RecognizerState.degraded"))
        object.__setattr__(
            self,
            "matched_name",
            _require_optional_non_empty_str(self.matched_name, "RecognizerState.matched_name"),
        )
        object.__setattr__(
            self,
            "profile_ids",
            _require_string_tuple(self.profile_ids, "RecognizerState.profile_ids"),
        )

    def to_dict(self) -> dict[str, Any]:
        return {
            "type": "current_weapon",
            "game": self.game,
            "canonical_weapon_id": self.canonical_weapon_id,
            "confidence": self.confidence,
            "source": self.source,
            "timestamp": self.timestamp,
            "degraded": self.degraded,
            "matched_name": self.matched_name,
            "profile_ids": list(self.profile_ids),
        }

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "RecognizerState":
        _require_required_keys(
            data,
            "RecognizerState",
            {
                "game",
                "canonical_weapon_id",
                "confidence",
                "source",
                "timestamp",
                "degraded",
                "matched_name",
                "profile_ids",
            },
        )
        payload_type = data.get("type")
        if payload_type is not None and payload_type != "current_weapon":
            raise ValueError("RecognizerState.type must be 'current_weapon'")
        return cls(
            game=_require_non_empty_str(data["game"], "RecognizerState.game"),
            canonical_weapon_id=_require_non_empty_str(
                data["canonical_weapon_id"],
                "RecognizerState.canonical_weapon_id",
            ),
            confidence=_require_confidence(data["confidence"], "RecognizerState.confidence"),
            source=_require_non_empty_str(data["source"], "RecognizerState.source"),
            timestamp=_require_non_empty_str(data["timestamp"], "RecognizerState.timestamp"),
            degraded=_require_bool(data["degraded"], "RecognizerState.degraded"),
            matched_name=_require_optional_non_empty_str(
                data["matched_name"],
                "RecognizerState.matched_name",
            ),
            profile_ids=_require_string_tuple(data["profile_ids"], "RecognizerState.profile_ids"),
        )


@dataclass(slots=True, frozen=True)
class SidecarRuntimeContext:
    stance: str = "standing"
    aim_mode: str | None = None

    def __post_init__(self) -> None:
        object.__setattr__(self, "stance", _require_non_empty_str(self.stance, "SidecarRuntimeContext.stance"))
        object.__setattr__(
            self,
            "aim_mode",
            _require_optional_aim_mode(self.aim_mode, "SidecarRuntimeContext.aim_mode"),
        )

    def to_dict(self) -> dict[str, Any]:
        return {
            "stance": self.stance,
            "aim_mode": self.aim_mode,
        }

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "SidecarRuntimeContext":
        if not isinstance(data, dict):
            raise ValueError("SidecarRuntimeContext must be a dict")
        return cls(
            stance=data.get("stance", "standing"),
            aim_mode=data.get("aim_mode"),
        )


@dataclass(slots=True, frozen=True)
class ActiveProfilePayload:
    canonical_weapon_id: str | None
    profile_id: str | None
    game: str | None
    stance: str | None
    aim_mode: str | None
    profile_confidence: float | None
    identity_confidence: float | None
    updated_at: str | None
    status: str

    def __post_init__(self) -> None:
        object.__setattr__(
            self,
            "canonical_weapon_id",
            _require_optional_non_empty_str(
                self.canonical_weapon_id,
                "ActiveProfilePayload.canonical_weapon_id",
            ),
        )
        object.__setattr__(
            self,
            "profile_id",
            _require_optional_non_empty_str(self.profile_id, "ActiveProfilePayload.profile_id"),
        )
        object.__setattr__(self, "game", _require_optional_non_empty_str(self.game, "ActiveProfilePayload.game"))
        object.__setattr__(
            self,
            "stance",
            _require_optional_non_empty_str(self.stance, "ActiveProfilePayload.stance"),
        )
        object.__setattr__(
            self,
            "aim_mode",
            _require_optional_aim_mode(self.aim_mode, "ActiveProfilePayload.aim_mode"),
        )
        object.__setattr__(
            self,
            "profile_confidence",
            _require_optional_confidence(
                self.profile_confidence,
                "ActiveProfilePayload.profile_confidence",
            ),
        )
        object.__setattr__(
            self,
            "identity_confidence",
            _require_optional_confidence(
                self.identity_confidence,
                "ActiveProfilePayload.identity_confidence",
            ),
        )
        object.__setattr__(
            self,
            "updated_at",
            _require_optional_non_empty_str(self.updated_at, "ActiveProfilePayload.updated_at"),
        )
        object.__setattr__(self, "status", _require_status(self.status, "ActiveProfilePayload.status"))

    def to_dict(self) -> dict[str, Any]:
        return {
            "canonical_weapon_id": self.canonical_weapon_id,
            "profile_id": self.profile_id,
            "game": self.game,
            "stance": self.stance,
            "aim_mode": self.aim_mode,
            "profile_confidence": self.profile_confidence,
            "identity_confidence": self.identity_confidence,
            "updated_at": self.updated_at,
            "status": self.status,
        }

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "ActiveProfilePayload":
        _require_required_keys(
            data,
            "ActiveProfilePayload",
            {
                "canonical_weapon_id",
                "profile_id",
                "game",
                "stance",
                "aim_mode",
                "profile_confidence",
                "identity_confidence",
                "updated_at",
                "status",
            },
        )
        return cls(
            canonical_weapon_id=_require_optional_non_empty_str(
                data["canonical_weapon_id"],
                "ActiveProfilePayload.canonical_weapon_id",
            ),
            profile_id=_require_optional_non_empty_str(data["profile_id"], "ActiveProfilePayload.profile_id"),
            game=_require_optional_non_empty_str(data["game"], "ActiveProfilePayload.game"),
            stance=_require_optional_non_empty_str(data["stance"], "ActiveProfilePayload.stance"),
            aim_mode=_require_optional_aim_mode(data["aim_mode"], "ActiveProfilePayload.aim_mode"),
            profile_confidence=_require_optional_confidence(
                data["profile_confidence"],
                "ActiveProfilePayload.profile_confidence",
            ),
            identity_confidence=_require_optional_confidence(
                data["identity_confidence"],
                "ActiveProfilePayload.identity_confidence",
            ),
            updated_at=_require_optional_non_empty_str(data["updated_at"], "ActiveProfilePayload.updated_at"),
            status=_require_status(data["status"], "ActiveProfilePayload.status"),
        )


def _require_required_keys(data: Any, label: str, required_keys: set[str]) -> None:
    if not isinstance(data, dict):
        raise ValueError(f"{label} must be a dict")
    missing = required_keys - set(data)
    if missing:
        raise ValueError(f"{label} schema mismatch (missing={sorted(missing)})")


def _require_str(value: Any, label: str) -> str:
    if type(value) is not str:
        raise ValueError(f"{label} must be a string")
    return value


def _require_non_empty_str(value: Any, label: str) -> str:
    result = _require_str(value, label).strip()
    if not result:
        raise ValueError(f"{label} must be a non-empty string")
    return result


def _require_optional_non_empty_str(value: Any, label: str) -> str | None:
    if value is None:
        return None
    return _require_non_empty_str(value, label)


def _require_string_tuple(value: Any, label: str) -> tuple[str, ...]:
    if not isinstance(value, (list, tuple)):
        raise ValueError(f"{label} must be a list or tuple of strings")
    return tuple(_require_non_empty_str(item, f"{label}[{index}]") for index, item in enumerate(value))


def _require_confidence(value: Any, label: str) -> float:
    if type(value) not in {int, float}:
        raise ValueError(f"{label} must be a number")
    confidence = float(value)
    if confidence < 0.0 or confidence > 1.0:
        raise ValueError(f"{label} must be between 0.0 and 1.0")
    return confidence


def _require_optional_confidence(value: Any, label: str) -> float | None:
    if value is None:
        return None
    return _require_confidence(value, label)


def _require_bool(value: Any, label: str) -> bool:
    if type(value) is not bool:
        raise ValueError(f"{label} must be a boolean")
    return value


def _require_optional_aim_mode(value: Any, label: str) -> str | None:
    if value is None:
        return None
    aim_mode = _require_non_empty_str(value, label)
    if aim_mode not in _AIM_MODES:
        raise ValueError(f"{label} must be one of ['ads', 'hipfire']")
    return aim_mode


def _require_status(value: Any, label: str) -> str:
    status = _require_non_empty_str(value, label)
    if status not in _ACTIVE_PROFILE_STATUSES:
        raise ValueError(f"{label} must be one of ['degraded', 'ready', 'unknown']")
    return status


__all__ = [
    "ActiveProfilePayload",
    "RecognizerState",
    "SidecarRuntimeContext",
]
