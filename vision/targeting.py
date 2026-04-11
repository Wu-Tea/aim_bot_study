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

    @property
    def dx(self):
        return self.target_x - self.screen_center_x

    @property
    def dy(self):
        return self.target_y - self.screen_center_y


class TargetSelector:
    MAX_COLOR_BONUS = 10000
    CONFIDENCE_THRESHOLD = 0.40

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

    def select_target(self, detections: list[ParsedDetections], frame: np.ndarray):
        best_target_abs = None
        highest_score = -float("inf")
        frame_h, frame_w = frame.shape[:2]
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

                current_area = box_w * box_h
                if current_area > self.MAX_AREA_LIMIT:
                    score -= (current_area - self.MAX_AREA_LIMIT) * 0.1
                else:
                    area_diff = abs(current_area - self.IDEAL_AREA)
                    score += (self.IDEAL_AREA - area_diff) * 0.005

                if last_target_center and math.hypot(tx - last_target_center[0], ty - last_target_center[1]) < self.TRACKING_RADIUS:
                    score += self.TRACKING_BONUS

                if score + self.MAX_COLOR_BONUS <= highest_score:
                    continue

                cx = (x1 + x2) * 0.5
                roi_h = int(max(12, min(36, box_h * 0.20)))
                roi_w = int(max(24, min(80, box_w * 0.80)))
                roi_bottom = max(0, min(frame_h, int(y1) - 2))
                roi_top = max(0, roi_bottom - roi_h)
                roi_left = max(0, int(cx - roi_w / 2))
                roi_right = min(frame_w, int(cx + roi_w / 2))

                if (roi_bottom - roi_top) >= 4 and (roi_right - roi_left) >= 4:
                    roi_hsv = cv2.cvtColor(frame[roi_top:roi_bottom, roi_left:roi_right], cv2.COLOR_RGB2HSV)
                    roi_area = roi_hsv.shape[0] * roi_hsv.shape[1]

                    friendly_mask = cv2.bitwise_or(
                        cv2.inRange(roi_hsv, LOWER_GREEN, UPPER_GREEN),
                        cv2.inRange(roi_hsv, LOWER_BLUE, UPPER_BLUE),
                    )
                    if cv2.countNonZero(friendly_mask) > roi_area * 0.08:
                        score -= 100000

                    enemy_mask = cv2.inRange(roi_hsv, LOWER_YELLOW, UPPER_YELLOW)
                    enemy_mask = cv2.bitwise_or(enemy_mask, cv2.inRange(roi_hsv, LOWER_RED1, UPPER_RED1))
                    enemy_mask = cv2.bitwise_or(enemy_mask, cv2.inRange(roi_hsv, LOWER_RED2, UPPER_RED2))
                    if cv2.countNonZero(enemy_mask) > roi_area * 0.10:
                        score += self.MAX_COLOR_BONUS

                if score > highest_score:
                    highest_score = score
                    best_target_abs = (tx, ty)

        if best_target_abs and highest_score > self.MIN_SCORE_THRESHOLD:
            if last_target_center is not None:
                jump = math.hypot(best_target_abs[0] - last_target_center[0], best_target_abs[1] - last_target_center[1])
                if jump > self.MAX_JUMP_PIXELS:
                    self.reset_tracking()
                    return None
            else:
                flick_dx = best_target_abs[0] - self.screen_center_x
                flick_dy = best_target_abs[1] - self.screen_center_y
                if abs(flick_dx) >= self.MAX_JUMP_PIXELS or abs(flick_dy) >= self.MAX_JUMP_PIXELS:
                    self.reset_tracking()
                    return None

            self.last_target_center = best_target_abs
            return SelectedTarget(
                target_x=best_target_abs[0],
                target_y=best_target_abs[1],
                screen_center_x=self.screen_center_x,
                screen_center_y=self.screen_center_y,
                score=highest_score,
            )

        self.reset_tracking()
        return None

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
