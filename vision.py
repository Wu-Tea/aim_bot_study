import math
import os
import threading
import time
from dataclasses import dataclass

import cv2
import dxcam
import numpy as np
import torch
import win32api
from ultralytics import YOLO


LOWER_GREEN = np.array([45, 80, 50])
UPPER_GREEN = np.array([75, 255, 255])
LOWER_BLUE = np.array([90, 80, 50])
UPPER_BLUE = np.array([115, 255, 255])
# Enemy yellow narrowed from H=15 to H=20 so it stops bleeding into orange /
# warm skin tones, which was a source of false positives on close-range shots.
LOWER_YELLOW = np.array([20, 120, 120])
UPPER_YELLOW = np.array([35, 255, 255])
# Red wraps the hue circle at H=0, so we need two ranges to cover it.
LOWER_RED1 = np.array([0, 120, 80])
UPPER_RED1 = np.array([10, 255, 255])
LOWER_RED2 = np.array([170, 120, 80])
UPPER_RED2 = np.array([180, 255, 255])

# Short alias for the hot loop timer. perf_counter is the canonical high-res
# monotonic clock on Windows; aliasing it shaves attribute lookups and keeps
# the post-processing timing lines readable.
now = time.perf_counter


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
    target_fps: int = 90
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
            target_fps=_get_env_int("VISION_TARGET_FPS", 90),
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


class ScreenCaptureThread(threading.Thread):
    def __init__(self, target_fps: int = 90, crop_size: int = 640):
        super().__init__(daemon=True)
        self.target_fps = target_fps
        self.crop_size = crop_size

        screen_width = win32api.GetSystemMetrics(0)
        screen_height = win32api.GetSystemMetrics(1)
        left = (screen_width - crop_size) // 2
        top = (screen_height - crop_size) // 2

        self.region = (left, top, left + crop_size, top + crop_size)
        self.camera = dxcam.create(output_color="RGB", region=self.region)
        self.running = True
        self._condition = threading.Condition()
        self._latest_frame = None
        self._latest_frame_id = 0

        self.camera.start(target_fps=self.target_fps, video_mode=True)

    def run(self):
        # dxcam in video_mode already paces internally at target_fps and
        # get_latest_frame() blocks until a new frame is available, so any
        # extra sleep here just injects latency and degrades freshness.
        while self.running:
            frame = self.camera.get_latest_frame()
            if frame is None:
                continue
            with self._condition:
                self._latest_frame = frame
                self._latest_frame_id += 1
                self._condition.notify()

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
    # Upper bound of the positive color contribution (enemy tag bonus). Used to
    # short-circuit HSV work for candidates whose cheap pre-score already can't
    # beat the running best even if they landed a perfect enemy tag.
    MAX_COLOR_BONUS = 10000

    def __init__(self, crop_size: int = 640):
        self.last_target_center = None
        self.screen_center_x = crop_size / 2.0
        self.screen_center_y = crop_size / 2.0

        self.TRACKING_BONUS = 2000
        self.TRACKING_RADIUS = 120
        self.IDEAL_AREA = 8000
        self.MAX_AREA_LIMIT = 40000
        self.CONFIDENCE_THRESHOLD = _get_env_float("VISION_KPT_CONF", 0.40)
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

    def find_best_target(self, detections: list[ParsedDetections], frame: np.ndarray):
        """Pick the best target.

        Scoring order is:
          1. Cheap score  = -dist*2.5 + area term + tracking bonus
          2. Short-circuit: skip HSV work if cheap + MAX_COLOR_BONUS can't beat best
          3. Color ROI    = adaptive name-plate window above each head, using
                            fractional thresholds so behavior is consistent across
                            near and far targets.

        Enemy detection considers both yellow and red; friendly is green/blue.
        """
        best_target_abs = None
        highest_score = -float("inf")
        frame_h, frame_w = frame.shape[0], frame.shape[1]
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

                # ---- Cheap score (everything except color) ----
                cheap_score = -dist * 2.5

                current_area = box_w * box_h
                if current_area > self.MAX_AREA_LIMIT:
                    cheap_score -= (current_area - self.MAX_AREA_LIMIT) * 0.1
                else:
                    area_diff = abs(current_area - self.IDEAL_AREA)
                    cheap_score += (self.IDEAL_AREA - area_diff) * 0.005

                if last_target_center and math.hypot(
                    tx - last_target_center[0],
                    ty - last_target_center[1],
                ) < self.TRACKING_RADIUS:
                    cheap_score += self.TRACKING_BONUS

                # Short-circuit: even with a perfect enemy color bonus this
                # candidate can't beat the running best. Friendly penalty would
                # only make it worse, so there is no reason to run HSV at all.
                if cheap_score + self.MAX_COLOR_BONUS <= highest_score:
                    continue

                # ---- Adaptive color ROI ----
                # Name plate / tag sits in a narrow band just above the head.
                # Height tracks ~20% of box_h (clamped), width tracks ~80% of
                # box_w, and horizontal center uses the box center x (more
                # stable than tx, which follows shoulders/nose and can drift
                # sideways when the player is turned).
                score = cheap_score
                cx = (x1 + x2) * 0.5
                roi_h = int(max(12, min(36, box_h * 0.20)))
                roi_w = int(max(24, min(80, box_w * 0.80)))
                roi_bottom = int(y1) - 2  # small gap so head/hair doesn't leak in
                roi_top = roi_bottom - roi_h
                roi_left = int(cx - roi_w / 2)
                roi_right = int(cx + roi_w / 2)

                roi_top = max(0, roi_top)
                roi_bottom = max(0, min(frame_h, roi_bottom))
                roi_left = max(0, roi_left)
                roi_right = min(frame_w, roi_right)

                if (roi_bottom - roi_top) >= 4 and (roi_right - roi_left) >= 4:
                    roi_rgb = frame[roi_top:roi_bottom, roi_left:roi_right]
                    roi_hsv = cv2.cvtColor(roi_rgb, cv2.COLOR_RGB2HSV)
                    roi_area = roi_hsv.shape[0] * roi_hsv.shape[1]

                    friendly_mask = cv2.bitwise_or(
                        cv2.inRange(roi_hsv, LOWER_GREEN, UPPER_GREEN),
                        cv2.inRange(roi_hsv, LOWER_BLUE, UPPER_BLUE),
                    )
                    if cv2.countNonZero(friendly_mask) > roi_area * 0.08:
                        score -= 100000

                    # Enemy = yellow OR red. Red needs two hue ranges because
                    # it wraps around H=0 in OpenCV's HSV space.
                    enemy_mask = cv2.inRange(roi_hsv, LOWER_YELLOW, UPPER_YELLOW)
                    enemy_mask = cv2.bitwise_or(
                        enemy_mask,
                        cv2.inRange(roi_hsv, LOWER_RED1, UPPER_RED1),
                    )
                    enemy_mask = cv2.bitwise_or(
                        enemy_mask,
                        cv2.inRange(roi_hsv, LOWER_RED2, UPPER_RED2),
                    )
                    if cv2.countNonZero(enemy_mask) > roi_area * 0.10:
                        score += self.MAX_COLOR_BONUS

                if score > highest_score:
                    highest_score = score
                    best_target_abs = (tx, ty)

        if best_target_abs and highest_score > self.MIN_SCORE_THRESHOLD:
            # Jump limiter. Two regimes:
            #  - Locked on a target already: reject picks that are more than
            #    MAX_JUMP_PIXELS away from last_target_center (defeats snap
            #    between two different players standing apart).
            #  - Fresh acquisition: keep the original flick limit against the
            #    screen center so we don't whip the crosshair hundreds of
            #    pixels on the very first aim-down frame.
            if last_target_center is not None:
                jump = math.hypot(
                    best_target_abs[0] - last_target_center[0],
                    best_target_abs[1] - last_target_center[1],
                )
                if jump > self.MAX_JUMP_PIXELS:
                    self.reset_tracking()
                    return None
            else:
                flick_dx = best_target_abs[0] - self.screen_center_x
                flick_dy = best_target_abs[1] - self.screen_center_y
                if abs(flick_dx) >= self.MAX_JUMP_PIXELS or abs(flick_dy) >= self.MAX_JUMP_PIXELS:
                    self.reset_tracking()
                    return None

            best_target_delta = (
                best_target_abs[0] - self.screen_center_x,
                best_target_abs[1] - self.screen_center_y,
            )
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

    # Proportional shrink applied to each bbox edge when computing the auto-fire
    # hit zone. The goal is "slightly tighter than the raw bbox" — just enough
    # to exclude outstretched arms / feet / above-head empty space — NOT a
    # chest-only sniper zone. Because we shrink by a fraction of box size, this
    # scales automatically: close targets keep a generous fire zone, distant
    # targets collapse to near-limb tolerance.
    FIRE_SHRINK_X = 0.12  # left + right each lose 12% (central 76% kept)
    FIRE_SHRINK_TOP = 0.05
    FIRE_SHRINK_BOTTOM = 0.15

    def _fire_box_for(self, box: np.ndarray):
        x1, y1, x2, y2 = box
        box_w = float(x2 - x1)
        box_h = float(y2 - y1)
        fx1 = x1 + box_w * self.FIRE_SHRINK_X
        fx2 = x2 - box_w * self.FIRE_SHRINK_X
        fy1 = y1 + box_h * self.FIRE_SHRINK_TOP
        fy2 = y2 - box_h * self.FIRE_SHRINK_BOTTOM
        return fx1, fy1, fx2, fy2

    def _is_crosshair_touching_person(self, detections: list[ParsedDetections]):
        cx, cy = self.center_x, self.center_y
        pad = self.edge_padding

        for detection in detections:
            for box, conf in zip(detection.boxes, detection.confs):
                if conf < self.min_conf:
                    continue

                fx1, fy1, fx2, fy2 = self._fire_box_for(box)
                if (fx1 - pad) <= cx <= (fx2 + pad) and (fy1 - pad) <= cy <= (fy2 + pad):
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
    def __init__(
        self,
        enabled: bool = False,
        log_interval: float = 2.0,
        clock=None,
        printer=None,
    ):
        self.enabled = enabled
        self.log_interval = log_interval
        self._clock = clock or time.perf_counter
        self._printer = printer or print
        self.reset_window()

    def reset_window(self):
        self._window_start = self._clock()
        self._frame_count = 0
        self._capture_wait_ms = 0.0
        self._infer_ms = 0.0
        self._post_ms = 0.0
        self._boxes_seen = 0
        self._tracking_window_start = None
        self._tracking_frame_count = 0
        self._tracking_capture_wait_ms = 0.0
        self._tracking_infer_ms = 0.0
        self._tracking_post_ms = 0.0
        self._tracking_boxes_seen = 0

    def update(
        self,
        capture_wait_ms: float,
        infer_ms: float,
        post_ms: float,
        boxes_seen: int,
        tracking_active: bool = False,
    ):
        if not self.enabled:
            return

        self._frame_count += 1
        self._capture_wait_ms += capture_wait_ms
        self._infer_ms += infer_ms
        self._post_ms += post_ms
        self._boxes_seen += boxes_seen

        now = self._clock()
        if tracking_active:
            if self._tracking_window_start is None:
                self._tracking_window_start = now
            self._tracking_frame_count += 1
            self._tracking_capture_wait_ms += capture_wait_ms
            self._tracking_infer_ms += infer_ms
            self._tracking_post_ms += post_ms
            self._tracking_boxes_seen += boxes_seen

        elapsed = now - self._window_start
        if elapsed < self.log_interval or self._frame_count == 0:
            return

        avg_wait = self._capture_wait_ms / self._frame_count
        avg_infer = self._infer_ms / self._frame_count
        avg_post = self._post_ms / self._frame_count
        avg_boxes = self._boxes_seen / self._frame_count
        fps = self._frame_count / elapsed

        self._printer(
            "[Perf][ADS] "
            f"loop={fps:.1f} FPS | wait={avg_wait:.1f}ms | "
            f"infer={avg_infer:.1f}ms | post={avg_post:.1f}ms | boxes={avg_boxes:.1f}"
        )
        if self._tracking_frame_count > 0 and self._tracking_window_start is not None:
            tracking_elapsed = max(now - self._tracking_window_start, 1e-9)
            tracking_avg_wait = self._tracking_capture_wait_ms / self._tracking_frame_count
            tracking_avg_infer = self._tracking_infer_ms / self._tracking_frame_count
            tracking_avg_post = self._tracking_post_ms / self._tracking_frame_count
            tracking_avg_boxes = self._tracking_boxes_seen / self._tracking_frame_count
            tracking_fps = self._tracking_frame_count / tracking_elapsed
            self._printer(
                "[Perf][TRACK] "
                f"loop={tracking_fps:.1f} FPS | wait={tracking_avg_wait:.1f}ms | "
                f"infer={tracking_avg_infer:.1f}ms | post={tracking_avg_post:.1f}ms | "
                f"boxes={tracking_avg_boxes:.1f}"
            )

        self.reset_window()


def _load_model(config: VisionConfig):
    try:
        model = YOLO(config.model_path, task=config.model_task)
        print(f"Loaded model: {config.model_path}")
        return model
    except Exception as exc:
        print(f"Failed to load {config.model_path}: {exc}. Falling back to {config.fallback_model_path}.")
        return YOLO(config.fallback_model_path, task=config.model_task)


def _resolve_autobackend(model):
    """Return the live AutoBackend module, or None.

    Ultralytics stores the real forward module on `model.predictor.model`
    after the first predict() call. `model.model` is only the same object
    for eager .pt loads; for .engine paths it stays as the weights string.
    Always prefer the predictor path and fall back to `model.model` only
    as a last resort.
    """
    predictor = getattr(model, "predictor", None)
    backend = getattr(predictor, "model", None) if predictor is not None else None
    if backend is None:
        backend = getattr(model, "model", None)
    # Guard against the ".model is a path string" case we saw on engine loads.
    if isinstance(backend, str):
        return None
    return backend


def _describe_model_backend(model):
    """Diagnostic: inspect AutoBackend state AFTER first predict() has lazy-initialized it."""
    try:
        inner = _resolve_autobackend(model)
        if inner is None:
            print(f"[Vision] backend unresolved; model.model={type(getattr(model, 'model', None)).__name__}")
            return
        attrs = {
            "class": type(inner).__name__,
            "pt": getattr(inner, "pt", None),
            "engine": getattr(inner, "engine", None),
            "onnx": getattr(inner, "onnx", None),
            "fp16": getattr(inner, "fp16", None),
            "device": str(getattr(inner, "device", "?")),
        }
        print(f"[Vision] backend {attrs}")
    except Exception as exc:
        print(f"[Vision] backend inspect failed: {exc}")


def _bench_once(fn, iters: int) -> tuple[float, float, float]:
    timings = []
    for _ in range(iters):
        t0 = time.perf_counter()
        fn()
        timings.append((time.perf_counter() - t0) * 1000.0)
    timings.sort()
    return (
        timings[len(timings) // 10],
        timings[len(timings) // 2],
        timings[(len(timings) * 9) // 10],
    )


def _warmup_model(model, config: VisionConfig, predict_kwargs: dict) -> "FastPath | None":
    print("Warming up model...")
    dummy_frame = np.zeros((config.crop_size, config.crop_size, 3), dtype=np.uint8)
    for _ in range(config.warmup_iterations):
        model.predict(source=dummy_frame, **predict_kwargs)

    # NOW the AutoBackend has been lazy-instantiated by the first predict() call,
    # so we can inspect what's really running the forward pass.
    _describe_model_backend(model)

    fast_path = _init_fast_path(model, config)

    # Optional post-warmup micro-benchmark. Gated behind VISION_BENCH=1 so
    # regular runs don't eat the extra ~150-300ms of cold-start latency — it's
    # only useful when you're actively diagnosing inference cost. We bench
    # both paths so the delta between Ultralytics predict() and _fast_predict
    # is visible in one go, which makes Step 4 regressions obvious.
    if _get_env_bool("VISION_BENCH", False):
        bench_iters = 30
        b_p10, b_median, b_p90 = _bench_once(
            lambda: model.predict(source=dummy_frame, **predict_kwargs),
            bench_iters,
        )
        print(
            f"[Vision] bench predict()    {bench_iters} iters: "
            f"p10={b_p10:.2f}ms median={b_median:.2f}ms p90={b_p90:.2f}ms"
        )
        if fast_path is not None:
            f_p10, f_median, f_p90 = _bench_once(
                lambda: _fast_predict(fast_path, dummy_frame),
                bench_iters,
            )
            print(
                f"[Vision] bench _fast_predict {bench_iters} iters: "
                f"p10={f_p10:.2f}ms median={f_median:.2f}ms p90={f_p90:.2f}ms"
            )

    print("Warmup complete.")
    return fast_path


@dataclass(slots=True)
class FastPath:
    """State for the Ultralytics-bypass forward path.

    We keep a pre-allocated FP16 GPU input buffer and a direct reference to the
    AutoBackend module. `output_kind` is detected once on the warmup forward so
    the hot loop can branch between NMS-in-engine and raw engine outputs without
    re-inspecting shapes every call.
    """
    backend: object
    gpu_input: "torch.Tensor"
    conf_thr: float
    max_det: int
    output_kind: str | None = None  # "nms_in_engine" | "raw"


def _detect_output_kind(raw) -> str:
    """Inspect an engine forward result and classify its layout.

    Two shapes are supported:
      * NMS-in-engine pose: `(1, K, 57)` — K padded to max_det,
        columns = [x1, y1, x2, y2, conf, cls, 17*(x,y,c)].
      * Raw YOLO pose:      `(1, 56, 8400)` — channels-first,
        columns per anchor = [cx, cy, w, h, cls_score, 17*(x,y,c)].

    The heuristic keys on the channel count and spatial extent, both of which
    are bolted to the model topology and don't change run-to-run.
    """
    pred = raw[0] if isinstance(raw, (tuple, list)) else raw
    if not isinstance(pred, torch.Tensor):
        raise TypeError(f"Unexpected backend output type: {type(pred)}")
    if pred.ndim == 2:
        pred = pred.unsqueeze(0)
    if pred.ndim != 3:
        raise ValueError(f"Unexpected backend output ndim: {tuple(pred.shape)}")

    _, d1, d2 = pred.shape
    # NMS-in-engine: last dim is fixed at 6 + 17*3 = 57.
    if d2 == 57:
        return "nms_in_engine"
    # Raw pose: 56 channels over ~8400 anchors at 640 imgsz.
    if d1 == 56 and d2 >= 1000:
        return "raw"
    # Fallback: if the middle dim is small enough to look like max_det bins,
    # treat as NMS-in-engine; otherwise assume raw.
    if d1 <= 300 and d2 >= 6:
        return "nms_in_engine"
    return "raw"


def _init_fast_path(model, config: VisionConfig) -> FastPath | None:
    """Construct a FastPath after the Ultralytics predictor has lazy-initialized.

    Must be called AFTER at least one `model.predict()` call so that
    `model.model` (the AutoBackend) has been built. A warmup forward is run
    immediately with the pre-allocated buffer to lock in the engine's kernel
    plan and cache the output layout.
    """
    backend = _resolve_autobackend(model)
    if backend is None:
        print("[Vision] Fast path disabled: could not resolve AutoBackend after warmup.")
        return None

    device = torch.device(f"cuda:{config.device}")
    dtype = _fast_path_input_dtype(backend, config.half)
    gpu_input = torch.empty(
        (1, 3, config.crop_size, config.crop_size),
        dtype=dtype,
        device=device,
    )

    fast_path = FastPath(
        backend=backend,
        gpu_input=gpu_input,
        conf_thr=float(config.conf),
        max_det=10,
    )

    with torch.inference_mode():
        raw = backend(gpu_input)
    fast_path.output_kind = _detect_output_kind(raw)
    print(f"[Vision] Fast path ready: output_kind={fast_path.output_kind}")
    return fast_path


def _fast_path_input_dtype(backend, config_half: bool):
    """Choose the fast-path tensor dtype from backend capability, not user intent.

    Feeding FP16 into an FP32-only TensorRT engine crushes confidences and makes
    detections disappear. Conversely, if the resolved AutoBackend is already an
    FP16 backend, the fast path should honor that regardless of the user-side
    hint because the engine was compiled for that precision. `config_half` only
    matters for non-engine fallbacks where backend.fp16 may be absent.
    """
    if getattr(backend, "fp16", False):
        return torch.float16
    if config_half and not hasattr(backend, "fp16"):
        return torch.float16
    return torch.float32


def _fast_predict(fast_path: FastPath, frame_rgb: np.ndarray) -> list[ParsedDetections]:
    """Replacement for `model.predict(...) + _extract_detections(...)`.

    `frame_rgb` is expected to be HWC uint8 RGB (dxcam already gives us RGB,
    see ScreenCaptureThread). The function copies directly into the
    pre-allocated GPU buffer, runs the AutoBackend, and decodes the result
    into the same `list[ParsedDetections]` contract downstream code already
    speaks. Downstream (`TargetSelector`, `CrosshairPersonHitDetector`) is
    untouched.
    """
    if not frame_rgb.flags.c_contiguous:
        frame_rgb = np.ascontiguousarray(frame_rgb)

    # HWC uint8 numpy -> HWC uint8 GPU -> CHW fp16 /255 -> pre-allocated buffer.
    # Doing the /255 and dtype cast on-GPU is cheaper than synthesizing a new
    # fp16 host tensor per frame, and copying into the existing buffer avoids
    # fragmenting the allocator's fp16 pool every iteration.
    cpu_u8 = torch.from_numpy(frame_rgb)
    gpu_u8 = cpu_u8.to(fast_path.gpu_input.device)
    fast_path.gpu_input[0].copy_(
        gpu_u8.permute(2, 0, 1).to(fast_path.gpu_input.dtype).div_(255.0)
    )

    with torch.inference_mode():
        raw = fast_path.backend(fast_path.gpu_input)

    if fast_path.output_kind is None:
        fast_path.output_kind = _detect_output_kind(raw)

    if fast_path.output_kind == "nms_in_engine":
        return _decode_nms_in_engine(raw, fast_path.conf_thr, fast_path.max_det)
    return _decode_raw_pose(raw, fast_path.conf_thr, fast_path.max_det)


def _decode_nms_in_engine(raw, conf_thr: float, max_det: int) -> list[ParsedDetections]:
    """NMS-in-engine pose output: `(1, K, 57)`.

    Columns: `[x1, y1, x2, y2, conf, cls, 17*(x,y,c)]`.
    Padding rows have `conf=0`, so filtering by conf threshold also trims
    them. Box coordinates are already in the 640-space input coordinate
    system (no letterbox — dxcam's region is already 640x640), so they drop
    straight into ParsedDetections without remapping.
    """
    pred = raw[0] if isinstance(raw, (tuple, list)) else raw
    if pred.ndim == 3:
        pred = pred[0]  # (K, 57)
    if pred.ndim != 2 or pred.shape[1] < 6:
        return []

    scores = pred[:, 4]
    mask = scores >= conf_thr
    if not torch.any(mask):
        return []

    pred = pred[mask]
    if pred.shape[0] > max_det:
        top = torch.topk(pred[:, 4], max_det, largest=True, sorted=False)
        pred = pred[top.indices]

    # Single device->host transfer for the whole tensor; splitting later
    # on CPU is much cheaper than hopping back to GPU per column.
    pred_np = pred.detach().to(torch.float32).cpu().numpy()
    boxes = np.ascontiguousarray(pred_np[:, :4], dtype=np.float32)
    confs = np.ascontiguousarray(pred_np[:, 4], dtype=np.float32)
    keypoints = None
    kpt_cols = pred_np.shape[1] - 6
    if kpt_cols >= 17 * 3:
        keypoints = pred_np[:, 6:6 + 17 * 3].reshape(-1, 17, 3).astype(np.float32, copy=False)
    return [ParsedDetections(boxes=boxes, confs=confs, keypoints=keypoints)]


def _decode_raw_pose(raw, conf_thr: float, max_det: int) -> list[ParsedDetections]:
    """Raw YOLO pose engine output: `(1, 56, 8400)`.

    Layout per column:
      `[0..3]  : cx, cy, w, h`  (640-space; no letterbox reverse needed)
      `[4]     : class score`   (single-class pose = person)
      `[5..55] : 17 keypoints × (x, y, conf)`

    We pre-top-K to 50 candidates before torchvision NMS so the NMS cost is
    bounded regardless of how permissive the conf threshold is.
    """
    pred = raw[0] if isinstance(raw, (tuple, list)) else raw
    if pred.ndim == 3:
        pred = pred[0]  # (56, N)
    if pred.ndim != 2 or pred.shape[0] < 6:
        return []

    pred_t = pred.transpose(0, 1).contiguous()  # (N, 56)
    scores = pred_t[:, 4]
    mask = scores >= conf_thr
    if not torch.any(mask):
        return []

    cand = pred_t[mask]
    pre_nms_top_k = 50
    if cand.shape[0] > pre_nms_top_k:
        top = torch.topk(cand[:, 4], pre_nms_top_k, largest=True, sorted=False)
        cand = cand[top.indices]

    cx = cand[:, 0]
    cy = cand[:, 1]
    w = cand[:, 2]
    h = cand[:, 3]
    boxes = torch.stack(
        (cx - w * 0.5, cy - h * 0.5, cx + w * 0.5, cy + h * 0.5),
        dim=1,
    )
    scores = cand[:, 4]

    # Lazy import: only the raw fallback needs torchvision NMS, so users
    # running the preferred NMS-in-engine build don't have to have it installed.
    from torchvision.ops import nms as tv_nms
    keep = tv_nms(boxes.float(), scores.float(), 0.45)
    if keep.numel() == 0:
        return []
    if keep.numel() > max_det:
        keep = keep[:max_det]

    boxes = boxes[keep]
    scores = scores[keep]
    kpts_flat = cand[keep, 5:5 + 17 * 3]

    boxes_np = boxes.detach().to(torch.float32).cpu().numpy().astype(np.float32, copy=False)
    scores_np = scores.detach().to(torch.float32).cpu().numpy().astype(np.float32, copy=False)
    kpts_np = (
        kpts_flat.detach().to(torch.float32).cpu().numpy()
        .reshape(-1, 17, 3).astype(np.float32, copy=False)
    )
    return [ParsedDetections(boxes=boxes_np, confs=scores_np, keypoints=kpts_np)]


def _extract_detections(results) -> list[ParsedDetections]:
    parsed: list[ParsedDetections] = []
    for result in results:
        boxes_obj = result.boxes
        if boxes_obj is None or len(boxes_obj) == 0:
            continue

        # Collapse all device->host transfers into a single Results.cpu() call so
        # we don't pay the implicit stream sync penalty N times (xyxy, conf,
        # keypoints each used to .cpu() separately).
        result_cpu = result.cpu()
        boxes_obj = result_cpu.boxes

        boxes = boxes_obj.xyxy.numpy()
        confs = (
            boxes_obj.conf.numpy()
            if boxes_obj.conf is not None
            else np.ones(len(boxes), dtype=np.float32)
        )
        keypoints = None
        kpts_obj = result_cpu.keypoints
        if kpts_obj is not None and kpts_obj.data is not None and len(kpts_obj.data) > 0:
            keypoints = kpts_obj.data.numpy()

        parsed.append(ParsedDetections(boxes=boxes, confs=confs, keypoints=keypoints))

    return parsed


def _should_log_performance():
    return _get_env_bool("VISION_PERF_LOG", False)


def process_vision(controller=None):
    config = VisionConfig.from_env()
    frame_is_rgb = True
    # Kept tight on purpose: `half` is a no-op under an FP16 engine and
    # `agnostic_nms=False` is the default. Every key left in here is there
    # because the predict wrapper / fallback path actually needs it.
    predict_kwargs = {
        "classes": list(config.classes),
        "conf": config.conf,
        "verbose": False,
        "device": config.device,
        "imgsz": config.crop_size,
        # NMS output cap. For a single-class aim-assist loop we only ever need
        # the closest handful of detections; the default 300 causes NMS to
        # process and allocate for boxes we immediately throw away.
        "max_det": 10,
    }

    model = _load_model(config)
    fast_path = _warmup_model(model, config, predict_kwargs)
    use_fast_path = fast_path is not None and _get_env_bool("VISION_FAST_PATH", True)

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
        f"fast_path={'on' if use_fast_path else 'off'} | "
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
                    perf_tracker.reset_window()
                was_aiming = False
                time.sleep(config.idle_sleep)
                continue

            if not was_aiming:
                perf_tracker.reset_window()
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
            if use_fast_path:
                # dxcam already gives us contiguous HWC uint8 RGB; _fast_predict
                # owns the entire preprocess + forward + decode pipeline.
                detections = _fast_predict(fast_path, frame)
                fallback_results = None
            else:
                predict_frame = frame if not frame_is_rgb else np.ascontiguousarray(frame[:, :, ::-1])
                fallback_results = model.predict(source=predict_frame, **predict_kwargs)
                detections = None
            infer_ms = (time.perf_counter() - inference_start) * 1000.0

            post_start = now()
            if not use_fast_path:
                detections = _extract_detections(fallback_results)
            auto_rb_active = rb_hit_detector.update(detections)
            if detections:
                best_target_delta = target_selector.find_best_target(detections, frame)
                boxes_seen = sum(len(detection.boxes) for detection in detections)
            else:
                best_target_delta = None
                boxes_seen = 0
            post_ms = (now() - post_start) * 1000.0

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
                tracking_active=best_target_delta is not None,
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
