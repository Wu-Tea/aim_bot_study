import win32api
import threading
import vgamepad as vg
import time
import os
os.environ['PYGAME_HIDE_SUPPORT_PROMPT'] = "hide"
import pygame
from pynput import mouse, keyboard # 需要 pip install pynput

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
    def __init__(self, smoothing=0.65, max_pixels=150):
        super().__init__()
        self.daemon = True

        self.virtual_gamepad = vg.VX360Gamepad()
        print("[Gamepad] AI 虚拟 Xbox 360 手柄已上线！")

        pygame.init()
        pygame.joystick.init()
        if pygame.joystick.get_count() == 0:
            print("[Error] 未检测到物理手柄！")
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

        # ==========================================
        # 核心调教参数区 (根据手感随时修改)
        # ==========================================
        self.INVERT_X = False  # 如果准星总是往反方向跑，改成 True
        self.INVERT_Y = False  # 如果上下反了，改成 True
        self.MAX_AI_FORCE = 0.6 # AI 摇杆最大推力占比 (0.3代表最多只能推30%的摇杆，防止拉飞)
        self.DEADZONE = 5      # 像素死区：距离目标 15 像素内，AI 完全松手，靠你自己微调
        # ==========================================

        self.running = True
        self.start()

    def update(self, dx, dy):
        with self.lock:
            # 只更新目标位置，不在这里做复杂的乘法计算
            self.target_dx = dx * 0.7
            self.target_dy = dy * 0.7

    def reset(self):
        with self.lock:
            self.target_dx = 0.0
            self.target_dy = 0.0
            # 【核心修复】删除了 self.ai_stick_x = 0.0
            # 依靠平滑衰减，解决“一顿一顿”的问题

    def is_aiming(self):
        return self._is_aiming

    def _map_pixel_to_stick(self, delta):
        clamped_delta = max(-self.max_pixels, min(self.max_pixels, delta))
        return (clamped_delta / self.max_pixels) * 32767

    def _axis_to_xbox(self, val):
        return int(val * 32767)

    def _trigger_to_xbox(self, val):
        return int(((val + 1.0) / 2.0) * 255)

    def run(self):
        button_map = {
            0: vg.XUSB_BUTTON.XUSB_GAMEPAD_A,
            1: vg.XUSB_BUTTON.XUSB_GAMEPAD_B,
            2: vg.XUSB_BUTTON.XUSB_GAMEPAD_X,
            3: vg.XUSB_BUTTON.XUSB_GAMEPAD_Y,
            4: vg.XUSB_BUTTON.XUSB_GAMEPAD_BACK,
            5: vg.XUSB_BUTTON.XUSB_GAMEPAD_GUIDE,
            6: vg.XUSB_BUTTON.XUSB_GAMEPAD_START,
            7: vg.XUSB_BUTTON.XUSB_GAMEPAD_LEFT_THUMB,
            8: vg.XUSB_BUTTON.XUSB_GAMEPAD_RIGHT_THUMB,
            9: vg.XUSB_BUTTON.XUSB_GAMEPAD_LEFT_SHOULDER,
            10: vg.XUSB_BUTTON.XUSB_GAMEPAD_RIGHT_SHOULDER,
            11: vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_UP,
            12: vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_DOWN,
            13: vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_LEFT,
            14: vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_RIGHT,
        }

        trigger_initialized = False

        while self.running:
            pygame.event.clear()

            # --- 摇杆与按键透传 (保持不变) ---
            lx = self._axis_to_xbox(self.joystick.get_axis(0))
            ly = self._axis_to_xbox(-self.joystick.get_axis(1))
            self.virtual_gamepad.left_joystick(x_value=lx, y_value=ly)

            raw_l2 = self.joystick.get_axis(4)
            raw_r2 = self.joystick.get_axis(5)
            if not trigger_initialized:
                if raw_l2 != 0.0 or raw_r2 != 0.0: trigger_initialized = True
                else: raw_l2, raw_r2 = -1.0, -1.0

            l2_val = self._trigger_to_xbox(raw_l2)
            r2_val = self._trigger_to_xbox(raw_r2)
            self.virtual_gamepad.left_trigger(value=l2_val)
            self.virtual_gamepad.right_trigger(value=r2_val)
            self._is_aiming = l2_val > 128

            if self.joystick.get_numhats() > 0:
                hat_x, hat_y = self.joystick.get_hat(0)
                if hat_y == 1: self.virtual_gamepad.press_button(vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_UP)
                else: self.virtual_gamepad.release_button(vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_UP)
                if hat_y == -1: self.virtual_gamepad.press_button(vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_DOWN)
                else: self.virtual_gamepad.release_button(vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_DOWN)
                if hat_x == -1: self.virtual_gamepad.press_button(vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_LEFT)
                else: self.virtual_gamepad.release_button(vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_LEFT)
                if hat_x == 1: self.virtual_gamepad.press_button(vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_RIGHT)
                else: self.virtual_gamepad.release_button(vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_RIGHT)

            for ps_idx, xbox_btn in button_map.items():
                if ps_idx < self.joystick.get_numbuttons():
                    if self.joystick.get_button(ps_idx): self.virtual_gamepad.press_button(button=xbox_btn)
                    else: self.virtual_gamepad.release_button(button=xbox_btn)

            # ==========================================
            # 右摇杆 AI 融合核心逻辑
            # ==========================================
            # ==========================================
            # 3. 纯右摇杆干涉与融合逻辑
            # ==========================================
            phys_rx = self._axis_to_xbox(self.joystick.get_axis(2))
            phys_ry = self._axis_to_xbox(-self.joystick.get_axis(3))

            with self.lock:
                if self._is_aiming:
                    # 1. 像素死区判断
                    if abs(self.target_dx) <= self.DEADZONE and abs(self.target_dy) <= self.DEADZONE:
                        desired_ai_x, desired_ai_y = 0.0, 0.0
                    else:
                        # 2. 计算 AI 理想推力
                        desired_ai_x = self._map_pixel_to_stick(self.target_dx)
                        desired_ai_y = self._map_pixel_to_stick(-self.target_dy)

                        if getattr(self, 'INVERT_X', False): desired_ai_x = -desired_ai_x
                        if getattr(self, 'INVERT_Y', False): desired_ai_y = -desired_ai_y

                        limit = 32767 * self.MAX_AI_FORCE
                        desired_ai_x = max(-limit, min(limit, desired_ai_x))
                        desired_ai_y = max(-limit, min(limit, desired_ai_y))
                else:
                    desired_ai_x, desired_ai_y = 0.0, 0.0

                # 3. 指数平滑计算 AI 当前实际输出
                self.ai_stick_x = (self.ai_stick_x * self.smoothing) + (desired_ai_x * (1.0 - self.smoothing))
                self.ai_stick_y = (self.ai_stick_y * self.smoothing) + (desired_ai_y * (1.0 - self.smoothing))

                # ==========================================
                # 【核心】右摇杆过滤与限制 (磁性与挣脱)
                # ==========================================
                # 设定挣脱阈值：当物理推力超过 10000 (约推了30%的摇杆) 时，视为玩家想强行转移目标
                user_is_flicking = abs(phys_rx) > 10000 or abs(phys_ry) > 10000

                if self._is_aiming:
                    if user_is_flicking:
                        # 玩家大力推摇杆，AI 瞬间归零，交出控制权
                        self.ai_stick_x, self.ai_stick_y = 0.0, 0.0

                # 4. 最终指令合并
                final_rx = int(phys_rx + self.ai_stick_x)
                final_ry = int(phys_ry + self.ai_stick_y)

            final_rx = max(-32768, min(32767, final_rx))
            final_ry = max(-32768, min(32767, final_ry))

            self.virtual_gamepad.right_joystick(x_value=final_rx, y_value=final_ry)
            self.virtual_gamepad.update()

            time.sleep(0.001)


class MouseToGamepadController(threading.Thread):
    def __init__(self, smoothing=0.6, base_sens=800.0, curve=1.2, ai_sensitivity=0.7):
        super().__init__()
        self.daemon = True

        self.virtual_gamepad = vg.VX360Gamepad()
        print("[KBM Converter] 纯鼠标转虚拟手柄系统已上线！(左键=R1, 右键=L2)")

        self.lock = threading.Lock()

        # --- 解决卡顿的核心：累积器与当前状态 ---
        self.acc_dx = 0.0  # 鼠标事件累积器 X
        self.acc_dy = 0.0  # 鼠标事件累积器 Y

        self.current_rx = 0.0  # 摇杆当前的真实物理位置 X
        self.current_ry = 0.0  # 摇杆当前的真实物理位置 Y

        self.ai_target_dx = 0.0
        self.ai_target_dy = 0.0

        # --- 算法调教参数 ---
        # smoothing: 0.0 ~ 0.99，越大越平滑(但也越有惯性拖拽感)，越小越跟手
        self.smoothing = smoothing
        self.base_sens = base_sens  # 基础灵敏度乘子
        self.curve = curve  # 加速曲线指数 (1.0为线性，1.2~1.5能让微操更准，转身更快)
        self.ai_sens = ai_sensitivity  # AI 辅助权重
        self.DEADZONE = 5  # AI 停止干预的死区
        self._is_aiming = False  # 是否按住右键

        # 启动 pynput 鼠标监听器 (彻底干掉键盘监听)
        self.mouse_listener = mouse.Listener(on_move=self._on_mouse_move, on_click=self._on_mouse_click)
        self.mouse_listener.start()

        # 记录上一帧鼠标位置
        self.last_mouse_x, self.last_mouse_y = win32api.GetCursorPos()

        self.running = True
        self.start()

    # ==========================================
    # 鼠标事件监听 (事件驱动层)
    # ==========================================
    def _on_mouse_move(self, x, y):
        dx = x - self.last_mouse_x
        dy = y - self.last_mouse_y
        self.last_mouse_x, self.last_mouse_y = x, y

        # 【核心变化1】不直接改摇杆，而是把位移“存”起来
        with self.lock:
            self.acc_dx += dx
            self.acc_dy += dy

    def _on_mouse_click(self, x, y, button, pressed):
        # 右键 -> L2 (左扳机，开镜)
        if button == mouse.Button.right:
            self._is_aiming = pressed
            if pressed:
                self.virtual_gamepad.left_trigger(value=255)
            else:
                self.virtual_gamepad.left_trigger(value=0)
                self.reset()  # 松开开镜清空 AI 目标

        # 左键 -> R1 (右肩键，开火/攻击)
        elif button == mouse.Button.left:
            if pressed:
                self.virtual_gamepad.press_button(vg.XUSB_BUTTON.XUSB_GAMEPAD_RIGHT_SHOULDER)
            else:
                self.virtual_gamepad.release_button(vg.XUSB_BUTTON.XUSB_GAMEPAD_RIGHT_SHOULDER)

    # ==========================================
    # AI 视觉指令层
    # ==========================================
    def update(self, dx, dy):
        with self.lock:
            self.ai_target_dx = dx * self.ai_sens
            self.ai_target_dy = dy * self.ai_sens

    def reset(self):
        with self.lock:
            self.ai_target_dx = 0.0
            self.ai_target_dy = 0.0

    def is_aiming(self):
        return self._is_aiming

    # ==========================================
    # 数学转换工具
    # ==========================================
    def _apply_curve(self, delta):
        """对鼠标位移应用加速曲线，保留符号"""
        if delta == 0: return 0.0
        sign = 1 if delta > 0 else -1
        # 使用指数曲线：微小移动放大较少，大范围甩鼠标放大极多
        return sign * (abs(delta) ** self.curve) * self.base_sens

    # ==========================================
    # 核心转换循环 (固定频率轮询层)
    # ==========================================
    def run(self):
        # 手柄轮询率：约 125Hz - 250Hz，过高没意义反而增加 CPU 负担
        sleep_time = 0.005

        while self.running:
            with self.lock:
                # 【核心变化2】读取这 5 毫秒内累积的位移，并立即清零
                raw_dx = self.acc_dx
                raw_dy = self.acc_dy
                self.acc_dx = 0.0
                self.acc_dy = 0.0

                # 1. 玩家鼠标输入转换 (带加速曲线)
                target_rx = self._apply_curve(raw_dx)
                target_ry = self._apply_curve(raw_dy)

                # 2. 融入 AI 推力 (仅在开镜且目标在死区外时介入)
                if self._is_aiming and (
                        abs(self.ai_target_dx) > self.DEADZONE or abs(self.ai_target_dy) > self.DEADZONE):
                    target_rx += (self.ai_target_dx * 200.0)
                    target_ry += (self.ai_target_dy * 200.0)

                # 【核心变化3】EMA 平滑滤波！
                # 不要让摇杆瞬间跳到 target_rx，而是平滑地“滑过去”
                # 这样即使鼠标由于回报率问题某几毫秒没发信号，摇杆也不会瞬间归零造成卡顿
                self.current_rx = (self.current_rx * self.smoothing) + (target_rx * (1.0 - self.smoothing))
                self.current_ry = (self.current_ry * self.smoothing) + (target_ry * (1.0 - self.smoothing))

                # 消除极小浮点数导致的微弱漂移
                if abs(self.current_rx) < 10: self.current_rx = 0
                if abs(self.current_ry) < 10: self.current_ry = 0

                # 限制在 Xbox 摇杆极值内 (-32768 到 32767)
                final_rx = max(-32768, min(32767, int(self.current_rx)))
                final_ry = max(-32768, min(32767, int(-self.current_ry)))  # 鼠标下移是正，摇杆下推是负，取反

            # 推送给虚拟手柄
            self.virtual_gamepad.right_joystick(x_value=final_rx, y_value=final_ry)
            self.virtual_gamepad.update()

            time.sleep(sleep_time)

# --- 工厂模式 ---
class ControllerFactory:
    @staticmethod
    def get_controller(controller_mode="mouse"):
        """
        根据传入的模式字符串获取对应的控制器实例。
        :param controller_mode:
            "mouse" - 原生鼠标控制 (AI直接操控系统鼠标)
            "gamepad" - 物理手柄控制 (读取物理手柄，融合AI输出虚拟手柄)
            "kbm_to_gamepad" - 键鼠转手柄 (读取键鼠，融合AI输出虚拟手柄)
        """
        if controller_mode == "gamepad":
            print("[Init] 加载物理手柄控制器 (DS4/Xbox 转虚拟手柄)...")
            return AsyncGamepadController()

        elif controller_mode == "kbm_to_gamepad":
            print("[Init] 加载转换器模式 (键鼠转虚拟手柄)...")
            return MouseToGamepadController()

        else:  # 默认 fallback 为原生鼠标
            print("[Init] 加载原生鼠标控制器...")
            return MouseController()