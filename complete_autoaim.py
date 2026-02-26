"""
complete_autoaim.py
====================

该模块实现了一个完整的自动瞄准辅助系统，集成了屏幕截取、目标检测、
颜色判定、偏移量计算以及鼠标/手柄控制。核心思想是将视觉推理与控制
逻辑解耦，通过独立线程运行 YOLO 检测，从而利用 GPU 加速并保持主
循环的高刷新率。用户只需运行本脚本，它会自动检测硬件并根据需要
使用鼠标或虚拟 DS4 手柄实现平滑的拉枪效果。

主要组件：

1. **ScreenCaptureThread**：使用 ``dxcam`` 持续截取屏幕中央的固定区域，
   将最新帧推送到队列中，供检测线程使用。
2. **MouseController / GamepadController**：根据用户是否连接手柄，
   控制鼠标移动或虚拟 DS4 手柄摇杆，提供平滑跟踪。
3. **DetectionThread**：独立线程，读取截取的帧，在按压瞄准键时调用
   YOLO 模型检测 ``person`` 类目标，并在目标头顶区域寻找指定颜色。
   若匹配成功，则计算目标脖子与屏幕中心的偏移量，写入共享数据结构。
4. **主循环**：检测瞄准键是否按下；按下时从检测线程获取最新偏移量，
   调用控制器平滑移动；松开时复位控制器。

为了获得最佳性能，脚本会在检测 GPU 可用时自动将模型加载到显卡，
并启用半精度 (FP16) 推理【903186689271889†L560-L567】。如果系统不支持 CUDA，则自动回落到 CPU。

运行说明：

```
python complete_autoaim.py --gamepad    # 使用虚拟 DS4 手柄控制模式
python complete_autoaim.py              # 默认使用鼠标控制模式
```

按住鼠标右键或手柄 L2 键即会启动检测和自动拉枪，松开则停止。按 'q' 退出程序。

注：使用本脚本前，请确保安装了 ultralytics、dxcam、vgamepad、pywin32 等依赖。
"""

import cv2
import numpy as np
import threading
import queue
import time
import math
import argparse
import ctypes

# 导入 torch 以检测 GPU 是否可用
import torch

from ultralytics import YOLO

# Windows 平台依赖
import win32api
import win32con

# 可选导入 vgamepad，用于虚拟 DS4 手柄
try:
    import vgamepad as vg
    VG_AVAILABLE = True
except ImportError:
    VG_AVAILABLE = False

# 可选导入 dxcam，用于高效截屏
try:
    import dxcam
    DXCAM_AVAILABLE = True
except ImportError:
    DXCAM_AVAILABLE = False


class ScreenCaptureThread(threading.Thread):
    """高频截屏线程。

    使用 dxcam 创建摄像头对象，只截取屏幕中心的固定区域。在运行中不断
    获取最新帧并推入队列，保证实时性。若 dxcam 不可用，则本线程无法
    正常工作。
    """

    def __init__(self, target_fps: int = 60, crop_size: int = 640):
        super().__init__(daemon=True)
        if not DXCAM_AVAILABLE:
            raise RuntimeError("dxcam 模块不可用，无法截屏")

        self.target_fps = target_fps
        self.crop_size = crop_size
        # 动态获取主屏幕分辨率
        screen_width = win32api.GetSystemMetrics(0)
        screen_height = win32api.GetSystemMetrics(1)
        left = (screen_width - crop_size) // 2
        top = (screen_height - crop_size) // 2
        right = left + crop_size
        bottom = top + crop_size
        self.region = (left, top, right, bottom)
        print(f"[Capture] 截取区域设定为: {self.region}")
        # 创建摄像头对象，指定输出为 BGR，并指定截取区域
        self.camera = dxcam.create(output_color="BGR", region=self.region)
        self.frame_queue: queue.Queue[np.ndarray] = queue.Queue(maxsize=1)
        self.running = False

    def run(self) -> None:
        self.running = True
        # 启动摄像头，设置 video_mode=True 以降低延迟
        self.camera.start(target_fps=self.target_fps, video_mode=True)
        # 按两倍采样间隔遍历，防止压满队列
        while self.running:
            frame = self.camera.get_latest_frame()
            if frame is not None:
                if self.frame_queue.full():
                    try:
                        self.frame_queue.get_nowait()
                    except queue.Empty:
                        pass
                self.frame_queue.put(frame)
            # 睡眠以控制循环频率
            time.sleep(1.0 / (self.target_fps * 2))

    def stop(self) -> None:
        self.running = False
        try:
            self.camera.stop()
        except Exception:
            pass


class BaseController:
    """控制器基类。定义了基本接口。"""

    def update(self, delta_x: float, delta_y: float) -> None:
        raise NotImplementedError

    def reset(self) -> None:
        raise NotImplementedError

    def is_aiming(self) -> bool:
        raise NotImplementedError


class MouseController(BaseController):
    """基于鼠标移动的控制器。

    相较于默认的平滑移动，本实现采用固定帧数内移动至目标位置的策略。
    默认在 10 帧内将准星移动到目标脖子位置，不再使用动态平滑系数。
    可以通过调整 ``frames_to_target`` 来改变拉枪速度。
    """

    def __init__(self, frames_to_target: int = 10, max_step: int = 50) -> None:
        # 每次 update 调用时按照目标偏移的 1/frames_to_target 移动
        # 例如 frames_to_target=10 意味着在约 10 帧内完全移动至目标位置
        self.frames_to_target = max(frames_to_target, 1)
        self.k = 1.0 / self.frames_to_target
        # 单帧最大移动像素，防止过快移动造成跳动
        self.max_step = max_step
        # Windows API 鼠标移动事件
        self.MOUSEEVENTF_MOVE = 0x0001

    def is_aiming(self) -> bool:
        # 鼠标右键为 0x02，通过 GetAsyncKeyState 检测是否按下
        return win32api.GetAsyncKeyState(0x02) < 0

    def update(self, delta_x: float, delta_y: float) -> None:
        # 直接按照 1/frames_to_target 的比例移动
        move_x = delta_x * self.k
        move_y = delta_y * self.k
        # 限制每帧最大移动量
        move_x = max(-self.max_step, min(self.max_step, move_x))
        move_y = max(-self.max_step, min(self.max_step, move_y))
        # 对小于 1 像素的移动进行方向性强制移动
        move_x = int(move_x) if abs(move_x) >= 1 else (1 if move_x > 0 else -1)
        move_y = int(move_y) if abs(move_y) >= 1 else (1 if move_y > 0 else -1)
        ctypes.windll.user32.mouse_event(self.MOUSEEVENTF_MOVE, move_x, move_y, 0, 0)

    def reset(self) -> None:
        # 鼠标无需特别复位，但保留接口
        pass


class GamepadController(BaseController):
    """基于 vgamepad 的虚拟 DS4 手柄控制器。

    将偏移量映射到右摇杆 [-1, 1] 区间，通过指数平滑获得更平滑的移动体验。"""

    def __init__(self, smoothing: float = 0.8, max_pixels: float = 100.0) -> None:
        if not VG_AVAILABLE:
            raise RuntimeError("vgamepad 模块不可用，无法使用手柄模式")
        self.gamepad = vg.VDS4Gamepad()
        self.smoothing = smoothing
        self.max_pixels = max_pixels
        self.current_stick_x = 0.0
        self.current_stick_y = 0.0

    def is_aiming(self) -> bool:
        # 真实手柄读取 L2 需专门的库，这里使用键盘 Left Alt 作为触发
        return (win32api.GetAsyncKeyState(0x12) & 0x8000) != 0

    def _map_pixel_to_stick(self, delta: float) -> float:
        # 将像素偏移限制到 [-max_pixels, max_pixels] 并归一化到 [-1, 1]
        delta = max(-self.max_pixels, min(self.max_pixels, delta))
        return delta / self.max_pixels

    def update(self, delta_x: float, delta_y: float) -> None:
        target_stick_x = self._map_pixel_to_stick(delta_x)
        target_stick_y = self._map_pixel_to_stick(delta_y)
        # 指数滑动平均，平滑摇杆运动
        self.current_stick_x = (self.current_stick_x * self.smoothing) + (target_stick_x * (1.0 - self.smoothing))
        self.current_stick_y = (self.current_stick_y * self.smoothing) + (target_stick_y * (1.0 - self.smoothing))
        # Y 轴取负以匹配游戏坐标
        self.gamepad.right_joystick_float(x_value_float=self.current_stick_x, y_value_float=-self.current_stick_y)
        self.gamepad.update()

    def reset(self) -> None:
        # 复位摇杆
        if self.current_stick_x != 0.0 or self.current_stick_y != 0.0:
            self.current_stick_x = 0.0
            self.current_stick_y = 0.0
            self.gamepad.right_joystick_float(x_value_float=0.0, y_value_float=0.0)
            self.gamepad.update()


class ControllerFactory:
    """根据参数返回 MouseController 或 GamepadController。"""

    @staticmethod
    def get_controller(use_gamepad: bool = False) -> BaseController:
        if use_gamepad:
            print("[Init] 加载 DS4 虚拟手柄控制器...")
            return GamepadController()
        else:
            print("[Init] 加载鼠标控制器...")
            return MouseController()


class DeltaHolder:
    """用于在检测线程和主线程之间安全共享偏移量数据。"""

    def __init__(self) -> None:
        self.lock = threading.Lock()
        self.dx: float = 0.0
        self.dy: float = 0.0
        self.found: bool = False

    def update(self, dx: float, dy: float, found: bool) -> None:
        with self.lock:
            self.dx = dx
            self.dy = dy
            self.found = found

    def get(self) -> tuple[float, float, bool]:
        with self.lock:
            return self.dx, self.dy, self.found


class DetectionThread(threading.Thread):
    """独立线程：在用户按下瞄准键时运行 YOLO 检测，并计算偏移量。"""

    def __init__(
        self,
        model: YOLO,
        capture_thread: ScreenCaptureThread,
        delta_holder: DeltaHolder,
        aiming_event: threading.Event,
        conf_threshold: float = 0.45,
        lower_yellow: tuple[int, int, int] = (15, 80, 80),
        upper_yellow: tuple[int, int, int] = (35, 255, 255),
        device: str | int = "cpu",
        use_half: bool = False,
    ) -> None:
        super().__init__(daemon=True)
        self.model = model
        self.capture_thread = capture_thread
        self.delta_holder = delta_holder
        self.aiming_event = aiming_event
        self.conf_threshold = conf_threshold
        # 颜色阈值 (HSV)
        self.lower_yellow = np.array(lower_yellow, dtype=np.uint8)
        self.upper_yellow = np.array(upper_yellow, dtype=np.uint8)
        self.running = False
        # 预计算中心坐标，稍后在 run 中更新
        self.center_point = None
        # 推理相关设置：指定设备和是否使用半精度
        self.device = device
        self.use_half = use_half

    def run(self) -> None:
        self.running = True
        # 计算中心点坐标（截取区域一半）
        crop_size = self.capture_thread.crop_size
        self.center_point = crop_size / 2.0
        while self.running:
            try:
                # 获取最新帧
                frame = self.capture_thread.frame_queue.get(timeout=0.1)
            except queue.Empty:
                continue

            # 如果没在瞄准状态，则不做检测，避免浪费计算资源
            if not self.aiming_event.is_set():
                # 重置偏移量
                self.delta_holder.update(0.0, 0.0, False)
                time.sleep(0.001)
                continue

            # 调用 YOLO 模型，过滤 person 类，禁用日志
            try:
                results = self.model.predict(
                    frame,
                    classes=[0],
                    conf=self.conf_threshold,
                    verbose=False,
                    device=self.device,
                    half=self.use_half,
                )
            except Exception as e:
                print(f"[Error] YOLO 推理失败: {e}")
                time.sleep(0.01)
                continue

            target_found = False
            delta_x, delta_y = 0.0, 0.0

            for result in results:
                # 提取检测框
                boxes = result.boxes.xyxy.cpu().numpy()
                # 遍历所有 person 检测框
                for box in boxes:
                    x1, y1, x2, y2 = map(int, box[:4])
                    # 计算 ROI 区域 (目标上方 50x50)
                    center_x = int((x1 + x2) / 2)
                    roi_x1 = max(0, center_x - 25)
                    roi_x2 = min(frame.shape[1], center_x + 25)
                    roi_y2 = y1
                    roi_y1 = max(0, roi_y2 - 50)
                    # 检查 ROI 范围合法
                    if roi_y2 <= roi_y1 or roi_x2 <= roi_x1:
                        continue
                    roi_frame = frame[roi_y1:roi_y2, roi_x1:roi_x2]
                    # HSV 颜色匹配
                    hsv_roi = cv2.cvtColor(roi_frame, cv2.COLOR_BGR2HSV)
                    mask = cv2.inRange(hsv_roi, self.lower_yellow, self.upper_yellow)
                    match_pixels = cv2.countNonZero(mask)
                    # 简单规则：超过 20 个像素视为匹配
                    if match_pixels > 20:
                        # 计算目标脖子近似位置：框顶往下 15%
                        target_neck_x = (x1 + x2) / 2.0
                        target_neck_y = y1 + (y2 - y1) * 0.15
                        delta_x = target_neck_x - self.center_point
                        delta_y = target_neck_y - self.center_point
                        target_found = True
                        break
                if target_found:
                    break

            # 更新共享变量
            self.delta_holder.update(delta_x, delta_y, target_found)

    def stop(self) -> None:
        self.running = False


def main() -> None:
    parser = argparse.ArgumentParser(description="自动瞄准辅助系统")
    parser.add_argument(
        "--gamepad",
        action="store_true",
        help="使用虚拟 DS4 手柄模式，默认使用鼠标控制模式",
    )
    parser.add_argument(
        "--model",
        type=str,
        default="yolov8n.pt",
        help="YOLO 模型权重路径 (默认为 yolov8n.pt)",
    )
    parser.add_argument(
        "--conf",
        type=float,
        default=0.45,
        help="YOLO 置信度阈值 (默认 0.45)",
    )
    parser.add_argument(
        "--fps",
        type=int,
        default=60,
        help="截屏线程目标 FPS (默认 60)",
    )
    parser.add_argument(
        "--crop",
        type=int,
        default=640,
        help="截屏区域大小 (默认 640)",
    )
    parser.add_argument(
        "--mouse_frames",
        type=int,
        default=10,
        help="鼠标在多少帧内移动到目标位置 (默认 10 帧)",
    )
    args = parser.parse_args()

    # 检查 dxcam 是否可用
    if not DXCAM_AVAILABLE:
        print("[Error] dxcam 模块不可用，无法启动截屏。请安装 dxcam 或使用其他截屏方式。")
        return

    # 检测 CUDA
    use_cuda = torch.cuda.is_available()
    device = 0 if use_cuda else "cpu"
    if use_cuda:
        print("[Init] 检测到可用 GPU，模型将运行在 CUDA 上，并启用半精度 (FP16)。")
    else:
        print("[Init] 未检测到 GPU，将在 CPU 上运行模型。")

    # 加载 YOLO 模型
    print("[Init] 正在加载 YOLO 模型...")
    try:
        model = YOLO(args.model)
        # 将模型移动到指定设备
        model.to(device)
        # 不再在此处调用 model.half()，统一在 predict 调用时设置 half 参数
    except Exception as e:
        print(f"[Error] 加载模型失败: {e}")
        return

    # 初始化截屏线程
    capture_thread = ScreenCaptureThread(target_fps=args.fps, crop_size=args.crop)
    capture_thread.start()

    # 初始化控制器
    if args.gamepad:
        controller: BaseController = ControllerFactory.get_controller(use_gamepad=True)
    else:
        # 将 frames_to_target 传入 MouseController
        controller = MouseController(frames_to_target=args.mouse_frames)

    # 初始化共享偏移量容器与瞄准状态事件
    delta_holder = DeltaHolder()
    aiming_event = threading.Event()

    # 初始化检测线程，并传入推理 device 与 half 参数
    detection_thread = DetectionThread(
        model=model,
        capture_thread=capture_thread,
        delta_holder=delta_holder,
        aiming_event=aiming_event,
        conf_threshold=args.conf,
        device=device,
        use_half=use_cuda,
    )
    detection_thread.start()

    print("[Ready] 系统已就绪。按住右键 (或手柄 L2) 开始自动瞄准，按 'q' 退出。")

    try:
        while True:
            # 获取最新截屏用于显示 (即便未瞄准也刷新)
            try:
                frame = capture_thread.frame_queue.get(timeout=0.1)
            except queue.Empty:
                frame = None

            # 检测瞄准状态
            if controller.is_aiming():
                aiming_event.set()
                # 从检测线程获取最新偏移
                dx, dy, found = delta_holder.get()
                if found:
                    controller.update(dx, dy)
                else:
                    # 未找到目标，避免鼠标漂移
                    controller.reset()
            else:
                aiming_event.clear()
                controller.reset()

            # 绘制屏幕中央准星供参考
            if frame is not None:
                center = int(args.crop / 2)
                cv2.circle(frame, (center, center), 2, (0, 0, 255), -1)
                cv2.imshow("Preview", frame)
                # 按下 'q' 退出
                if cv2.waitKey(1) & 0xFF == ord('q'):
                    break

            # 控制循环帧率
            time.sleep(1.0 / args.fps)

    except KeyboardInterrupt:
        pass
    finally:
        # 停止所有线程并释放资源
        print("\n[Exit] 正在关闭线程...")
        capture_thread.stop()
        detection_thread.stop()
        controller.reset()
        cv2.destroyAllWindows()
        print("[Exit] 程序已安全退出。")


if __name__ == "__main__":
    main()