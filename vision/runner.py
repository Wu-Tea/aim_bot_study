import os
import time
from dataclasses import dataclass
from pathlib import Path

import win32api

from .capture import ScreenCaptureThread
from .enhancement import AimEnhancementPipeline
from .fastpath import _extract_detections, _fast_predict, _load_model, _warmup_model
from .perf import PerformanceTracker
from .targeting import CrosshairPersonHitDetector, TargetSelector


PROJECT_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_MODEL_PATH = PROJECT_ROOT / "models" / "yolo26n.engine"
DEFAULT_FALLBACK_MODEL_PATH = PROJECT_ROOT / "models" / "yolo26n.pt"


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


@dataclass(slots=True, frozen=True)
class VisionConfig:
    capture_width: int = 896
    capture_height: int = 512
    capture_fps: int = 70
    model_path: str = str(DEFAULT_MODEL_PATH)
    fallback_model_path: str = str(DEFAULT_FALLBACK_MODEL_PATH)
    model_task: str = "detect"
    classes: tuple[int, ...] = (0,)
    conf: float = 0.35
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
        return cls(
            capture_width=capture_width,
            capture_height=capture_height,
            capture_fps=capture_fps,
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

    capture_thread.start()
    print(
        "[Vision] "
        f"fast_path={'on' if use_fast_path else 'off'} | "
        f"crop={config.capture_width}x{config.capture_height} | "
        f"capture_fps={config.capture_fps} | "
        f"conf={config.conf:.2f} | half={config.half}"
    )

    last_frame_id = 0
    was_aiming = False

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
                was_aiming = False
                time.sleep(config.idle_sleep)
                continue

            if not was_aiming:
                perf_tracker.reset_window()
            was_aiming = True

            capture_wait_start = time.perf_counter()
            frame, last_frame_id = capture_thread.get_latest_frame(last_seen_id=last_frame_id, timeout=config.frame_timeout)
            capture_wait_ms = (time.perf_counter() - capture_wait_start) * 1000.0

            if frame is None:
                if controller:
                    controller.set_auto_fire(False)
                    controller.reset()
                aim_enhancement.reset()
                target_selector.reset_tracking()
                rb_hit_detector.reset()
                continue

            inference_start = time.perf_counter()
            if use_fast_path:
                detections = _fast_predict(fast_path, frame)
            else:
                detections = _extract_detections(model.predict(source=frame[:, :, ::-1].copy(), **predict_kwargs))
            infer_ms = (time.perf_counter() - inference_start) * 1000.0

            post_start = time.perf_counter()
            if detections:
                selected_target = target_selector.select_target(detections, frame)
                auto_fire_active = rb_hit_detector.update(selected_target, detections, frame)
                if selected_target is not None:
                    best_target_delta = aim_enhancement.process(selected_target, timestamp=time.perf_counter())
                else:
                    best_target_delta = None
                    aim_enhancement.reset()
                boxes_seen = sum(len(detection.boxes) for detection in detections)
            else:
                selected_target = None
                auto_fire_active = rb_hit_detector.update(None)
                best_target_delta = None
                boxes_seen = 0
                aim_enhancement.reset()
                target_selector.reset_tracking()
            auto_fire_active = auto_fire_gate.allow_auto_fire(auto_fire_active, time.perf_counter())
            post_ms = (time.perf_counter() - post_start) * 1000.0

            if controller:
                controller.set_auto_fire(auto_fire_active)
                if best_target_delta:
                    controller.update(best_target_delta[0], best_target_delta[1])
                else:
                    controller.reset()

            perf_tracker.update(
                capture_wait_ms=capture_wait_ms,
                infer_ms=infer_ms,
                post_ms=post_ms,
                boxes_seen=boxes_seen,
                tracking_active=selected_target is not None,
            )

            if win32api.GetAsyncKeyState(config.quit_key_vk) & 0x8000:
                break
    finally:
        print("Stopping vision processing.")
        if controller:
            controller.set_auto_fire(False)
            controller.reset()
        capture_thread.stop()
        capture_thread.join(timeout=1.0)
