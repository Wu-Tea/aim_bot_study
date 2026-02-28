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
                    try: self.frame_queue.get_nowait()
                    except queue.Empty: pass
                self.frame_queue.put(frame)
            time.sleep(1 / (self.target_fps * 2))

    def stop(self):
        self.running = False
        self.camera.stop()

def process_vision(controller=None):
    # 这里必须用你自己的模型路径
    model = YOLO("yolov8n-pose.engine")
    capture_thread = ScreenCaptureThread(target_fps=120, crop_size=640)
    capture_thread.start()

    # 颜色与权重配置
    lower_yellow = np.array([15, 120, 120]); upper_yellow = np.array([35, 255, 255])
    lower_green = np.array([45, 80, 50]); upper_green = np.array([75, 255, 255])
    lower_blue = np.array([90, 80, 50]); upper_blue = np.array([115, 255, 255])

    last_target_center = None
    TRACKING_BONUS = 2000
    TRACKING_RADIUS = 120

    try:
        while True:
            try:
                frame = capture_thread.frame_queue.get(timeout=0.1)
            except queue.Empty: continue

            # ==========================================
            # 【核心逻辑 1】L2 触发器监听
            # ==========================================
            if controller and not controller.is_aiming():
                controller.reset()
                last_target_center = None
                continue

            # YOLO 推理
            results = model.predict(source=frame, classes=[0], conf=0.45, verbose=False, half=True)

            best_target_delta = None
            best_target_abs = None
            highest_score = -float('inf')

            for result in results:
                boxes = result.boxes.xyxy.cpu().numpy()
                for box in boxes:
                    x1, y1, x2, y2 = map(int, box)
                    box_w, box_h = x2 - x1, y2 - y1
                    if box_w <= 0 or box_h <= 0: continue

                    # 1. 坐标计算 (上胸 20% 处)
                    tx, ty = x1 + (box_w / 2.0), y1 + (box_h * 0.20)
                    dist = math.hypot(tx - 320.0, ty - 320.0)

                    # 2. 队友过滤 (蓝绿扣分)
                    roi = frame[max(0, y1-50):y1, max(0, int(tx)-25):min(640, int(tx)+25)]
                    score = 0
                    if roi.size > 0:
                        hsv = cv2.cvtColor(roi, cv2.COLOR_BGR2HSV)
                        if cv2.countNonZero(cv2.bitwise_or(cv2.inRange(hsv, lower_green, upper_green),
                                                           cv2.inRange(hsv, lower_blue, upper_blue))) > 15:
                            score -= 100000
                        if cv2.countNonZero(cv2.inRange(hsv, lower_yellow, upper_yellow)) > 20:
                            score += 10000

                    # 3. 距离/面积/贪心打分
                    score -= dist * 2.5
                    score += (box_w * box_h) * 0.005
                    if last_target_center:
                        if math.hypot(tx - last_target_center[0], ty - last_target_center[1]) < TRACKING_RADIUS:
                            score += TRACKING_BONUS

                    if score > highest_score:
                        highest_score = score
                        best_target_delta = (tx - 320.0, ty - 320.0)
                        best_target_abs = (tx, ty)

            # ==========================================
            # 【核心逻辑 2】指令下发
            # ==========================================
            if best_target_delta and highest_score > -50000:
                last_target_center = best_target_abs
                if controller:
                    controller.update(best_target_delta[0], best_target_delta[1])
            else:
                last_target_center = None
                if controller: controller.reset()

            if cv2.waitKey(1) & 0xFF == ord('q'): break
    finally:
        capture_thread.stop()