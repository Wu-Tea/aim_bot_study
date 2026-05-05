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
from .yellow_cue import ScreenCaptureCueProvider


NATIVE_BUILD_DIR = PROJECT_ROOT / "native" / "vision_native" / "build" / "Release"

_DLL_DIRECTORY_HANDLES = []
_ADDED_DLL_PATHS: set[str] = set()


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
    ) -> np.ndarray:
        canvas = np.zeros((self.height, self.width, 3), dtype=np.uint8)
        canvas[:] = (18, 18, 18)
        self._draw_grid(canvas)
        self._draw_crosshair(canvas, auto_fire_active=auto_fire_active)

        if result.get("has_body_box"):
            self._draw_body_box(canvas, result)
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
    ) -> None:
        if not self._enabled and self.frame_capture is None:
            return

        canvas = self.render_result(
            result,
            is_aiming=is_aiming,
            auto_fire_active=auto_fire_active,
            status_text=status_text,
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

    def _draw_target(self, canvas: np.ndarray, result: dict) -> None:
        tx = int(round(float(result.get("target_x", self.width * 0.5))))
        ty = int(round(float(result.get("target_y", self.height * 0.5))))
        cx = int(round(float(result.get("screen_center_x", self.width * 0.5))))
        cy = int(round(float(result.get("screen_center_y", self.height * 0.5))))
        source = str(result.get("target_source", "observed"))
        color = (0, 0, 255) if source == "observed" else (255, 180, 0)
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
        lines = [
            "NATIVE VISION (synthetic canvas)",
            f"AIM {'ON' if is_aiming else 'OFF'} | FIRE {'ON' if auto_fire_active else 'OFF'}",
            f"TARGET {'ON' if result.get('has_target') else 'OFF'} | BOXES {float(result.get('boxes_seen', 0.0)):.1f}",
            (
                f"CUE {'Y' if result.get('has_external_cue') else 'N'} "
                f"x={float(result.get('external_cue_x', 0.0)):.1f} "
                f"y={float(result.get('external_cue_y', 0.0)):.1f} "
                f"s={float(result.get('external_cue_score', 0.0)):.2f}"
            ),
            (
                f"wait={float(result.get('wait_ms', 0.0)):.1f} "
                f"pre={float(result.get('preprocess_ms', 0.0)):.1f} "
                f"copy={float(result.get('color_copy_ms', 0.0)):.1f} "
                f"infer={float(result.get('infer_ms', 0.0)):.1f} "
                f"post={float(result.get('post_ms', 0.0)):.1f} "
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
        target_source=(
            str(result.get("target_source"))
            if result.get("target_source") not in (None, "")
            else None
        ),
    )


def _resolve_cue_provider(controller, cue_provider):
    if cue_provider is not None:
        return cue_provider
    if controller is None:
        return None

    controller_dict = getattr(controller, "__dict__", {})

    for attr_name in ("get_external_cue", "get_targeting_cue", "get_yellow_cue"):
        type_candidate = getattr(type(controller), attr_name, None)
        if type_candidate is None:
            continue
        candidate = getattr(controller, attr_name, None)
        if callable(candidate):
            return candidate

    for attr_name in ("external_cue_provider", "cue_provider"):
        candidate = controller_dict.get(attr_name)
        if callable(candidate):
            return candidate

    return None


def _create_default_cue_provider(config: VisionConfig):
    if not _env_flag("VISION_NATIVE_CUE_SIDECAR", True):
        return None

    cue_fps = int(os.getenv("VISION_EXTERNAL_CUE_FPS", str(config.capture_fps)))
    stale_after_seconds = float(os.getenv("VISION_EXTERNAL_CUE_STALE_SECONDS", "0.08"))
    return ScreenCaptureCueProvider(
        capture_width=config.capture_width,
        capture_height=config.capture_height,
        target_fps=cue_fps,
        stale_after_seconds=stale_after_seconds,
    )


def _apply_external_cue(engine, cue_provider) -> None:
    if cue_provider is None:
        return

    cue = cue_provider()
    if cue is None:
        engine.set_external_cue(False, 0.0, 0.0, 0.0)
        return

    if isinstance(cue, dict):
        engine.set_external_cue(
            bool(cue.get("found", True)),
            float(cue.get("x", 0.0)),
            float(cue.get("y", 0.0)),
            float(cue.get("score", 0.0)),
        )
        return

    if isinstance(cue, (tuple, list)):
        if len(cue) == 4:
            found, x, y, score = cue
            engine.set_external_cue(bool(found), float(x), float(y), float(score))
            return
        if len(cue) == 3:
            x, y, score = cue
            engine.set_external_cue(True, float(x), float(y), float(score))
            return
        if len(cue) == 2:
            x, y = cue
            engine.set_external_cue(True, float(x), float(y), 0.0)
            return

    raise TypeError("cue_provider must return None, dict, or tuple/list cue data")


def _native_timeout_ms(config: VisionConfig) -> int:
    if config.capture_fps <= 0:
        return max(1, int(config.frame_timeout * 1000.0))
    return max(1, int(round(1000.0 / float(config.capture_fps))))


def _quit_requested(config: VisionConfig) -> bool:
    if config.quit_key_vk <= 0:
        return False
    return bool(win32api.GetAsyncKeyState(config.quit_key_vk) & 0x8000)


def process_native_vision(controller=None, cue_provider=None):
    config = VisionConfig.from_env()
    native_module = _load_native_module()
    timeout_ms = _native_timeout_ms(config)
    engine = native_module.NativeVisionEngine(
        config.capture_width,
        config.capture_height,
        0,
        -1,
        timeout_ms,
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

    resolved_cue_provider = _resolve_cue_provider(controller, cue_provider)
    owned_cue_provider = None
    cue_mode = "explicit" if cue_provider is not None else "controller"
    if resolved_cue_provider is None:
        owned_cue_provider = _create_default_cue_provider(config)
        resolved_cue_provider = owned_cue_provider
        cue_mode = "sidecar" if resolved_cue_provider is not None else "off"

    print(
        "[Vision][Native] "
        f"crop={config.capture_width}x{config.capture_height} | "
        f"capture_fps={config.capture_fps} | timeout={timeout_ms}ms | "
        f"cue={cue_mode} | "
        f"debug={'on' if config.debug_overlay else 'off'} | "
        f"debug_save={'on' if config.debug_save_frames else 'off'}"
    )

    was_aiming = False
    frame_interval = 1.0 / float(config.capture_fps) if config.capture_fps > 0 else 0.0

    try:
        while True:
            loop_start = time.perf_counter()
            is_aiming = True if controller is None else controller.is_aiming()
            auto_fire_gate.on_aiming(is_aiming, loop_start)

            if not is_aiming:
                if was_aiming:
                    if controller:
                        controller.set_auto_fire(False)
                        controller.reset()
                    engine.set_aiming(False)
                    engine.set_external_cue(False, 0.0, 0.0, 0.0)
                    engine.reset()
                    auto_fire_gate.reset()
                    perf_tracker.reset_window()
                was_aiming = False
                if debug_overlay is not None:
                    debug_overlay.show_message("Idle: hold ADS to run native vision")
                time.sleep(config.idle_sleep)
                if _quit_requested(config):
                    print("[Vision][Native] Quit hotkey requested; stopping.")
                    break
                continue

            if not was_aiming:
                perf_tracker.reset_window()
                engine.set_aiming(True)
            was_aiming = True

            _apply_external_cue(engine, resolved_cue_provider)
            result = engine.poll_once()
            has_target = bool(result.get("has_target"))
            auto_fire_active = auto_fire_gate.allow_auto_fire(
                bool(result.get("auto_fire")),
                time.perf_counter(),
            )

            if controller:
                controller.set_auto_fire(auto_fire_active)
                if has_target:
                    controller.update(
                        float(result.get("dx", 0.0)),
                        float(result.get("dy", 0.0)),
                        target=_controller_target_from_native_result(result),
                    )
                else:
                    controller.reset()

            if debug_overlay is not None:
                debug_overlay.show_result(
                    result,
                    is_aiming=is_aiming,
                    auto_fire_active=auto_fire_active,
                )

            perf_tracker.update(
                wait_ms=float(result.get("wait_ms", 0.0)),
                preprocess_ms=float(result.get("preprocess_ms", 0.0)),
                color_copy_ms=float(result.get("color_copy_ms", 0.0)),
                infer_ms=float(result.get("infer_ms", 0.0)),
                post_ms=float(result.get("post_ms", 0.0)),
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
            engine.set_aiming(False)
            engine.reset()
        except Exception:
            pass
        if owned_cue_provider is not None:
            owned_cue_provider.close()
        if controller:
            controller.set_auto_fire(False)
            controller.reset()
        if debug_overlay is not None:
            debug_overlay.close()
