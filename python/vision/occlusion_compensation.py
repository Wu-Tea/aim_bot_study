from __future__ import annotations

from dataclasses import dataclass
from enum import StrEnum
import statistics


class TargetSource(StrEnum):
    OBSERVED = "observed"
    RECONSTRUCTED = "reconstructed"
    PREDICTED = "predicted"


@dataclass(slots=True, frozen=True)
class TrackSample:
    target_x: float
    target_y: float
    selected_box: tuple[float, float, float, float]
    bottom_y: float
    height: float
    timestamp: float
    source: TargetSource


@dataclass(slots=True, frozen=True)
class CompensationResult:
    target_x: float
    target_y: float
    selected_box: tuple[float, float, float, float]
    source: TargetSource


class TargetOcclusionCompensator:
    def __init__(
        self,
        max_stable_samples: int = 3,
        max_predicted_frames: int = 2,
        max_center_x_delta: float = 24.0,
        max_bottom_delta: float = 24.0,
        min_height_ratio: float = 0.72,
        min_top_drop: float = 12.0,
        max_predicted_step_x: float = 28.0,
        max_predicted_step_y: float = 28.0,
        max_height_drift: float = 12.0,
    ):
        self.max_stable_samples = max(1, int(max_stable_samples))
        self.max_predicted_frames = max(1, int(max_predicted_frames))
        self.max_center_x_delta = float(max_center_x_delta)
        self.max_bottom_delta = float(max_bottom_delta)
        self.min_height_ratio = float(min_height_ratio)
        self.min_top_drop = float(min_top_drop)
        self.max_predicted_step_x = float(max_predicted_step_x)
        self.max_predicted_step_y = float(max_predicted_step_y)
        self.max_height_drift = float(max_height_drift)
        self.reset()

    def reset(self):
        self._stable_samples: list[TrackSample] = []
        self._predicted_frames_used = 0

    def _clear_prediction_state(self):
        self._predicted_frames_used = 0

    @staticmethod
    def _normalize_source(source: object) -> TargetSource:
        if source is None:
            return TargetSource.OBSERVED
        if isinstance(source, TargetSource):
            return source
        return TargetSource(str(source))

    @staticmethod
    def _clamp(value: float, limit: float) -> float:
        return max(-limit, min(limit, value))

    @staticmethod
    def _coerce_box(
        candidate: object,
    ) -> tuple[float, float, float, float] | None:
        if candidate is None:
            return None

        raw_box = getattr(candidate, "selected_box", candidate)
        if raw_box is None:
            return None

        left, top, right, bottom = raw_box
        return (
            float(left),
            float(top),
            float(right),
            float(bottom),
        )

    @staticmethod
    def _box_center_x(box: tuple[float, float, float, float]) -> float:
        return (box[0] + box[2]) * 0.5

    def _latest_sample(self) -> TrackSample | None:
        if not self._stable_samples:
            return None
        return self._stable_samples[-1]

    def _recent_heights(self) -> list[float]:
        return [sample.height for sample in self._stable_samples[-self.max_stable_samples :]]

    def _stable_height(self) -> float | None:
        heights = self._recent_heights()
        if not heights:
            return None
        return float(statistics.median(heights))

    def _stable_center_x(self) -> float | None:
        if not self._stable_samples:
            return None
        centers = [
            self._box_center_x(sample.selected_box)
            for sample in self._stable_samples[-self.max_stable_samples :]
        ]
        return float(statistics.median(centers))

    def _stable_bottom_y(self) -> float | None:
        if not self._stable_samples:
            return None
        bottoms = [
            sample.bottom_y
            for sample in self._stable_samples[-self.max_stable_samples :]
        ]
        return float(statistics.median(bottoms))

    def _stable_target_y_ratio(self) -> float:
        latest = self._latest_sample()
        if latest is None or latest.height <= 0.0:
            return 0.5

        ratios = []
        for sample in self._stable_samples[-self.max_stable_samples :]:
            left, top, right, bottom = sample.selected_box
            height = bottom - top
            if height <= 0.0:
                continue
            ratios.append((sample.target_y - top) / height)

        if not ratios:
            latest_top = latest.selected_box[1]
            ratio = (latest.target_y - latest_top) / latest.height
            return max(0.0, min(1.0, ratio))

        ratio = float(statistics.median(ratios))
        return max(0.0, min(1.0, ratio))

    def _stable_target_x_offset(self) -> float:
        latest = self._latest_sample()
        if latest is None:
            return 0.0
        return latest.target_x - self._box_center_x(latest.selected_box)

    def record_observation(self, target, timestamp: float):
        selected_box = self._coerce_box(target)
        if selected_box is None:
            return

        source = self._normalize_source(getattr(target, "source", TargetSource.OBSERVED))
        if source is TargetSource.PREDICTED:
            return

        left, top, right, bottom = selected_box
        if right <= left or bottom <= top:
            return

        sample = TrackSample(
            target_x=float(getattr(target, "target_x")),
            target_y=float(getattr(target, "target_y")),
            selected_box=selected_box,
            bottom_y=bottom,
            height=bottom - top,
            timestamp=float(timestamp),
            source=source,
        )
        self._stable_samples.append(sample)
        self._stable_samples = self._stable_samples[-self.max_stable_samples :]
        self._clear_prediction_state()

    def try_reconstruct(self, candidate, timestamp: float):
        _ = float(timestamp)
        latest = self._latest_sample()
        selected_box = self._coerce_box(candidate)
        if latest is None or selected_box is None:
            return None

        left, top, right, bottom = selected_box
        width = right - left
        current_height = bottom - top
        if width <= 0.0 or current_height <= 0.0:
            return None

        stable_height = self._stable_height()
        stable_center_x = self._stable_center_x()
        stable_bottom_y = self._stable_bottom_y()
        if (
            stable_height is None
            or stable_center_x is None
            or stable_bottom_y is None
            or current_height >= stable_height
        ):
            return None

        center_x = self._box_center_x(selected_box)
        top_drop = top - latest.selected_box[1]
        height_shrank = current_height <= (stable_height * self.min_height_ratio)
        if abs(center_x - stable_center_x) > self.max_center_x_delta:
            return None
        if abs(bottom - stable_bottom_y) > self.max_bottom_delta:
            return None
        if not (top_drop >= self.min_top_drop or height_shrank):
            return None

        reconstructed_top = bottom - stable_height
        target_x = center_x + self._stable_target_x_offset()
        target_y = reconstructed_top + (stable_height * self._stable_target_y_ratio())
        return CompensationResult(
            target_x=target_x,
            target_y=target_y,
            selected_box=(left, reconstructed_top, right, bottom),
            source=TargetSource.RECONSTRUCTED,
        )

    def try_predict(self, timestamp: float):
        if len(self._stable_samples) < 2:
            return None
        if self._predicted_frames_used >= self.max_predicted_frames:
            return None

        prev_sample = self._stable_samples[-2]
        last_sample = self._stable_samples[-1]
        dt = max(1e-6, last_sample.timestamp - prev_sample.timestamp)
        predict_dt = max(1e-6, float(timestamp) - last_sample.timestamp)

        vx = (last_sample.target_x - prev_sample.target_x) / dt
        vy = (last_sample.target_y - prev_sample.target_y) / dt
        v_bottom = (last_sample.bottom_y - prev_sample.bottom_y) / dt
        v_height = (last_sample.height - prev_sample.height) / dt

        dx = self._clamp(vx * predict_dt, self.max_predicted_step_x)
        dy = self._clamp(vy * predict_dt, self.max_predicted_step_y)
        d_bottom = self._clamp(v_bottom * predict_dt, self.max_predicted_step_y)
        d_height = self._clamp(v_height * predict_dt, self.max_height_drift)

        left, top, right, bottom = last_sample.selected_box
        predicted_height = max(8.0, last_sample.height + d_height)
        predicted_bottom = bottom + d_bottom
        predicted_left = left + dx
        predicted_right = right + dx
        predicted_top = predicted_bottom - predicted_height

        self._predicted_frames_used += 1
        return CompensationResult(
            target_x=last_sample.target_x + dx,
            target_y=last_sample.target_y + dy,
            selected_box=(
                predicted_left,
                predicted_top,
                predicted_right,
                predicted_bottom,
            ),
            source=TargetSource.PREDICTED,
        )
