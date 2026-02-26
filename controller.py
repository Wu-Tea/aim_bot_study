import win32api
import threading
import vgamepad as vg
import time
import os
os.environ['PYGAME_HIDE_SUPPORT_PROMPT'] = "hide"
import pygame

# ==========================================
# 建立 XInput 位掩码 到 vgamepad 按键的完美映射字典
# ==========================================
XINPUT_BUTTON_MAP = {
    0x1000: vg.XUSB_BUTTON.XUSB_GAMEPAD_A,
    0x2000: vg.XUSB_BUTTON.XUSB_GAMEPAD_B,
    0x4000: vg.XUSB_BUTTON.XUSB_GAMEPAD_X,
    0x8000: vg.XUSB_BUTTON.XUSB_GAMEPAD_Y,
    0x0001: vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_UP,      # 十字键上
    0x0002: vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_DOWN,    # 十字键下
    0x0004: vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_LEFT,    # 十字键左
    0x0008: vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_RIGHT,   # 十字键右
    0x0100: vg.XUSB_BUTTON.XUSB_GAMEPAD_LEFT_SHOULDER,  # LB / L1
    0x0200: vg.XUSB_BUTTON.XUSB_GAMEPAD_RIGHT_SHOULDER, # RB / R1
    0x0040: vg.XUSB_BUTTON.XUSB_GAMEPAD_LEFT_THUMB,     # 左摇杆按下 (L3)
    0x0080: vg.XUSB_BUTTON.XUSB_GAMEPAD_RIGHT_THUMB,    # 右摇杆按下 (R3)
    0x0010: vg.XUSB_BUTTON.XUSB_GAMEPAD_START,          # Start / Options
    0x0020: vg.XUSB_BUTTON.XUSB_GAMEPAD_BACK,           # Back / Share
}

class BaseController:
    def update(self, delta_x, delta_y): pass
    def reset(self): pass
    def is_aiming(self): return False

class MouseController(threading.Thread):
    def __init__(self, smooth=0.5, sensitivity_multiplier=0.7):
        super().__init__()
        self.daemon = True  # 守护线程
        self.smooth = smooth  # 平滑系数
        self.target_dx = 0.0  # 共享黑板上的 X 偏移
        self.target_dy = 0.0  # 共享黑板上的 Y 偏移
        self.lock = threading.Lock()  # 线程锁，防止读写冲突
        self.running = True
        self.MOUSEEVENTF_MOVE = 0x0001

        # 核心：将屏幕像素转换为鼠标实际移动量的比例系数
        # 如果总是拉过头，说明这个值应该小于 1.0
        self.multiplier = sensitivity_multiplier

        # 启动鼠标平滑移动独立线程
        self.start()

    def update(self, dx, dy):
        """YOLO 线程调用此方法，更新最新的偏移量 (大脑发指令)"""
        with self.lock:
            self.target_dx = dx * self.multiplier
            self.target_dy = dy * self.multiplier
            # self.target_dx = dx
            # self.target_dy = dy

    def reset(self):
        """松开开镜键时，清空偏移量，瞬间停止"""
        with self.lock:
            self.target_dx = 0.0
            self.target_dy = 0.0

    def is_aiming(self):
        # 0x02 是右键。用 < 0 判断最稳
        return win32api.GetAsyncKeyState(0x02) < 0

    def run(self):
        """鼠标控制独立线程：以 120Hz 高频执行平滑移动 (手在干活)"""
        while self.running:
            with self.lock:
                # 只有偏移量大于 1 像素时才移动
                # ==========================================
                # 核心新增：死区 (Deadzone) 判断
                # ==========================================
                # 如果准星距离目标已经小于 8 个像素 (可根据感觉微调)
                if abs(self.target_dx) < 8 and abs(self.target_dy) < 8:
                    # 认为已经锁到位了，清空目标，AI 停止发力
                    self.target_dx = 0.0
                    self.target_dy = 0.0
                    # 直接进入下一次循环，不调用底层鼠标移动
                    pass

                elif abs(self.target_dx) > 1 or abs(self.target_dy) > 1:
                    # 1. 计算当前这一小步要移动多少
                    move_x = self.target_dx * self.smooth
                    move_y = self.target_dy * self.smooth

                    # 2. 保证最小移动量为 1 像素，否则逼近目标时会停滞
                    if 0 < move_x < 1:
                        move_x = 1
                    elif -1 < move_x < 0:
                        move_x = -1
                    if 0 < move_y < 1:
                        move_y = 1
                    elif -1 < move_y < 0:
                        move_y = -1

                    move_x = int(move_x)
                    move_y = int(move_y)

                    # 3. 发送底层鼠标指令
                    win32api.mouse_event(self.MOUSEEVENTF_MOVE, move_x, move_y, 0, 0)

                    # 4. 【核心灵魂】消耗掉已经移动的距离！
                    # 这样在 YOLO 下一帧到来之前，鼠标会平滑减速，不会超调过冲。
                    self.target_dx -= move_x
                    self.target_dy -= move_y

            # 休眠 0.008 秒 (约 125Hz 的鼠标刷新率)，保证丝滑
            time.sleep(0.001)


class AsyncGamepadController(threading.Thread):
    def __init__(self, smoothing=0.6, max_pixels=200):
        super().__init__()
        self.daemon = True

        # 1. 初始化虚拟 Xbox 手柄 (输出端)
        self.virtual_gamepad = vg.VX360Gamepad()
        print("[Gamepad] AI 虚拟 Xbox 360 手柄已上线！")

        # 2. 初始化 Pygame 物理手柄读取 (输入端)
        pygame.init()
        pygame.joystick.init()
        if pygame.joystick.get_count() == 0:
            print("[Error] 未检测到物理手柄！请确保 DSE 已连接。")
            self.running = False
            return

        self.joystick = pygame.joystick.Joystick(0)
        self.joystick.init()
        print(f"[Gamepad] 成功直连原生手柄: {self.joystick.get_name()}")

        self.smoothing = smoothing
        self.max_pixels = max_pixels
        self.target_dx = 0.0
        self.target_dy = 0.0
        self.lock = threading.Lock()

        self._is_aiming = False
        self.ai_stick_x = 0.0
        self.ai_stick_y = 0.0

        self.running = True
        self.start()

    def update(self, dx, dy):
        with self.lock:
            self.target_dx = dx * 0.7
            self.target_dy = dy * 0.7

    def reset(self):
        with self.lock:
            self.target_dx = 0.0
            self.target_dy = 0.0
            self.ai_stick_x = 0.0
            self.ai_stick_y = 0.0

    def is_aiming(self):
        return self._is_aiming

    def _map_pixel_to_stick(self, delta):
        clamped_delta = max(-self.max_pixels, min(self.max_pixels, delta))
        return (clamped_delta / self.max_pixels) * 32767

    # --- 核心翻译器函数 ---
    def _axis_to_xbox(self, val):
        # Pygame 轴范围是 -1.0 到 1.0，Xbox 需要 -32768 到 32767
        return int(val * 32767)

    def _trigger_to_xbox(self, val):
        # Pygame 扳机默认静止是 -1.0，按满是 1.0。Xbox 需要 0 到 255
        return int(((val + 1.0) / 2.0) * 255)

    def run(self):
        # 索尼 DSE 按钮映射表 (基于标准 SDL2 映射)
        # PS 按键索引 -> Xbox 虚拟按键
        button_map = {
            0: vg.XUSB_BUTTON.XUSB_GAMEPAD_A,  # 叉 -> A
            1: vg.XUSB_BUTTON.XUSB_GAMEPAD_B,  # 圆 -> B
            2: vg.XUSB_BUTTON.XUSB_GAMEPAD_X,  # 方 -> X
            3: vg.XUSB_BUTTON.XUSB_GAMEPAD_Y,  # 角 -> Y
            4: vg.XUSB_BUTTON.XUSB_GAMEPAD_BACK,  # Share -> Back
            5: vg.XUSB_BUTTON.XUSB_GAMEPAD_GUIDE,  # PS -> Guide
            6: vg.XUSB_BUTTON.XUSB_GAMEPAD_START,  # Options -> Start
            7: vg.XUSB_BUTTON.XUSB_GAMEPAD_LEFT_THUMB,  # L3
            8: vg.XUSB_BUTTON.XUSB_GAMEPAD_RIGHT_THUMB,  # R3
            9: vg.XUSB_BUTTON.XUSB_GAMEPAD_LEFT_SHOULDER,  # L1
            10: vg.XUSB_BUTTON.XUSB_GAMEPAD_RIGHT_SHOULDER  # R1
        }

        while self.running:
            # 必须调用 pump 才能获取最新手柄事件
            pygame.event.pump()

            # ==========================================
            # 1. 摇杆与扳机透传 (DSE -> Pygame -> Xbox)
            # ==========================================
            # 左摇杆 (Axis 0: X, Axis 1: Y)
            lx = self._axis_to_xbox(self.joystick.get_axis(0))
            ly = self._axis_to_xbox(-self.joystick.get_axis(1))  # Y轴通常需要翻转
            self.virtual_gamepad.left_joystick(x_value=lx, y_value=ly)

            # 扳机 (Axis 4: L2, Axis 5: R2)
            l2_val = self._trigger_to_xbox(self.joystick.get_axis(4))
            r2_val = self._trigger_to_xbox(self.joystick.get_axis(5))
            self.virtual_gamepad.left_trigger(value=l2_val)
            self.virtual_gamepad.right_trigger(value=r2_val)

            # 判断是否开镜 (L2按下一半以上)
            self._is_aiming = l2_val > 128

            # ==========================================
            # 2. 十字键 (D-Pad) 与常规按键透传
            # ==========================================
            # 处理十字键 (Pygame 中识别为 Hat)
            if self.joystick.get_numhats() > 0:
                hat_x, hat_y = self.joystick.get_hat(0)
                if hat_y == 1:
                    self.virtual_gamepad.press_button(vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_UP)
                else:
                    self.virtual_gamepad.release_button(vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_UP)

                if hat_y == -1:
                    self.virtual_gamepad.press_button(vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_DOWN)
                else:
                    self.virtual_gamepad.release_button(vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_DOWN)

                if hat_x == -1:
                    self.virtual_gamepad.press_button(vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_LEFT)
                else:
                    self.virtual_gamepad.release_button(vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_LEFT)

                if hat_x == 1:
                    self.virtual_gamepad.press_button(vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_RIGHT)
                else:
                    self.virtual_gamepad.release_button(vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_RIGHT)

            # 处理常规按键
            for ps_idx, xbox_btn in button_map.items():
                if ps_idx < self.joystick.get_numbuttons():
                    if self.joystick.get_button(ps_idx):
                        self.virtual_gamepad.press_button(button=xbox_btn)
                    else:
                        self.virtual_gamepad.release_button(button=xbox_btn)

            # ==========================================
            # 3. 右摇杆融合与篡改 (物理 + AI)
            # ==========================================
            # 读取原生右摇杆 (Axis 2: X, Axis 3: Y)
            phys_rx = self._axis_to_xbox(self.joystick.get_axis(2))
            phys_ry = self._axis_to_xbox(-self.joystick.get_axis(3))

            with self.lock:
                if self._is_aiming and (abs(self.target_dx) > 2 or abs(self.target_dy) > 2):
                    desired_ai_x = self._map_pixel_to_stick(self.target_dx)
                    desired_ai_y = self._map_pixel_to_stick(-self.target_dy)

                    self.ai_stick_x = (self.ai_stick_x * self.smoothing) + (desired_ai_x * (1.0 - self.smoothing))
                    self.ai_stick_y = (self.ai_stick_y * self.smoothing) + (desired_ai_y * (1.0 - self.smoothing))

                    final_rx = int(phys_rx + self.ai_stick_x)
                    final_ry = int(phys_ry + self.ai_stick_y)

                    final_rx = max(-32768, min(32767, final_rx))
                    final_ry = max(-32768, min(32767, final_ry))
                    print("ai", self.ai_stick_x, phys_rx, self.ai_stick_y, phys_ry)
                    print("final", final_rx, final_ry)
                else:
                    self.ai_stick_x = 0.0
                    self.ai_stick_y = 0.0
                    final_rx = phys_rx
                    final_ry = phys_ry

            self.virtual_gamepad.right_joystick(x_value=final_rx, y_value=final_ry)
            self.virtual_gamepad.update()

            time.sleep(0.001)  # 100Hz 刷新率

class ControllerFactory:
    @staticmethod
    def get_controller(use_gamepad=False):
        return AsyncGamepadController() if use_gamepad else MouseController()

# --- 工厂模式 ---
class ControllerFactory:
    @staticmethod
    def get_controller(use_gamepad=False):
        if use_gamepad:
            print("[Init] 加载 DS4 虚拟手柄控制器...")
            return AsyncGamepadController()
        else:
            print("[Init] 加载鼠标控制器...")
            return MouseController()

