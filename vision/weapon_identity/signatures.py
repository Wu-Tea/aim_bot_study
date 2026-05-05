from __future__ import annotations

from dataclasses import dataclass
from typing import Any
from typing import Iterable
from typing import Mapping

import cv2
import numpy as np

from .models import VisualSignatureRecord

CLASSICAL_SIGNATURE_FEATURE_TYPE = "classical_signature_v1"
_TEMPLATE_SIZE = (32, 32)
_EDGE_MAP_SIZE = (32, 32)
_HASH_SIZE = 8
_HASH_BITS = _HASH_SIZE * _HASH_SIZE
_FOREGROUND_THRESHOLD = 0.05
_EPSILON = 1e-9
_FILL_RATIO_REFERENCE = 0.10
_TEMPLATE_WEIGHT = 0.45
_EDGE_WEIGHT = 0.20
_HASH_WEIGHT = 0.10
_STRUCTURE_WEIGHT = 0.25
_STRUCTURE_SPAN_WEIGHT = 0.40
_STRUCTURE_FILL_WEIGHT = 0.60
_STRUCTURE_MISSING_FILL_PENALTY = 0.18


@dataclass(slots=True, frozen=True)
class ExtractedSignature:
    template: tuple[tuple[float, ...], ...]
    edge_map: tuple[tuple[int, ...], ...]
    perceptual_hash: str

    def __post_init__(self) -> None:
        object.__setattr__(
            self,
            "template",
            _require_float_matrix(
                self.template,
                "ExtractedSignature.template",
                expected_shape=_TEMPLATE_SIZE,
            ),
        )
        object.__setattr__(
            self,
            "edge_map",
            _require_binary_matrix(
                self.edge_map,
                "ExtractedSignature.edge_map",
                expected_shape=_EDGE_MAP_SIZE,
            ),
        )
        object.__setattr__(
            self,
            "perceptual_hash",
            _require_hash_bits(
                self.perceptual_hash,
                "ExtractedSignature.perceptual_hash",
                expected_length=_HASH_BITS,
            ),
        )

    def to_feature_payload(self) -> dict[str, Any]:
        return {
            "version": 1,
            "template": [list(row) for row in self.template],
            "edge_map": [list(row) for row in self.edge_map],
            "perceptual_hash": self.perceptual_hash,
        }

    @classmethod
    def from_feature_payload(cls, payload: Mapping[str, Any]) -> "ExtractedSignature":
        if not isinstance(payload, Mapping):
            raise ValueError("feature_payload must be a mapping")
        if payload.get("version") != 1:
            raise ValueError("feature_payload.version must be 1")
        return cls(
            template=_require_float_matrix(
                payload.get("template"),
                "feature_payload.template",
                expected_shape=_TEMPLATE_SIZE,
            ),
            edge_map=_require_binary_matrix(
                payload.get("edge_map"),
                "feature_payload.edge_map",
                expected_shape=_EDGE_MAP_SIZE,
            ),
            perceptual_hash=_require_hash_bits(
                payload.get("perceptual_hash"),
                "feature_payload.perceptual_hash",
                expected_length=_HASH_BITS,
            ),
        )


@dataclass(slots=True, frozen=True)
class SignatureMatch:
    signature_id: str
    canonical_weapon_id: str
    score: float
    template_score: float
    edge_score: float
    hash_score: float
    structure_score: float

    def __post_init__(self) -> None:
        object.__setattr__(self, "signature_id", _require_non_empty_str(self.signature_id, "SignatureMatch.signature_id"))
        object.__setattr__(
            self,
            "canonical_weapon_id",
            _require_non_empty_str(
                self.canonical_weapon_id,
                "SignatureMatch.canonical_weapon_id",
            ),
        )
        object.__setattr__(self, "score", _require_score(self.score, "SignatureMatch.score"))
        object.__setattr__(
            self,
            "template_score",
            _require_score(self.template_score, "SignatureMatch.template_score"),
        )
        object.__setattr__(self, "edge_score", _require_score(self.edge_score, "SignatureMatch.edge_score"))
        object.__setattr__(self, "hash_score", _require_score(self.hash_score, "SignatureMatch.hash_score"))
        object.__setattr__(
            self,
            "structure_score",
            _require_score(self.structure_score, "SignatureMatch.structure_score"),
        )


def extract_signature(image: Any) -> ExtractedSignature:
    grayscale = _to_grayscale_uint8(image)
    template = cv2.resize(grayscale, _TEMPLATE_SIZE, interpolation=cv2.INTER_AREA).astype(np.float32) / 255.0
    edge_map = cv2.Canny(grayscale, 64, 160)
    edge_map = cv2.resize(edge_map, _EDGE_MAP_SIZE, interpolation=cv2.INTER_NEAREST)
    edge_map = (edge_map > 0).astype(np.uint8)

    return ExtractedSignature(
        template=_matrix_to_float_tuples(template),
        edge_map=_matrix_to_int_tuples(edge_map),
        perceptual_hash=_compute_perceptual_hash(grayscale),
    )


def score_candidates(image: Any, candidates: Iterable[VisualSignatureRecord]) -> list[SignatureMatch]:
    live_signature = extract_signature(image)
    live_template = _matrix_to_numpy(live_signature.template, dtype=np.float32)
    live_edges = _matrix_to_numpy(live_signature.edge_map, dtype=np.uint8)
    compatible_candidates = [
        candidate for candidate in candidates if candidate.feature_type == CLASSICAL_SIGNATURE_FEATURE_TYPE
    ]
    if not compatible_candidates:
        return []

    stored_signatures = [
        (candidate, ExtractedSignature.from_feature_payload(candidate.feature_payload))
        for candidate in compatible_candidates
    ]
    ranked: list[SignatureMatch] = []
    for candidate, stored_signature in stored_signatures:
        template_score = _compare_templates(
            live_template,
            _matrix_to_numpy(stored_signature.template, dtype=np.float32),
        )
        edge_score = _compare_edges(
            live_edges,
            _matrix_to_numpy(stored_signature.edge_map, dtype=np.uint8),
        )
        hash_score = _compare_hashes(live_signature.perceptual_hash, stored_signature.perceptual_hash)
        structure_score = _compute_structure_score(live_signature, stored_signature)
        final_score = (
            (template_score * _TEMPLATE_WEIGHT)
            + (edge_score * _EDGE_WEIGHT)
            + (hash_score * _HASH_WEIGHT)
            + (structure_score * _STRUCTURE_WEIGHT)
        )
        ranked.append(
            SignatureMatch(
                signature_id=candidate.signature_id,
                canonical_weapon_id=candidate.canonical_weapon_id,
                score=final_score,
                template_score=template_score,
                edge_score=edge_score,
                hash_score=hash_score,
                structure_score=structure_score,
            )
        )

    return sorted(ranked, key=lambda match: match.score, reverse=True)


def _to_grayscale_uint8(image: Any) -> np.ndarray:
    array = np.asarray(image)
    if array.size == 0:
        raise ValueError("image must not be empty")
    if array.ndim == 2:
        grayscale = array
    elif array.ndim == 3 and array.shape[2] == 1:
        grayscale = array[:, :, 0]
    elif array.ndim == 3 and array.shape[2] == 3:
        grayscale = cv2.cvtColor(array, cv2.COLOR_BGR2GRAY)
    elif array.ndim == 3 and array.shape[2] == 4:
        grayscale = cv2.cvtColor(array, cv2.COLOR_BGRA2GRAY)
    else:
        raise ValueError("image must be a 2D grayscale image or a 1/3/4-channel image")
    if grayscale.dtype != np.uint8:
        grayscale = np.clip(grayscale, 0, 255).astype(np.uint8)
    return grayscale


def _compute_perceptual_hash(grayscale: np.ndarray) -> str:
    resized = cv2.resize(grayscale, (32, 32), interpolation=cv2.INTER_AREA).astype(np.float32)
    dct = cv2.dct(resized)
    low_frequency = dct[:_HASH_SIZE, :_HASH_SIZE]
    threshold = float(np.median(low_frequency[1:, :]))
    bits = low_frequency >= threshold
    return "".join("1" if value else "0" for value in bits.flatten())


def _compare_templates(left: np.ndarray, right: np.ndarray) -> float:
    mask = left > _FOREGROUND_THRESHOLD
    if not np.any(mask):
        return 1.0
    difference = np.abs(left[mask] - right[mask])
    return float(max(0.0, 1.0 - float(np.mean(difference))))


def _compare_edges(left: np.ndarray, right: np.ndarray) -> float:
    left_edges = left > 0
    right_edges = right > 0
    left_edge_count = int(left_edges.sum())
    if left_edge_count == 0:
        return 1.0
    # Small HUD crops can shift a couple of normalized pixels after resize, so
    # edge agreement should tolerate a narrow neighborhood rather than exact overlap.
    kernel = np.ones((7, 7), dtype=np.uint8)
    right_dilated = cv2.dilate(right_edges.astype(np.uint8), kernel, iterations=1) > 0
    matched_left = int(np.logical_and(left_edges, right_dilated).sum())
    return float(matched_left / left_edge_count)


def _compare_hashes(left: str, right: str) -> float:
    if len(left) != len(right):
        raise ValueError("perceptual_hash sizes must match")
    mismatches = sum(1 for left_bit, right_bit in zip(left, right) if left_bit != right_bit)
    return float(1.0 - (mismatches / max(len(left), 1)))


def _matrix_to_float_tuples(matrix: np.ndarray) -> tuple[tuple[float, ...], ...]:
    return tuple(tuple(float(value) for value in row) for row in matrix)


def _matrix_to_int_tuples(matrix: np.ndarray) -> tuple[tuple[int, ...], ...]:
    return tuple(tuple(int(value) for value in row) for row in matrix)


def _matrix_to_numpy(matrix: tuple[tuple[float, ...], ...] | tuple[tuple[int, ...], ...], dtype: Any) -> np.ndarray:
    return np.asarray(matrix, dtype=dtype)


def _require_float_matrix(
    value: Any,
    label: str,
    *,
    expected_shape: tuple[int, int] | None = None,
) -> tuple[tuple[float, ...], ...]:
    rows = _require_matrix_rows(value, label, expected_shape=expected_shape)
    normalized_rows = []
    for row_index, row in enumerate(rows):
        normalized_row = []
        for column_index, item in enumerate(row):
            if type(item) not in {int, float}:
                raise ValueError(f"{label}[{row_index}][{column_index}] must be numeric")
            number = float(item)
            if number < 0.0 or number > 1.0:
                raise ValueError(f"{label}[{row_index}][{column_index}] must be between 0.0 and 1.0")
            normalized_row.append(number)
        normalized_rows.append(tuple(normalized_row))
    result = tuple(normalized_rows)
    if expected_shape is not None and _matrix_shape(result) != expected_shape:
        raise ValueError(f"{label} must have shape {expected_shape[0]}x{expected_shape[1]}")
    return result


def _require_binary_matrix(
    value: Any,
    label: str,
    *,
    expected_shape: tuple[int, int] | None = None,
) -> tuple[tuple[int, ...], ...]:
    rows = _require_matrix_rows(value, label, expected_shape=expected_shape)
    normalized_rows = []
    for row_index, row in enumerate(rows):
        normalized_row = []
        for column_index, item in enumerate(row):
            if type(item) not in {int, float}:
                raise ValueError(f"{label}[{row_index}][{column_index}] must be numeric")
            number = int(item)
            if number not in {0, 1}:
                raise ValueError(f"{label}[{row_index}][{column_index}] must be 0 or 1")
            normalized_row.append(number)
        normalized_rows.append(tuple(normalized_row))
    result = tuple(normalized_rows)
    if expected_shape is not None and _matrix_shape(result) != expected_shape:
        raise ValueError(f"{label} must have shape {expected_shape[0]}x{expected_shape[1]}")
    return result


def _require_matrix_rows(
    value: Any,
    label: str,
    *,
    expected_shape: tuple[int, int] | None = None,
) -> tuple[tuple[Any, ...], ...]:
    if not isinstance(value, (list, tuple)):
        raise ValueError(f"{label} must be a list or tuple of rows")
    if not value:
        raise ValueError(f"{label} must not be empty")
    if expected_shape is not None and len(value) != expected_shape[0]:
        raise ValueError(f"{label} must have shape {expected_shape[0]}x{expected_shape[1]}")
    rows = []
    expected_width = expected_shape[1] if expected_shape is not None else None
    for row_index, row in enumerate(value):
        if not isinstance(row, (list, tuple)):
            raise ValueError(f"{label}[{row_index}] must be a list or tuple")
        if not row:
            raise ValueError(f"{label}[{row_index}] must not be empty")
        if expected_width is None:
            expected_width = len(row)
        elif len(row) != expected_width and expected_shape is None:
            raise ValueError(f"{label} rows must all have the same width")
        elif len(row) != expected_width:
            raise ValueError(f"{label} must have shape {expected_shape[0]}x{expected_shape[1]}")
        rows.append(tuple(row))
    return tuple(rows)


def _require_hash_bits(value: Any, label: str, *, expected_length: int | None = None) -> str:
    if type(value) is not str:
        raise ValueError(f"{label} must be a string")
    if not value:
        raise ValueError(f"{label} must not be empty")
    if any(bit not in {"0", "1"} for bit in value):
        raise ValueError(f"{label} must contain only 0 and 1 characters")
    if expected_length is not None and len(value) != expected_length:
        raise ValueError(f"{label} must be {expected_length} bits")
    return value


def _matrix_shape(matrix: tuple[tuple[Any, ...], ...]) -> tuple[int, int]:
    return len(matrix), len(matrix[0])


def _compute_structure_score(live_signature: ExtractedSignature, candidate_signature: ExtractedSignature) -> float:
    _, live_fill_ratio, live_structure = _compute_structure_features(live_signature)
    _, candidate_fill_ratio, candidate_structure = _compute_structure_features(candidate_signature)
    if live_structure <= 0.0 or candidate_structure <= 0.0:
        return 0.0

    # Penalize candidates that omit live-observed structure, but do not punish
    # fuller candidates for extra pixels that may be hidden by occlusion.
    missing_fill_ratio = max(live_fill_ratio - candidate_fill_ratio, 0.0)
    missing_fill_penalty = min(missing_fill_ratio / _FILL_RATIO_REFERENCE, 1.0) * _STRUCTURE_MISSING_FILL_PENALTY
    return float(max(candidate_structure - missing_fill_penalty, 0.0))


def _compute_structure_features(signature: ExtractedSignature) -> tuple[float, float, float]:
    template = _matrix_to_numpy(signature.template, dtype=np.float32)
    occupied = template > _FOREGROUND_THRESHOLD
    positions = np.argwhere(occupied)
    if positions.size == 0:
        return 0.0, 0.0, 0.0
    left = int(positions[:, 1].min())
    right = int(positions[:, 1].max())
    span_ratio = (right - left + 1) / template.shape[1]
    fill_ratio = float(np.mean(occupied))
    fill_score = min(fill_ratio / _FILL_RATIO_REFERENCE, 1.0)
    structure_score = (span_ratio * _STRUCTURE_SPAN_WEIGHT) + (fill_score * _STRUCTURE_FILL_WEIGHT)
    return float(span_ratio), fill_ratio, float(structure_score)


def _require_non_empty_str(value: Any, label: str) -> str:
    if type(value) is not str:
        raise ValueError(f"{label} must be a string")
    result = value.strip()
    if not result:
        raise ValueError(f"{label} must be a non-empty string")
    return result


def _require_score(value: Any, label: str) -> float:
    if type(value) not in {int, float}:
        raise ValueError(f"{label} must be numeric")
    score = float(value)
    if score < -_EPSILON or score > 1.0 + _EPSILON:
        raise ValueError(f"{label} must be between 0.0 and 1.0")
    return min(1.0, max(0.0, score))


__all__ = [
    "CLASSICAL_SIGNATURE_FEATURE_TYPE",
    "ExtractedSignature",
    "SignatureMatch",
    "extract_signature",
    "score_candidates",
]
