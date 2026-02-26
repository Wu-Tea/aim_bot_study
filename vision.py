import cv2
import numpy as np
import dxcam
import threading
import queue
import time
from ultralytics import YOLO
import win32api


# ==========================================
# 1. 优化后的高频截图守护线程
# ==========================================
class ScreenCaptureThread(threading.Thread):
    def __init__(self, target_fps=120, crop_size=640):
        super().__init__()
        self.daemon = True
        self.target_fps = target_fps
        self.crop_size = crop_size

        # 动态获取主屏幕分辨率
        screen_width = win32api.GetSystemMetrics(0)
        screen_height = win32api.GetSystemMetrics(1)


        # 计算中心 400x400 的 region (left, top, right, bottom)
        left = (screen_width - crop_size) // 2
        top = (screen_height - crop_size) // 2
        right = left + crop_size
        bottom = top + crop_size
        self.region = (left, top, right, bottom)

        print(f"[Capture] 截取区域设定为: {self.region}")

        # 传入 region 参数，dxcam 现在只会抓取这 400x400 的区域
        self.camera = dxcam.create(output_color="BGR", region=self.region)
        self.frame_queue = queue.Queue(maxsize=1)
        self.running = True
        self.camera.start(target_fps=self.target_fps, video_mode=True)

    # run() 和 stop() 方法保持不变...
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


# ==========================================
# 2. 视觉推理与校验核心逻辑
# ==========================================
def process_vision():
    print("[Init] 正在加载 YOLOv8n 模型...")
    # 第一次运行会自动下载 yolov8n.pt，建议提前放到同级目录
    model = YOLO("yolov8n.pt")

    print("[Init] 启动高频截图线程...")
    capture_thread = ScreenCaptureThread(target_fps=120)
    capture_thread.start()

    # 目标颜色 #FFE607 对应的 HSV 容差范围
    # H (色相): 黄色大约在 20-35 之间 (OpenCV 中 H 范围是 0-179)
    # S (饱和度), V (亮度): 预留较大的下限以兼容游戏阴影
    lower_yellow = np.array([15, 120, 120])
    upper_yellow = np.array([35, 255, 255])

    print("\n[Start] 开始视觉侦测！按 'q' 键退出预览窗口。")

    try:
        while True:
            # 1. 从队列获取最新画面
            try:
                frame = capture_thread.frame_queue.get(timeout=0.1)
            except queue.Empty:
                continue

            # 2. YOLOv8 推理 (过滤 class=0 person, conf>0.55)
            # verbose=False 关闭控制台疯狂打印
            results = model.predict(source=frame, classes=[0], conf=0.55, verbose=False, half=True)

            valid_targets = []

            for result in results:
                boxes = result.boxes.xyxy.cpu().numpy()  # 获取 [x1, y1, x2, y2]

                for box in boxes:
                    x1, y1, x2, y2 = map(int, box)

                    # 3. 计算上方 50x50 的 ROI 区域
                    center_x = int((x1 + x2) / 2)
                    roi_x1 = max(0, center_x - 25)
                    roi_x2 = min(frame.shape[1], center_x + 25)
                    roi_y2 = y1  # ROI 的底部就是 Bounding Box 的顶部
                    roi_y1 = max(0, roi_y2 - 50)

                    # 检查 ROI 是否有效 (防止目标在屏幕极上边缘导致切片错误)
                    if roi_y2 <= roi_y1 or roi_x2 <= roi_x1:
                        continue

                    roi_frame = frame[roi_y1:roi_y2, roi_x1:roi_x2]

                    # 4. 找色校验 (HSV 匹配)
                    hsv_roi = cv2.cvtColor(roi_frame, cv2.COLOR_BGR2HSV)
                    mask = cv2.inRange(hsv_roi, lower_yellow, upper_yellow)

                    # 统计符合颜色的像素点数量
                    match_pixels = cv2.countNonZero(mask)

                    # 假设 50x50 (2500像素) 中有至少 20 个像素符合目标黄色，即判定为合法目标
                    is_valid = match_pixels > 20

                    if is_valid:
                        # 1. 计算目标的绝对中心坐标
                        target_center_x = (x1 + x2) / 2

                        # 2. 估算脖子位置 (通常是框顶往下 15% 处)
                        target_neck_y = y1 + (y2 - y1) * 0.15

                        # 3. 计算距离 400x400 画面中心点 (200, 200) 的偏移量
                        # 画面中心点 = capture_thread.crop_size / 2
                        center_point = 200.0

                        delta_x = target_center_x - center_point
                        delta_y = target_neck_y - center_point

                        print(f"锁定目标！偏移量: dx={delta_x:.1f}, dy={delta_y:.1f}")

                        # 【重要】在这里，你应该将 delta_x 和 delta_y 发送给 Controller
                        # controller.update(delta_x, delta_y)

                        color = (0, 255, 0)
                        label = f"Valid: dx={int(delta_x)} dy={int(delta_y)}"
                    else:
                        color = (0, 0, 255)
                        label = "Invalid"

                    # --- 绘制 Preview 可视化 ---
                    # 画人物大框
                    cv2.rectangle(frame, (x1, y1), (x2, y2), color, 2)
                    # 画上方 50x50 的 ROI 区域
                    cv2.rectangle(frame, (roi_x1, roi_y1), (roi_x2, roi_y2), (255, 255, 0), 2)
                    # 贴字
                    cv2.putText(frame, label, (x1, y1 - 60), cv2.FONT_HERSHEY_SIMPLEX, 0.6, color, 2)

            # 缩放预览窗口大小 (防止 1080p/2k 原图占满屏幕)
            preview_frame = cv2.resize(frame, (960, 540))
            cv2.imshow("Vision Preview", preview_frame)

            # 按 'q' 退出
            if cv2.waitKey(1) & 0xFF == ord('q'):
                break

    finally:
        capture_thread.stop()
        cv2.destroyAllWindows()
        print("[Exit] 视觉模块已安全关闭。")


if __name__ == "__main__":
    process_vision()