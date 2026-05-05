from __future__ import annotations

from dataclasses import dataclass
from types import MappingProxyType
from typing import Any, Mapping


@dataclass(slots=True, frozen=True)
class RecoilSample:
    offset_ms: int
    x: float
    y: float

    def __post_init__(self) -> None:
        object.__setattr__(self, "offset_ms", _require_non_negative_int(self.offset_ms, "RecoilSample.offset_ms"))
        object.__setattr__(self, "x", _require_number(self.x, "RecoilSample.x"))
        object.__setattr__(self, "y", _require_number(self.y, "RecoilSample.y"))

    def to_dict(self) -> dict[str, Any]:
        return {
            "offset_ms": self.offset_ms,
            "x": self.x,
            "y": self.y,
        }

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "RecoilSample":
        _require_exact_keys(data, "RecoilSample", {"offset_ms", "x", "y"})
        return cls(
            offset_ms=_require_non_negative_int(data["offset_ms"], "RecoilSample.offset_ms"),
            x=_require_number(data["x"], "RecoilSample.x"),
            y=_require_number(data["y"], "RecoilSample.y"),
        )


@dataclass(slots=True, frozen=True)
class RecoilCollectionSession:
    session_id: str
    canonical_weapon_id: str
    game: str
    stance: str
    aim_mode: str
    capture_resolution: str
    capture_fps: float
    collector_version: str
    started_at: str

    def __post_init__(self) -> None:
        object.__setattr__(
            self,
            "session_id",
            _require_non_empty_str(self.session_id, "RecoilCollectionSession.session_id"),
        )
        object.__setattr__(
            self,
            "canonical_weapon_id",
            _require_non_empty_str(
                self.canonical_weapon_id,
                "RecoilCollectionSession.canonical_weapon_id",
            ),
        )
        object.__setattr__(self, "game", _require_non_empty_str(self.game, "RecoilCollectionSession.game"))
        object.__setattr__(
            self,
            "stance",
            _require_v1_stance(self.stance, "RecoilCollectionSession.stance"),
        )
        object.__setattr__(
            self,
            "aim_mode",
            _require_v1_aim_mode(self.aim_mode, "RecoilCollectionSession.aim_mode"),
        )
        object.__setattr__(
            self,
            "capture_resolution",
            _require_non_empty_str(
                self.capture_resolution,
                "RecoilCollectionSession.capture_resolution",
            ),
        )
        object.__setattr__(
            self,
            "capture_fps",
            _require_positive_number(self.capture_fps, "RecoilCollectionSession.capture_fps"),
        )
        object.__setattr__(
            self,
            "collector_version",
            _require_non_empty_str(
                self.collector_version,
                "RecoilCollectionSession.collector_version",
            ),
        )
        object.__setattr__(
            self,
            "started_at",
            _require_non_empty_str(self.started_at, "RecoilCollectionSession.started_at"),
        )

    def to_dict(self) -> dict[str, Any]:
        return {
            "session_id": self.session_id,
            "canonical_weapon_id": self.canonical_weapon_id,
            "game": self.game,
            "stance": self.stance,
            "aim_mode": self.aim_mode,
            "capture_resolution": self.capture_resolution,
            "capture_fps": self.capture_fps,
            "collector_version": self.collector_version,
            "started_at": self.started_at,
        }

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "RecoilCollectionSession":
        _require_exact_keys(
            data,
            "RecoilCollectionSession",
            {
                "session_id",
                "canonical_weapon_id",
                "game",
                "stance",
                "aim_mode",
                "capture_resolution",
                "capture_fps",
                "collector_version",
                "started_at",
            },
        )
        return cls(
            session_id=_require_non_empty_str(data["session_id"], "RecoilCollectionSession.session_id"),
            canonical_weapon_id=_require_non_empty_str(
                data["canonical_weapon_id"],
                "RecoilCollectionSession.canonical_weapon_id",
            ),
            game=_require_non_empty_str(data["game"], "RecoilCollectionSession.game"),
            stance=_require_non_empty_str(data["stance"], "RecoilCollectionSession.stance"),
            aim_mode=_require_non_empty_str(data["aim_mode"], "RecoilCollectionSession.aim_mode"),
            capture_resolution=_require_non_empty_str(
                data["capture_resolution"],
                "RecoilCollectionSession.capture_resolution",
            ),
            capture_fps=_require_positive_number(
                data["capture_fps"],
                "RecoilCollectionSession.capture_fps",
            ),
            collector_version=_require_non_empty_str(
                data["collector_version"],
                "RecoilCollectionSession.collector_version",
            ),
            started_at=_require_non_empty_str(
                data["started_at"],
                "RecoilCollectionSession.started_at",
            ),
        )


@dataclass(slots=True, frozen=True)
class RecoilBurstWindow:
    burst_id: str
    session_id: str
    start_offset_ms: int
    end_offset_ms: int
    start_reason: str
    end_reason: str

    def __post_init__(self) -> None:
        object.__setattr__(
            self,
            "burst_id",
            _require_non_empty_str(self.burst_id, "RecoilBurstWindow.burst_id"),
        )
        object.__setattr__(
            self,
            "session_id",
            _require_non_empty_str(self.session_id, "RecoilBurstWindow.session_id"),
        )
        object.__setattr__(
            self,
            "start_offset_ms",
            _require_non_negative_int(self.start_offset_ms, "RecoilBurstWindow.start_offset_ms"),
        )
        object.__setattr__(
            self,
            "end_offset_ms",
            _require_non_negative_int(self.end_offset_ms, "RecoilBurstWindow.end_offset_ms"),
        )
        if self.end_offset_ms < self.start_offset_ms:
            raise ValueError("RecoilBurstWindow.end_offset_ms must be greater than or equal to start_offset_ms")
        object.__setattr__(
            self,
            "start_reason",
            _require_burst_start_reason(self.start_reason, "RecoilBurstWindow.start_reason"),
        )
        object.__setattr__(
            self,
            "end_reason",
            _require_burst_end_reason(self.end_reason, "RecoilBurstWindow.end_reason"),
        )

    @property
    def duration_ms(self) -> int:
        return self.end_offset_ms - self.start_offset_ms

    def to_dict(self) -> dict[str, Any]:
        return {
            "burst_id": self.burst_id,
            "session_id": self.session_id,
            "start_offset_ms": self.start_offset_ms,
            "end_offset_ms": self.end_offset_ms,
            "start_reason": self.start_reason,
            "end_reason": self.end_reason,
        }

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "RecoilBurstWindow":
        _require_exact_keys(
            data,
            "RecoilBurstWindow",
            {
                "burst_id",
                "session_id",
                "start_offset_ms",
                "end_offset_ms",
                "start_reason",
                "end_reason",
            },
        )
        return cls(
            burst_id=_require_non_empty_str(data["burst_id"], "RecoilBurstWindow.burst_id"),
            session_id=_require_non_empty_str(data["session_id"], "RecoilBurstWindow.session_id"),
            start_offset_ms=_require_non_negative_int(
                data["start_offset_ms"],
                "RecoilBurstWindow.start_offset_ms",
            ),
            end_offset_ms=_require_non_negative_int(
                data["end_offset_ms"],
                "RecoilBurstWindow.end_offset_ms",
            ),
            start_reason=_require_non_empty_str(
                data["start_reason"],
                "RecoilBurstWindow.start_reason",
            ),
            end_reason=_require_non_empty_str(data["end_reason"], "RecoilBurstWindow.end_reason"),
        )


@dataclass(slots=True, frozen=True)
class RecoilBurstSampleSeries:
    burst_id: str
    session_id: str
    sample_interval_ms: int
    samples: tuple[RecoilSample, ...]
    sample_count: int

    def __post_init__(self) -> None:
        object.__setattr__(
            self,
            "burst_id",
            _require_non_empty_str(self.burst_id, "RecoilBurstSampleSeries.burst_id"),
        )
        object.__setattr__(
            self,
            "session_id",
            _require_non_empty_str(self.session_id, "RecoilBurstSampleSeries.session_id"),
        )
        object.__setattr__(
            self,
            "sample_interval_ms",
            _require_positive_int(
                self.sample_interval_ms,
                "RecoilBurstSampleSeries.sample_interval_ms",
            ),
        )
        object.__setattr__(
            self,
            "samples",
            _require_recoil_sample_tuple(self.samples, "RecoilBurstSampleSeries.samples"),
        )
        object.__setattr__(
            self,
            "sample_count",
            _require_positive_int(self.sample_count, "RecoilBurstSampleSeries.sample_count"),
        )
        if self.sample_count != len(self.samples):
            raise ValueError("RecoilBurstSampleSeries.sample_count must match the sample array length")
        previous_offset_ms = None
        for sample in self.samples:
            if previous_offset_ms is not None and sample.offset_ms <= previous_offset_ms:
                raise ValueError("RecoilBurstSampleSeries.samples must be strictly increasing by offset_ms")
            previous_offset_ms = sample.offset_ms

    def to_dict(self) -> dict[str, Any]:
        return {
            "burst_id": self.burst_id,
            "session_id": self.session_id,
            "sample_interval_ms": self.sample_interval_ms,
            "samples": [sample.to_dict() for sample in self.samples],
            "sample_count": self.sample_count,
        }

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "RecoilBurstSampleSeries":
        _require_exact_keys(
            data,
            "RecoilBurstSampleSeries",
            {
                "burst_id",
                "session_id",
                "sample_interval_ms",
                "samples",
                "sample_count",
            },
        )
        return cls(
            burst_id=_require_non_empty_str(data["burst_id"], "RecoilBurstSampleSeries.burst_id"),
            session_id=_require_non_empty_str(data["session_id"], "RecoilBurstSampleSeries.session_id"),
            sample_interval_ms=_require_positive_int(
                data["sample_interval_ms"],
                "RecoilBurstSampleSeries.sample_interval_ms",
            ),
            samples=_require_recoil_sample_tuple(
                data["samples"],
                "RecoilBurstSampleSeries.samples",
            ),
            sample_count=_require_positive_int(
                data["sample_count"],
                "RecoilBurstSampleSeries.sample_count",
            ),
        )


@dataclass(slots=True, frozen=True)
class RecoilProfileRecord:
    profile_id: str
    canonical_weapon_id: str
    game: str
    stance: str
    aim_mode: str
    sample_interval_ms: int
    duration_ms: int
    initial_delay_ms: int
    samples_x: tuple[float, ...]
    samples_y: tuple[float, ...]
    sample_count: int
    burst_count: int
    variance_summary: Mapping[str, float]
    confidence: float
    capture_resolution: str
    capture_fps: float
    collector_version: str
    created_at: str

    def __post_init__(self) -> None:
        object.__setattr__(
            self,
            "profile_id",
            _require_non_empty_str(self.profile_id, "RecoilProfileRecord.profile_id"),
        )
        object.__setattr__(
            self,
            "canonical_weapon_id",
            _require_non_empty_str(
                self.canonical_weapon_id,
                "RecoilProfileRecord.canonical_weapon_id",
            ),
        )
        object.__setattr__(self, "game", _require_non_empty_str(self.game, "RecoilProfileRecord.game"))
        object.__setattr__(self, "stance", _require_v1_stance(self.stance, "RecoilProfileRecord.stance"))
        object.__setattr__(
            self,
            "aim_mode",
            _require_v1_aim_mode(self.aim_mode, "RecoilProfileRecord.aim_mode"),
        )
        object.__setattr__(
            self,
            "sample_interval_ms",
            _require_positive_int(
                self.sample_interval_ms,
                "RecoilProfileRecord.sample_interval_ms",
            ),
        )
        object.__setattr__(
            self,
            "duration_ms",
            _require_non_negative_int(self.duration_ms, "RecoilProfileRecord.duration_ms"),
        )
        object.__setattr__(
            self,
            "initial_delay_ms",
            _require_non_negative_int(
                self.initial_delay_ms,
                "RecoilProfileRecord.initial_delay_ms",
            ),
        )
        object.__setattr__(
            self,
            "samples_x",
            _require_number_tuple(self.samples_x, "RecoilProfileRecord.samples_x"),
        )
        object.__setattr__(
            self,
            "samples_y",
            _require_number_tuple(self.samples_y, "RecoilProfileRecord.samples_y"),
        )
        if len(self.samples_x) != len(self.samples_y):
            raise ValueError("RecoilProfileRecord.samples_y length must match samples_x length")
        object.__setattr__(
            self,
            "sample_count",
            _require_positive_int(self.sample_count, "RecoilProfileRecord.sample_count"),
        )
        if self.sample_count != len(self.samples_x):
            raise ValueError("RecoilProfileRecord.sample_count must match the sample array length")
        sample_window_ms = self.sample_count * self.sample_interval_ms
        if self.duration_ms < sample_window_ms:
            raise ValueError("RecoilProfileRecord.duration_ms must cover the sampled window length")
        object.__setattr__(
            self,
            "burst_count",
            _require_positive_int(self.burst_count, "RecoilProfileRecord.burst_count"),
        )
        object.__setattr__(
            self,
            "variance_summary",
            _require_float_mapping(
                self.variance_summary,
                "RecoilProfileRecord.variance_summary",
            ),
        )
        object.__setattr__(
            self,
            "confidence",
            _require_confidence(self.confidence, "RecoilProfileRecord.confidence"),
        )
        object.__setattr__(
            self,
            "capture_resolution",
            _require_non_empty_str(
                self.capture_resolution,
                "RecoilProfileRecord.capture_resolution",
            ),
        )
        object.__setattr__(
            self,
            "capture_fps",
            _require_positive_number(self.capture_fps, "RecoilProfileRecord.capture_fps"),
        )
        object.__setattr__(
            self,
            "collector_version",
            _require_non_empty_str(
                self.collector_version,
                "RecoilProfileRecord.collector_version",
            ),
        )
        object.__setattr__(
            self,
            "created_at",
            _require_non_empty_str(self.created_at, "RecoilProfileRecord.created_at"),
        )

    @property
    def samples(self) -> tuple[RecoilSample, ...]:
        return tuple(
            RecoilSample(
                offset_ms=self.initial_delay_ms + (index * self.sample_interval_ms),
                x=sample_x,
                y=sample_y,
            )
            for index, (sample_x, sample_y) in enumerate(zip(self.samples_x, self.samples_y))
        )

    def to_dict(self) -> dict[str, Any]:
        return {
            "profile_id": self.profile_id,
            "canonical_weapon_id": self.canonical_weapon_id,
            "game": self.game,
            "stance": self.stance,
            "aim_mode": self.aim_mode,
            "sample_interval_ms": self.sample_interval_ms,
            "duration_ms": self.duration_ms,
            "initial_delay_ms": self.initial_delay_ms,
            "samples_x": list(self.samples_x),
            "samples_y": list(self.samples_y),
            "sample_count": self.sample_count,
            "burst_count": self.burst_count,
            "variance_summary": dict(self.variance_summary),
            "confidence": self.confidence,
            "capture_resolution": self.capture_resolution,
            "capture_fps": self.capture_fps,
            "collector_version": self.collector_version,
            "created_at": self.created_at,
        }

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "RecoilProfileRecord":
        _require_exact_keys(
            data,
            "RecoilProfileRecord",
            {
                "profile_id",
                "canonical_weapon_id",
                "game",
                "stance",
                "aim_mode",
                "sample_interval_ms",
                "duration_ms",
                "initial_delay_ms",
                "samples_x",
                "samples_y",
                "sample_count",
                "burst_count",
                "variance_summary",
                "confidence",
                "capture_resolution",
                "capture_fps",
                "collector_version",
                "created_at",
            },
        )
        return cls(
            profile_id=_require_non_empty_str(data["profile_id"], "RecoilProfileRecord.profile_id"),
            canonical_weapon_id=_require_non_empty_str(
                data["canonical_weapon_id"],
                "RecoilProfileRecord.canonical_weapon_id",
            ),
            game=_require_non_empty_str(data["game"], "RecoilProfileRecord.game"),
            stance=_require_non_empty_str(data["stance"], "RecoilProfileRecord.stance"),
            aim_mode=_require_non_empty_str(data["aim_mode"], "RecoilProfileRecord.aim_mode"),
            sample_interval_ms=_require_positive_int(
                data["sample_interval_ms"],
                "RecoilProfileRecord.sample_interval_ms",
            ),
            duration_ms=_require_non_negative_int(
                data["duration_ms"],
                "RecoilProfileRecord.duration_ms",
            ),
            initial_delay_ms=_require_non_negative_int(
                data["initial_delay_ms"],
                "RecoilProfileRecord.initial_delay_ms",
            ),
            samples_x=_require_number_tuple(data["samples_x"], "RecoilProfileRecord.samples_x"),
            samples_y=_require_number_tuple(data["samples_y"], "RecoilProfileRecord.samples_y"),
            sample_count=_require_positive_int(
                data["sample_count"],
                "RecoilProfileRecord.sample_count",
            ),
            burst_count=_require_positive_int(data["burst_count"], "RecoilProfileRecord.burst_count"),
            variance_summary=_require_float_mapping(
                data["variance_summary"],
                "RecoilProfileRecord.variance_summary",
            ),
            confidence=_require_confidence(data["confidence"], "RecoilProfileRecord.confidence"),
            capture_resolution=_require_non_empty_str(
                data["capture_resolution"],
                "RecoilProfileRecord.capture_resolution",
            ),
            capture_fps=_require_positive_number(
                data["capture_fps"],
                "RecoilProfileRecord.capture_fps",
            ),
            collector_version=_require_non_empty_str(
                data["collector_version"],
                "RecoilProfileRecord.collector_version",
            ),
            created_at=_require_non_empty_str(
                data["created_at"],
                "RecoilProfileRecord.created_at",
            ),
        )


@dataclass(slots=True, frozen=True)
class RecoilProfileSummary:
    profile_id: str
    canonical_weapon_id: str
    game: str
    stance: str
    aim_mode: str
    sample_count: int
    burst_count: int
    confidence: float
    peak_abs_x: float
    peak_abs_y: float
    created_at: str

    def __post_init__(self) -> None:
        object.__setattr__(
            self,
            "profile_id",
            _require_non_empty_str(self.profile_id, "RecoilProfileSummary.profile_id"),
        )
        object.__setattr__(
            self,
            "canonical_weapon_id",
            _require_non_empty_str(
                self.canonical_weapon_id,
                "RecoilProfileSummary.canonical_weapon_id",
            ),
        )
        object.__setattr__(self, "game", _require_non_empty_str(self.game, "RecoilProfileSummary.game"))
        object.__setattr__(
            self,
            "stance",
            _require_v1_stance(self.stance, "RecoilProfileSummary.stance"),
        )
        object.__setattr__(
            self,
            "aim_mode",
            _require_v1_aim_mode(self.aim_mode, "RecoilProfileSummary.aim_mode"),
        )
        object.__setattr__(
            self,
            "sample_count",
            _require_non_negative_int(self.sample_count, "RecoilProfileSummary.sample_count"),
        )
        object.__setattr__(
            self,
            "burst_count",
            _require_positive_int(self.burst_count, "RecoilProfileSummary.burst_count"),
        )
        object.__setattr__(
            self,
            "confidence",
            _require_confidence(self.confidence, "RecoilProfileSummary.confidence"),
        )
        object.__setattr__(
            self,
            "peak_abs_x",
            _require_non_negative_number(self.peak_abs_x, "RecoilProfileSummary.peak_abs_x"),
        )
        object.__setattr__(
            self,
            "peak_abs_y",
            _require_non_negative_number(self.peak_abs_y, "RecoilProfileSummary.peak_abs_y"),
        )
        object.__setattr__(
            self,
            "created_at",
            _require_non_empty_str(self.created_at, "RecoilProfileSummary.created_at"),
        )

    def to_dict(self) -> dict[str, Any]:
        return {
            "profile_id": self.profile_id,
            "canonical_weapon_id": self.canonical_weapon_id,
            "game": self.game,
            "stance": self.stance,
            "aim_mode": self.aim_mode,
            "sample_count": self.sample_count,
            "burst_count": self.burst_count,
            "confidence": self.confidence,
            "peak_abs_x": self.peak_abs_x,
            "peak_abs_y": self.peak_abs_y,
            "created_at": self.created_at,
        }

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "RecoilProfileSummary":
        _require_required_keys(
            data,
            "RecoilProfileSummary",
            {
                "profile_id",
                "canonical_weapon_id",
                "game",
                "stance",
                "aim_mode",
                "sample_count",
                "burst_count",
                "confidence",
                "peak_abs_x",
                "peak_abs_y",
                "created_at",
            },
        )
        return cls(
            profile_id=_require_non_empty_str(data["profile_id"], "RecoilProfileSummary.profile_id"),
            canonical_weapon_id=_require_non_empty_str(
                data["canonical_weapon_id"],
                "RecoilProfileSummary.canonical_weapon_id",
            ),
            game=_require_non_empty_str(data["game"], "RecoilProfileSummary.game"),
            stance=_require_non_empty_str(data["stance"], "RecoilProfileSummary.stance"),
            aim_mode=_require_non_empty_str(data["aim_mode"], "RecoilProfileSummary.aim_mode"),
            sample_count=_require_non_negative_int(
                data["sample_count"],
                "RecoilProfileSummary.sample_count",
            ),
            burst_count=_require_positive_int(
                data["burst_count"],
                "RecoilProfileSummary.burst_count",
            ),
            confidence=_require_confidence(data["confidence"], "RecoilProfileSummary.confidence"),
            peak_abs_x=_require_non_negative_number(
                data["peak_abs_x"],
                "RecoilProfileSummary.peak_abs_x",
            ),
            peak_abs_y=_require_non_negative_number(
                data["peak_abs_y"],
                "RecoilProfileSummary.peak_abs_y",
            ),
            created_at=_require_non_empty_str(
                data["created_at"],
                "RecoilProfileSummary.created_at",
            ),
        )


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
    text = _require_str(value, label).strip()
    if not text:
        raise ValueError(f"{label} must be a non-empty string")
    return text


def _require_v1_stance(value: Any, label: str) -> str:
    stance = _require_non_empty_str(value, label)
    if stance != "standing":
        raise ValueError(f"{label} must be 'standing' in V1")
    return stance


def _require_v1_aim_mode(value: Any, label: str) -> str:
    aim_mode = _require_non_empty_str(value, label)
    if aim_mode not in {"hipfire", "ads"}:
        raise ValueError(f"{label} must be one of ['ads', 'hipfire'] in V1")
    return aim_mode


def _require_non_negative_int(value: Any, label: str) -> int:
    if type(value) is not int:
        raise ValueError(f"{label} must be an integer")
    if value < 0:
        raise ValueError(f"{label} must be non-negative")
    return value


def _require_positive_int(value: Any, label: str) -> int:
    integer = _require_non_negative_int(value, label)
    if integer <= 0:
        raise ValueError(f"{label} must be positive")
    return integer


def _require_number(value: Any, label: str) -> float:
    if type(value) not in {int, float}:
        raise ValueError(f"{label} must be a number")
    return float(value)


def _require_positive_number(value: Any, label: str) -> float:
    number = _require_number(value, label)
    if number <= 0.0:
        raise ValueError(f"{label} must be positive")
    return number


def _require_non_negative_number(value: Any, label: str) -> float:
    number = _require_number(value, label)
    if number < 0.0:
        raise ValueError(f"{label} must be non-negative")
    return number


def _require_number_tuple(value: Any, label: str) -> tuple[float, ...]:
    if not isinstance(value, (list, tuple)):
        raise ValueError(f"{label} must be a list or tuple of numbers")
    return tuple(_require_number(item, f"{label}[{index}]") for index, item in enumerate(value))


def _require_recoil_sample_tuple(value: Any, label: str) -> tuple[RecoilSample, ...]:
    if not isinstance(value, (list, tuple)):
        raise ValueError(f"{label} must be a list or tuple of recoil samples")
    result: list[RecoilSample] = []
    for index, item in enumerate(value):
        if isinstance(item, RecoilSample):
            result.append(item)
            continue
        if isinstance(item, dict):
            result.append(RecoilSample.from_dict(item))
            continue
        raise ValueError(f"{label}[{index}] must be a RecoilSample or dict")
    return tuple(result)


def _require_float_mapping(value: Any, label: str) -> Mapping[str, float]:
    if not isinstance(value, Mapping):
        raise ValueError(f"{label} must be a mapping")
    result: dict[str, float] = {}
    for key, item in value.items():
        if type(key) is not str or not key.strip():
            raise ValueError(f"{label} keys must be non-empty strings")
        result[key] = _require_number(item, f"{label}[{key!r}]")
    return MappingProxyType(result)


def _require_confidence(value: Any, label: str) -> float:
    confidence = _require_number(value, label)
    if confidence < 0.0 or confidence > 1.0:
        raise ValueError(f"{label} must be between 0.0 and 1.0")
    return confidence


def _require_burst_start_reason(value: Any, label: str) -> str:
    reason = _require_non_empty_str(value, label)
    if reason not in {"motion", "ammo", "manual"}:
        raise ValueError(f"{label} must be one of ['ammo', 'manual', 'motion']")
    return reason


def _require_burst_end_reason(value: Any, label: str) -> str:
    reason = _require_non_empty_str(value, label)
    if reason not in {"motion_settled", "manual", "end_of_samples"}:
        raise ValueError(f"{label} must be one of ['end_of_samples', 'manual', 'motion_settled']")
    return reason
