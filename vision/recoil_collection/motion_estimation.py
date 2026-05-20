from __future__ import annotations

from dataclasses import dataclass
from typing import Any

import cv2
import numpy as np


@dataclass(slots=True, frozen=True)
class MotionEstimate:
    delta_x: float
    delta_y: float
    quality: float
    feature_count: int


def estimate_static_roi_motion(previous_frame: Any, current_frame: Any) -> MotionEstimate:
    previous = _central_static_roi(previous_frame)
    current = _central_static_roi(current_frame)
    previous_gray = _to_gray(previous)
    current_gray = _to_gray(current)
    if _is_low_texture(previous_gray) or _is_low_texture(current_gray):
        return MotionEstimate(0.0, 0.0, 0.0, 0)

    shift, response = cv2.phaseCorrelate(previous_gray.astype(np.float32), current_gray.astype(np.float32))
    quality = float(max(0.0, min(1.0, response)))
    if quality < 0.05:
        return MotionEstimate(0.0, 0.0, quality, 0)
    return MotionEstimate(float(shift[0]), float(shift[1]), quality, int(previous_gray.size))


def _central_static_roi(frame: Any) -> np.ndarray:
    array = np.asarray(frame)
    height, width = array.shape[:2]
    if min(height, width) < 96:
        return array
    top = int(height * 0.15)
    bottom = int(height * 0.72)
    left = int(width * 0.18)
    right = int(width * 0.82)
    return array[top:bottom, left:right]


def _to_gray(frame: np.ndarray) -> np.ndarray:
    if frame.ndim == 2:
        return frame
    return cv2.cvtColor(frame[:, :, :3], cv2.COLOR_RGB2GRAY)


def _is_low_texture(gray: np.ndarray) -> bool:
    texture_threshold = 2.0 if float(np.max(gray)) > 10.0 else 0.01
    return float(gray.std()) < texture_threshold


__all__ = ["MotionEstimate", "estimate_static_roi_motion"]
