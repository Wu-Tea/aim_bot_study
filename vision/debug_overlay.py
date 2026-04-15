from dataclasses import dataclass
from datetime import datetime

import cv2
import numpy as np

from .debug_capture import DebugFrameCapture
from .targeting import ParsedDetections, SelectedTarget, TargetSelector


@dataclass(slots=True, frozen=True)
class OverlayStatus:
    auto_fire_active: bool
    is_aiming: bool
    detections_count: int
    best_target_delta: tuple[float, float] | None = None
    status_text: str | None = None


class VisionDebugOverlay:
    def __init__(
        self,
        window_name: str = "Vision Debug",
        *,
        frame_capture: DebugFrameCapture | None = None,
        display_window: bool = True,
    ):
        self.window_name = window_name
        self.frame_capture = frame_capture
        self.display_window = display_window
        self._window_initialized = False
        self._enabled = True

    def render_frame(
        self,
        *,
        frame: np.ndarray,
        detections: list[ParsedDetections],
        selected_target: SelectedTarget | None,
        target_selector: TargetSelector,
        auto_fire_active: bool,
        is_aiming: bool,
        best_target_delta: tuple[float, float] | None = None,
        status_text: str | None = None,
    ) -> np.ndarray:
        rendered = cv2.cvtColor(frame.copy(), cv2.COLOR_RGB2BGR)
        status = OverlayStatus(
            auto_fire_active=auto_fire_active,
            is_aiming=is_aiming,
            detections_count=sum(len(detection.boxes) for detection in detections),
            best_target_delta=best_target_delta,
            status_text=status_text,
        )

        self._draw_detections(rendered, frame, detections, selected_target, target_selector)
        self._draw_selected_target(rendered, selected_target)
        self._draw_crosshair(rendered, auto_fire_active=auto_fire_active)
        self._draw_status(rendered, status)
        return rendered

    def show(
        self,
        *,
        frame: np.ndarray,
        detections: list[ParsedDetections],
        selected_target: SelectedTarget | None,
        target_selector: TargetSelector,
        auto_fire_active: bool,
        is_aiming: bool,
        best_target_delta: tuple[float, float] | None = None,
        status_text: str | None = None,
    ) -> None:
        if not self._enabled and self.frame_capture is None:
            return

        rendered = self.render_frame(
            frame=frame,
            detections=detections,
            selected_target=selected_target,
            target_selector=target_selector,
            auto_fire_active=auto_fire_active,
            is_aiming=is_aiming,
            best_target_delta=best_target_delta,
            status_text=status_text,
        )
        if self.frame_capture is not None and sum(len(detection.boxes) for detection in detections) > 0:
            self.frame_capture.save_frame(
                frame_bgr=rendered,
                detections_count=sum(len(detection.boxes) for detection in detections),
                has_selected_target=selected_target is not None,
                auto_fire_active=auto_fire_active,
                timestamp_text=datetime.now().strftime("%Y-%m-%d_%H-%M-%S_%f"),
            )
        if self.display_window and self._enabled:
            self._present(rendered)

    def show_message(self, *, width: int, height: int, message: str) -> None:
        if not self._enabled or not self.display_window:
            return

        canvas = np.zeros((height, width, 3), dtype=np.uint8)
        cv2.putText(
            canvas,
            message,
            (16, max(28, height // 2)),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.7,
            (255, 255, 255),
            2,
            cv2.LINE_AA,
        )
        self._present(canvas)

    def close(self) -> None:
        if self.frame_capture is not None:
            self.frame_capture.close()
        if not self._window_initialized:
            return
        try:
            cv2.destroyWindow(self.window_name)
        except cv2.error:
            pass
        self._window_initialized = False

    def _present(self, frame_bgr: np.ndarray) -> None:
        try:
            frame_height, frame_width = frame_bgr.shape[:2]
            if not self._window_initialized:
                cv2.namedWindow(
                    self.window_name,
                    cv2.WINDOW_NORMAL | cv2.WINDOW_KEEPRATIO,
                )
                try:
                    cv2.setWindowProperty(self.window_name, cv2.WND_PROP_TOPMOST, 1)
                except cv2.error:
                    pass
                cv2.resizeWindow(self.window_name, frame_width, frame_height)
                self._window_initialized = True
            cv2.imshow(self.window_name, frame_bgr)
            cv2.waitKey(1)
        except cv2.error as exc:
            print(f"[VisionDebug] Disabled overlay window: {exc}")
            self._enabled = False
            self.close()

    def _draw_detections(
        self,
        canvas: np.ndarray,
        source_frame_rgb: np.ndarray,
        detections: list[ParsedDetections],
        selected_target: SelectedTarget | None,
        target_selector: TargetSelector,
    ) -> None:
        for detection in detections:
            for box, conf in zip(detection.boxes, detection.confs):
                label, color = self._classify_box(box, source_frame_rgb, target_selector)
                if self._is_selected_box(box, selected_target):
                    label = f"selected {label}"
                    color = (0, 0, 255)
                    thickness = 3
                else:
                    label = f"raw {label}"
                    thickness = 1

                x1, y1, x2, y2 = [int(round(float(value))) for value in box]
                cv2.rectangle(canvas, (x1, y1), (x2, y2), color, thickness)
                cv2.putText(
                    canvas,
                    f"{label} {float(conf):.2f}",
                    (x1, max(16, y1 - 6)),
                    cv2.FONT_HERSHEY_SIMPLEX,
                    0.45,
                    color,
                    1,
                    cv2.LINE_AA,
                )

    def _draw_selected_target(
        self,
        canvas: np.ndarray,
        selected_target: SelectedTarget | None,
    ) -> None:
        if selected_target is None:
            return

        tx = int(round(selected_target.target_x))
        ty = int(round(selected_target.target_y))
        cv2.circle(canvas, (tx, ty), 5, (0, 0, 255), 2)
        cv2.putText(
            canvas,
            f"lock dx={selected_target.dx:.1f} dy={selected_target.dy:.1f}",
            (tx + 8, max(18, ty - 8)),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.45,
            (0, 0, 255),
            1,
            cv2.LINE_AA,
        )
        if selected_target.slow_zone is not None:
            self._draw_zone(canvas, selected_target.slow_zone, color=(255, 180, 0), thickness=1)
        if selected_target.fire_zone is not None:
            self._draw_zone(canvas, selected_target.fire_zone, color=(0, 0, 255), thickness=2)

    def _draw_crosshair(self, canvas: np.ndarray, *, auto_fire_active: bool) -> None:
        height, width = canvas.shape[:2]
        cx, cy = width // 2, height // 2
        color = (0, 0, 255) if auto_fire_active else (0, 255, 0)
        cv2.line(canvas, (cx - 8, cy), (cx + 8, cy), color, 1)
        cv2.line(canvas, (cx, cy - 8), (cx, cy + 8), color, 1)

    def _draw_status(self, canvas: np.ndarray, status: OverlayStatus) -> None:
        delta = (
            "n/a"
            if status.best_target_delta is None
            else f"{status.best_target_delta[0]:.1f},{status.best_target_delta[1]:.1f}"
        )
        lines = [
            f"AIM {'ON' if status.is_aiming else 'OFF'}",
            f"FIRE {'ON' if status.auto_fire_active else 'OFF'}",
            f"BOXES {status.detections_count}",
            f"DELTA {delta}",
        ]
        if status.status_text:
            lines.append(status.status_text)

        y = 18
        for line in lines:
            cv2.putText(
                canvas,
                line,
                (10, y),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.5,
                (255, 255, 255),
                1,
                cv2.LINE_AA,
            )
            y += 18

    def _classify_box(
        self,
        box: np.ndarray,
        source_frame_rgb: np.ndarray,
        target_selector: TargetSelector,
    ) -> tuple[str, tuple[int, int, int]]:
        color_bonus, is_friendly = target_selector._classify_color(box, source_frame_rgb)
        if is_friendly:
            return "friendly", (60, 220, 60)
        if color_bonus > 0.0:
            return "enemy", (0, 215, 255)
        return "neutral", (220, 220, 220)

    @staticmethod
    def _is_selected_box(
        box: np.ndarray,
        selected_target: SelectedTarget | None,
    ) -> bool:
        if selected_target is None or selected_target.selected_box is None:
            return False
        actual = [float(value) for value in box]
        expected = list(selected_target.selected_box)
        return all(abs(lhs - rhs) <= 1e-3 for lhs, rhs in zip(actual, expected))

    @staticmethod
    def _draw_zone(
        canvas: np.ndarray,
        zone: tuple[float, float, float, float],
        *,
        color: tuple[int, int, int],
        thickness: int,
    ) -> None:
        left, top, right, bottom = [int(round(value)) for value in zone]
        cv2.rectangle(canvas, (left, top), (right, bottom), color, thickness)
