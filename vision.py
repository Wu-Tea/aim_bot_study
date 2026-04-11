import math
import os
import threading
import time
from dataclasses import dataclass

import cv2
import dxcam
import numpy as np
import win32api
from ultralytics import YOLO


LOWER_GREEN = np.array([45, 80, 50])
UPPER_GREEN = np.array([75, 255, 255])
LOWER_BLUE = np.array([90, 80, 50])
UPPER_BLUE = np.array([115, 255, 255])
LOWER_YELLOW = np.array([15, 120, 120])
UPPER_YELLOW = np.array([35, 255, 255])


def _get_env_int(name: str, default: int) -> int:
    value = os.getenv(name)
    if value is None:
        return default
    try:
        return int(value)
    except ValueError:
        return default


def _get_env_float(name: str, default: float) -> float:
    value = os.getenv(name)
    if value is None:
        return default
    try:
        return float(value)
    except ValueError:
        return default


def _get_env_bool(name: str, default: bool) -> bool:
    value = os.getenv(name)
    if value is None:
        return default
    return value.strip().lower() in {"1", "true", "yes", "on"}


@dataclass(slots=True, frozen=True)
class VisionConfig:
    crop_size: int = 640
    target_fps: int = 70
    model_path: str = "yolo26n-pose.engine"
    fallback_model_path: str = "yolo26n-pose.pt"
    model_task: str = "pose"
    classes: tuple[int, ...] = (0,)
    conf: float = 0.50
    half: bool = True
    device: int = 0
    warmup_iterations: int = 10
    frame_timeout: float = 0.10
    idle_sleep: float = 0.01
    perf_log_interval: float = 2.0
    quit_key_vk: int = 0x51  # Q

    @classmethod
    def from_env(cls):
        return cls(
            crop_size=_get_env_int("VISION_CROP_SIZE", 640),
            target_fps=_get_env_int("VISION_TARGET_FPS", 70),
            model_path=os.getenv("VISION_MODEL_PATH", "yolo26n-pose.engine"),
            fallback_model_path=os.getenv("VISION_FALLBACK_MODEL_PATH", "yolo26n-pose.pt"),
            conf=_get_env_float("VISION_CONF", 0.50),
            half=_get_env_bool("VISION_HALF", True),
            device=_get_env_int("VISION_DEVICE", 0),
            warmup_iterations=_get_env_int("VISION_WARMUP_ITERATIONS", 10),
            frame_timeout=_get_env_float("VISION_FRAME_TIMEOUT", 0.10),
            idle_sleep=_get_env_float("VISION_IDLE_SLEEP", 0.01),
            perf_log_interval=_get_env_float("VISION_PERF_LOG_INTERVAL", 2.0),
            quit_key_vk=_get_env_int("VISION_QUIT_KEY_VK", 0x51),
        )


@dataclass(slots=True)
class ParsedDetections:
    boxes: np.ndarray
    confs: np.ndarray
    keypoints: np.ndarray | None = None


@dataclass(slots=True)
class FrameColorMasks:
    friendly: np.ndarray
    enemy: np.ndarray


class ScreenCaptureThread(threading.Thread):
    def __init__(self, target_fps: int = 70, crop_size: int = 640):
        super().__init__(daemon=True)
        self.target_fps = target_fps
        self.crop_size = crop_size

        screen_width = win32api.GetSystemMetrics(0)
        screen_height = win32api.GetSystemMetrics(1)
        left = (screen_width - crop_size) // 2
        top = (screen_height - crop_size) // 2

        self.region = (left, top, left + crop_size, top + crop_size)
        self.camera = dxcam.create(output_color="BGR", region=self.region)
        self.running = True
        self._poll_interval = 1.0 / max(self.target_fps * 2, 1)
        self._condition = threading.Condition()
        self._latest_frame = None
        self._latest_frame_id = 0

        self.camera.start(target_fps=self.target_fps, video_mode=True)

    def run(self):
        while self.running:
            frame = self.camera.get_latest_frame()
            if frame is not None:
                with self._condition:
                    self._latest_frame = frame
                    self._latest_frame_id += 1
                    self._condition.notify_all()
            time.sleep(self._poll_interval)

    def get_latest_frame(self, last_seen_id: int = 0, timeout: float = 0.1):
        deadline = time.perf_counter() + timeout
        with self._condition:
            while self.running and self._latest_frame_id <= last_seen_id:
                remaining = deadline - time.perf_counter()
                if remaining <= 0:
                    return None, last_seen_id
                self._condition.wait(timeout=remaining)

            if self._latest_frame_id <= last_seen_id or self._latest_frame is None:
                return None, last_seen_id

            return self._latest_frame, self._latest_frame_id

    def stop(self):
        self.running = False
        with self._condition:
            self._condition.notify_all()
        self.camera.stop()


class TargetSelector:
    def __init__(self, crop_size: int = 640):
        self.last_target_center = None
        self.screen_center_x = crop_size / 2.0
        self.screen_center_y = crop_size / 2.0

        self.TRACKING_BONUS = 2000
        self.TRACKING_RADIUS = 120
        self.IDEAL_AREA = 8000
        self.MAX_AREA_LIMIT = 40000
        self.CONFIDENCE_THRESHOLD = 0.40
        self.MIN_SCORE_THRESHOLD = -50000
        self.MAX_JUMP_PIXELS = 180

    def reset_tracking(self):
        self.last_target_center = None

    def _get_target_point(self, box: np.ndarray, keypoints: np.ndarray | None, box_index: int):
        x1, y1, x2, y2 = box
        box_w = float(x2 - x1)
        box_h = float(y2 - y1)

        if keypoints is not None and len(keypoints) > box_index:
            kpts = keypoints[box_index]
            l_shoulder, r_shoulder = kpts[5], kpts[6]
            nose = kpts[0]

            if l_shoulder[2] > self.CONFIDENCE_THRESHOLD and r_shoulder[2] > self.CONFIDENCE_THRESHOLD:
                return (l_shoulder[0] + r_shoulder[0]) / 2.0, (l_shoulder[1] + r_shoulder[1]) / 2.0
            if nose[2] > self.CONFIDENCE_THRESHOLD:
                return nose[0], nose[1] + (box_h * 0.05)

        tx = x1 + (box_w / 2.0)
        aspect_ratio = box_h / box_w if box_w > 0.0 else 0.0
        ratio = 0.20 if aspect_ratio > 1.2 else 0.40
        ty = y1 + (box_h * ratio)
        return tx, ty

    def find_best_target(self, detections: list[ParsedDetections], color_masks: FrameColorMasks):
        best_target_abs = None
        highest_score = -float("inf")
        frame_width = color_masks.friendly.shape[1]
        last_target_center = self.last_target_center

        for detection in detections:
            for i, box in enumerate(detection.boxes):
                x1, y1, x2, y2 = box
                box_w = float(x2 - x1)
                box_h = float(y2 - y1)
                if box_w <= 0.0 or box_h <= 0.0:
                    continue

                tx, ty = self._get_target_point(box, detection.keypoints, i)
                dist = math.hypot(tx - self.screen_center_x, ty - self.screen_center_y)
                score = -dist * 2.5

                roi_top = max(0, int(y1) - 50)
                roi_bottom = max(0, int(y1))
                roi_left = max(0, int(tx) - 25)
                roi_right = min(frame_width, int(tx) + 25)

                friendly_roi = color_masks.friendly[roi_top:roi_bottom, roi_left:roi_right]
                if friendly_roi.size > 0 and cv2.countNonZero(friendly_roi) > 15:
                    score -= 100000

                enemy_roi = color_masks.enemy[roi_top:roi_bottom, roi_left:roi_right]
                if enemy_roi.size > 0 and cv2.countNonZero(enemy_roi) > 20:
                    score += 10000

                current_area = box_w * box_h
                if current_area > self.MAX_AREA_LIMIT:
                    score -= (current_area - self.MAX_AREA_LIMIT) * 0.1
                else:
                    area_diff = abs(current_area - self.IDEAL_AREA)
                    score += (self.IDEAL_AREA - area_diff) * 0.005

                if last_target_center and math.hypot(
                    tx - last_target_center[0],
                    ty - last_target_center[1],
                ) < self.TRACKING_RADIUS:
                    score += self.TRACKING_BONUS

                if score > highest_score:
                    highest_score = score
                    best_target_abs = (tx, ty)

        if best_target_abs and highest_score > self.MIN_SCORE_THRESHOLD:
            best_target_delta = (
                best_target_abs[0] - self.screen_center_x,
                best_target_abs[1] - self.screen_center_y,
            )
            if abs(best_target_delta[0]) < self.MAX_JUMP_PIXELS and abs(best_target_delta[1]) < self.MAX_JUMP_PIXELS:
                self.last_target_center = best_target_abs
                return best_target_delta

        self.reset_tracking()
        return None


class CrosshairPersonHitDetector:
    def __init__(self, crop_size: int = 640, edge_padding: int = 2, min_conf: float = 0.35, release_grace_frames: int = 4):
        self.center_x = crop_size / 2.0
        self.center_y = crop_size / 2.0
        self.edge_padding = edge_padding
        self.min_conf = min_conf
        self.release_grace_frames = release_grace_frames
        self._holding = False
        self._miss_frames = 0

    def _is_crosshair_touching_person(self, detections: list[ParsedDetections]):
        cx, cy = self.center_x, self.center_y
        pad = self.edge_padding

        for detection in detections:
            for box, conf in zip(detection.boxes, detection.confs):
                if conf < self.min_conf:
                    continue

                x1, y1, x2, y2 = box
                if (x1 - pad) <= cx <= (x2 + pad) and (y1 - pad) <= cy <= (y2 + pad):
                    return True

        return False

    def update(self, detections: list[ParsedDetections]):
        touching = self._is_crosshair_touching_person(detections)
        if touching:
            self._holding = True
            self._miss_frames = 0
            return True

        if self._holding:
            self._miss_frames += 1
            if self._miss_frames >= self.release_grace_frames:
                self._holding = False
                self._miss_frames = 0

        return self._holding

    def reset(self):
        self._holding = False
        self._miss_frames = 0


class PerformanceTracker:
    def __init__(self, enabled: bool = False, log_interval: float = 2.0):
        self.enabled = enabled
        self.log_interval = log_interval
        self._window_start = time.perf_counter()
        self._frame_count = 0
        self._capture_wait_ms = 0.0
        self._infer_ms = 0.0
        self._post_ms = 0.0
        self._boxes_seen = 0

    def update(self, capture_wait_ms: float, infer_ms: float, post_ms: float, boxes_seen: int):
        if not self.enabled:
            return

        self._frame_count += 1
        self._capture_wait_ms += capture_wait_ms
        self._infer_ms += infer_ms
        self._post_ms += post_ms
        self._boxes_seen += boxes_seen

        now = time.perf_counter()
        elapsed = now - self._window_start
        if elapsed < self.log_interval or self._frame_count == 0:
            return

        avg_wait = self._capture_wait_ms / self._frame_count
        avg_infer = self._infer_ms / self._frame_count
        avg_post = self._post_ms / self._frame_count
        avg_boxes = self._boxes_seen / self._frame_count
        fps = self._frame_count / elapsed

        print(
            "[Perf] "
            f"loop={fps:.1f} FPS | wait={avg_wait:.1f}ms | "
            f"infer={avg_infer:.1f}ms | post={avg_post:.1f}ms | boxes={avg_boxes:.1f}"
        )

        self._window_start = now
        self._frame_count = 0
        self._capture_wait_ms = 0.0
        self._infer_ms = 0.0
        self._post_ms = 0.0
        self._boxes_seen = 0


def _build_color_masks(frame: np.ndarray) -> FrameColorMasks:
    hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)
    friendly_mask = cv2.bitwise_or(
        cv2.inRange(hsv, LOWER_GREEN, UPPER_GREEN),
        cv2.inRange(hsv, LOWER_BLUE, UPPER_BLUE),
    )
    enemy_mask = cv2.inRange(hsv, LOWER_YELLOW, UPPER_YELLOW)
    return FrameColorMasks(friendly=friendly_mask, enemy=enemy_mask)


def _load_model(config: VisionConfig):
    try:
        model = YOLO(config.model_path, task=config.model_task)
        print(f"Loaded model: {config.model_path}")
        return model
    except Exception as exc:
        print(f"Failed to load {config.model_path}: {exc}. Falling back to {config.fallback_model_path}.")
        return YOLO(config.fallback_model_path, task=config.model_task)


def _warmup_model(model, config: VisionConfig, predict_kwargs: dict):
    print("Warming up model...")
    dummy_frame = np.zeros((config.crop_size, config.crop_size, 3), dtype=np.uint8)
    for _ in range(config.warmup_iterations):
        model.predict(source=dummy_frame, **predict_kwargs)
    print("Warmup complete.")


def _extract_detections(results) -> list[ParsedDetections]:
    parsed: list[ParsedDetections] = []
    for result in results:
        boxes_obj = result.boxes
        if boxes_obj is None or len(boxes_obj) == 0:
            continue

        boxes = boxes_obj.xyxy.cpu().numpy()
        confs = boxes_obj.conf.cpu().numpy() if boxes_obj.conf is not None else np.ones(len(boxes), dtype=np.float32)
        keypoints = None
        if result.keypoints is not None and result.keypoints.data is not None and len(result.keypoints.data) > 0:
            keypoints = result.keypoints.data.cpu().numpy()

        parsed.append(ParsedDetections(boxes=boxes, confs=confs, keypoints=keypoints))

    return parsed


def _should_log_performance():
    return _get_env_bool("VISION_PERF_LOG", False)


def process_vision(controller=None):
    config = VisionConfig.from_env()
    predict_kwargs = {
        "classes": list(config.classes),
        "conf": config.conf,
        "verbose": False,
        "half": config.half,
        "device": config.device,
        "imgsz": config.crop_size,
    }

    model = _load_model(config)
    _warmup_model(model, config, predict_kwargs)

    capture_thread = ScreenCaptureThread(target_fps=config.target_fps, crop_size=config.crop_size)
    target_selector = TargetSelector(crop_size=config.crop_size)
    rb_hit_detector = CrosshairPersonHitDetector(crop_size=config.crop_size)
    perf_tracker = PerformanceTracker(
        enabled=_should_log_performance(),
        log_interval=config.perf_log_interval,
    )

    capture_thread.start()
    print(
        "[Vision] "
        f"crop={config.crop_size} | target_fps={config.target_fps} | "
        f"conf={config.conf:.2f} | half={config.half}"
    )

    last_frame_id = 0
    was_aiming = False

    try:
        while True:
            is_aiming = True if controller is None else controller.is_aiming()
            if not is_aiming:
                if was_aiming:
                    if controller:
                        controller.reset()
                        controller.set_auto_rb(False)
                    target_selector.reset_tracking()
                    rb_hit_detector.reset()
                was_aiming = False
                time.sleep(config.idle_sleep)
                continue

            was_aiming = True

            capture_wait_start = time.perf_counter()
            frame, last_frame_id = capture_thread.get_latest_frame(
                last_seen_id=last_frame_id,
                timeout=config.frame_timeout,
            )
            capture_wait_ms = (time.perf_counter() - capture_wait_start) * 1000.0

            if frame is None:
                if controller:
                    controller.set_auto_rb(False)
                rb_hit_detector.reset()
                continue

            inference_start = time.perf_counter()
            results = model.predict(source=frame, **predict_kwargs)
            infer_ms = (time.perf_counter() - inference_start) * 1000.0

            post_start = time.perf_counter()
            detections = _extract_detections(results)
            auto_rb_active = rb_hit_detector.update(detections)
            if detections:
                color_masks = _build_color_masks(frame)
                best_target_delta = target_selector.find_best_target(detections, color_masks)
                boxes_seen = sum(len(detection.boxes) for detection in detections)
            else:
                best_target_delta = None
                boxes_seen = 0
            post_ms = (time.perf_counter() - post_start) * 1000.0

            if controller:
                controller.set_auto_rb(auto_rb_active)

            if best_target_delta and controller:
                controller.update(best_target_delta[0], best_target_delta[1])
            elif controller:
                controller.reset()

            perf_tracker.update(
                capture_wait_ms=capture_wait_ms,
                infer_ms=infer_ms,
                post_ms=post_ms,
                boxes_seen=boxes_seen,
            )

            if win32api.GetAsyncKeyState(config.quit_key_vk) & 0x8000:
                break
    finally:
        print("Stopping vision processing.")
        if controller:
            controller.set_auto_rb(False)
            controller.reset()
        capture_thread.stop()
        capture_thread.join(timeout=1.0)
