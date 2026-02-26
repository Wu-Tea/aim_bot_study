import cv2
import numpy as np
import time
import queue
from ultralytics import YOLO
# 假设你把上面的 Controller 代码保存为了 controller.py
# 假设你把之前的 ScreenCaptureThread 保存为了 vision.py
from controller import ControllerFactory
from vision import ScreenCaptureThread


def main():
    # 1. 初始化控制器 (填 False 用鼠标，填 True 用手柄)
    controller = ControllerFactory.get_controller(use_gamepad=True)

    # 2. 初始化视觉和截图模型
    print("[Init] 加载 YOLO 模型中...")
    model = YOLO("yolov8n-pose.engine")
    capture_thread = ScreenCaptureThread(target_fps=120, crop_size=400)
    capture_thread.start()

    # 找色参数
    lower_yellow = np.array([15, 80, 80])
    upper_yellow = np.array([35, 255, 255])

    print("[Ready] 系统已就绪。按住鼠标右键(或手柄测试键)开始平滑追踪！")

    try:
        while True:
            # ==========================================
            # 1. 始终获取最新画面 (保证预览窗口实时刷新)
            # ==========================================
            try:
                frame = capture_thread.frame_queue.get(timeout=0.1)
            except queue.Empty:
                continue

            # ==========================================
            # 3. YOLO 推理 (仅在开镜时执行)
            # ==========================================
            results = model.predict(source=frame, classes=[0], conf=0.45, verbose=False)
            target_found = False

            for result in results:
                # 针对 Pose 模型的解析
                boxes = result.boxes.xyxy.cpu().numpy()
                if result.keypoints is not None and result.keypoints.xy is not None:
                    keypoints = result.keypoints.xy.cpu().numpy()
                else:
                    continue

                for i, box in enumerate(boxes):
                    x1, y1, x2, y2 = map(int, box)

                    center_x = int((x1 + x2) / 2)
                    roi_x1 = max(0, center_x - 25)
                    roi_x2 = min(frame.shape[1], center_x + 25)
                    roi_y2 = y1
                    roi_y1 = max(0, roi_y2 - 50)

                    if roi_y2 <= roi_y1 or roi_x2 <= roi_x1:
                        continue

                    # hsv_roi = cv2.cvtColor(frame[roi_y1:roi_y2, roi_x1:roi_x2], cv2.COLOR_BGR2HSV)
                    # mask = cv2.inRange(hsv_roi, lower_yellow, upper_yellow)

                    # ==========================================
                    # 2. 触发器检测
                    # ==========================================
                    if not controller.is_aiming():
                        controller.reset()

                        time.sleep(0.001)
                        continue

                    # if cv2.countNonZero(mask) > 20:
                    person_kpts = keypoints[i]

                    left_shoulder_x, left_shoulder_y = person_kpts[5]
                    right_shoulder_x, right_shoulder_y = person_kpts[6]

                    if left_shoulder_x > 0 and right_shoulder_x > 0:
                        target_neck_x = (left_shoulder_x + right_shoulder_x) / 2
                        target_neck_y = (left_shoulder_y + right_shoulder_y) / 2
                    else:
                        target_neck_x = center_x
                        target_neck_y = y1 + (y2 - y1) * 0.15

                    delta_x = target_neck_x - 200.0
                    delta_y = target_neck_y - 200.0

                    controller.update(delta_x, delta_y)
                    target_found = True
                    break

                if target_found:
                    break

            if not target_found:
                controller.reset()

    except KeyboardInterrupt:
        pass
    finally:
        capture_thread.stop()
        controller.reset()
        print("[Exit] 程序已安全退出。")


if __name__ == "__main__":
    main()