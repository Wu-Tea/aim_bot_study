import threading
import time

import win32api
from pynput import mouse as pynput_mouse

from .base_controller import BaseController
from .mouse import (
    AIAimPlugin,
    AutoFireConfig,
    AutoFirePlugin,
    MouseFrame,
    MouseOutput,
    RecoilCompensationConfig,
    RecoilCompensationPlugin,
    apply_plugins,
    reset_plugins,
)

MOUSEEVENTF_MOVE = 0x0001
MOUSEEVENTF_LEFTDOWN = 0x0002
MOUSEEVENTF_LEFTUP = 0x0004
MANUAL_OVERRIDE_TRIGGER_PX = 9.0
MANUAL_OVERRIDE_HOLD_SECONDS = 0.120


class MouseController(BaseController, threading.Thread):
    """
    Native mouse controller with plugin-based enhancements.
    AI corrections are injected as additional mouse_event deltas
    on top of the physical mouse movement.
    """

    def __init__(self, plugins=None):
        super().__init__()
        self.daemon = True
        self.running = True
        self.ready = True
        self.lock = threading.Lock()

        self.target_dx = 0.0
        self.target_dy = 0.0
        self.target_revision = 0
        self.target_timestamp = None
        self.target_info = None
        self._inject_remainder_dx = 0.0
        self._inject_remainder_dy = 0.0
        self._is_aiming = False
        self._auto_fire_requested = False
        self._acc_dx = 0.0
        self._acc_dy = 0.0
        self._manual_left_pressed = False
        self._manual_override_until = None
        self._left_click_held = False
        self._input_session_id = 0

        from config import load_tuning_config

        tuning = load_tuning_config()
        self.plugins = list(plugins) if plugins is not None else [
            AIAimPlugin(tuning.mouse_ai_aim),
            AutoFirePlugin(AutoFireConfig()),
            RecoilCompensationPlugin(RecoilCompensationConfig()),
        ]

        self._last_mouse_x, self._last_mouse_y = win32api.GetCursorPos()
        self._mouse_listener = pynput_mouse.Listener(
            on_move=self._on_mouse_move,
            on_click=self._on_mouse_click,
        )
        self._mouse_listener.start()
        self.start()

    def _on_mouse_move(self, x, y):
        dx = x - self._last_mouse_x
        dy = y - self._last_mouse_y
        self._last_mouse_x, self._last_mouse_y = x, y
        with self.lock:
            self._acc_dx += dx
            self._acc_dy += dy

    def _on_mouse_click(self, x, y, button, pressed):
        if button == pynput_mouse.Button.right:
            self._is_aiming = pressed
            if not pressed:
                self.reset()
            return

        if button == pynput_mouse.Button.left:
            with self.lock:
                self._manual_left_pressed = pressed
                synthetic_left_held = self._left_click_held
                if pressed and synthetic_left_held:
                    self._left_click_held = False
            if pressed and synthetic_left_held:
                win32api.mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)

    def update(self, dx, dy, target=None):
        with self.lock:
            self.target_dx = dx
            self.target_dy = dy
            self.target_info = target
            self.target_revision += 1
            self.target_timestamp = time.perf_counter()

    def _clear_target_state_locked(self):
        self.target_dx = 0.0
        self.target_dy = 0.0
        self.target_info = None
        self.target_revision += 1
        self.target_timestamp = time.perf_counter()

    def clear_target(self):
        with self.lock:
            self._clear_target_state_locked()

    def reset(self):
        synthetic_left_held = False
        with self.lock:
            self._clear_target_state_locked()
            self._inject_remainder_dx = 0.0
            self._inject_remainder_dy = 0.0
            self._manual_override_until = None
            self._auto_fire_requested = False
            self._manual_left_pressed = False
            synthetic_left_held = self._left_click_held
            self._left_click_held = False
            self._input_session_id += 1
        reset_plugins(self.plugins)
        if synthetic_left_held:
            win32api.mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)

    def is_aiming(self):
        return self._is_aiming

    def set_auto_fire(self, pressed: bool):
        with self.lock:
            self._auto_fire_requested = bool(pressed)

    def set_auto_rb(self, pressed: bool):
        self.set_auto_fire(pressed)

    def stop(self):
        self.running = False
        self._mouse_listener.stop()

    def _build_frame(self, *, timestamp):
        with self.lock:
            manual_dx = self._acc_dx
            manual_dy = self._acc_dy
            self._acc_dx = 0.0
            self._acc_dy = 0.0
            target_dx = self.target_dx
            target_dy = self.target_dy
            auto_fire_requested = self._auto_fire_requested
            target_revision = self.target_revision
            target_timestamp = self.target_timestamp
            target_info = self.target_info
            manual_left_pressed = self._manual_left_pressed
            input_session_id = self._input_session_id
            is_aiming = self._is_aiming
            manual_override_active = self._refresh_manual_override_locked(
                timestamp=timestamp,
                manual_dx=manual_dx,
                manual_dy=manual_dy,
            )

        return MouseFrame(
            timestamp=timestamp,
            manual_dx=manual_dx,
            manual_dy=manual_dy,
            manual_left_pressed=manual_left_pressed,
            manual_override_active=manual_override_active,
            is_aiming=is_aiming,
            target_dx=target_dx,
            target_dy=target_dy,
            auto_fire_requested=auto_fire_requested,
            input_session_id=input_session_id,
            target_revision=target_revision,
            target_timestamp=target_timestamp,
            target=target_info,
        )

    def _refresh_manual_override_locked(self, *, timestamp, manual_dx, manual_dy):
        if not self._is_aiming:
            self._manual_override_until = None
            return False

        manual_speed = (manual_dx ** 2 + manual_dy ** 2) ** 0.5
        if manual_speed >= MANUAL_OVERRIDE_TRIGGER_PX:
            self._manual_override_until = timestamp + MANUAL_OVERRIDE_HOLD_SECONDS

        if self._manual_override_until is None:
            return False
        if timestamp > self._manual_override_until:
            self._manual_override_until = None
            return False
        return True

    def _apply_output(self, output: MouseOutput, *, input_session_id=None):
        with self.lock:
            if (
                input_session_id is not None
                and input_session_id != self._input_session_id
            ):
                return
        with self.lock:
            self._inject_remainder_dx += output.move_dx
            self._inject_remainder_dy += output.move_dy
            move_x = int(self._inject_remainder_dx)
            move_y = int(self._inject_remainder_dy)
            self._inject_remainder_dx -= move_x
            self._inject_remainder_dy -= move_y
        if move_x != 0 or move_y != 0:
            win32api.mouse_event(MOUSEEVENTF_MOVE, move_x, move_y, 0, 0)
            # Subtract injected movement from the accumulator so pynput's
            # on_move callback (which sees ALL cursor movement including our
            # synthetic injection) doesn't feed it back as "manual" input.
            with self.lock:
                self._acc_dx -= move_x
                self._acc_dy -= move_y

        if output.left_click and not self._left_click_held:
            # Always send UP before DOWN to create a clean press edge.
            # This ensures the game registers a fresh click even if the
            # button was already held (e.g. by user or previous cycle).
            win32api.mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
            win32api.mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
            self._left_click_held = True
        elif not output.left_click and self._left_click_held:
            win32api.mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
            self._left_click_held = False

    def run(self):
        while self.running:
            frame = self._build_frame(timestamp=time.perf_counter())
            output = MouseOutput()
            apply_plugins(self.plugins, frame, output)
            self._apply_output(output, input_session_id=frame.input_session_id)
            time.sleep(0.001)
