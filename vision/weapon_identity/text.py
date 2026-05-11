from __future__ import annotations

from typing import Any
from typing import Iterable
import os

import cv2
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
    multi_pass: bool = False,
) -> tuple[str, ...]:
    crop = _crop_normalized_roi(frame, roi)
    if crop.size == 0:
        return ()

    reader = ocr_reader if ocr_reader is not None else _load_default_ocr_reader()
    if reader is None:
        return ()

    raw_lines: list[str] = []
    variants = _iter_ocr_variants(crop) if multi_pass else (crop,)
    for variant in variants:
        try:
            raw_output = reader(variant)
        except Exception:
            if not multi_pass:
                return ()
            continue
        raw_lines.extend(_iter_ocr_strings(raw_output))

    normalized = normalize_ocr_lines(raw_lines)
    if not normalized:
        return ()
    if not multi_pass:
        return normalized
    return _augment_text_candidates(normalized)


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
        provider_hint = os.environ.get("RECOIL_OCR_PROVIDER", "cuda").strip().casefold()
        rapidocr_kwargs = {
            "intra_op_num_threads": 1,
            "inter_op_num_threads": 1,
        }
        if provider_hint == "dml":
            rapidocr_kwargs.update(
                {
                    "det_use_dml": True,
                    "cls_use_dml": True,
                    "rec_use_dml": True,
                }
            )
        elif provider_hint == "cpu":
            pass
        else:
            rapidocr_kwargs.update(
                {
                    "det_use_cuda": True,
                    "cls_use_cuda": True,
                    "rec_use_cuda": True,
                }
            )
        _DEFAULT_OCR_READER = RapidOCR(**rapidocr_kwargs)
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


def _iter_ocr_variants(crop: np.ndarray) -> tuple[np.ndarray, ...]:
    array = np.asarray(crop)
    if array.size == 0:
        return ()
    if array.ndim == 2:
        gray = array
    elif array.ndim == 3 and array.shape[2] >= 3:
        gray = cv2.cvtColor(array[:, :, :3], cv2.COLOR_RGB2GRAY)
    else:
        gray = array[:, :, 0]

    gray_upscaled = cv2.resize(gray, None, fx=2.0, fy=2.0, interpolation=cv2.INTER_CUBIC)
    clahe = cv2.createCLAHE(clipLimit=2.0, tileGridSize=(8, 8)).apply(gray_upscaled)
    _, clahe_otsu = cv2.threshold(clahe, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
    return (array, gray_upscaled, clahe, clahe_otsu)


def _augment_text_candidates(lines: tuple[str, ...]) -> tuple[str, ...]:
    candidates: list[str] = list(lines)

    if len(lines) >= 2:
        for first, second in zip(lines, lines[1:]):
            second_first_token = second.split()[0] if second.split() else ""
            candidates.extend(
                [
                    f"{first}{second}",
                    f"{first} {second}",
                ]
            )
            if second_first_token:
                candidates.append(f"{first}{second_first_token}")
                candidates.append(f"{first} {second_first_token}")

        candidates.append("".join(lines))
        candidates.append(" ".join(lines))

    return normalize_ocr_lines(candidates)


def _normalize_text(value: str) -> str | None:
    parts = value.strip().split()
    if not parts:
        return None
    kept: list[str] = []
    for index, part in enumerate(parts):
        token = part.strip()
        if not token:
            continue
        if not kept and _looks_like_ui_noise_token(token):
            return None
        if kept and _looks_like_ui_noise_token(token):
            break
        kept.append(token)
    if not kept:
        return None
    return " ".join(kept)


def _looks_like_ui_noise_token(token: str) -> bool:
    compact = "".join(character for character in token if not character.isspace())
    if not compact:
        return True
    if any(_is_cjk(character) for character in compact):
        return False
    digit_count = sum(character.isdigit() for character in compact)
    alpha_count = sum(character.isalpha() for character in compact)
    if digit_count and not alpha_count:
        return True
    if digit_count >= 3 and digit_count > alpha_count:
        return True
    return False


def _is_cjk(character: str) -> bool:
    return "\u4e00" <= character <= "\u9fff"


__all__ = ["extract_text_candidates", "normalize_ocr_lines"]
