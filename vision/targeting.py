import math
import time
from dataclasses import dataclass

import cv2
import numpy as np

from .occlusion_compensation import TargetOcclusionCompensator, TargetSource


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
    selected_box: tuple[float, float, float, float] | None = None
    slow_zone: tuple[float, float, float, float] | None = None
    fire_zone: tuple[float, float, float, float] | None = None
    source: str = TargetSource.OBSERVED

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
    PICKUP_CONFIDENCE_THRESHOLD = 0.65
    PICKUP_ENEMY_CONFIDENCE_THRESHOLD = 0.42
    TRACKING_CONFIDENCE_THRESHOLD = 0.40
    UPPER_CHEST_RATIO = 0.38
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
    MIN_SMOOTHING_ALPHA = 0.25
    TRACKING_SWITCH_MARGIN = 80.0
    MAX_JUMP_X_RATIO = 180.0 / 640.0
    MAX_JUMP_Y_RATIO = 180.0 / 640.0
    DISTANCE_SCORE_SCALE = 800.0
    TRACKING_RADIUS_RATIO = 120.0 / 640.0
    MAX_SMOOTHING_JUMP_RATIO = 24.0 / 640.0
    PICKUP_CONFIRM_RADIUS_RATIO = 32.0 / 640.0
    IDEAL_AREA_RATIO = 8000.0 / (640.0 * 640.0)
    MAX_AREA_LIMIT_RATIO = 40000.0 / (640.0 * 640.0)
    PICKUP_CONFIRM_FRAMES = 2
    TARGET_HOLD_FRAMES = 2
    SWITCH_CONFIRM_FRAMES = 2
    ACTIVE_TARGET_IOU_THRESHOLD = 0.12
    ACTIVE_TARGET_CENTER_X_RATIO = 0.65
    ACTIVE_TARGET_CENTER_Y_RATIO = 0.35
    ACTIVE_TARGET_SCORE_SWITCH_MARGIN = 2000.0
    SWITCH_CROSSHAIR_MARGIN_RATIO = 16.0 / 640.0
    CROSSHAIR_PRIORITY_MARGIN_RATIO = 10.0 / 640.0
    FRIENDLY_MASK_MIN_RATIO = 0.02
    FRIENDLY_MASK_MAX_RATIO = 0.35
    ENEMY_MASK_MIN_RATIO = 0.03
    ENEMY_MASK_MAX_RATIO = 0.45

    def __init__(
        self,
        crop_size: int = 640,
        frame_width: int | None = None,
        frame_height: int | None = None,
    ):
        width = float(frame_width if frame_width is not None else crop_size)
        height = float(frame_height if frame_height is not None else crop_size)
        self.frame_width = width
        self.frame_height = height
        self.screen_center_x = width / 2.0
        self.screen_center_y = height / 2.0
        avg_dim = (width + height) / 2.0
        frame_area = width * height
        self.TRACKING_BONUS = 2000
        self.MIN_SCORE_THRESHOLD = -50000
        self.max_jump_x = width * self.MAX_JUMP_X_RATIO
        self.max_jump_y = height * self.MAX_JUMP_Y_RATIO
        self.tracking_radius = avg_dim * self.TRACKING_RADIUS_RATIO
        self.max_smoothing_jump = avg_dim * self.MAX_SMOOTHING_JUMP_RATIO
        self.pickup_confirm_radius = avg_dim * self.PICKUP_CONFIRM_RADIUS_RATIO
        self.switch_crosshair_margin = avg_dim * self.SWITCH_CROSSHAIR_MARGIN_RATIO
        self.crosshair_priority_margin = avg_dim * self.CROSSHAIR_PRIORITY_MARGIN_RATIO
        self.ideal_area = frame_area * self.IDEAL_AREA_RATIO
        self.max_area_limit = frame_area * self.MAX_AREA_LIMIT_RATIO
        self._sample_clock = time.perf_counter
        self._compensator = TargetOcclusionCompensator()
        self.reset_tracking()

    def reset_tracking(self):
        self.last_target_center = None
        self._active_target: SelectedTarget | None = None
        self._pending_target: SelectedTarget | None = None
        self._pending_frames = 0
        self._pending_switch_target: SelectedTarget | None = None
        self._pending_switch_frames = 0
        self._hold_frames = 0
        self._compensator.reset()

    def _clear_pending(self):
        self._pending_target = None
        self._pending_frames = 0

    def _clear_switch_pending(self):
        self._pending_switch_target = None
        self._pending_switch_frames = 0

    def _create_selected_target(
        self,
        point: tuple[float, float],
        score: float,
        selected_box: tuple[float, float, float, float] | None,
        slow_zone: tuple[float, float, float, float] | None,
        fire_zone: tuple[float, float, float, float] | None,
        *,
        source: str = TargetSource.OBSERVED,
    ):
        return SelectedTarget(
            target_x=point[0],
            target_y=point[1],
            screen_center_x=self.screen_center_x,
            screen_center_y=self.screen_center_y,
            score=score,
            selected_box=selected_box,
            slow_zone=slow_zone,
            fire_zone=fire_zone,
            source=source,
        )

    @staticmethod
    def _box_center(box: tuple[float, float, float, float]):
        x1, y1, x2, y2 = box
        return ((x1 + x2) * 0.5, (y1 + y2) * 0.5)

    @staticmethod
    def _box_iou(
        lhs: tuple[float, float, float, float],
        rhs: tuple[float, float, float, float],
    ):
        left = max(lhs[0], rhs[0])
        top = max(lhs[1], rhs[1])
        right = min(lhs[2], rhs[2])
        bottom = min(lhs[3], rhs[3])
        inter_w = max(0.0, right - left)
        inter_h = max(0.0, bottom - top)
        inter_area = inter_w * inter_h
        if inter_area <= 0.0:
            return 0.0

        lhs_area = max(0.0, lhs[2] - lhs[0]) * max(0.0, lhs[3] - lhs[1])
        rhs_area = max(0.0, rhs[2] - rhs[0]) * max(0.0, rhs[3] - rhs[1])
        union_area = lhs_area + rhs_area - inter_area
        if union_area <= 0.0:
            return 0.0
        return inter_area / union_area

    def _boxes_match(
        self,
        lhs: tuple[float, float, float, float] | None,
        rhs: tuple[float, float, float, float] | None,
    ):
        if lhs is None or rhs is None:
            return False

        if self._box_iou(lhs, rhs) >= self.ACTIVE_TARGET_IOU_THRESHOLD:
            return True

        lhs_w = max(0.0, lhs[2] - lhs[0])
        lhs_h = max(0.0, lhs[3] - lhs[1])
        rhs_w = max(0.0, rhs[2] - rhs[0])
        rhs_h = max(0.0, rhs[3] - rhs[1])
        lhs_cx, lhs_cy = self._box_center(lhs)
        rhs_cx, rhs_cy = self._box_center(rhs)
        allowed_dx = max(8.0, max(lhs_w, rhs_w) * self.ACTIVE_TARGET_CENTER_X_RATIO)
        allowed_dy = max(8.0, max(lhs_h, rhs_h) * self.ACTIVE_TARGET_CENTER_Y_RATIO)
        return abs(lhs_cx - rhs_cx) <= allowed_dx and abs(lhs_cy - rhs_cy) <= allowed_dy

    def _targets_match(self, lhs: SelectedTarget, rhs: SelectedTarget):
        if self._boxes_match(lhs.selected_box, rhs.selected_box):
            return True
        return (
            math.hypot(lhs.target_x - rhs.target_x, lhs.target_y - rhs.target_y)
            <= self.pickup_confirm_radius
        )

    def _confirm_pickup(self, target: SelectedTarget):
        required = max(1, int(self.PICKUP_CONFIRM_FRAMES))
        if required <= 1:
            return target

        if self._pending_target is None or not self._targets_match(self._pending_target, target):
            self._pending_target = target
            self._pending_frames = 1
            return None

        self._pending_target = target
        self._pending_frames += 1
        if self._pending_frames < required:
            return None

        self._clear_pending()
        return target

    def _confirm_switch(self, target: SelectedTarget):
        required = max(1, int(self.SWITCH_CONFIRM_FRAMES))
        if required <= 1:
            return target

        if self._pending_switch_target is None or not self._targets_match(self._pending_switch_target, target):
            self._pending_switch_target = target
            self._pending_switch_frames = 1
            return None

        self._pending_switch_target = target
        self._pending_switch_frames += 1
        if self._pending_switch_frames < required:
            return None

        self._clear_switch_pending()
        return target

    def _hold_or_reset(self):
        self._clear_pending()
        if self._active_target is None:
            self.reset_tracking()
            return None

        if self._hold_frames < self.TARGET_HOLD_FRAMES:
            self._hold_frames += 1
            return self._active_target

        self.reset_tracking()
        return None

    def _commit_target(self, target: SelectedTarget, *, clear_switch_pending: bool = True):
        if self._active_target is None:
            target = self._confirm_pickup(target)
            if target is None:
                return None
        else:
            self._clear_pending()

        if clear_switch_pending:
            self._clear_switch_pending()
        self._active_target = target
        self._hold_frames = 0
        self.last_target_center = (target.target_x, target.target_y)
        return target

    def _commit_and_record_target(
        self,
        target: SelectedTarget,
        *,
        sample_timestamp: float,
        clear_switch_pending: bool = True,
    ):
        committed = self._commit_target(target, clear_switch_pending=clear_switch_pending)
        if committed is not None and committed.source != TargetSource.PREDICTED:
            self._compensator.record_observation(committed, timestamp=sample_timestamp)
        return committed

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

    @staticmethod
    def _normalize_box(box: np.ndarray):
        return tuple(float(value) for value in box)

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
        friendly_ratio = cv2.countNonZero(friendly_mask) / roi_area
        if self.FRIENDLY_MASK_MIN_RATIO <= friendly_ratio <= self.FRIENDLY_MASK_MAX_RATIO:
            return 0.0, True

        enemy_mask = cv2.inRange(roi_hsv, LOWER_YELLOW, UPPER_YELLOW)
        enemy_mask = cv2.bitwise_or(enemy_mask, cv2.inRange(roi_hsv, LOWER_RED1, UPPER_RED1))
        enemy_mask = cv2.bitwise_or(enemy_mask, cv2.inRange(roi_hsv, LOWER_RED2, UPPER_RED2))
        enemy_ratio = cv2.countNonZero(enemy_mask) / roi_area
        if self.ENEMY_MASK_MIN_RATIO <= enemy_ratio <= self.ENEMY_MASK_MAX_RATIO:
            return float(self.MAX_COLOR_BONUS), False

        return 0.0, False

    def _fails_tracking_jump(self, point: tuple[float, float]) -> bool:
        if self.last_target_center is None:
            return False
        dx = point[0] - self.last_target_center[0]
        dy = point[1] - self.last_target_center[1]
        return abs(dx) > self.max_jump_x or abs(dy) > self.max_jump_y

    def _fails_first_pickup_flick(self, point: tuple[float, float]) -> bool:
        flick_dx = point[0] - self.screen_center_x
        flick_dy = point[1] - self.screen_center_y
        return abs(flick_dx) >= self.max_jump_x or abs(flick_dy) >= self.max_jump_y

    def _is_tracking_candidate(self, point: tuple[float, float], last_target_center: tuple[float, float] | None):
        if last_target_center is None:
            return False
        return math.hypot(point[0] - last_target_center[0], point[1] - last_target_center[1]) < self.tracking_radius

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

    def _passes_confidence_gate(self, conf: float, tracking_candidate: bool, enemy_colored: bool = False):
        if tracking_candidate:
            min_conf = self.TRACKING_CONFIDENCE_THRESHOLD
        elif enemy_colored:
            min_conf = self.PICKUP_ENEMY_CONFIDENCE_THRESHOLD
        else:
            min_conf = self.PICKUP_CONFIDENCE_THRESHOLD
        return conf >= min_conf

    def _smooth_target_point(self, point: tuple[float, float]):
        if self.last_target_center is None:
            return point

        jump = math.hypot(
            point[0] - self.last_target_center[0],
            point[1] - self.last_target_center[1],
        )
        if jump <= 0.0 or jump >= self.max_smoothing_jump:
            return point

        alpha = max(self.MIN_SMOOTHING_ALPHA, jump / self.max_smoothing_jump)
        return (
            self.last_target_center[0] + ((point[0] - self.last_target_center[0]) * alpha),
            self.last_target_center[1] + ((point[1] - self.last_target_center[1]) * alpha),
        )

    def _tracking_distance(self, point: tuple[float, float], last_target_center: tuple[float, float] | None):
        if last_target_center is None:
            return None
        return math.hypot(point[0] - last_target_center[0], point[1] - last_target_center[1])

    def _tracking_bonus_for_distance(self, tracking_distance: float | None):
        if tracking_distance is None or tracking_distance >= self.tracking_radius:
            return 0.0
        proximity = 1.0 - (tracking_distance / self.tracking_radius)
        return self.TRACKING_BONUS * proximity

    def _crosshair_distance(self, point: tuple[float, float]):
        return math.hypot(point[0] - self.screen_center_x, point[1] - self.screen_center_y)

    def _active_target_matches_candidate(
        self,
        candidate_box: tuple[float, float, float, float] | None,
        candidate_point: tuple[float, float],
    ):
        if self._active_target is None:
            return False

        if self._boxes_match(self._active_target.selected_box, candidate_box):
            return True

        return math.hypot(
            candidate_point[0] - self._active_target.target_x,
            candidate_point[1] - self._active_target.target_y,
        ) <= self.pickup_confirm_radius

    def _should_switch_targets(self, locked_target: SelectedTarget, challenger: SelectedTarget):
        if challenger.score >= (locked_target.score + self.ACTIVE_TARGET_SCORE_SWITCH_MARGIN):
            return True
        locked_crosshair_distance = self._crosshair_distance((locked_target.target_x, locked_target.target_y))
        challenger_crosshair_distance = self._crosshair_distance((challenger.target_x, challenger.target_y))
        return challenger_crosshair_distance < (locked_crosshair_distance - self.switch_crosshair_margin)

    def _prefer_candidate(
        self,
        current_point: tuple[float, float] | None,
        current_score: float,
        challenger_point: tuple[float, float],
        challenger_score: float,
    ):
        if current_point is None:
            return True

        current_crosshair_distance = self._crosshair_distance(current_point)
        challenger_crosshair_distance = self._crosshair_distance(challenger_point)
        if challenger_crosshair_distance < (current_crosshair_distance - self.crosshair_priority_margin):
            return True
        if current_crosshair_distance < (challenger_crosshair_distance - self.crosshair_priority_margin):
            return False
        return challenger_score > current_score

    def select_target(self, detections: list[ParsedDetections], frame: np.ndarray):
        last_target_center = self.last_target_center
        sample_timestamp = self._sample_clock()

        candidates = []
        for detection in detections:
            for i, (box, conf) in enumerate(zip(detection.boxes, detection.confs)):
                normalized_box = self._normalize_box(box)
                x1, y1, x2, y2 = normalized_box
                box_w = float(x2 - x1)
                box_h = float(y2 - y1)
                if box_w <= 0.0 or box_h <= 0.0:
                    continue

                point = self._get_target_point(box, detection.keypoints, i)
                selected_box = normalized_box
                source = TargetSource.OBSERVED
                if self._active_target is not None:
                    reconstructed = self._compensator.try_reconstruct(
                        normalized_box,
                        timestamp=sample_timestamp,
                    )
                    if reconstructed is not None:
                        point = (reconstructed.target_x, reconstructed.target_y)
                        selected_box = reconstructed.selected_box
                        source = reconstructed.source

                box_w = float(selected_box[2] - selected_box[0])
                box_h = float(selected_box[3] - selected_box[1])
                tracking_candidate = self._is_tracking_candidate(point, last_target_center)
                if not self._passes_geometry_gate(box_w, box_h, tracking_candidate):
                    continue

                selected_box_array = np.array(selected_box, dtype=np.float32)
                color_bonus, is_friendly = self._classify_color(selected_box_array, frame)
                if is_friendly:
                    continue
                if not self._passes_confidence_gate(
                    float(conf),
                    tracking_candidate,
                    enemy_colored=(color_bonus > 0.0),
                ):
                    continue

                if source == TargetSource.OBSERVED:
                    slow_zone = self._get_slow_zone(selected_box_array, detection.keypoints, i)
                else:
                    slow_zone = self._fallback_slow_zone(selected_box_array)
                fire_zone = self._get_fire_zone(selected_box_array)
                candidates.append(
                    (
                        point[0],
                        point[1],
                        box_w,
                        box_h,
                        float(conf),
                        color_bonus,
                        selected_box,
                        slow_zone,
                        fire_zone,
                        source,
                    )
                )

        if not candidates:
            self._clear_switch_pending()
            predicted = self._compensator.try_predict(timestamp=sample_timestamp)
            if predicted is not None:
                predicted_box = np.array(predicted.selected_box, dtype=np.float32)
                return self._commit_target(
                    self._create_selected_target(
                        (predicted.target_x, predicted.target_y),
                        self._active_target.score if self._active_target is not None else 0.0,
                        predicted.selected_box,
                        self._fallback_slow_zone(predicted_box),
                        self._get_fire_zone(predicted_box),
                        source=predicted.source,
                    )
                )
            self.reset_tracking()
            return None

        active_match_target = None
        if len(candidates) == 1:
            tx, ty, _, _, conf, color_bonus, selected_box, slow_zone, fire_zone, source = candidates[0]
            chosen_target = self._create_selected_target(
                (tx, ty),
                color_bonus + (conf * self.CONFIDENCE_SCORE_SCALE),
                selected_box,
                slow_zone,
                fire_zone,
                source=source,
            )
            if self._active_target_matches_candidate(selected_box, (tx, ty)):
                active_match_target = chosen_target
        else:
            best = None
            best_score = -float("inf")
            best_box = None
            best_slow_zone = None
            best_fire_zone = None
            best_source = TargetSource.OBSERVED
            tracked = None
            tracked_score = -float("inf")
            tracked_distance = None
            tracked_box = None
            tracked_slow_zone = None
            tracked_fire_zone = None
            tracked_source = TargetSource.OBSERVED
            active_match = None
            active_match_score = -float("inf")
            active_match_distance = None
            active_match_box = None
            active_match_slow_zone = None
            active_match_fire_zone = None
            active_match_source = TargetSource.OBSERVED
            half_w = self.frame_width / 2.0
            half_h = self.frame_height / 2.0
            for tx, ty, box_w, box_h, conf, color_bonus, selected_box, slow_zone, fire_zone, source in candidates:
                norm_dx = (tx - self.screen_center_x) / half_w
                norm_dy = (ty - self.screen_center_y) / half_h
                dist_norm = math.hypot(norm_dx, norm_dy)
                score = -dist_norm * self.DISTANCE_SCORE_SCALE + color_bonus + (conf * self.CONFIDENCE_SCORE_SCALE)
                current_area = box_w * box_h
                if current_area > self.max_area_limit:
                    score -= (current_area - self.max_area_limit) * 0.1
                else:
                    area_diff = abs(current_area - self.ideal_area)
                    score += (self.ideal_area - area_diff) * 0.005
                candidate_tracking_distance = self._tracking_distance((tx, ty), last_target_center)
                score += self._tracking_bonus_for_distance(candidate_tracking_distance)
                if self._prefer_candidate(best, best_score, (tx, ty), score):
                    best_score = score
                    best = (tx, ty)
                    best_box = selected_box
                    best_slow_zone = slow_zone
                    best_fire_zone = fire_zone
                    best_source = source
                if candidate_tracking_distance is not None and candidate_tracking_distance < self.tracking_radius:
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
                        tracked_box = selected_box
                        tracked_slow_zone = slow_zone
                        tracked_fire_zone = fire_zone
                        tracked_source = source
                if self._active_target_matches_candidate(selected_box, (tx, ty)):
                    candidate_active_distance = math.hypot(
                        tx - self._active_target.target_x,
                        ty - self._active_target.target_y,
                    )
                    if (
                        active_match is None
                        or candidate_active_distance < active_match_distance
                        or (
                            math.isclose(candidate_active_distance, active_match_distance)
                            and score > active_match_score
                        )
                    ):
                        active_match = (tx, ty)
                        active_match_score = score
                        active_match_distance = candidate_active_distance
                        active_match_box = selected_box
                        active_match_slow_zone = slow_zone
                        active_match_fire_zone = fire_zone
                        active_match_source = source

            if best is None or best_score <= self.MIN_SCORE_THRESHOLD:
                self._clear_switch_pending()
                return self._hold_or_reset()

            if (
                self._active_target is None
                and
                tracked is not None
                and best != tracked
                and best_score < (tracked_score + self.TRACKING_SWITCH_MARGIN)
            ):
                best = tracked
                best_score = tracked_score
                best_box = tracked_box
                best_slow_zone = tracked_slow_zone
                best_fire_zone = tracked_fire_zone
                best_source = tracked_source

            chosen_target = self._create_selected_target(
                best,
                best_score,
                best_box,
                best_slow_zone,
                best_fire_zone,
                source=best_source,
            )
            if active_match is not None:
                active_match_target = self._create_selected_target(
                    active_match,
                    active_match_score,
                    active_match_box,
                    active_match_slow_zone,
                    active_match_fire_zone,
                    source=active_match_source,
                )

        preserve_switch_pending = False
        if self._active_target is not None:
            if active_match_target is not None:
                if not self._targets_match(chosen_target, active_match_target):
                    if self._should_switch_targets(active_match_target, chosen_target):
                        confirmed_switch = self._confirm_switch(chosen_target)
                        if confirmed_switch is None:
                            chosen_target = active_match_target
                            preserve_switch_pending = True
                        else:
                            chosen_target = confirmed_switch
                    else:
                        self._clear_switch_pending()
                        chosen_target = active_match_target
                else:
                    self._clear_switch_pending()
                    chosen_target = active_match_target
            else:
                confirmed_switch = self._confirm_switch(chosen_target)
                if confirmed_switch is None:
                    return self._hold_or_reset()
                chosen_target = confirmed_switch
        else:
            self._clear_switch_pending()

        chosen_point = (chosen_target.target_x, chosen_target.target_y)
        if last_target_center is None and self._fails_first_pickup_flick(chosen_point):
            self._clear_pending()
            self._clear_switch_pending()
            return None

        if self._fails_tracking_jump(chosen_point):
            self._clear_switch_pending()
            return self._hold_or_reset()

        smoothed_chosen = self._smooth_target_point(chosen_point)
        return self._commit_and_record_target(
            self._create_selected_target(
                smoothed_chosen,
                chosen_target.score,
                chosen_target.selected_box,
                chosen_target.slow_zone,
                chosen_target.fire_zone,
                source=chosen_target.source,
            ),
            sample_timestamp=sample_timestamp,
            clear_switch_pending=not preserve_switch_pending,
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
        if self._is_crosshair_touching_selected_target(selected_target):
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
