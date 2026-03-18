import cv2
import numpy as np
import dxcam
import threading
import queue
import time
from ultralytics import YOLO
import win32api
import math


class ScreenCaptureThread(threading.Thread):
    def __init__(self, target_fps=120, crop_size=640):
        super().__init__()
        self.daemon = True
        self.target_fps = target_fps
        self.crop_size = crop_size
        # 自动计算屏幕中心裁剪区域
        screen_width = win32api.GetSystemMetrics(0)
        screen_height = win32api.GetSystemMetrics(1)
        left = (screen_width - crop_size) // 2
        top = (screen_height - crop_size) // 2
        self.region = (left, top, left + crop_size, top + crop_size)
        self.camera = dxcam.create(output_color="BGR", region=self.region)
        self.frame_queue = queue.Queue(maxsize=1)
        self.running = True
        self.camera.start(target_fps=self.target_fps, video_mode=True)

    def run(self):
        while self.running:
            frame = self.camera.get_latest_frame()
            if frame is not None:
                if self.frame_queue.full():
                    try:
                        self.frame_queue.get_nowait()
                    except queue.Empty:
                        pass
                self.frame_queue.put(frame)
            time.sleep(1 / (self.target_fps * 2))

    def stop(self):
        self.running = False
        self.camera.stop()


class TargetSelector:
    """
    Encapsulates the logic for selecting the best target from YOLO model outputs.
    This class handles scoring, filtering, and tracking of targets.
    """
    def __init__(self, crop_size=640):
        # Color configurations for teammate filtering
        self.lower_yellow = np.array([15, 120, 120])
        self.upper_yellow = np.array([35, 255, 255])
        self.lower_green = np.array([45, 80, 50])
        self.upper_green = np.array([75, 255, 255])
        self.lower_blue = np.array([90, 80, 50])
        self.upper_blue = np.array([115, 255, 255])

        # Tracking state
        self.last_target_center = None

        # Center of the capture region
        self.screen_center_x = crop_size / 2.0
        self.screen_center_y = crop_size / 2.0
        
        # --- Tuning Parameters ---
        self.TRACKING_BONUS = 2000      # Score bonus for locking onto the same target
        self.TRACKING_RADIUS = 120      # Radius to consider a target as the "same" one
        self.IDEAL_AREA = 8000          # Ideal target area for scoring
        self.MAX_AREA_LIMIT = 40000     # Ignore targets larger than this
        self.CONFIDENCE_THRESHOLD = 0.45 # Confidence for keypoints
        self.MIN_SCORE_THRESHOLD = -50000 # Minimum score to consider a target valid
        self.MAX_JUMP_PIXELS = 180      # Max pixels the crosshair can jump in one frame

    def reset_tracking(self):
        """Resets the tracking state."""
        self.last_target_center = None

    def _get_target_point(self, box, keypoints, box_index):
        """Calculates the optimal target point (tx, ty) using keypoints or box ratios."""
        x1, y1, x2, y2 = map(int, box)
        box_w, box_h = x2 - x1, y2 - y1
        
        # Priority 1: Use keypoints for upper chest
        if keypoints is not None and len(keypoints) > box_index:
            kpts = keypoints[box_index]
            l_shoulder, r_shoulder = kpts[5], kpts[6]  # 5: left_shoulder, 6: right_shoulder
            nose = kpts[0]

            # If both shoulders are visible, target their midpoint
            if l_shoulder[2] > self.CONFIDENCE_THRESHOLD and r_shoulder[2] > self.CONFIDENCE_THRESHOLD:
                tx = (l_shoulder[0] + r_shoulder[0]) / 2.0
                ty = (l_shoulder[1] + r_shoulder[1]) / 2.0
                return tx, ty
            # Fallback to nose if visible
            elif nose[2] > self.CONFIDENCE_THRESHOLD:
                tx = nose[0]
                ty = nose[1] + (box_h * 0.05) # Slightly below the nose
                return tx, ty

        # Fallback 2: Use box aspect ratio if keypoints are unreliable
        tx = x1 + (box_w / 2.0)
        aspect_ratio = box_h / box_w if box_w > 0 else 0
        # Adjust vertical offset based on posture (standing vs. crouching/sliding)
        ratio = 0.20 if aspect_ratio > 1.2 else 0.40
        ty = y1 + (box_h * ratio)
        return tx, ty

    def find_best_target(self, results, frame):
        """
        Analyzes YOLO results to find the best target based on a scoring system.
        Returns the delta (dx, dy) to the best target, or None if no suitable target is found.
        """
        best_target_abs = None
        highest_score = -float('inf')

        for result in results:
            if result.boxes is None: continue
            
            boxes = result.boxes.xyxy.cpu().numpy()
            keypoints = result.keypoints.data.cpu().numpy() if result.keypoints is not None else None

            for i, box in enumerate(boxes):
                x1, y1, x2, y2 = map(int, box)
                box_w, box_h = x2 - x1, y2 - y1
                if box_w <= 0 or box_h <= 0: continue

                tx, ty = self._get_target_point(box, keypoints, i)
                if tx is None: continue

                # --- Scoring Logic ---
                # 1. Base score: Proximity to crosshair
                dist = math.hypot(tx - self.screen_center_x, ty - self.screen_center_y)
                score = -dist * 2.5

                # 2. Teammate filter (penalty for friendly colors)
                roi = frame[max(0, y1 - 50):y1, max(0, int(tx) - 25):min(frame.shape[1], int(tx) + 25)]
                if roi.size > 0:
                    hsv = cv2.cvtColor(roi, cv2.COLOR_BGR2HSV)
                    # Penalty for green/blue (common friendly UI colors)
                    if cv2.countNonZero(cv2.bitwise_or(cv2.inRange(hsv, self.lower_green, self.upper_green),
                                                       cv2.inRange(hsv, self.lower_blue, self.upper_blue))) > 15:
                        score -= 100000
                    # Bonus for yellow (common enemy UI colors)
                    if cv2.countNonZero(cv2.inRange(hsv, self.lower_yellow, self.upper_yellow)) > 20:
                        score += 10000

                # 3. Area weight (prefer ideal size, penalize too-close or too-small)
                current_area = box_w * box_h
                if current_area > self.MAX_AREA_LIMIT:
                    score -= (current_area - self.MAX_AREA_LIMIT) * 0.1
                else:
                    area_diff = abs(current_area - self.IDEAL_AREA)
                    score += (self.IDEAL_AREA - area_diff) * 0.005

                # 4. Tracking bonus (prioritize last frame's target)
                if self.last_target_center:
                    if math.hypot(tx - self.last_target_center[0], ty - self.last_target_center[1]) < self.TRACKING_RADIUS:
                        score += self.TRACKING_BONUS

                # Update best target if current score is higher
                if score > highest_score:
                    highest_score = score
                    best_target_abs = (tx, ty)

        # --- Final Selection and Safety Checks ---
        if best_target_abs and highest_score > self.MIN_SCORE_THRESHOLD:
            best_target_delta = (best_target_abs[0] - self.screen_center_x, best_target_abs[1] - self.screen_center_y)
            
            # Safety check: prevent sudden large jumps
            if abs(best_target_delta[0]) < self.MAX_JUMP_PIXELS and abs(best_target_delta[1]) < self.MAX_JUMP_PIXELS:
                self.last_target_center = best_target_abs
                return best_target_delta
        
        # No suitable target found, reset tracking
        self.reset_tracking()
        return None


def process_vision(controller=None):
    """
    Main function for vision processing. Orchestrates screen capture, model inference,
    and target selection to send commands to the controller.
    """
    CROP_SIZE = 640
    # Load TensorRT engine with a fallback to the .pt file for robustness
    try:
        model = YOLO("yolov8n-pose.engine", task='pose')
        print("Successfully loaded TensorRT engine.")
    except Exception as e:
        print(f"Failed to load TensorRT engine: {e}. Falling back to yolov8n-pose.pt.")
        model = YOLO("yolov8n-pose.pt", task='pose')

    capture_thread = ScreenCaptureThread(target_fps=120, crop_size=CROP_SIZE)
    target_selector = TargetSelector(crop_size=CROP_SIZE)
    
    capture_thread.start()
    print("Vision processing started. Waiting for aim input...")

    try:
        while True:
            # If not aiming, reduce CPU usage and prepare for the next aim action
            if controller and not controller.is_aiming():
                controller.reset()
                target_selector.reset_tracking()
                
                # Clear the queue of old frames to ensure responsiveness
                while not capture_thread.frame_queue.empty():
                    try:
                        capture_thread.frame_queue.get_nowait()
                    except queue.Empty:
                        break
                time.sleep(0.01)  # Sleep to yield CPU
                continue

            # Get the latest frame from the queue
            try:
                frame = capture_thread.frame_queue.get(timeout=0.1)
            except queue.Empty:
                continue

            # Run inference
            results = model.predict(source=frame, classes=[0], conf=0.55, verbose=False, half=True)

            # Find the best target using the selector
            best_target_delta = target_selector.find_best_target(results, frame)

            # Dispatch command to controller
            if best_target_delta and controller:
                controller.update(best_target_delta[0], best_target_delta[1])
            elif controller:
                controller.reset()

            # Allow for graceful exit (though main.py handles KeyboardInterrupt)
            if cv2.waitKey(1) & 0xFF == ord('q'):
                break
    finally:
        print("Stopping vision processing.")
        capture_thread.stop()
