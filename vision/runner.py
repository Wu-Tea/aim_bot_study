import os
import time
from dataclasses import dataclass
from pathlib import Path

import win32api

from controllers.base_controller import ControllerTarget

from .capture import ScreenCaptureThread
from .debug_capture import DebugFrameCapture
from .debug_overlay import VisionDebugOverlay
from .enhancement import AimEnhancementPipeline
from .fastpath import _load_model, _warmup_model
from .inference import InferenceThread
from .perf import PerformanceTracker
from .targeting import CrosshairPersonHitDetector, TargetSelector


PROJECT_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_MODEL_PATH = PROJECT_ROOT / "models" / "best.engine"
DEFAULT_FALLBACK_MODEL_PATH = PROJECT_ROOT / "models" / "best.pt"
DEFAULT_DEBUG_CAPTURE_DIR = PROJECT_ROOT / "debug_captures"


class AdsAutoFireGate:
    def __init__(self, delay_seconds: float = 0.12):
        self.delay_seconds = max(0.0, float(delay_seconds))
        self.reset()

    def reset(self):
        self._aim_started_at = None

    def on_aiming(self, is_aiming: bool, timestamp: float):
        if not is_aiming:
            self.reset()
            return
        if self._aim_started_at is None:
            self._aim_started_at = timestamp

    def allow_auto_fire(self, raw_auto_fire_active: bool, timestamp: float):
        if not raw_auto_fire_active:
            return False
        if self._aim_started_at is None:
            return False
        return (timestamp - self._aim_started_at) >= self.delay_seconds


def _env_flag(name: str, default: bool = False):
    value = os.getenv(name)
    if value is None:
        return default
    return value.strip().lower() in {"1", "true", "yes", "on"}


def _env_quit_key(default: int) -> int:
    value = os.getenv("VISION_QUIT_KEY")
    if value is None:
        return default
    normalized = value.strip().lower()
    if normalized in {"", "0", "none", "off", "false", "disabled"}:
        return 0
    if len(normalized) == 1 and not normalized.isdigit():
        return ord(normalized.upper())
    return int(normalized, 0)


def _quit_requested(config) -> bool:
    if config.quit_key_vk <= 0:
        return False
    return bool(win32api.GetAsyncKeyState(config.quit_key_vk) & 0x8000)


@dataclass(slots=True, frozen=True)
class VisionConfig:
    capture_width: int = 640
    capture_height: int = 512
    capture_fps: int = 80
    debug_overlay: bool = False
    debug_save_frames: bool = False
    model_path: str = str(DEFAULT_MODEL_PATH)
    fallback_model_path: str = str(DEFAULT_FALLBACK_MODEL_PATH)
    model_task: str = "detect"
    classes: tuple[int, ...] = (0,)
    conf: float = 0.40
    half: bool = True
    device: int = 0
    warmup_iterations: int = 10
    frame_timeout: float = 0.10
    idle_sleep: float = 0.01
    perf_log_interval: float = 2.0
    quit_key_vk: int = 0x51

    @property
    def image_size(self) -> tuple[int, int]:
        return (self.capture_height, self.capture_width)

    @classmethod
    def from_env(cls):
        defaults = cls()
        legacy_crop_size = os.getenv("VISION_CROP_SIZE")
        capture_width = int(
            os.getenv(
                "VISION_CROP_WIDTH",
                legacy_crop_size if legacy_crop_size is not None else str(defaults.capture_width),
            )
        )
        capture_height = int(
            os.getenv(
                "VISION_CROP_HEIGHT",
                legacy_crop_size if legacy_crop_size is not None else str(defaults.capture_height),
            )
        )
        capture_fps = int(
            os.getenv(
                "VISION_CAPTURE_FPS",
                os.getenv("VISION_TARGET_FPS", str(defaults.capture_fps)),
            )
        )
        model_path = os.getenv("VISION_MODEL_PATH", defaults.model_path)
        fallback_model_path = os.getenv("VISION_FALLBACK_MODEL_PATH", defaults.fallback_model_path)
        return cls(
            capture_width=capture_width,
            capture_height=capture_height,
            capture_fps=capture_fps,
            debug_overlay=_env_flag("VISION_DEBUG_OVERLAY"),
            debug_save_frames=_env_flag("VISION_DEBUG_SAVE"),
            model_path=model_path,
            fallback_model_path=fallback_model_path,
            quit_key_vk=_env_quit_key(defaults.quit_key_vk),
        )


@dataclass(slots=True, frozen=True)
class TrackingFrameResolution:
    selected_target: object | None
    auto_fire_active: bool
    best_target_delta: tuple[float, float] | None
    boxes_seen: int


def _resolve_tracking_frame(
    *,
    frame,
    detections,
    target_selector,
    rb_hit_detector,
    aim_enhancement,
    timestamp: float,
) -> TrackingFrameResolution:
    if frame is None:
        rb_hit_detector.reset()
        aim_enhancement.reset()
        return TrackingFrameResolution(
            selected_target=None,
            auto_fire_active=False,
            best_target_delta=None,
            boxes_seen=0,
        )

    selected_target = target_selector.select_target(detections, frame)
    auto_fire_active = rb_hit_detector.update(selected_target, detections, frame)
    if selected_target is not None:
        best_target_delta = aim_enhancement.process(selected_target, timestamp=timestamp)
    else:
        best_target_delta = None
        aim_enhancement.reset()

    return TrackingFrameResolution(
        selected_target=selected_target,
        auto_fire_active=auto_fire_active,
        best_target_delta=best_target_delta,
        boxes_seen=sum(len(detection.boxes) for detection in detections),
    )


def _controller_target(selected_target) -> ControllerTarget | None:
    if selected_target is None:
        return None
    return ControllerTarget(
        aim_point_x=selected_target.target_x,
        aim_point_y=selected_target.target_y,
        screen_center_x=selected_target.screen_center_x,
        screen_center_y=selected_target.screen_center_y,
        body_box=selected_target.selected_box,
        target_source=getattr(selected_target, "source", None),
    )


def process_vision(controller=None):
    config = VisionConfig.from_env()
    predict_kwargs = {
        "classes": list(config.classes),
        "conf": config.conf,
        "verbose": False,
        "device": config.device,
        "imgsz": config.image_size,
        "max_det": 10,
    }

    model = _load_model(config)
    fast_path = _warmup_model(model, config, predict_kwargs, bench=_env_flag("VISION_BENCH"))
    use_fast_path = fast_path is not None and _env_flag("VISION_FAST_PATH", True)

    capture_thread = ScreenCaptureThread(
        target_fps=config.capture_fps,
        crop_width=config.capture_width,
        crop_height=config.capture_height,
    )
    target_selector = TargetSelector(
        frame_width=config.capture_width,
        frame_height=config.capture_height,
    )
    aim_enhancement = AimEnhancementPipeline()
    rb_hit_detector = CrosshairPersonHitDetector(
        frame_width=config.capture_width,
        frame_height=config.capture_height,
        target_selector=target_selector,
    )
    auto_fire_gate = AdsAutoFireGate(delay_seconds=0.12)
    perf_tracker = PerformanceTracker(enabled=_env_flag("VISION_PERF_LOG"), log_interval=config.perf_log_interval)
    debug_capture = (
        DebugFrameCapture(base_dir=DEFAULT_DEBUG_CAPTURE_DIR, asynchronous=True)
        if config.debug_save_frames
        else None
    )
    debug_overlay = (
        VisionDebugOverlay(
            frame_capture=debug_capture,
            display_window=config.debug_overlay,
        )
        if (config.debug_overlay or config.debug_save_frames)
        else None
    )

    capture_thread.start()
    print(
        "[Vision] "
        f"fast_path={'on' if use_fast_path else 'off'} | "
        f"crop={config.capture_width}x{config.capture_height} | "
        f"capture_fps={config.capture_fps} | "
        f"conf={config.conf:.2f} | half={config.half} | "
        f"debug={'on' if config.debug_overlay else 'off'} | "
        f"debug_save={'on' if config.debug_save_frames else 'off'}"
    )

    inference_thread = InferenceThread(
        capture_thread=capture_thread,
        frame_timeout=config.frame_timeout,
        model=model,
        predict_kwargs=predict_kwargs,
        fast_path=fast_path,
        use_fast_path=use_fast_path,
    )

    last_result_id = 0
    was_aiming = False
    last_frame = None

    inference_thread.start()

    try:
        while True:
            is_aiming = True if controller is None else controller.is_aiming()
            loop_timestamp = time.perf_counter()
            auto_fire_gate.on_aiming(is_aiming, loop_timestamp)
            if not is_aiming:
                if was_aiming:
                    if controller:
                        controller.reset()
                        controller.set_auto_fire(False)
                    target_selector.reset_tracking()
                    aim_enhancement.reset()
                    rb_hit_detector.reset()
                    auto_fire_gate.reset()
                    perf_tracker.reset_window()
                    inference_thread.pause(clear_result=True)
                    last_result_id = 0
                    last_frame = None
                was_aiming = False
                if debug_overlay is not None:
                    debug_overlay.show_message(
                        width=config.capture_width,
                        height=config.capture_height,
                        message="Idle: hold ADS to capture",
                    )
                time.sleep(config.idle_sleep)
                continue

            if not was_aiming:
                perf_tracker.reset_window()
                inference_thread.resume()
                last_result_id = 0
                last_frame = None
            was_aiming = True

            wait_start = time.perf_counter()
            result, last_result_id = inference_thread.get_latest_result(
                last_seen_id=last_result_id,
                timeout=config.frame_timeout,
            )
            wait_ms = (time.perf_counter() - wait_start) * 1000.0

            if result is None:
                gap_frame = last_frame
                resolved = _resolve_tracking_frame(
                    frame=gap_frame,
                    detections=[],
                    target_selector=target_selector,
                    rb_hit_detector=rb_hit_detector,
                    aim_enhancement=aim_enhancement,
                    timestamp=time.perf_counter(),
                )
                selected_target = resolved.selected_target
                best_target_delta = resolved.best_target_delta
                if controller:
                    controller.set_auto_fire(False)
                    if best_target_delta:
                        controller.update(
                            best_target_delta[0],
                            best_target_delta[1],
                            target=_controller_target(selected_target),
                        )
                    else:
                        controller.clear_target()
                if debug_overlay is not None:
                    if gap_frame is None:
                        debug_overlay.show_message(
                            width=config.capture_width,
                            height=config.capture_height,
                            message="Waiting for inference...",
                        )
                    else:
                        debug_overlay.show(
                            frame=gap_frame,
                            detections=[],
                            selected_target=selected_target,
                            target_selector=target_selector,
                            auto_fire_active=False,
                            is_aiming=is_aiming,
                            best_target_delta=best_target_delta,
                            status_text="Inference gap",
                        )
                continue

            frame = result.frame
            last_frame = frame
            detections = result.detections
            infer_ms = result.infer_ms

            post_start = time.perf_counter()
            resolved = _resolve_tracking_frame(
                frame=frame,
                detections=detections,
                target_selector=target_selector,
                rb_hit_detector=rb_hit_detector,
                aim_enhancement=aim_enhancement,
                timestamp=time.perf_counter(),
            )
            selected_target = resolved.selected_target
            auto_fire_active = resolved.auto_fire_active
            best_target_delta = resolved.best_target_delta
            boxes_seen = resolved.boxes_seen
            auto_fire_active = auto_fire_gate.allow_auto_fire(auto_fire_active, time.perf_counter())
            post_ms = (time.perf_counter() - post_start) * 1000.0
            age_ms = (time.perf_counter() - result.captured_at) * 1000.0

            if controller:
                controller.set_auto_fire(auto_fire_active)
                if best_target_delta:
                    controller.update(
                        best_target_delta[0],
                        best_target_delta[1],
                        target=_controller_target(selected_target),
                    )
                else:
                    controller.clear_target()

            if debug_overlay is not None:
                debug_overlay.show(
                    frame=frame,
                    detections=detections,
                    selected_target=selected_target,
                    target_selector=target_selector,
                    auto_fire_active=auto_fire_active,
                    is_aiming=is_aiming,
                    best_target_delta=best_target_delta,
                )

            perf_tracker.update(
                wait_ms=wait_ms,
                infer_ms=infer_ms,
                post_ms=post_ms,
                boxes_seen=boxes_seen,
                age_ms=age_ms,
                tracking_active=selected_target is not None,
            )

            if _quit_requested(config):
                print("[Vision] Quit hotkey requested; stopping.")
                break
    finally:
        print("Stopping vision processing.")
        if controller:
            controller.set_auto_fire(False)
            controller.reset()
        if debug_overlay is not None:
            debug_overlay.close()
        inference_thread.stop()
        inference_thread.join(timeout=1.0)
        capture_thread.stop()
        capture_thread.join(timeout=1.0)
