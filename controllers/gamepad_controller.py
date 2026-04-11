import threading
import vgamepad as vg
import time
import os
os.environ['PYGAME_HIDE_SUPPORT_PROMPT'] = "hide"
import pygame
from .gamepad_horizontal_assist import (
    HorizontalAimAssist,
    HorizontalAimAssistConfig,
    compute_axis_soft_strengths,
)
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
        self.ready = False
        self.smoothing = smoothing
        self.target_dx = 0.0
        self.target_dy = 0.0
        self.ai_stick_x = 0.0
        self.ai_stick_y = 0.0
        self.lock = threading.Lock()
        self._is_aiming = False
        self._auto_rb_pressed = False

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

        # --- Tuning Parameters ---
        self.INVERT_X = False
        self.INVERT_Y = False
        self.MAX_AI_FORCE = 0.6        # Max % of stick deflection AI can apply
        # Radial deadzone with a soft outer ramp. Values are in post-gain pixel
        # space (i.e. already multiplied by AI_DELTA_GAIN). Inside INNER we kill
        # AI completely to absorb pose-keypoint jitter at the chest center;
        # between INNER and OUTER we linearly ramp to full strength so the
        # crosshair can finish the pull onto chest center instead of stopping
        # at the deadzone edge. _map_pixel_to_stick is already a linear pixel->
        # stick mapping, so small deltas naturally produce small stick outputs
        # and we don't need a large deadzone to damp over-correction.
        self.DEADZONE_INNER = 1.5      # Below this radial distance, AI fully off
        self.DEADZONE_OUTER = 5.0      # Above this radial distance, AI full strength
        self.X_DEADZONE_OUTER = 3.0    # X reaches full strength earlier than radial deadzone
        self.PHYS_STICK_DEADZONE = 2500  # Right-stick raw drift filter (~7.6% deflection)
        self.AI_FADE_FULL = 8000       # Post-deadzone user input magnitude at which AI fully fades to 0 (~24%)
        self.AI_DELTA_GAIN = 0.7       # Scales vision module's pixel deltas before stick mapping (lower = softer correction)
        # Conservative X-axis-only enhancement for lateral tracking. It does
        # not read left-stick input; it only reacts to screen-space horizontal
        # error growth and briefly raises X-axis authority when we're clearly
        # not catching up.
        self.horizontal_assist = HorizontalAimAssist(
            HorizontalAimAssistConfig(
                min_error_px=4.0,
                min_velocity_px_per_sec=60.0,
                velocity_filter_alpha=0.45,
                feedforward_lead_seconds=0.02,
                feedforward_gain=0.65,
                max_feedforward_px=6.0,
                catchup_trigger_frames=3,
                catchup_gain_per_update=0.02,
                catchup_max_bonus=0.10,
                catchup_decay=0.04,
                opposing_input_threshold=5000,
                convergence_epsilon_px=0.25,
            )
        )
        # Downward bias applied to the right stick while auto-fire is active,
        # as a fraction of full stick range. Counteracts the weapon's upward
        # recoil kick. Applied as a pure post-blend offset so it does not
        # interact with the AI convergence pipeline (smoothing, MAX_AI_FORCE,
        # per-axis user-input fade). Flip the sign if your game uses inverted
        # vertical aim at the game level.
        self.RECOIL_COMPENSATION = 0.20  # 3.5% of 32767 ~= 1147 stick units down

        self.ready = True
        self.start()

    def update(self, dx, dy):
        with self.lock:
            self.target_dx = dx * self.AI_DELTA_GAIN
            self.target_dy = dy * self.AI_DELTA_GAIN
            self.horizontal_assist.observe_target(
                target_dx=self.target_dx,
                is_aiming=self._is_aiming,
                timestamp=time.perf_counter(),
            )

    def reset(self):
        # NOTE: do NOT touch self._auto_rb_pressed here. Auto-fire lifecycle is
        # owned by the vision-layer detector (with its own release grace frames)
        # and applied through set_auto_rb(). Clearing it here would nullify the
        # detector's grace window on any frame where best_target_delta is None.
        with self.lock:
            self.target_dx = 0.0
            self.target_dy = 0.0
            self.horizontal_assist.reset()

    def is_aiming(self):
        return self._is_aiming

    def set_auto_rb(self, pressed: bool):
        with self.lock:
            self._auto_rb_pressed = bool(pressed)

    def stop(self):
        self.running = False
        pygame.quit()

    def _map_pixel_to_stick(self, delta, max_pixels=150):
        clamped = max(-max_pixels, min(max_pixels, delta))
        return (clamped / max_pixels) * 32767

    def _axis_to_xbox(self, val):
        return int(val * 32767)

    def _apply_stick_deadzone(self, val):
        """Radial deadzone with rescale so full range is preserved above the deadzone."""
        if abs(val) < self.PHYS_STICK_DEADZONE:
            return 0
        sign = 1 if val > 0 else -1
        scaled = (abs(val) - self.PHYS_STICK_DEADZONE) / (32767 - self.PHYS_STICK_DEADZONE) * 32767
        return int(sign * scaled)

    def _ai_scale_factor(self, user_val):
        """Linear AI attenuation: AI at 100% when user idle, 0% when user reaches AI_FADE_FULL."""
        mag = abs(user_val)
        if mag >= self.AI_FADE_FULL:
            return 0.0
        return 1.0 - mag / self.AI_FADE_FULL

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
        rb_button = vg.XUSB_BUTTON.XUSB_GAMEPAD_RIGHT_SHOULDER
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
                if btn_idx == 10:
                    continue
                if self.joystick.get_button(btn_idx): self.virtual_gamepad.press_button(button=xbox_btn)
                else: self.virtual_gamepad.release_button(button=xbox_btn)

            manual_rb_pressed = bool(self.joystick.get_button(10))
            with self.lock:
                auto_rb_pressed = self._auto_rb_pressed and self._is_aiming
            if manual_rb_pressed or auto_rb_pressed:
                self.virtual_gamepad.press_button(button=rb_button)
            else:
                self.virtual_gamepad.release_button(button=rb_button)

            # --- AI Aim Assist Logic ---
            phys_rx_raw = self._axis_to_xbox(self.joystick.get_axis(2))
            phys_ry_raw = self._axis_to_xbox(-self.joystick.get_axis(3))
            # Filter stick drift so tiny noise doesn't bleed into the AI blend
            phys_rx = self._apply_stick_deadzone(phys_rx_raw)
            phys_ry = self._apply_stick_deadzone(phys_ry_raw)

            with self.lock:
                desired_ai_x, desired_ai_y = 0.0, 0.0
                if self._is_aiming:
                    dx = self.target_dx
                    dy = self.target_dy
                    feedforward_dx, x_force_bonus = self.horizontal_assist.compute_adjustment(phys_rx)
                    assist_dx = dx + feedforward_dx
                    x_strength, y_strength = compute_axis_soft_strengths(
                        dx=assist_dx,
                        dy=dy,
                        inner=self.DEADZONE_INNER,
                        radial_outer=self.DEADZONE_OUTER,
                        x_outer=self.X_DEADZONE_OUTER,
                    )

                    # X uses a slightly faster ramp than the radial deadzone so
                    # lateral corrections keep authority close to the target.
                    # Y keeps the original radial strength to preserve vertical feel.
                    if x_strength > 0.0 or y_strength > 0.0:
                        desired_ai_x = self._map_pixel_to_stick(assist_dx) * x_strength
                        desired_ai_y = self._map_pixel_to_stick(-dy) * y_strength  # Y is inverted

                        if self.INVERT_X: desired_ai_x = -desired_ai_x
                        if self.INVERT_Y: desired_ai_y = -desired_ai_y

                        x_limit = 32767 * min(1.0, self.MAX_AI_FORCE + x_force_bonus)
                        y_limit = 32767 * self.MAX_AI_FORCE
                        desired_ai_x = max(-x_limit, min(x_limit, desired_ai_x))
                        desired_ai_y = max(-y_limit, min(y_limit, desired_ai_y))

                # Smooth the AI's stick input
                self.ai_stick_x = (self.ai_stick_x * self.smoothing) + (desired_ai_x * (1.0 - self.smoothing))
                self.ai_stick_y = (self.ai_stick_y * self.smoothing) + (desired_ai_y * (1.0 - self.smoothing))

                # Per-axis proportional fade: stronger user input on an axis linearly suppresses AI
                # on that same axis. AI on the untouched axis is preserved so large-direction pulls
                # keep helping while the user fine-tunes the other axis.
                scale_x = self._ai_scale_factor(phys_rx)
                scale_y = self._ai_scale_factor(phys_ry)

                final_rx = int(phys_rx + self.ai_stick_x * scale_x)
                final_ry = int(phys_ry + self.ai_stick_y * scale_y)

            # Recoil compensation while auto-fire is active. Pure post-blend
            # offset: doesn't feed into AI smoothing / MAX_AI_FORCE / user
            # input fade, so it won't be attenuated by the AI pipeline or
            # ramp up slowly via smoothing. Xbox right stick convention is
            # positive Y = up, so subtracting pushes the crosshair down in
            # game-space.
            if auto_rb_pressed and self.RECOIL_COMPENSATION != 0.0:
                final_ry -= int(self.RECOIL_COMPENSATION * 32767)

            # Clamp and send final values to virtual gamepad
            final_rx = max(-32768, min(32767, final_rx))
            final_ry = max(-32768, min(32767, final_ry))
            self.virtual_gamepad.right_joystick(x_value=final_rx, y_value=final_ry)
            
            self.virtual_gamepad.update()
            time.sleep(0.002) # ~500Hz update rate
