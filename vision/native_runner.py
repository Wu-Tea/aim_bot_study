import os
import sys
import time
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path

import cv2
import numpy as np
import win32api

from controllers.base_controller import ControllerTarget

from .debug_capture import DebugFrameCapture
from .perf import PerformanceTracker
from .runner import AdsAutoFireGate, DEFAULT_DEBUG_CAPTURE_DIR, PROJECT_ROOT, VisionConfig, _env_flag


NATIVE_BUILD_DIR = PROJECT_ROOT / "native" / "vision_native" / "build" / "Release"

_DLL_DIRECTORY_HANDLES = []
_ADDED_DLL_PATHS: set[str] = set()


def _clamp_timing_delta(total_ms: float, part_ms: float) -> float:
    return max(round(total_ms - part_ms, 6), 0.0)


def _fps_to_interval_ms(fps: float) -> float:
    if fps <= 0.0:
        return 0.0
    return 1000.0 / float(fps)


def _default_tensorrt_root() -> Path:
    return Path(os.getenv("TensorRT_ROOT", os.getenv("TENSORRT_ROOT", r"D:\env\TensorRT-10.15.1.29")))


def _default_cuda_path() -> Path:
    return Path(os.getenv("CUDA_PATH", r"C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.1"))


@dataclass(slots=True, frozen=True)
class NativeVisionRuntimePaths:
    build_dir: Path = NATIVE_BUILD_DIR
    tensorrt_root: Path = field(default_factory=_default_tensorrt_root)
    cuda_path: Path = field(default_factory=_default_cuda_path)

    @property
    def tensorrt_bin(self) -> Path:
        return self.tensorrt_root / "bin"

    @property
    def cuda_bin(self) -> Path:
        return self.cuda_path / "bin"


class NativeVisionDebugOverlay:
    def __init__(
        self,
        *,
        width: int,
        height: int,
        window_name: str = "Native Vision Debug",
        frame_capture: DebugFrameCapture | None = None,
        display_window: bool = True,
    ):
        self.width = int(width)
        self.height = int(height)
        self.window_name = window_name
        self.frame_capture = frame_capture
        self.display_window = display_window
        self._enabled = True
        self._window_initialized = False

    def render_result(
        self,
        result: dict,
        *,
        is_aiming: bool,
        auto_fire_active: bool,
        status_text: str | None = None,
        frame_bgr: np.ndarray | None = None,
    ) -> np.ndarray:
        canvas = self._base_canvas(frame_bgr)
        self._draw_crosshair(canvas, auto_fire_active=auto_fire_active)
        self._draw_scan_boxes(canvas, result)

        if result.get("has_body_box"):
            self._draw_body_box(canvas, result)
        if self._has_torso_box(result):
            self._draw_torso_box(canvas, result)
        self._draw_tracking_debug(canvas, result)
        self._draw_center_cue_debug(canvas, result)
        if result.get("has_target"):
            self._draw_target(canvas, result)

        self._draw_status(
            canvas,
            result,
            is_aiming=is_aiming,
            auto_fire_active=auto_fire_active,
            status_text=status_text,
        )
        return canvas

    def show_result(
        self,
        result: dict,
        *,
        is_aiming: bool,
        auto_fire_active: bool,
        status_text: str | None = None,
        frame_bgr: np.ndarray | None = None,
    ) -> None:
        if not self._enabled and self.frame_capture is None:
            return

        canvas = self.render_result(
            result,
            is_aiming=is_aiming,
            auto_fire_active=auto_fire_active,
            status_text=status_text,
            frame_bgr=frame_bgr,
        )
        if self.frame_capture is not None and result.get("has_target"):
            self.frame_capture.save_frame(
                frame_bgr=canvas,
                detections_count=int(result.get("boxes_seen", 0)),
                has_selected_target=bool(result.get("has_target")),
                auto_fire_active=auto_fire_active,
                timestamp_text=datetime.now().strftime("%Y-%m-%d_%H-%M-%S_%f"),
            )
        if self.display_window and self._enabled:
            self._present(canvas)

    def show_message(self, message: str) -> None:
        if not self._enabled or not self.display_window:
            return

        result = {
            "has_target": False,
            "auto_fire": False,
            "dx": 0.0,
            "dy": 0.0,
            "target_x": self.width * 0.5,
            "target_y": self.height * 0.5,
            "screen_center_x": self.width * 0.5,
            "screen_center_y": self.height * 0.5,
            "target_source": "idle",
            "boxes_seen": 0,
        }
        self.show_result(
            result,
            is_aiming=False,
            auto_fire_active=False,
            status_text=message,
        )

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
            if not self._window_initialized:
                cv2.namedWindow(self.window_name, cv2.WINDOW_NORMAL | cv2.WINDOW_KEEPRATIO)
                try:
                    cv2.setWindowProperty(self.window_name, cv2.WND_PROP_TOPMOST, 1)
                except cv2.error:
                    pass
                cv2.resizeWindow(self.window_name, self.width, self.height)
                self._window_initialized = True
            cv2.imshow(self.window_name, frame_bgr)
            cv2.waitKey(1)
        except cv2.error as exc:
            print(f"[NativeVisionDebug] Disabled overlay window: {exc}")
            self._enabled = False
            self.close()

    def _base_canvas(self, frame_bgr: np.ndarray | None) -> np.ndarray:
        if frame_bgr is not None and frame_bgr.size > 0:
            canvas = np.ascontiguousarray(frame_bgr.copy())
            if canvas.shape[:2] != (self.height, self.width):
                canvas = cv2.resize(canvas, (self.width, self.height), interpolation=cv2.INTER_LINEAR)
            return canvas
        canvas = np.zeros((self.height, self.width, 3), dtype=np.uint8)
        canvas[:] = (18, 18, 18)
        self._draw_grid(canvas)
        return canvas

    def _draw_grid(self, canvas: np.ndarray) -> None:
        for x in range(0, self.width, 80):
            cv2.line(canvas, (x, 0), (x, self.height), (32, 32, 32), 1)
        for y in range(0, self.height, 64):
            cv2.line(canvas, (0, y), (self.width, y), (32, 32, 32), 1)

    def _draw_crosshair(self, canvas: np.ndarray, *, auto_fire_active: bool) -> None:
        cx = self.width // 2
        cy = self.height // 2
        color = (0, 0, 255) if auto_fire_active else (0, 255, 0)
        cv2.line(canvas, (cx - 12, cy), (cx + 12, cy), color, 1)
        cv2.line(canvas, (cx, cy - 12), (cx, cy + 12), color, 1)
        cv2.circle(canvas, (cx, cy), 18, color, 1)

    def _draw_body_box(self, canvas: np.ndarray, result: dict) -> None:
        x1 = int(round(float(result.get("body_x1", 0.0))))
        y1 = int(round(float(result.get("body_y1", 0.0))))
        x2 = int(round(float(result.get("body_x2", 0.0))))
        y2 = int(round(float(result.get("body_y2", 0.0))))
        color = (0, 215, 255) if result.get("target_source") == "observed" else (255, 180, 0)
        cv2.rectangle(canvas, (x1, y1), (x2, y2), color, 2)

    def _has_torso_box(self, result: dict) -> bool:
        return (
            float(result.get("torso_x2", 0.0)) > float(result.get("torso_x1", 0.0))
            and float(result.get("torso_y2", 0.0)) > float(result.get("torso_y1", 0.0))
        )

    def _draw_torso_box(self, canvas: np.ndarray, result: dict) -> None:
        x1 = int(round(float(result.get("torso_x1", 0.0))))
        y1 = int(round(float(result.get("torso_y1", 0.0))))
        x2 = int(round(float(result.get("torso_x2", 0.0))))
        y2 = int(round(float(result.get("torso_y2", 0.0))))
        cv2.rectangle(canvas, (x1, y1), (x2, y2), (255, 120, 0), 1)

    def _draw_scan_boxes(self, canvas: np.ndarray, result: dict) -> None:
        boxes = result.get("debug_scan_boxes", []) or []
        for index in range(0, len(boxes), 5):
            if index + 4 >= len(boxes):
                break
            x1 = int(round(float(boxes[index + 0])))
            y1 = int(round(float(boxes[index + 1])))
            x2 = int(round(float(boxes[index + 2])))
            y2 = int(round(float(boxes[index + 3])))
            conf = float(boxes[index + 4])
            cv2.rectangle(canvas, (x1, y1), (x2, y2), (0, 220, 220), 1)
            cv2.putText(
                canvas,
                f"{conf:.2f}",
                (x1, max(14, y1 - 3)),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.4,
                (0, 220, 220),
                1,
                cv2.LINE_AA,
            )

    def _draw_tracking_debug(self, canvas: np.ndarray, result: dict) -> None:
        sx1 = float(result.get("debug_search_x1", 0.0))
        sy1 = float(result.get("debug_search_y1", 0.0))
        sx2 = float(result.get("debug_search_x2", 0.0))
        sy2 = float(result.get("debug_search_y2", 0.0))
        if sx2 > sx1 and sy2 > sy1:
            cv2.rectangle(
                canvas,
                (int(round(sx1)), int(round(sy1))),
                (int(round(sx2)), int(round(sy2))),
                (255, 0, 255),
                1,
            )

        predicted_x = float(result.get("debug_predicted_x", 0.0))
        predicted_y = float(result.get("debug_predicted_y", 0.0))
        if predicted_x > 0.0 and predicted_y > 0.0:
            px = int(round(predicted_x))
            py = int(round(predicted_y))
            cv2.drawMarker(canvas, (px, py), (255, 0, 0), markerType=cv2.MARKER_CROSS, markerSize=10, thickness=1)

        if bool(result.get("debug_patch_valid")):
            patch_x = int(round(float(result.get("debug_patch_x", 0.0))))
            patch_y = int(round(float(result.get("debug_patch_y", 0.0))))
            cv2.drawMarker(canvas, (patch_x, patch_y), (0, 220, 255), markerType=cv2.MARKER_TILTED_CROSS, markerSize=10, thickness=1)

        points = result.get("debug_track_points", []) or []
        for index in range(0, len(points), 2):
            if index + 1 >= len(points):
                break
            x = int(round(float(points[index + 0])))
            y = int(round(float(points[index + 1])))
            cv2.circle(canvas, (x, y), 2, (0, 255, 0), -1)

        template_w = int(round(float(result.get("debug_template_w", 0.0))))
        template_h = int(round(float(result.get("debug_template_h", 0.0))))
        anchor_x = int(round(float(result.get("target_x", 0.0))))
        anchor_y = int(round(float(result.get("target_y", 0.0))))
        if template_w > 0 and template_h > 0 and anchor_x > 0 and anchor_y > 0:
            half_w = max(1, template_w // 2)
            half_h = max(1, template_h // 2)
            cv2.rectangle(
                canvas,
                (anchor_x - half_w, anchor_y - half_h),
                (anchor_x + half_w, anchor_y + half_h),
                (180, 180, 255),
                1,
            )

    def _draw_center_cue_debug(self, canvas: np.ndarray, result: dict) -> None:
        if bool(result.get("yellow_cue_present")):
            x1 = int(round(float(result.get("yellow_roi_x1", 0.0))))
            y1 = int(round(float(result.get("yellow_roi_y1", 0.0))))
            x2 = int(round(float(result.get("yellow_roi_x2", 0.0))))
            y2 = int(round(float(result.get("yellow_roi_y2", 0.0))))
            if x2 > x1 and y2 > y1:
                cv2.rectangle(canvas, (x1, y1), (x2, y2), (0, 255, 255), 1)

            cue_x = int(round(float(result.get("yellow_cue_x", 0.0))))
            cue_y = int(round(float(result.get("yellow_cue_y", 0.0))))
            if cue_x > 0 and cue_y > 0:
                cv2.circle(canvas, (cue_x, cue_y), 3, (0, 255, 255), -1)

        if bool(result.get("refiner_applied")):
            refined_x = int(round(float(result.get("refined_target_x", 0.0))))
            refined_y = int(round(float(result.get("refined_target_y", 0.0))))
            if refined_x > 0 and refined_y > 0:
                cv2.drawMarker(
                    canvas,
                    (refined_x, refined_y),
                    (0, 255, 255),
                    markerType=cv2.MARKER_DIAMOND,
                    markerSize=10,
                    thickness=1,
                )

    def _draw_target(self, canvas: np.ndarray, result: dict) -> None:
        tx = int(round(float(result.get("target_x", self.width * 0.5))))
        ty = int(round(float(result.get("target_y", self.height * 0.5))))
        cx = int(round(float(result.get("screen_center_x", self.width * 0.5))))
        cy = int(round(float(result.get("screen_center_y", self.height * 0.5))))
        source = str(result.get("target_source", "observed"))
        if source in {"observed", "torso_anchor"}:
            color = (0, 0, 255)
        elif source == "torso_prior":
            color = (255, 120, 0)
        else:
            color = (255, 180, 0)
        cv2.line(canvas, (cx, cy), (tx, ty), color, 1)
        cv2.circle(canvas, (tx, ty), 6, color, 2)
        cv2.putText(
            canvas,
            f"{source} dx={float(result.get('dx', 0.0)):.1f} dy={float(result.get('dy', 0.0)):.1f}",
            (tx + 8, max(18, ty - 8)),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.45,
            color,
            1,
            cv2.LINE_AA,
        )

    def _draw_status(
        self,
        canvas: np.ndarray,
        result: dict,
        *,
        is_aiming: bool,
        auto_fire_active: bool,
        status_text: str | None,
    ) -> None:
        capture_ms = float(result.get("capture_ms", result.get("roi_ms", result.get("wait_ms", 0.0))))
        acquire_ms = float(result.get("acquire_ms", 0.0))
        copy_ms = float(result.get("copy_ms", max(capture_ms - acquire_ms, 0.0)))
        preprocess_ms = float(result.get("preprocess_ms", 0.0))
        infer_ms = float(result.get("infer_ms", result.get("yolo_ms", 0.0)))
        decode_ms = float(result.get("decode_ms", 0.0))
        total_post_ms = float(result.get("post_ms", 0.0))
        post_cpu_ms = _clamp_timing_delta(total_post_ms, decode_ms)
        lines = [
            "NATIVE VISION DEBUG",
            f"AIM {'ON' if is_aiming else 'OFF'} | FIRE {'ON' if auto_fire_active else 'OFF'}",
            f"TARGET {'ON' if result.get('has_target') else 'OFF'} | BOXES {float(result.get('boxes_seen', 0.0)):.1f}",
            (
                f"engine={str(result.get('engine_mode', 'idle'))} "
                f"scan={'Y' if result.get('scan_ran') else 'N'} "
                f"age={float(result.get('scan_age_ms', 0.0)):.1f}ms "
                f"reason={str(result.get('scan_reason', ''))}"
            ),
            (
                f"mode={str(result.get('body_state_mode', 'drop'))} "
                f"src={str(result.get('anchor_source', result.get('target_source', 'none')))} "
                f"anchor={float(result.get('anchor_confidence', 0.0)):.2f} "
                f"ego={float(result.get('ego_confidence', 0.0)):.2f}"
            ),
            (
                f"ego_model={str(result.get('ego_model', 'identity'))} "
                f"keyframe={float(result.get('keyframe_age_ms', 0.0)):.1f}ms "
                f"prewarm={'Y' if result.get('prewarm_used') else 'N'}"
            ),
            (
                f"patch={int(round(float(result.get('debug_template_w', 0.0))))}x"
                f"{int(round(float(result.get('debug_template_h', 0.0))))} "
                f"pts={len(result.get('debug_track_points', []) or []) // 2} "
                f"patch_valid={'Y' if result.get('debug_patch_valid') else 'N'}"
            ),
            (
                f"yellow={'Y' if result.get('yellow_cue_present') else 'N'} "
                f"score={float(result.get('yellow_cue_score', 0.0)):.2f} "
                f"refined={'Y' if result.get('refiner_applied') else 'N'}"
            ),
            (
                f"capture={capture_ms:.1f} "
                f"acquire={acquire_ms:.1f} "
                f"copy={copy_ms:.1f} "
                f"pre={preprocess_ms:.1f} "
                f"infer={infer_ms:.1f}"
            ),
            (
                f"decode={decode_ms:.1f} "
                f"post={post_cpu_ms:.1f} "
                f"age={float(result.get('age_ms', 0.0)):.1f}ms"
            ),
        ]
        if status_text:
            lines.append(status_text)

        y = 20
        for line in lines:
            cv2.putText(
                canvas,
                line,
                (10, y),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.5,
                (235, 235, 235),
                1,
                cv2.LINE_AA,
            )
            y += 20


def _prepend_env_path(path: Path) -> None:
    if not path.exists():
        return
    path_text = str(path)
    current_parts = os.environ.get("PATH", "").split(os.pathsep)
    if path_text not in current_parts:
        os.environ["PATH"] = path_text + os.pathsep + os.environ.get("PATH", "")


def _add_dll_directory(path: Path) -> None:
    if not path.exists() or not hasattr(os, "add_dll_directory"):
        return
    resolved = str(path.resolve())
    if resolved in _ADDED_DLL_PATHS:
        return
    _DLL_DIRECTORY_HANDLES.append(os.add_dll_directory(resolved))
    _ADDED_DLL_PATHS.add(resolved)


def _configure_native_runtime_paths(paths: NativeVisionRuntimePaths | None = None) -> NativeVisionRuntimePaths:
    paths = paths or NativeVisionRuntimePaths()
    os.environ.setdefault("TensorRT_ROOT", str(paths.tensorrt_root))
    os.environ.setdefault("CUDA_PATH", str(paths.cuda_path))
    os.environ.setdefault("CudaToolkitDir", str(paths.cuda_path))

    for path in (paths.build_dir, paths.tensorrt_bin, paths.cuda_bin):
        _prepend_env_path(path)
        _add_dll_directory(path)

    build_dir_text = str(paths.build_dir)
    if paths.build_dir.exists() and build_dir_text not in sys.path:
        sys.path.insert(0, build_dir_text)
    return paths


def _load_native_module():
    paths = _configure_native_runtime_paths()
    try:
        import vision_native_cpp  # type: ignore

        return vision_native_cpp
    except ImportError as exc:
        raise RuntimeError(
            "Native vision module is unavailable. Run "
            "tools\\build_native_vision.ps1, then retry gamepad_native_debug.bat. "
            f"Expected module under {paths.build_dir}."
        ) from exc


def _controller_target_from_native_result(result: dict) -> ControllerTarget | None:
    body_box = None
    if result.get("has_body_box"):
        body_box = (
            float(result.get("body_x1", 0.0)),
            float(result.get("body_y1", 0.0)),
            float(result.get("body_x2", 0.0)),
            float(result.get("body_y2", 0.0)),
        )
    return ControllerTarget(
        aim_point_x=float(result.get("target_x", 0.0)),
        aim_point_y=float(result.get("target_y", 0.0)),
        screen_center_x=float(result.get("screen_center_x", 0.0)),
        screen_center_y=float(result.get("screen_center_y", 0.0)),
        body_box=body_box,
        target_source=result.get("target_source"),
    )


def _native_timeout_ms(config: VisionConfig) -> int:
    if config.capture_fps <= 0:
        return max(1, int(config.frame_timeout * 1000.0))
    return max(1, int(round(1000.0 / float(config.capture_fps))))


def _quit_requested(config: VisionConfig) -> bool:
    if config.quit_key_vk <= 0:
        return False
    return bool(win32api.GetAsyncKeyState(config.quit_key_vk) & 0x8000)


def _native_stage_metrics(result: dict) -> dict[str, float]:
    capture_ms = float(result.get("capture_ms", result.get("roi_ms", result.get("wait_ms", 0.0))))
    acquire_ms = float(result.get("acquire_ms", 0.0))
    copy_ms = float(result.get("copy_ms", max(capture_ms - acquire_ms, 0.0)))
    preprocess_ms = float(result.get("preprocess_ms", 0.0))
    infer_ms = float(result.get("infer_ms", result.get("yolo_ms", 0.0)))
    decode_ms = float(result.get("decode_ms", 0.0))
    total_post_ms = float(result.get("post_ms", 0.0))
    post_cpu_ms = _clamp_timing_delta(total_post_ms, decode_ms)
    return {
        "capture": capture_ms,
        "acquire": acquire_ms,
        "copy": copy_ms,
        "pre": preprocess_ms,
        "infer": infer_ms,
        "decode": decode_ms,
        "post": post_cpu_ms,
    }


def process_native_vision(controller=None):
    config = VisionConfig.from_env()
    native_module = _load_native_module()
    timeout_ms = _native_timeout_ms(config)
    engine = native_module.NativeVisionEngine(
        config.capture_width,
        config.capture_height,
        0,
        -1,
        timeout_ms,
        _fps_to_interval_ms(config.track_fps),
        _fps_to_interval_ms(config.warm_scan_fps),
        _fps_to_interval_ms(config.scan_fps),
        _fps_to_interval_ms(config.recovery_scan_fps),
    )
    auto_fire_gate = AdsAutoFireGate(delay_seconds=0.12)
    perf_tracker = PerformanceTracker(enabled=_env_flag("VISION_PERF_LOG"), log_interval=config.perf_log_interval)
    debug_capture = (
        DebugFrameCapture(base_dir=DEFAULT_DEBUG_CAPTURE_DIR / "native", asynchronous=True)
        if config.debug_save_frames
        else None
    )
    debug_overlay = (
        NativeVisionDebugOverlay(
            width=config.capture_width,
            height=config.capture_height,
            frame_capture=debug_capture,
            display_window=config.debug_overlay,
        )
        if (config.debug_overlay or config.debug_save_frames)
        else None
    )

    print(
        "[Vision][Native] "
        f"crop={config.capture_width}x{config.capture_height} | "
        f"capture_fps={config.capture_fps} | "
        f"track_fps={config.track_fps:.1f} | "
        f"warm_scan_fps={config.warm_scan_fps:.1f} | "
        f"scan_fps={config.scan_fps:.1f} | "
        f"recovery_scan_fps={config.recovery_scan_fps:.1f} | "
        f"timeout={timeout_ms}ms | "
        f"debug={'on' if config.debug_overlay else 'off'} | "
        f"debug_save={'on' if config.debug_save_frames else 'off'}"
    )

    current_mode = None
    frame_interval = 1.0 / float(config.capture_fps) if config.capture_fps > 0 else 0.0

    try:
        while True:
            loop_start = time.perf_counter()
            is_aiming = True if controller is None else controller.is_aiming()
            auto_fire_gate.on_aiming(is_aiming, loop_start)
            desired_mode = "active_track" if is_aiming else "warm_scan"
            if desired_mode != current_mode:
                if hasattr(engine, "set_mode"):
                    engine.set_mode(desired_mode)
                else:
                    engine.set_aiming(is_aiming)
                if desired_mode == "active_track":
                    perf_tracker.reset_window()
                else:
                    auto_fire_gate.reset()
                    perf_tracker.reset_window()
                    if controller:
                        controller.set_auto_fire(False)
                        if current_mode == "active_track":
                            controller.clear_target()
                current_mode = desired_mode

            result = engine.poll_once()
            result["capture_ms"] = float(result.get("capture_ms", result.get("wait_ms", 0.0)))
            result["roi_ms"] = result["capture_ms"]
            result["yolo_ms"] = float(result.get("yolo_ms", result.get("infer_ms", 0.0)))
            has_target = bool(result.get("has_target")) and is_aiming
            auto_fire_active = (
                auto_fire_gate.allow_auto_fire(
                    bool(result.get("auto_fire")),
                    time.perf_counter(),
                )
                if is_aiming
                else False
            )

            if controller:
                controller.set_auto_fire(auto_fire_active)
                if is_aiming and has_target:
                    controller.update(
                        float(result.get("dx", 0.0)),
                        float(result.get("dy", 0.0)),
                        target=_controller_target_from_native_result(result),
                    )
                elif is_aiming:
                    controller.clear_target()

            if debug_overlay is not None:
                debug_frame_bgr = None
                if hasattr(engine, "get_debug_frame_bgr"):
                    try:
                        debug_frame_bgr = engine.get_debug_frame_bgr()
                    except Exception:
                        debug_frame_bgr = None
                debug_overlay.show_result(
                    result,
                    is_aiming=is_aiming,
                    auto_fire_active=auto_fire_active,
                    frame_bgr=debug_frame_bgr,
                )

            perf_tracker.update(
                stage_ms=_native_stage_metrics(result),
                boxes_seen=int(float(result.get("boxes_seen", 0.0))),
                age_ms=float(result.get("age_ms", 0.0)),
                tracking_active=has_target,
            )

            if _quit_requested(config):
                print("[Vision][Native] Quit hotkey requested; stopping.")
                break

            if frame_interval > 0.0:
                elapsed = time.perf_counter() - loop_start
                remaining = frame_interval - elapsed
                if remaining > 0.0:
                    time.sleep(remaining)
    finally:
        print("Stopping native vision processing.")
        try:
            if hasattr(engine, "set_mode"):
                engine.set_mode("idle")
            else:
                engine.set_aiming(False)
            engine.reset()
        except Exception:
            pass
        if controller:
            controller.set_auto_fire(False)
            controller.reset()
        if debug_overlay is not None:
            debug_overlay.close()
