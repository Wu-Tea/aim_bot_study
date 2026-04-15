from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from queue import Full, Queue
import threading

import cv2
import numpy as np


@dataclass(slots=True, frozen=True)
class _FrameWriteRequest:
    path: Path
    frame_bgr: np.ndarray


class DebugFrameCapture:
    def __init__(
        self,
        *,
        base_dir: Path,
        asynchronous: bool = True,
        queue_size: int = 64,
        jpeg_quality: int = 92,
    ):
        self.base_dir = Path(base_dir)
        self.asynchronous = bool(asynchronous)
        self.jpeg_quality = int(jpeg_quality)
        self._queue: Queue[_FrameWriteRequest | None] | None = None
        self._writer_thread: threading.Thread | None = None
        if self.asynchronous:
            self._queue = Queue(maxsize=max(1, int(queue_size)))
            self._writer_thread = threading.Thread(
                target=self._writer_loop,
                name="vision-debug-capture",
                daemon=True,
            )
            self._writer_thread.start()

    def save_frame(
        self,
        *,
        frame_bgr: np.ndarray,
        detections_count: int,
        has_selected_target: bool,
        auto_fire_active: bool,
        timestamp_text: str | None = None,
    ) -> Path | None:
        timestamp = self._resolve_timestamp(timestamp_text)
        date_dir = self.base_dir / timestamp.strftime("%Y-%m-%d")
        date_dir.mkdir(parents=True, exist_ok=True)
        file_name = (
            f"{timestamp.strftime('%H%M%S_%f')}"
            f"_boxes{int(detections_count)}"
            f"_lock{1 if has_selected_target else 0}"
            f"_fire{1 if auto_fire_active else 0}.jpg"
        )
        path = date_dir / file_name
        request = _FrameWriteRequest(path=path, frame_bgr=np.ascontiguousarray(frame_bgr.copy()))

        if not self.asynchronous:
            self._write_frame(request)
            return path

        assert self._queue is not None
        try:
            self._queue.put_nowait(request)
            return path
        except Full:
            return None

    def close(self) -> None:
        if not self.asynchronous or self._queue is None:
            return
        self._queue.put(None)
        if self._writer_thread is not None:
            self._writer_thread.join(timeout=2.0)

    def _writer_loop(self) -> None:
        assert self._queue is not None
        while True:
            request = self._queue.get()
            if request is None:
                self._queue.task_done()
                break
            self._write_frame(request)
            self._queue.task_done()

    def _write_frame(self, request: _FrameWriteRequest) -> None:
        request.path.parent.mkdir(parents=True, exist_ok=True)
        cv2.imwrite(
            str(request.path),
            request.frame_bgr,
            [int(cv2.IMWRITE_JPEG_QUALITY), self.jpeg_quality],
        )

    @staticmethod
    def _resolve_timestamp(timestamp_text: str | None) -> datetime:
        if timestamp_text is None:
            return datetime.now()
        return datetime.strptime(timestamp_text, "%Y-%m-%d_%H-%M-%S_%f")
