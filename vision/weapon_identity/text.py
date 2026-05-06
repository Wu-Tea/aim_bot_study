from __future__ import annotations

from typing import Any
from typing import Iterable

import numpy as np

from .adapters import NormalizedROI

_DEFAULT_OCR_READER = None
_DEFAULT_OCR_READER_INITIALIZED = False


def normalize_ocr_lines(lines: Iterable[str]) -> tuple[str, ...]:
    seen: set[str] = set()
    normalized: list[str] = []
    for line in lines:
        if type(line) is not str:
            continue
        text = _normalize_text(line)
        if text is None:
            continue
        dedup_key = text.casefold()
        if dedup_key in seen:
            continue
        seen.add(dedup_key)
        normalized.append(text)
    return tuple(normalized)


def extract_text_candidates(
    frame: Any,
    roi: NormalizedROI,
    *,
    ocr_reader: Any = None,
) -> tuple[str, ...]:
    crop = _crop_normalized_roi(frame, roi)
    if crop.size == 0:
        return ()

    reader = ocr_reader if ocr_reader is not None else _load_default_ocr_reader()
    if reader is None:
        return ()

    try:
        raw_output = reader(crop)
    except Exception:
        return ()

    return normalize_ocr_lines(_iter_ocr_strings(raw_output))


def _load_default_ocr_reader() -> Any:
    global _DEFAULT_OCR_READER
    global _DEFAULT_OCR_READER_INITIALIZED
    if _DEFAULT_OCR_READER_INITIALIZED:
        return _DEFAULT_OCR_READER
    _DEFAULT_OCR_READER_INITIALIZED = True
    try:
        from rapidocr_onnxruntime import RapidOCR
    except Exception:
        _DEFAULT_OCR_READER = None
        return None

    try:
        _DEFAULT_OCR_READER = RapidOCR()
    except Exception:
        _DEFAULT_OCR_READER = None
    return _DEFAULT_OCR_READER


def _iter_ocr_strings(raw_output: Any) -> Iterable[str]:
    entries = raw_output
    if isinstance(raw_output, tuple) and raw_output:
        entries = raw_output[0]
    if entries is None:
        return ()
    if isinstance(entries, str):
        return (entries,)
    if not isinstance(entries, (list, tuple)):
        return ()

    candidates: list[str] = []
    for entry in entries:
        if type(entry) is str:
            candidates.append(entry)
            continue
        if isinstance(entry, (list, tuple)) and len(entry) >= 2 and type(entry[1]) is str:
            candidates.append(entry[1])
    return tuple(candidates)


def _crop_normalized_roi(frame: Any, roi: NormalizedROI) -> np.ndarray:
    array = np.asarray(frame)
    if array.size == 0 or array.ndim < 2:
        return np.zeros((0, 0), dtype=np.uint8)
    height, width = array.shape[:2]
    left = max(0, min(width, int(round(roi.left * width))))
    top = max(0, min(height, int(round(roi.top * height))))
    right = max(left, min(width, int(round((roi.left + roi.width) * width))))
    bottom = max(top, min(height, int(round((roi.top + roi.height) * height))))
    return array[top:bottom, left:right]


def _normalize_text(value: str) -> str | None:
    parts = value.strip().split()
    if not parts:
        return None
    return " ".join(parts)


__all__ = ["extract_text_candidates", "normalize_ocr_lines"]
