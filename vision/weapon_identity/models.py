from __future__ import annotations

from dataclasses import dataclass
from types import MappingProxyType
from typing import Any, Mapping


@dataclass(slots=True, frozen=True)
class WeaponIdentityRecord:
    canonical_weapon_id: str
    game: str
    weapon_family: str
    display_name: str
    alias_names: tuple[str, ...] = ()
    blueprint_names: tuple[str, ...] = ()
    signature_refs: tuple[str, ...] = ()
    notes: str = ""
    created_at: str = ""
    updated_at: str = ""

    def __post_init__(self) -> None:
        object.__setattr__(
            self,
            "canonical_weapon_id",
            _require_non_empty_str(self.canonical_weapon_id, "WeaponIdentityRecord.canonical_weapon_id"),
        )
        object.__setattr__(self, "game", _require_non_empty_str(self.game, "WeaponIdentityRecord.game"))
        object.__setattr__(
            self,
            "weapon_family",
            _require_non_empty_str(self.weapon_family, "WeaponIdentityRecord.weapon_family"),
        )
        object.__setattr__(
            self,
            "display_name",
            _require_non_empty_str(self.display_name, "WeaponIdentityRecord.display_name"),
        )
        object.__setattr__(
            self,
            "alias_names",
            _require_string_tuple(self.alias_names, "WeaponIdentityRecord.alias_names"),
        )
        object.__setattr__(
            self,
            "blueprint_names",
            _require_string_tuple(self.blueprint_names, "WeaponIdentityRecord.blueprint_names"),
        )
        object.__setattr__(
            self,
            "signature_refs",
            _require_string_tuple(self.signature_refs, "WeaponIdentityRecord.signature_refs"),
        )
        object.__setattr__(self, "notes", _require_str(self.notes, "WeaponIdentityRecord.notes"))
        object.__setattr__(
            self,
            "created_at",
            _require_non_empty_str(self.created_at, "WeaponIdentityRecord.created_at"),
        )
        object.__setattr__(
            self,
            "updated_at",
            _require_non_empty_str(self.updated_at, "WeaponIdentityRecord.updated_at"),
        )

    def resolve_name(self, candidate_name: str) -> str | None:
        normalized_candidate = _normalize_name(candidate_name)
        if normalized_candidate is None:
            return None
        normalized_names = {
            _normalize_name(self.canonical_weapon_id),
            _normalize_name(self.display_name),
            *(_normalize_name(name) for name in self.alias_names),
            *(_normalize_name(name) for name in self.blueprint_names),
        }
        if normalized_candidate in normalized_names:
            return self.canonical_weapon_id
        return None

    def to_dict(self) -> dict[str, Any]:
        return {
            "canonical_weapon_id": self.canonical_weapon_id,
            "game": self.game,
            "weapon_family": self.weapon_family,
            "display_name": self.display_name,
            "alias_names": list(self.alias_names),
            "blueprint_names": list(self.blueprint_names),
            "signature_refs": list(self.signature_refs),
            "notes": self.notes,
            "created_at": self.created_at,
            "updated_at": self.updated_at,
        }

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "WeaponIdentityRecord":
        _require_exact_keys(
            data,
            "WeaponIdentityRecord",
            {
                "canonical_weapon_id",
                "game",
                "weapon_family",
                "display_name",
                "alias_names",
                "blueprint_names",
                "signature_refs",
                "notes",
                "created_at",
                "updated_at",
            },
        )
        return cls(
            canonical_weapon_id=_require_non_empty_str(
                data["canonical_weapon_id"],
                "WeaponIdentityRecord.canonical_weapon_id",
            ),
            game=_require_non_empty_str(data["game"], "WeaponIdentityRecord.game"),
            weapon_family=_require_non_empty_str(
                data["weapon_family"],
                "WeaponIdentityRecord.weapon_family",
            ),
            display_name=_require_non_empty_str(
                data["display_name"],
                "WeaponIdentityRecord.display_name",
            ),
            alias_names=_require_string_tuple(data["alias_names"], "WeaponIdentityRecord.alias_names"),
            blueprint_names=_require_string_tuple(
                data["blueprint_names"],
                "WeaponIdentityRecord.blueprint_names",
            ),
            signature_refs=_require_string_tuple(
                data["signature_refs"],
                "WeaponIdentityRecord.signature_refs",
            ),
            notes=_require_str(data["notes"], "WeaponIdentityRecord.notes"),
            created_at=_require_non_empty_str(
                data["created_at"],
                "WeaponIdentityRecord.created_at",
            ),
            updated_at=_require_non_empty_str(
                data["updated_at"],
                "WeaponIdentityRecord.updated_at",
            ),
        )


@dataclass(slots=True, frozen=True)
class VisualSignatureRecord:
    signature_id: str
    canonical_weapon_id: str
    game: str
    region_type: str
    resolution_bucket: str
    ui_scale_bucket: str
    feature_type: str
    feature_payload: Mapping[str, Any]
    captured_from: str
    confidence: float

    def __post_init__(self) -> None:
        object.__setattr__(
            self,
            "signature_id",
            _require_non_empty_str(self.signature_id, "VisualSignatureRecord.signature_id"),
        )
        object.__setattr__(
            self,
            "canonical_weapon_id",
            _require_non_empty_str(
                self.canonical_weapon_id,
                "VisualSignatureRecord.canonical_weapon_id",
            ),
        )
        object.__setattr__(self, "game", _require_non_empty_str(self.game, "VisualSignatureRecord.game"))
        object.__setattr__(
            self,
            "region_type",
            _require_non_empty_str(self.region_type, "VisualSignatureRecord.region_type"),
        )
        object.__setattr__(
            self,
            "resolution_bucket",
            _require_non_empty_str(
                self.resolution_bucket,
                "VisualSignatureRecord.resolution_bucket",
            ),
        )
        object.__setattr__(
            self,
            "ui_scale_bucket",
            _require_non_empty_str(
                self.ui_scale_bucket,
                "VisualSignatureRecord.ui_scale_bucket",
            ),
        )
        object.__setattr__(
            self,
            "feature_type",
            _require_non_empty_str(self.feature_type, "VisualSignatureRecord.feature_type"),
        )
        object.__setattr__(
            self,
            "feature_payload",
            _require_mapping(self.feature_payload, "VisualSignatureRecord.feature_payload"),
        )
        object.__setattr__(
            self,
            "captured_from",
            _require_non_empty_str(
                self.captured_from,
                "VisualSignatureRecord.captured_from",
            ),
        )
        object.__setattr__(
            self,
            "confidence",
            _require_confidence(self.confidence, "VisualSignatureRecord.confidence"),
        )

    def to_dict(self) -> dict[str, Any]:
        return {
            "signature_id": self.signature_id,
            "canonical_weapon_id": self.canonical_weapon_id,
            "game": self.game,
            "region_type": self.region_type,
            "resolution_bucket": self.resolution_bucket,
            "ui_scale_bucket": self.ui_scale_bucket,
            "feature_type": self.feature_type,
            "feature_payload": dict(self.feature_payload),
            "captured_from": self.captured_from,
            "confidence": self.confidence,
        }

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "VisualSignatureRecord":
        _require_exact_keys(
            data,
            "VisualSignatureRecord",
            {
                "signature_id",
                "canonical_weapon_id",
                "game",
                "region_type",
                "resolution_bucket",
                "ui_scale_bucket",
                "feature_type",
                "feature_payload",
                "captured_from",
                "confidence",
            },
        )
        return cls(
            signature_id=_require_non_empty_str(
                data["signature_id"],
                "VisualSignatureRecord.signature_id",
            ),
            canonical_weapon_id=_require_non_empty_str(
                data["canonical_weapon_id"],
                "VisualSignatureRecord.canonical_weapon_id",
            ),
            game=_require_non_empty_str(data["game"], "VisualSignatureRecord.game"),
            region_type=_require_non_empty_str(
                data["region_type"],
                "VisualSignatureRecord.region_type",
            ),
            resolution_bucket=_require_non_empty_str(
                data["resolution_bucket"],
                "VisualSignatureRecord.resolution_bucket",
            ),
            ui_scale_bucket=_require_non_empty_str(
                data["ui_scale_bucket"],
                "VisualSignatureRecord.ui_scale_bucket",
            ),
            feature_type=_require_non_empty_str(
                data["feature_type"],
                "VisualSignatureRecord.feature_type",
            ),
            feature_payload=_require_mapping(
                data["feature_payload"],
                "VisualSignatureRecord.feature_payload",
            ),
            captured_from=_require_non_empty_str(
                data["captured_from"],
                "VisualSignatureRecord.captured_from",
            ),
            confidence=_require_confidence(
                data["confidence"],
                "VisualSignatureRecord.confidence",
            ),
        )


@dataclass(slots=True, frozen=True)
class RecognitionEvent:
    game: str
    canonical_weapon_id: str
    confidence: float
    source: str
    timestamp: str
    degraded: bool = False
    matched_name: str | None = None

    def __post_init__(self) -> None:
        object.__setattr__(self, "game", _require_non_empty_str(self.game, "RecognitionEvent.game"))
        object.__setattr__(
            self,
            "canonical_weapon_id",
            _require_non_empty_str(
                self.canonical_weapon_id,
                "RecognitionEvent.canonical_weapon_id",
            ),
        )
        object.__setattr__(
            self,
            "confidence",
            _require_confidence(self.confidence, "RecognitionEvent.confidence"),
        )
        object.__setattr__(
            self,
            "source",
            _require_non_empty_str(self.source, "RecognitionEvent.source"),
        )
        object.__setattr__(
            self,
            "timestamp",
            _require_non_empty_str(self.timestamp, "RecognitionEvent.timestamp"),
        )
        object.__setattr__(self, "degraded", _require_bool(self.degraded, "RecognitionEvent.degraded"))
        object.__setattr__(
            self,
            "matched_name",
            _require_optional_non_empty_str(
                self.matched_name,
                "RecognitionEvent.matched_name",
            ),
        )

    def to_dict(self) -> dict[str, Any]:
        return {
            "game": self.game,
            "canonical_weapon_id": self.canonical_weapon_id,
            "confidence": self.confidence,
            "source": self.source,
            "timestamp": self.timestamp,
            "degraded": self.degraded,
            "matched_name": self.matched_name,
        }

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "RecognitionEvent":
        _require_exact_keys(
            data,
            "RecognitionEvent",
            {
                "game",
                "canonical_weapon_id",
                "confidence",
                "source",
                "timestamp",
                "degraded",
                "matched_name",
            },
        )
        return cls(
            game=_require_non_empty_str(data["game"], "RecognitionEvent.game"),
            canonical_weapon_id=_require_non_empty_str(
                data["canonical_weapon_id"],
                "RecognitionEvent.canonical_weapon_id",
            ),
            confidence=_require_confidence(data["confidence"], "RecognitionEvent.confidence"),
            source=_require_non_empty_str(data["source"], "RecognitionEvent.source"),
            timestamp=_require_non_empty_str(data["timestamp"], "RecognitionEvent.timestamp"),
            degraded=_require_bool(data["degraded"], "RecognitionEvent.degraded"),
            matched_name=_require_optional_non_empty_str(
                data["matched_name"],
                "RecognitionEvent.matched_name",
            ),
        )


def _normalize_name(value: Any) -> str | None:
    if type(value) is not str:
        return None
    normalized = value.strip().casefold()
    return normalized or None


def _require_exact_keys(data: Any, label: str, expected_keys: set[str]) -> None:
    if not isinstance(data, dict):
        raise ValueError(f"{label} must be a dict")
    actual_keys = set(data)
    missing = expected_keys - actual_keys
    extra = actual_keys - expected_keys
    if missing or extra:
        details = []
        if missing:
            details.append(f"missing={sorted(missing)}")
        if extra:
            details.append(f"extra={sorted(extra)}")
        raise ValueError(f"{label} schema mismatch ({', '.join(details)})")


def _require_str(value: Any, label: str) -> str:
    if type(value) is not str:
        raise ValueError(f"{label} must be a string")
    return value


def _require_non_empty_str(value: Any, label: str) -> str:
    text = _require_str(value, label).strip()
    if not text:
        raise ValueError(f"{label} must be a non-empty string")
    return text


def _require_optional_non_empty_str(value: Any, label: str) -> str | None:
    if value is None:
        return None
    return _require_non_empty_str(value, label)


def _require_string_tuple(value: Any, label: str) -> tuple[str, ...]:
    if not isinstance(value, (list, tuple)):
        raise ValueError(f"{label} must be a list or tuple of strings")
    result = []
    for index, item in enumerate(value):
        result.append(_require_non_empty_str(item, f"{label}[{index}]"))
    return tuple(result)


def _require_mapping(value: Any, label: str) -> Mapping[str, Any]:
    if not isinstance(value, Mapping):
        raise ValueError(f"{label} must be a mapping")
    result: dict[str, Any] = {}
    for key, item in value.items():
        if type(key) is not str or not key.strip():
            raise ValueError(f"{label} keys must be non-empty strings")
        result[key] = item
    return MappingProxyType(result)


def _require_confidence(value: Any, label: str) -> float:
    if type(value) not in {int, float}:
        raise ValueError(f"{label} must be a number")
    confidence = float(value)
    if confidence < 0.0 or confidence > 1.0:
        raise ValueError(f"{label} must be between 0.0 and 1.0")
    return confidence


def _require_bool(value: Any, label: str) -> bool:
    if type(value) is not bool:
        raise ValueError(f"{label} must be a boolean")
    return value
