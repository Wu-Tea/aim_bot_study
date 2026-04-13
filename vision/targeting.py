import math
from dataclasses import dataclass

import cv2
import numpy as np


LOWER_GREEN = np.array([45, 80, 50])
UPPER_GREEN = np.array([75, 255, 255])
LOWER_BLUE = np.array([90, 80, 50])
UPPER_BLUE = np.array([115, 255, 255])
LOWER_YELLOW = np.array([20, 120, 120])
UPPER_YELLOW = np.array([35, 255, 255])
LOWER_RED1 = np.array([0, 120, 80])
UPPER_RED1 = np.array([10, 255, 255])
LOWER_RED2 = np.array([170, 120, 80])
UPPER_RED2 = np.array([180, 255, 255])


@dataclass(slots=True)
class ParsedDetections:
    boxes: np.ndarray
    confs: np.ndarray
    keypoints: np.ndarray | None = None


@dataclass(slots=True, frozen=True)
class SelectedTarget:
    target_x: float
    target_y: float
    screen_center_x: float
    screen_center_y: float
    score: float
    slow_zone: tuple[float, float, float, float] | None = None
    fire_zone: tuple[float, float, float, float] | None = None

    @property
    def dx(self):
        return self.target_x - self.screen_center_x

    @property
    def dy(self):
        return self.target_y - self.screen_center_y

    def is_crosshair_in_slow_zone(self) -> bool:
        if self.slow_zone is None:
            return True

        left, top, right, bottom = self.slow_zone
        return (
            left <= self.screen_center_x <= right
            and top <= self.screen_center_y <= bottom
        )


class TargetSelector:
    MAX_COLOR_BONUS = 10000
    CONFIDENCE_THRESHOLD = 0.40
    PICKUP_CONFIDENCE_THRESHOLD = 0.55
    TRACKING_CONFIDENCE_THRESHOLD = 0.35
    UPPER_CHEST_RATIO = 0.30
    TORSO_BOX_SHRINK_X = 0.22
    TORSO_BOX_SHRINK_TOP = 0.18
    TORSO_BOX_SHRINK_BOTTOM = 0.20
    TORSO_KEYPOINT_SHRINK_X = 0.10
    FIRE_SHRINK_X = 0.12
    FIRE_SHRINK_TOP = 0.05
    FIRE_SHRINK_BOTTOM = 0.15
    MIN_PICKUP_HEIGHT_RATIO = 0.08
    MIN_TRACKING_HEIGHT_RATIO = 0.06
    MIN_PICKUP_AREA_RATIO = 0.003
    MIN_TRACKING_AREA_RATIO = 0.002
    MIN_ASPECT_RATIO = 0.85
    MAX_ASPECT_RATIO = 4.50
    CONFIDENCE_SCORE_SCALE = 400.0
    MAX_SMOOTHING_JUMP_PIXELS = 24.0
    MIN_SMOOTHING_ALPHA = 0.25
    TRACKING_SWITCH_MARGIN = 80.0

    def __init__(
        self,
        crop_size: int = 640,
        frame_width: int | None = None,
        frame_height: int | None = None,
    ):
        self.last_target_center = None
        width = float(frame_width if frame_width is not None else crop_size)
        height = float(frame_height if frame_height is not None else crop_size)
        self.frame_width = width
        self.frame_height = height
        self.screen_center_x = width / 2.0
        self.screen_center_y = height / 2.0
        self.TRACKING_BONUS = 2000
        self.TRACKING_RADIUS = 120
        self.IDEAL_AREA = 8000
        self.MAX_AREA_LIMIT = 40000
        self.MIN_SCORE_THRESHOLD = -50000
        self.MAX_JUMP_PIXELS = 180

    def reset_tracking(self):
        self.last_target_center = None

    def _get_target_point(self, box: np.ndarray, keypoints: np.ndarray | None, box_index: int):
        x1, y1, x2, y2 = box
        box_w = float(x2 - x1)
        box_h = float(y2 - y1)
        tx = x1 + (box_w * 0.5)
        ty = y1 + (box_h * self.UPPER_CHEST_RATIO)
        return tx, ty

    def _fallback_slow_zone(self, box: np.ndarray):
        x1, y1, x2, y2 = box
        box_w = float(x2 - x1)
        box_h = float(y2 - y1)
        return (
            x1 + (box_w * self.TORSO_BOX_SHRINK_X),
            y1 + (box_h * self.TORSO_BOX_SHRINK_TOP),
            x2 - (box_w * self.TORSO_BOX_SHRINK_X),
            y2 - (box_h * self.TORSO_BOX_SHRINK_BOTTOM),
        )

    def _keypoint_slow_zone(
        self,
        box: np.ndarray,
        keypoints: np.ndarray | None,
        box_index: int,
    ):
        if keypoints is None or len(keypoints) <= box_index:
            return None

        kpts = keypoints[box_index]
        left_shoulder, right_shoulder = kpts[5], kpts[6]
        left_hip, right_hip = kpts[11], kpts[12]
        torso_points = (left_shoulder, right_shoulder, left_hip, right_hip)
        if any(point[2] <= self.CONFIDENCE_THRESHOLD for point in torso_points):
            return None

        left = min(left_shoulder[0], left_hip[0])
        right = max(right_shoulder[0], right_hip[0])
        top = min(left_shoulder[1], right_shoulder[1])
        bottom = max(left_hip[1], right_hip[1])
        width = max(0.0, right - left)
        shrink_x = width * self.TORSO_KEYPOINT_SHRINK_X
        return (
            left + shrink_x,
            top,
            right - shrink_x,
            bottom,
        )

    def _get_slow_zone(
        self,
        box: np.ndarray,
        keypoints: np.ndarray | None,
        box_index: int,
    ):
        return self._fallback_slow_zone(box)

    def _get_fire_zone(self, box: np.ndarray):
        x1, y1, x2, y2 = box
        box_w = float(x2 - x1)
        box_h = float(y2 - y1)
        return (
            x1 + (box_w * self.FIRE_SHRINK_X),
            y1 + (box_h * self.FIRE_SHRINK_TOP),
            x2 - (box_w * self.FIRE_SHRINK_X),
            y2 - (box_h * self.FIRE_SHRINK_BOTTOM),
        )

    def _classify_color(self, box: np.ndarray, frame: np.ndarray):
        x1, y1, x2, y2 = box
        frame_h, frame_w = frame.shape[:2]
        box_w = float(x2 - x1)
        box_h = float(y2 - y1)
        cx = (x1 + x2) * 0.5
        roi_h = int(max(12, min(36, box_h * 0.20)))
        roi_w = int(max(24, min(80, box_w * 0.80)))
        roi_bottom = max(0, min(frame_h, int(y1) - 2))
        roi_top = max(0, roi_bottom - roi_h)
        roi_left = max(0, int(cx - roi_w / 2))
        roi_right = min(frame_w, int(cx + roi_w / 2))

        if (roi_bottom - roi_top) < 4 or (roi_right - roi_left) < 4:
            return 0.0, False

        roi_hsv = cv2.cvtColor(frame[roi_top:roi_bottom, roi_left:roi_right], cv2.COLOR_RGB2HSV)
        roi_area = roi_hsv.shape[0] * roi_hsv.shape[1]

        friendly_mask = cv2.bitwise_or(
            cv2.inRange(roi_hsv, LOWER_GREEN, UPPER_GREEN),
            cv2.inRange(roi_hsv, LOWER_BLUE, UPPER_BLUE),
        )
        if cv2.countNonZero(friendly_mask) > roi_area * 0.08:
            return 0.0, True

        enemy_mask = cv2.inRange(roi_hsv, LOWER_YELLOW, UPPER_YELLOW)
        enemy_mask = cv2.bitwise_or(enemy_mask, cv2.inRange(roi_hsv, LOWER_RED1, UPPER_RED1))
        enemy_mask = cv2.bitwise_or(enemy_mask, cv2.inRange(roi_hsv, LOWER_RED2, UPPER_RED2))
        if cv2.countNonZero(enemy_mask) > roi_area * 0.10:
            return float(self.MAX_COLOR_BONUS), False

        return 0.0, False

    def _fails_tracking_jump(self, point: tuple[float, float]) -> bool:
        if self.last_target_center is None:
            return False
        jump = math.hypot(
            point[0] - self.last_target_center[0],
            point[1] - self.last_target_center[1],
        )
        return jump > self.MAX_JUMP_PIXELS

    def _fails_first_pickup_flick(self, point: tuple[float, float]) -> bool:
        flick_dx = point[0] - self.screen_center_x
        flick_dy = point[1] - self.screen_center_y
        return abs(flick_dx) >= self.MAX_JUMP_PIXELS or abs(flick_dy) >= self.MAX_JUMP_PIXELS

    def _is_tracking_candidate(self, point: tuple[float, float], last_target_center: tuple[float, float] | None):
        if last_target_center is None:
            return False
        return math.hypot(point[0] - last_target_center[0], point[1] - last_target_center[1]) < self.TRACKING_RADIUS

    def _passes_geometry_gate(self, box_w: float, box_h: float, tracking_candidate: bool):
        aspect_ratio = box_h / box_w if box_w > 0.0 else 0.0
        if aspect_ratio < self.MIN_ASPECT_RATIO or aspect_ratio > self.MAX_ASPECT_RATIO:
            return False

        min_height = self.frame_height * (
            self.MIN_TRACKING_HEIGHT_RATIO if tracking_candidate else self.MIN_PICKUP_HEIGHT_RATIO
        )
        min_area = (self.frame_width * self.frame_height) * (
            self.MIN_TRACKING_AREA_RATIO if tracking_candidate else self.MIN_PICKUP_AREA_RATIO
        )
        return box_h >= min_height and (box_w * box_h) >= min_area

    def _passes_confidence_gate(self, conf: float, tracking_candidate: bool):
        min_conf = self.TRACKING_CONFIDENCE_THRESHOLD if tracking_candidate else self.PICKUP_CONFIDENCE_THRESHOLD
        return conf >= min_conf

    def _smooth_target_point(self, point: tuple[float, float]):
        if self.last_target_center is None:
            return point

        jump = math.hypot(
            point[0] - self.last_target_center[0],
            point[1] - self.last_target_center[1],
        )
        if jump <= 0.0 or jump >= self.MAX_SMOOTHING_JUMP_PIXELS:
            return point

        alpha = max(self.MIN_SMOOTHING_ALPHA, jump / self.MAX_SMOOTHING_JUMP_PIXELS)
        return (
            self.last_target_center[0] + ((point[0] - self.last_target_center[0]) * alpha),
            self.last_target_center[1] + ((point[1] - self.last_target_center[1]) * alpha),
        )

    def _tracking_distance(self, point: tuple[float, float], last_target_center: tuple[float, float] | None):
        if last_target_center is None:
            return None
        return math.hypot(point[0] - last_target_center[0], point[1] - last_target_center[1])

    def _tracking_bonus_for_distance(self, tracking_distance: float | None):
        if tracking_distance is None or tracking_distance >= self.TRACKING_RADIUS:
            return 0.0
        proximity = 1.0 - (tracking_distance / self.TRACKING_RADIUS)
        return self.TRACKING_BONUS * proximity

    def select_target(self, detections: list[ParsedDetections], frame: np.ndarray):
        last_target_center = self.last_target_center

        candidates = []
        for detection in detections:
            for i, (box, conf) in enumerate(zip(detection.boxes, detection.confs)):
                x1, y1, x2, y2 = box
                box_w = float(x2 - x1)
                box_h = float(y2 - y1)
                if box_w <= 0.0 or box_h <= 0.0:
                    continue

                tx, ty = self._get_target_point(box, detection.keypoints, i)
                tracking_candidate = self._is_tracking_candidate((tx, ty), last_target_center)
                if not self._passes_confidence_gate(float(conf), tracking_candidate):
                    continue
                if not self._passes_geometry_gate(box_w, box_h, tracking_candidate):
                    continue

                color_bonus, is_friendly = self._classify_color(box, frame)
                if is_friendly:
                    continue

                slow_zone = self._get_slow_zone(box, detection.keypoints, i)
                fire_zone = self._get_fire_zone(box)
                candidates.append((tx, ty, box_w, box_h, float(conf), color_bonus, slow_zone, fire_zone))

        if not candidates:
            self.reset_tracking()
            return None

        if len(candidates) == 1:
            tx, ty, _, _, conf, color_bonus, slow_zone, fire_zone = candidates[0]
            chosen = (tx, ty)
            chosen_score = color_bonus + (conf * self.CONFIDENCE_SCORE_SCALE)
            chosen_slow_zone = slow_zone
            chosen_fire_zone = fire_zone
        else:
            best = None
            best_score = -float("inf")
            best_slow_zone = None
            best_fire_zone = None
            tracked = None
            tracked_score = -float("inf")
            tracked_distance = None
            for tx, ty, box_w, box_h, conf, color_bonus, slow_zone, fire_zone in candidates:
                dist = math.hypot(tx - self.screen_center_x, ty - self.screen_center_y)
                score = -dist * 2.5 + color_bonus + (conf * self.CONFIDENCE_SCORE_SCALE)
                current_area = box_w * box_h
                if current_area > self.MAX_AREA_LIMIT:
                    score -= (current_area - self.MAX_AREA_LIMIT) * 0.1
                else:
                    area_diff = abs(current_area - self.IDEAL_AREA)
                    score += (self.IDEAL_AREA - area_diff) * 0.005
                candidate_tracking_distance = self._tracking_distance((tx, ty), last_target_center)
                score += self._tracking_bonus_for_distance(candidate_tracking_distance)
                if score > best_score:
                    best_score = score
                    best = (tx, ty)
                    best_slow_zone = slow_zone
                    best_fire_zone = fire_zone
                if candidate_tracking_distance is not None and candidate_tracking_distance < self.TRACKING_RADIUS:
                    if (
                        tracked is None
                        or candidate_tracking_distance < tracked_distance
                        or (
                            math.isclose(candidate_tracking_distance, tracked_distance)
                            and score > tracked_score
                        )
                    ):
                        tracked = (tx, ty)
                        tracked_score = score
                        tracked_distance = candidate_tracking_distance
                        tracked_slow_zone = slow_zone
                        tracked_fire_zone = fire_zone

            if best is None or best_score <= self.MIN_SCORE_THRESHOLD:
                self.reset_tracking()
                return None

            if (
                tracked is not None
                and best != tracked
                and best_score < (tracked_score + self.TRACKING_SWITCH_MARGIN)
            ):
                best = tracked
                best_score = tracked_score
                best_slow_zone = tracked_slow_zone
                best_fire_zone = tracked_fire_zone

            if last_target_center is None and self._fails_first_pickup_flick(best):
                self.reset_tracking()
                return None

            chosen = best
            chosen_score = best_score
            chosen_slow_zone = best_slow_zone
            chosen_fire_zone = best_fire_zone

        if self._fails_tracking_jump(chosen):
            self.reset_tracking()
            return None

        smoothed_chosen = self._smooth_target_point(chosen)
        self.last_target_center = smoothed_chosen
        return SelectedTarget(
            target_x=smoothed_chosen[0],
            target_y=smoothed_chosen[1],
            screen_center_x=self.screen_center_x,
            screen_center_y=self.screen_center_y,
            score=chosen_score,
            slow_zone=chosen_slow_zone,
            fire_zone=chosen_fire_zone,
        )

    def find_best_target(self, detections: list[ParsedDetections], frame: np.ndarray):
        selected_target = self.select_target(detections, frame)
        if selected_target is None:
            return None
        return selected_target.dx, selected_target.dy


class CrosshairPersonHitDetector:
    def __init__(
        self,
        crop_size: int = 640,
        edge_padding: int = 2,
        min_conf: float = 0.30,
        release_grace_frames: int = 4,
        frame_width: int | None = None,
        frame_height: int | None = None,
        target_selector: TargetSelector | None = None,
    ):
        width = float(frame_width if frame_width is not None else crop_size)
        height = float(frame_height if frame_height is not None else crop_size)
        self.center_x = width / 2.0
        self.center_y = height / 2.0
        self.edge_padding = edge_padding
        self.min_conf = min_conf
        self.release_grace_frames = release_grace_frames
        self.target_selector = target_selector
        self._holding = False
        self._miss_frames = 0

    def _is_crosshair_inside_zone(self, zone: tuple[float, float, float, float] | None):
        if zone is None:
            return False

        cx, cy = self.center_x, self.center_y
        pad = self.edge_padding
        left, top, right, bottom = zone
        return (left - pad) <= cx <= (right + pad) and (top - pad) <= cy <= (bottom + pad)

    def _is_crosshair_touching_selected_target(self, selected_target: SelectedTarget | None):
        if selected_target is None or selected_target.fire_zone is None:
            return False
        return self._is_crosshair_inside_zone(selected_target.fire_zone)

    def _is_crosshair_touching_filtered_detection(
        self,
        detections: list[ParsedDetections] | None,
        frame: np.ndarray | None,
    ):
        if self.target_selector is None or detections is None or frame is None:
            return False

        for detection in detections:
            for box, conf in zip(detection.boxes, detection.confs):
                if float(conf) < self.min_conf:
                    continue

                x1, y1, x2, y2 = box
                box_w = float(x2 - x1)
                box_h = float(y2 - y1)
                if box_w <= 0.0 or box_h <= 0.0:
                    continue
                if not self.target_selector._passes_geometry_gate(box_w, box_h, tracking_candidate=True):
                    continue

                _, is_friendly = self.target_selector._classify_color(box, frame)
                if is_friendly:
                    continue

                if self._is_crosshair_inside_zone(self.target_selector._get_fire_zone(box)):
                    return True

        return False

    def update(
        self,
        selected_target: SelectedTarget | None,
        detections: list[ParsedDetections] | None = None,
        frame: np.ndarray | None = None,
    ):
        if self._is_crosshair_touching_selected_target(selected_target) or self._is_crosshair_touching_filtered_detection(
            detections,
            frame,
        ):
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
