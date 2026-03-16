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
    def __init__(self, target_fps=120, crop_size=400):
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


def process_vision(controller=None):
    # 加载 TensorRT 引擎以实现 2026 年的高性能推理需求
    model = YOLO("yolov8n-pose.engine")
    capture_thread = ScreenCaptureThread(target_fps=120, crop_size=640)
    capture_thread.start()

    # 颜色配置（用于队友过滤）
    lower_yellow = np.array([15, 120, 120]);
    upper_yellow = np.array([35, 255, 255])
    lower_green = np.array([45, 80, 50]);
    upper_green = np.array([75, 255, 255])
    lower_blue = np.array([90, 80, 50]);
    upper_blue = np.array([115, 255, 255])

    last_target_center = None
    TRACKING_BONUS = 2000  # 连续追踪奖励
    TRACKING_RADIUS = 120  # 追踪判定半径

    try:
        while True:
            try:
                frame = capture_thread.frame_queue.get(timeout=0.1)
            except queue.Empty:
                continue

            # 只有在开镜（L2/右键）时才进行 AI 计算
            if controller and not controller.is_aiming():
                controller.reset()
                last_target_center = None
                continue

            # 执行 YOLOv8-Pose 推理
            results = model.predict(source=frame, classes=[0], conf=0.55, verbose=False, half=True)

            best_target_delta = None
            best_target_abs = None
            highest_score = -float('inf')

            for result in results:
                boxes = result.boxes.xyxy.cpu().numpy()
                # 提取关键点 (Keypoints) 数据
                keypoints = result.keypoints.data.cpu().numpy() if result.keypoints is not None else None

                for i, box in enumerate(boxes):
                    x1, y1, x2, y2 = map(int, box)
                    box_w, box_h = x2 - x1, y2 - y1
                    if box_w <= 0 or box_h <= 0: continue

                    # ==========================================
                    # 1. 动态部位锁定 (解决滑铲上漂的核心逻辑)
                    # ==========================================
                    tx, ty = None, None

                    # 优先：使用骨骼关键点定位上胸部 (肩膀中点)
                    if keypoints is not None and len(keypoints) > i:
                        kpts = keypoints[i]
                        l_sh, r_sh = kpts[5], kpts[6]  # 5:左肩, 6:右肩

                        if l_sh[2] > 0.45 and r_sh[2] > 0.45:
                            tx = (l_sh[0] + r_sh[0]) / 2.0
                            ty = (l_sh[1] + r_sh[1]) / 2.0
                        elif kpts[0][2] > 0.45:  # 鼻子保底
                            tx = kpts[0][0]
                            ty = kpts[0][1] + (box_h * 0.05)

                    # 保底：如果关键点丢失，根据框的高宽比动态调整偏移
                    if tx is None or ty is None:
                        tx = x1 + (box_w / 2.0)
                        aspect_ratio = box_h / box_w
                        # 站立(高)比例为0.2，蹲滑(扁)比例下移至0.4，防止准星飞到头顶空气
                        ratio = 0.20 if aspect_ratio > 1.2 else 0.40
                        ty = y1 + (box_h * ratio)

                    # ==========================================
                    # 2. 综合打分系统 (距离 + 面积权重 + 队友过滤)
                    # ==========================================
                    # 基础分：距离准星中心越近分越高
                    dist = math.hypot(tx - 320.0, ty - 320.0)
                    score = -dist * 2.5

                    # 队友过滤：检测头顶 UI 颜色
                    roi = frame[max(0, y1 - 50):y1, max(0, int(tx) - 25):min(400, int(tx) + 25)]
                    if roi.size > 0:
                        hsv = cv2.cvtColor(roi, cv2.COLOR_BGR2HSV)
                        if cv2.countNonZero(cv2.bitwise_or(cv2.inRange(hsv, lower_green, upper_green),
                                                           cv2.inRange(hsv, lower_blue, upper_blue))) > 15:
                            score -= 100000
                        if cv2.countNonZero(cv2.inRange(hsv, lower_yellow, upper_yellow)) > 20:
                            score += 10000

                    # 面积权重：解决“近大远小”与“过大干扰”
                    IDEAL_AREA = 8000  # 理想锁定目标大小
                    MAX_AREA_LIMIT = 40000  # 超过此面积视为无效/贴脸目标
                    current_area = box_w * box_h

                    if current_area > MAX_AREA_LIMIT:
                        score -= (current_area - MAX_AREA_LIMIT) * 0.1
                    else:
                        area_diff = abs(current_area - IDEAL_AREA)
                        score += (IDEAL_AREA - area_diff) * 0.005

                    # 追踪奖励：优先锁定上一帧的目标
                    if last_target_center:
                        if math.hypot(tx - last_target_center[0], ty - last_target_center[1]) < TRACKING_RADIUS:
                            score += TRACKING_BONUS

                    # 更新最优目标
                    if score > highest_score:
                        highest_score = score
                        best_target_delta = (tx - 320.0, ty - 320.0)
                        best_target_abs = (tx, ty)

            # ==========================================
            # 3. 控制器指令分发
            # ==========================================
            if best_target_delta and highest_score > -50000:
                # 最后的安全检查：防止准星突跳
                if abs(best_target_delta[0]) < 180 and abs(best_target_delta[1]) < 180:
                    last_target_center = best_target_abs

                    if controller:
                        controller.update(best_target_delta[0], best_target_delta[1])
                else:
                    if controller: controller.reset()
            else:
                last_target_center = None
                if controller: controller.reset()

            if cv2.waitKey(1) & 0xFF == ord('q'): break
    finally:
        capture_thread.stop()