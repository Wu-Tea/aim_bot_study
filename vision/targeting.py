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
    TORSO_BOX_SHRINK_X = 0.22
    TORSO_BOX_SHRINK_TOP = 0.18
    TORSO_BOX_SHRINK_BOTTOM = 0.20
    TORSO_KEYPOINT_SHRINK_X = 0.10

    def __init__(self, crop_size: int = 640):
        self.last_target_center = None
        self.screen_center_x = crop_size / 2.0
        self.screen_center_y = crop_size / 2.0
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
        ty = y1 + (box_h * (0.20 if aspect_ratio > 1.2 else 0.40))
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
        return self._keypoint_slow_zone(box, keypoints, box_index) or self._fallback_slow_zone(box)

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

    def select_target(self, detections: list[ParsedDetections], frame: np.ndarray):
        last_target_center = self.last_target_center

        candidates = []
        for detection in detections:
            for i, box in enumerate(detection.boxes):
                x1, y1, x2, y2 = box
                box_w = float(x2 - x1)
                box_h = float(y2 - y1)
                if box_w <= 0.0 or box_h <= 0.0:
                    continue

                tx, ty = self._get_target_point(box, detection.keypoints, i)
                color_bonus, is_friendly = self._classify_color(box, frame)
                if is_friendly:
                    continue

                slow_zone = self._get_slow_zone(box, detection.keypoints, i)
                candidates.append((tx, ty, box_w, box_h, color_bonus, slow_zone))

        if not candidates:
            self.reset_tracking()
            return None

        if len(candidates) == 1:
            tx, ty, _, _, color_bonus, slow_zone = candidates[0]
            chosen = (tx, ty)
            chosen_score = color_bonus
            chosen_slow_zone = slow_zone
        else:
            best = None
            best_score = -float("inf")
            best_slow_zone = None
            for tx, ty, box_w, box_h, color_bonus, slow_zone in candidates:
                dist = math.hypot(tx - self.screen_center_x, ty - self.screen_center_y)
                score = -dist * 2.5 + color_bonus
                current_area = box_w * box_h
                if current_area > self.MAX_AREA_LIMIT:
                    score -= (current_area - self.MAX_AREA_LIMIT) * 0.1
                else:
                    area_diff = abs(current_area - self.IDEAL_AREA)
                    score += (self.IDEAL_AREA - area_diff) * 0.005
                if last_target_center and math.hypot(tx - last_target_center[0], ty - last_target_center[1]) < self.TRACKING_RADIUS:
                    score += self.TRACKING_BONUS
                if score > best_score:
                    best_score = score
                    best = (tx, ty)
                    best_slow_zone = slow_zone

            if best is None or best_score <= self.MIN_SCORE_THRESHOLD:
                self.reset_tracking()
                return None

            if last_target_center is None and self._fails_first_pickup_flick(best):
                self.reset_tracking()
                return None

            chosen = best
            chosen_score = best_score
            chosen_slow_zone = best_slow_zone

        if self._fails_tracking_jump(chosen):
            self.reset_tracking()
            return None

        self.last_target_center = chosen
        return SelectedTarget(
            target_x=chosen[0],
            target_y=chosen[1],
            screen_center_x=self.screen_center_x,
            screen_center_y=self.screen_center_y,
            score=chosen_score,
            slow_zone=chosen_slow_zone,
        )

    def find_best_target(self, detections: list[ParsedDetections], frame: np.ndarray):
        selected_target = self.select_target(detections, frame)
        if selected_target is None:
            return None
        return selected_target.dx, selected_target.dy


class CrosshairPersonHitDetector:
    FIRE_SHRINK_X = 0.12
    FIRE_SHRINK_TOP = 0.05
    FIRE_SHRINK_BOTTOM = 0.15

    def __init__(self, crop_size: int = 640, edge_padding: int = 2, min_conf: float = 0.35, release_grace_frames: int = 4):
        self.center_x = crop_size / 2.0
        self.center_y = crop_size / 2.0
        self.edge_padding = edge_padding
        self.min_conf = min_conf
        self.release_grace_frames = release_grace_frames
        self._holding = False
        self._miss_frames = 0

    def _fire_box_for(self, box: np.ndarray):
        x1, y1, x2, y2 = box
        box_w = float(x2 - x1)
        box_h = float(y2 - y1)
        return (
            x1 + box_w * self.FIRE_SHRINK_X,
            y1 + box_h * self.FIRE_SHRINK_TOP,
            x2 - box_w * self.FIRE_SHRINK_X,
            y2 - box_h * self.FIRE_SHRINK_BOTTOM,
        )

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
        if self._is_crosshair_touching_person(detections):
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
