import threading
import vgamepad as vg
import time
import os
os.environ['PYGAME_HIDE_SUPPORT_PROMPT'] = "hide"
import pygame
from .base_controller import BaseController

class GamepadController(BaseController, threading.Thread):
    """
    Reads a physical gamepad and merges its input with AI aim adjustments,
    outputting to a virtual Xbox 360 gamepad.
    """
    def __init__(self, smoothing=0.65, max_pixels=150):
        super().__init__()
        self.daemon = True
        self.running = True

        try:
            self.virtual_gamepad = vg.VX360Gamepad()
            print("[Gamepad] AI virtual Xbox 360 gamepad is online.")
            
            pygame.init()
            pygame.joystick.init()
            if pygame.joystick.get_count() == 0:
                raise ConnectionError("No physical gamepad detected.")
            
            self.joystick = pygame.joystick.Joystick(0)
            self.joystick.init()
            print(f"[Gamepad] Successfully connected to: {self.joystick.get_name()}")

        except Exception as e:
            print(f"[Error] Gamepad initialization failed: {e}")
            self.running = False
            return

        # --- AI Aiming State ---
        self.smoothing = smoothing
        self.target_dx = 0.0
        self.target_dy = 0.0
        self.ai_stick_x = 0.0
        self.ai_stick_y = 0.0
        self.lock = threading.Lock()
        self._is_aiming = False

        # --- Tuning Parameters ---
        self.INVERT_X = False
        self.INVERT_Y = False
        self.MAX_AI_FORCE = 0.6  # Max % of stick deflection AI can apply
        self.DEADZONE = 5        # Pixel distance where AI stops intervening
        self.FLICK_THRESHOLD = 10000 # User stick input required to override AI

        self.start()

    def update(self, dx, dy):
        with self.lock:
            self.target_dx = dx * 0.7
            self.target_dy = dy * 0.7

    def reset(self):
        with self.lock:
            self.target_dx = 0.0
            self.target_dy = 0.0

    def is_aiming(self):
        return self._is_aiming

    def stop(self):
        self.running = False
        pygame.quit()

    def _map_pixel_to_stick(self, delta, max_pixels=150):
        clamped = max(-max_pixels, min(max_pixels, delta))
        return (clamped / max_pixels) * 32767

    def _axis_to_xbox(self, val):
        return int(val * 32767)

    def _trigger_to_xbox(self, val):
        return int(((val + 1.0) / 2.0) * 255)

    def run(self):
        button_map = {
            0: vg.XUSB_BUTTON.XUSB_GAMEPAD_A, 1: vg.XUSB_BUTTON.XUSB_GAMEPAD_B,
            2: vg.XUSB_BUTTON.XUSB_GAMEPAD_X, 3: vg.XUSB_BUTTON.XUSB_GAMEPAD_Y,
            4: vg.XUSB_BUTTON.XUSB_GAMEPAD_BACK, 5: vg.XUSB_BUTTON.XUSB_GAMEPAD_GUIDE,
            6: vg.XUSB_BUTTON.XUSB_GAMEPAD_START, 7: vg.XUSB_BUTTON.XUSB_GAMEPAD_LEFT_THUMB,
            8: vg.XUSB_BUTTON.XUSB_GAMEPAD_RIGHT_THUMB, 9: vg.XUSB_BUTTON.XUSB_GAMEPAD_LEFT_SHOULDER,
            10: vg.XUSB_BUTTON.XUSB_GAMEPAD_RIGHT_SHOULDER,
            11: vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_UP,
            12: vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_DOWN,
            13: vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_LEFT,
            14: vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_RIGHT
        }
        trigger_initialized = False

        while self.running:
            pygame.event.pump() # Process event queue

            # --- Passthrough physical inputs to virtual gamepad ---
            lx = self._axis_to_xbox(self.joystick.get_axis(0))
            ly = self._axis_to_xbox(-self.joystick.get_axis(1))
            self.virtual_gamepad.left_joystick(x_value=lx, y_value=ly)

            raw_l2, raw_r2 = self.joystick.get_axis(4), self.joystick.get_axis(5)
            if not trigger_initialized and (raw_l2 != 0.0 or raw_r2 != 0.0):
                trigger_initialized = True
            if not trigger_initialized: raw_l2, raw_r2 = -1.0, -1.0
            
            l2_val = self._trigger_to_xbox(raw_l2)
            r2_val = self._trigger_to_xbox(raw_r2)
            self.virtual_gamepad.left_trigger(value=l2_val)
            self.virtual_gamepad.right_trigger(value=r2_val)
            self._is_aiming = l2_val > 10

            if self.joystick.get_numhats() > 0:
                hat_x, hat_y = self.joystick.get_hat(0)
                self.virtual_gamepad.directional_pad(
                    vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_UP if hat_y == 1 else
                    vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_DOWN if hat_y == -1 else
                    vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_LEFT if hat_x == -1 else
                    vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_RIGHT if hat_x == 1 else 0
                )

            for btn_idx, xbox_btn in button_map.items():
                if self.joystick.get_button(btn_idx): self.virtual_gamepad.press_button(button=xbox_btn)
                else: self.virtual_gamepad.release_button(button=xbox_btn)

            # --- AI Aim Assist Logic ---
            phys_rx = self._axis_to_xbox(self.joystick.get_axis(2))
            phys_ry = self._axis_to_xbox(-self.joystick.get_axis(3))
            
            with self.lock:
                desired_ai_x, desired_ai_y = 0.0, 0.0
                if self._is_aiming and not (abs(self.target_dx) <= self.DEADZONE and abs(self.target_dy) <= self.DEADZONE):
                    desired_ai_x = self._map_pixel_to_stick(self.target_dx)
                    desired_ai_y = self._map_pixel_to_stick(-self.target_dy) # Y is inverted
                    
                    if self.INVERT_X: desired_ai_x = -desired_ai_x
                    if self.INVERT_Y: desired_ai_y = -desired_ai_y

                    limit = 32767 * self.MAX_AI_FORCE
                    desired_ai_x = max(-limit, min(limit, desired_ai_x))
                    desired_ai_y = max(-limit, min(limit, desired_ai_y))

                # Smooth the AI's stick input
                self.ai_stick_x = (self.ai_stick_x * self.smoothing) + (desired_ai_x * (1.0 - self.smoothing))
                self.ai_stick_y = (self.ai_stick_y * self.smoothing) + (desired_ai_y * (1.0 - self.smoothing))

                # User flick overrides AI
                user_is_flicking = abs(phys_rx) > self.FLICK_THRESHOLD or abs(phys_ry) > self.FLICK_THRESHOLD
                if self._is_aiming and user_is_flicking:
                    self.ai_stick_x, self.ai_stick_y = 0.0, 0.0

                # Combine physical and AI inputs
                final_rx = int(phys_rx + self.ai_stick_x)
                final_ry = int(phys_ry + self.ai_stick_y)

            # Clamp and send final values to virtual gamepad
            final_rx = max(-32768, min(32767, final_rx))
            final_ry = max(-32768, min(32767, final_ry))
            self.virtual_gamepad.right_joystick(x_value=final_rx, y_value=final_ry)
            
            self.virtual_gamepad.update()
            time.sleep(0.002) # ~500Hz update rate
