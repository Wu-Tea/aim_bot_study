from __future__ import annotations

from dataclasses import asdict
from dataclasses import dataclass
import json
from pathlib import Path


@dataclass(slots=True, frozen=True)
class RecoilControlCalibration:
    game: str
    aim_mode: str
    stance: str
    pixels_per_full_stick_x_per_second: float
    pixels_per_full_stick_y_per_second: float
    created_at: str


def stick_from_pixels(
    pixels: float,
    *,
    axis: str,
    duration_ms: int,
    calibration: RecoilControlCalibration,
) -> int:
    if duration_ms <= 0:
        return 0
    if axis == "x":
        rate = calibration.pixels_per_full_stick_x_per_second
    elif axis == "y":
        rate = calibration.pixels_per_full_stick_y_per_second
    else:
        raise ValueError("axis must be one of ['x', 'y']")
    if rate <= 0.0:
        return 0
    full_stick_fraction = float(pixels) / (float(rate) * (float(duration_ms) / 1000.0))
    return int(round(max(-1.0, min(1.0, full_stick_fraction)) * 32767.0))


def save_calibration(path: Path, calibration: RecoilControlCalibration) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(asdict(calibration), ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def load_calibration(path: Path) -> RecoilControlCalibration:
    payload = json.loads(path.read_text(encoding="utf-8"))
    return RecoilControlCalibration(**payload)


__all__ = [
    "RecoilControlCalibration",
    "load_calibration",
    "save_calibration",
    "stick_from_pixels",
]
