import threading
import time
from dataclasses import dataclass

import numpy as np

from .capture import CapturedFrame
from .targeting import color_roi_bounds


@dataclass(slots=True, frozen=True)
class InferenceResult:
    frame_id: int
    captured_at: float
    inferred_at: float
    frame: object | None
    detections: list
    infer_ms: float


class CachedRoiFrame:
    def __init__(self, *, shape, roi_cache):
        self.shape = tuple(shape)
        self._roi_cache = dict(roi_cache)

    def get_roi_rgb(self, left: int, top: int, right: int, bottom: int):
        return self._roi_cache[(int(left), int(top), int(right), int(bottom))]


def _detections_need_frame(detections) -> bool:
    if not detections:
        return False
    for detection in detections:
        boxes = getattr(detection, "boxes", None)
        if boxes is None:
            return True
        if len(boxes) > 0:
            return True
    return False


def _build_detection_roi_frame(captured_frame: CapturedFrame, detections):
    frame_shape = captured_frame.shape
    roi_cache = {}
    for detection in detections:
        boxes = getattr(detection, "boxes", None)
        if boxes is None:
            continue
        for box in boxes:
            bounds = color_roi_bounds(np.asarray(box, dtype=np.float32), frame_shape)
            if bounds is None or bounds in roi_cache:
                continue
            roi_cache[bounds] = captured_frame.get_roi_rgb(*bounds)
    return CachedRoiFrame(shape=frame_shape, roi_cache=roi_cache)


class InferenceThread(threading.Thread):
    def __init__(
        self,
        *,
        capture_thread,
        frame_timeout: float = 0.1,
        model=None,
        predict_kwargs: dict | None = None,
        fast_path=None,
        use_fast_path: bool = False,
        require_result_frame: bool = False,
        predict_fn=None,
    ):
        super().__init__(daemon=True)
        self.capture_thread = capture_thread
        self.frame_timeout = float(frame_timeout)
        self.model = model
        self.predict_kwargs = dict(predict_kwargs or {})
        self.fast_path = fast_path
        self.use_fast_path = bool(use_fast_path)
        self.require_result_frame = bool(require_result_frame)
        self.predict_fn = predict_fn
        self.running = True
        self._paused = True
        self._condition = threading.Condition()
        self._latest_result: InferenceResult | None = None
        self._fatal_error: Exception | None = None

    def _predict(self, captured_frame: CapturedFrame):
        if self.predict_fn is not None:
            frame = captured_frame.frame
            return self.predict_fn(frame)
        if self.use_fast_path and self.fast_path is not None:
            from .fastpath import _fast_predict

            return _fast_predict(self.fast_path, captured_frame)
        if self.model is None:
            raise RuntimeError("InferenceThread requires a model, fast path, or predict_fn.")

        from .fastpath import _extract_detections

        frame = captured_frame.frame
        return _extract_detections(
            self.model.predict(source=frame[:, :, ::-1].copy(), **self.predict_kwargs)
        )

    def run(self):
        last_frame_id = 0
        while self.running:
            with self._condition:
                while self.running and self._paused and self._fatal_error is None:
                    self._condition.wait()
                if self._fatal_error is not None:
                    return
                if not self.running:
                    return

            captured, next_frame_id = self.capture_thread.get_latest_frame(
                last_seen_id=last_frame_id,
                timeout=self.frame_timeout,
            )
            if captured is None:
                continue

            with self._condition:
                if self._fatal_error is not None:
                    return
                if not self.running:
                    return
                if self._paused:
                    continue

            infer_started_at = time.perf_counter()
            try:
                detections = self._predict(captured)
            except Exception as exc:
                captured.release_native_resources()
                with self._condition:
                    self._fatal_error = exc
                    self.running = False
                    self._condition.notify_all()
                return
            inferred_at = time.perf_counter()
            last_frame_id = next_frame_id
            result_frame = captured.peek_frame()
            if self.require_result_frame:
                result_frame = captured.frame
            elif _detections_need_frame(detections):
                if result_frame is None and captured.can_extract_roi():
                    result_frame = _build_detection_roi_frame(captured, detections)
                else:
                    result_frame = captured.frame
            else:
                captured.release_native_resources()
            result = InferenceResult(
                frame_id=captured.frame_id,
                captured_at=captured.captured_at,
                inferred_at=inferred_at,
                frame=result_frame,
                detections=detections,
                infer_ms=(inferred_at - infer_started_at) * 1000.0,
            )
            if result_frame is not captured.peek_frame():
                captured.release_native_resources()

            with self._condition:
                if not self.running or self._paused:
                    continue
                self._latest_result = result
                self._condition.notify_all()

    def get_latest_result(self, last_seen_id: int = 0, timeout: float = 0.1):
        deadline = time.perf_counter() + timeout
        with self._condition:
            while True:
                if self._fatal_error is not None:
                    raise RuntimeError("InferenceThread prediction failed") from self._fatal_error
                if self._latest_result is not None and self._latest_result.frame_id > last_seen_id:
                    return self._latest_result, self._latest_result.frame_id
                if not self.running:
                    return None, last_seen_id
                remaining = deadline - time.perf_counter()
                if remaining <= 0:
                    return None, last_seen_id
                self._condition.wait(timeout=remaining)

    def resume(self):
        with self._condition:
            self._paused = False
            self._condition.notify_all()

    def pause(self, clear_result: bool = True):
        with self._condition:
            self._paused = True
            if clear_result:
                self._latest_result = None
            self._condition.notify_all()

    def stop(self):
        self.running = False
        with self._condition:
            self._condition.notify_all()
