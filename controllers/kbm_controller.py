import threading
import vgamepad as vg
import time
import win32api
from pynput import mouse
from .base_controller import BaseController

class KBMController(BaseController, threading.Thread):
    """
    Translates mouse and keyboard input into a virtual Xbox 360 gamepad,
    integrating AI aim adjustments. KBM stands for Keyboard & Mouse.
    """
    def __init__(self, smoothing=0.6, base_sens=800.0, curve=1.2, ai_sensitivity=0.7):
        super().__init__()
        self.daemon = True
        self.running = True

        self.virtual_gamepad = vg.VX360Gamepad()
        print("[KBM Converter] Mouse-to-gamepad converter is online.")

        # --- Input Accumulators & State ---
        self.lock = threading.Lock()
        self.acc_dx = 0.0
        self.acc_dy = 0.0
        self.current_rx = 0.0
        self.current_ry = 0.0
        self.ai_target_dx = 0.0
        self.ai_target_dy = 0.0
        self._is_aiming = False

        # --- Tuning Parameters ---
        self.smoothing = smoothing
        self.base_sens = base_sens
        self.curve = curve
        self.ai_sens = ai_sensitivity
        self.DEADZONE = 5 # AI pixel deadzone

        # Start listeners
        self.last_mouse_x, self.last_mouse_y = win32api.GetCursorPos()
        self.mouse_listener = mouse.Listener(on_move=self._on_mouse_move, on_click=self._on_mouse_click)
        self.mouse_listener.start()
        
        self.start()

    def _on_mouse_move(self, x, y):
        dx = x - self.last_mouse_x
        dy = y - self.last_mouse_y
        self.last_mouse_x, self.last_mouse_y = x, y
        with self.lock:
            self.acc_dx += dx
            self.acc_dy += dy

    def _on_mouse_click(self, x, y, button, pressed):
        if button == mouse.Button.right:
            self._is_aiming = pressed
            self.virtual_gamepad.left_trigger(value=255 if pressed else 0)
            if not pressed: self.reset()
        elif button == mouse.Button.left:
            if pressed: self.virtual_gamepad.press_button(vg.XUSB_BUTTON.XUSB_GAMEPAD_RIGHT_SHOULDER)
            else: self.virtual_gamepad.release_button(vg.XUSB_BUTTON.XUSB_GAMEPAD_RIGHT_SHOULDER)

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

    def stop(self):
        self.running = False
        self.mouse_listener.stop()

    def _apply_curve(self, delta):
        if delta == 0: return 0.0
        sign = 1 if delta > 0 else -1
        return sign * (abs(delta) ** self.curve) * self.base_sens

    def run(self):
        while self.running:
            with self.lock:
                # Consume accumulated mouse movement
                raw_dx, raw_dy = self.acc_dx, self.acc_dy
                self.acc_dx, self.acc_dy = 0.0, 0.0

                # 1. Convert player's mouse input with acceleration curve
                target_rx = self._apply_curve(raw_dx)
                target_ry = self._apply_curve(raw_dy)

                # 2. Add AI input if aiming and outside deadzone
                if self._is_aiming and (abs(self.ai_target_dx) > self.DEADZONE or abs(self.ai_target_dy) > self.DEADZONE):
                    target_rx += (self.ai_target_dx * 200.0)
                    target_ry += (self.ai_target_dy * 200.0)

                # 3. Smooth the final stick value (EMA filter)
                self.current_rx = (self.current_rx * self.smoothing) + (target_rx * (1.0 - self.smoothing))
                self.current_ry = (self.current_ry * self.smoothing) + (target_ry * (1.0 - self.smoothing))

                # Clamp to prevent drift
                if abs(self.current_rx) < 10: self.current_rx = 0
                if abs(self.current_ry) < 10: self.current_ry = 0

                # Clamp to joystick limits
                final_rx = max(-32768, min(32767, int(self.current_rx)))
                final_ry = max(-32768, min(32767, int(-self.current_ry))) # Y is inverted

            # Send to virtual gamepad
            self.virtual_gamepad.right_joystick(x_value=final_rx, y_value=final_ry)
            self.virtual_gamepad.update()
            time.sleep(0.005) # ~200Hz
