import threading
import time
import os
import csv
import ctypes
from datetime import datetime
from pathlib import Path
from ctypes import wintypes

import win32api
from pynput import mouse as pynput_mouse

from .base_controller import BaseController, ControllerVisionState, _state_authorized_target
from .mouse import (
    AIAimPlugin,
    AutoFirePlugin,
    MouseFrame,
    MouseOutput,
    RecoilCompensationPlugin,
    apply_plugins,
    reset_plugins,
)

MOUSEEVENTF_MOVE = 0x0001
MOUSEEVENTF_LEFTDOWN = 0x0002
MOUSEEVENTF_LEFTUP = 0x0004
VK_RBUTTON = 0x02
MANUAL_OVERRIDE_TRIGGER_PX = 9.0
MANUAL_OVERRIDE_HOLD_SECONDS = 0.120
MANUAL_FIRE_SUPPRESS_SECONDS = 0.150
INJECTED_MOTION_SUPPRESS_SECONDS = 0.020
INJECTED_MOTION_ECHO_SCALE = 5.0
RESPONSE_OBSERVATION_SETTLE_SECONDS = 0.004
TELEMETRY_TRUTHY = frozenset({"1", "true", "yes", "on"})
TELEMETRY_FLUSH_ROWS = 30
INPUT_MOUSE = 0
RESPONSE_BODY_BOX_CENTER_DELTA_RATIO = 0.35
RESPONSE_MIN_BODY_BOX_CENTER_DELTA_PX = 24.0


try:
    ULONG_PTR = wintypes.ULONG_PTR
except AttributeError:
    ULONG_PTR = ctypes.c_ulonglong if ctypes.sizeof(ctypes.c_void_p) == 8 else ctypes.c_ulong


class _MouseInput(ctypes.Structure):
    _fields_ = (
        ("dx", wintypes.LONG),
        ("dy", wintypes.LONG),
        ("mouseData", wintypes.DWORD),
        ("dwFlags", wintypes.DWORD),
        ("time", wintypes.DWORD),
        ("dwExtraInfo", ULONG_PTR),
    )


class _InputUnion(ctypes.Union):
    _fields_ = (("mi", _MouseInput),)


class _Input(ctypes.Structure):
    _fields_ = (("type", wintypes.DWORD), ("union", _InputUnion))


class _Win32MouseEventInjector:
    name = "mouse_event"

    def send(self, flags, dx=0, dy=0):
        win32api.mouse_event(flags, dx, dy, 0, 0)


class _SendInputInjector:
    name = "sendinput"

    def __init__(self):
        self._send_input = ctypes.windll.user32.SendInput
        self._send_input.argtypes = (
            wintypes.UINT,
            ctypes.POINTER(_Input),
            ctypes.c_int,
        )
        self._send_input.restype = wintypes.UINT

    def send(self, flags, dx=0, dy=0):
        packet = _Input(
            type=INPUT_MOUSE,
            union=_InputUnion(
                mi=_MouseInput(
                    dx=int(dx),
                    dy=int(dy),
                    mouseData=0,
                    dwFlags=int(flags),
                    time=0,
                    dwExtraInfo=0,
                )
            ),
        )
        sent = self._send_input(1, ctypes.byref(packet), ctypes.sizeof(packet))
        if sent != 1:
            raise ctypes.WinError()


def _mouse_injector_from_env():
    backend = os.getenv("MOUSE_INJECTION_BACKEND", "mouse_event").strip().lower()
    if backend in {"sendinput", "send_input"}:
        try:
            injector = _SendInputInjector()
        except Exception as exc:
            print(f"[MouseInput] SendInput unavailable ({exc}); falling back to mouse_event")
            return _Win32MouseEventInjector()
        print("[MouseInput] backend=sendinput")
        return injector
    if backend not in {"", "mouse_event", "mouseevent"}:
        print(f"[MouseInput] unknown backend={backend}; falling back to mouse_event")
    return _Win32MouseEventInjector()


class _MouseTelemetry:
    HEADER = (
        "timestamp",
        "is_aiming",
        "manual_dx",
        "manual_dy",
        "manual_override_active",
        "manual_left_pressed",
        "target_dx",
        "target_dy",
        "target_revision",
        "target_source",
        "target_age_ms",
        "vision_handoff_ms",
        "controller_consume_age_ms",
        "injection_backend",
        "ai_mode",
        "ai_phase",
        "output_dx",
        "output_dy",
        "move_x",
        "move_y",
        "left_click",
        "auto_fire_active",
        "inject_remainder_dx",
        "inject_remainder_dy",
        "pending_injected_dx",
        "pending_injected_dy",
        "local_motion_dx_since_target",
        "local_motion_dy_since_target",
        "physical_right_pressed",
        "physical_aim_sync_resets",
        "physical_aim_sync_presses",
        "injection_errors",
        "response_px_per_input",
        "response_input_scale",
    )

    def __init__(self, path: Path):
        self.path = path
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._file = self.path.open("w", newline="", encoding="utf-8")
        self._writer = csv.writer(self._file)
        self._writer.writerow(self.HEADER)
        self._file.flush()
        self._rows_since_flush = 0

    @classmethod
    def from_env(cls):
        enabled = os.getenv("MOUSE_TELEMETRY", "").lower() in TELEMETRY_TRUTHY
        if not enabled:
            return None
        configured_path = os.getenv("MOUSE_TELEMETRY_PATH")
        if configured_path:
            path = Path(configured_path)
        else:
            stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
            path = Path("artifacts") / "mouse_telemetry" / f"mouse-controller-{stamp}.csv"
        telemetry = cls(path)
        print(f"[MouseTelemetry] writing {telemetry.path}")
        return telemetry

    def write_row(self, row):
        self._writer.writerow(row)
        self._rows_since_flush += 1
        if self._rows_since_flush >= TELEMETRY_FLUSH_ROWS:
            self._file.flush()
            self._rows_since_flush = 0

    def close(self):
        self._file.flush()
        self._file.close()


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
        self._local_motion_dx_since_target = 0.0
        self._local_motion_dy_since_target = 0.0
        self._local_motion_updated_at = None
        self._is_aiming = False
        self._auto_fire_requested = False
        self._auto_fire_timestamp = None
        self._vision_received_at = None
        self._vision_submitted_at = None
        self._acc_dx = 0.0
        self._acc_dy = 0.0
        self._pending_injected_dx = 0.0
        self._pending_injected_dy = 0.0
        self._pending_injected_until = None
        self._manual_left_pressed = False
        self._manual_override_until = None
        self._manual_fire_suppress_until = None
        self._left_click_held = False
        self._input_session_id = 0
        self._physical_right_pressed = False
        self._physical_aim_sync_resets = 0
        self._physical_aim_sync_presses = 0
        self._injection_error_count = 0

        from config import load_tuning_config

        tuning = load_tuning_config()
        ai_config = tuning.mouse_ai_aim
        self._mouse_response_enabled = bool(ai_config.response_adaptive_enabled)
        self._mouse_response_min_px_per_input = max(
            0.001,
            float(ai_config.response_min_px_per_input),
        )
        self._mouse_response_max_px_per_input = max(
            self._mouse_response_min_px_per_input,
            float(ai_config.response_max_px_per_input),
        )
        initial_px_per_input = self._clamp_mouse_response_px_per_input(
            float(ai_config.response_px_per_input_initial)
        )
        self._mouse_response_px_per_input = initial_px_per_input
        self._mouse_response_local_min_px_per_input = initial_px_per_input
        self._mouse_response_sample_alpha = max(
            0.0,
            min(1.0, float(ai_config.response_sample_alpha)),
        )
        self._mouse_response_min_motion = max(
            0.0,
            float(ai_config.response_min_motion_input),
        )
        self._mouse_response_stall_factor = max(
            0.1,
            min(1.0, float(ai_config.response_stall_factor)),
        )
        self.plugins = list(plugins) if plugins is not None else [
            AIAimPlugin(ai_config),
            AutoFirePlugin(tuning.mouse_auto_fire),
            RecoilCompensationPlugin(tuning.mouse_recoil),
        ]
        self._injector = _mouse_injector_from_env()

        self._last_mouse_x, self._last_mouse_y = win32api.GetCursorPos()
        self._mouse_listener = pynput_mouse.Listener(
            on_move=self._on_mouse_move,
            on_click=self._on_mouse_click,
        )
        self._telemetry = _MouseTelemetry.from_env()
        self._mouse_listener.start()
        self.start()

    def _on_mouse_move(self, x, y):
        dx = x - self._last_mouse_x
        dy = y - self._last_mouse_y
        self._last_mouse_x, self._last_mouse_y = x, y
        timestamp = time.perf_counter()
        with self.lock:
            dx, dy = self._suppress_observed_injected_motion_locked(
                dx,
                dy,
                timestamp=timestamp,
            )
            self._acc_dx += dx
            self._acc_dy += dy

    def _on_mouse_click(self, x, y, button, pressed):
        if button == pynput_mouse.Button.right:
            with self.lock:
                self._is_aiming = pressed
                self._physical_right_pressed = pressed
            if not pressed:
                self.reset()
            return

        if button == pynput_mouse.Button.left:
            suppress_until = time.perf_counter() + MANUAL_FIRE_SUPPRESS_SECONDS
            right_pressed = self._read_physical_right_pressed()
            synthetic_left_held = False
            reset_after_release = False
            with self.lock:
                self._manual_left_pressed = pressed
                self._manual_fire_suppress_until = suppress_until
                synthetic_left_held = self._left_click_held
                if pressed and synthetic_left_held:
                    self._left_click_held = False
                if pressed:
                    self._is_aiming = True
                elif not right_pressed:
                    self._is_aiming = False
                    reset_after_release = True
            if pressed and synthetic_left_held:
                self._send_mouse(MOUSEEVENTF_LEFTUP)
            if reset_after_release:
                self.reset()

    def update(self, dx, dy, target=None):
        target_timestamp = getattr(target, "observed_at", None)
        if target_timestamp is None:
            target_timestamp = time.perf_counter()
        with self.lock:
            self._update_mouse_response_locked(
                dx,
                dy,
                target,
                observed_at=target_timestamp,
            )
            self.target_dx = dx
            self.target_dy = dy
            self.target_info = target
            self.target_revision += 1
            self.target_timestamp = target_timestamp
            self._local_motion_dx_since_target = 0.0
            self._local_motion_dy_since_target = 0.0
            self._local_motion_updated_at = None

    def update_vision_state(self, state: ControllerVisionState):
        target = _state_authorized_target(state)
        state_timestamp = state.observed_at
        if state_timestamp is None:
            state_timestamp = getattr(state.target, "observed_at", None)
        if state_timestamp is None:
            state_timestamp = time.perf_counter()
        submitted_at = state.submitted_at
        if submitted_at is None:
            submitted_at = time.perf_counter()
        auto_fire_requested = bool(
            state.auto_fire_requested and target is not None and state.fire_authority
        )
        auto_fire_timestamp = state.auto_fire_observed_at
        if auto_fire_timestamp is None:
            auto_fire_timestamp = state_timestamp

        with self.lock:
            if target is None:
                self.target_dx = 0.0
                self.target_dy = 0.0
                self.target_info = None
            else:
                self._update_mouse_response_locked(
                    state.dx,
                    state.dy,
                    target,
                    observed_at=state_timestamp,
                )
                self.target_dx = state.dx
                self.target_dy = state.dy
                self.target_info = target
            self.target_revision += 1
            self.target_timestamp = state_timestamp
            self._local_motion_dx_since_target = 0.0
            self._local_motion_dy_since_target = 0.0
            self._local_motion_updated_at = None
            self._auto_fire_requested = auto_fire_requested
            self._auto_fire_timestamp = auto_fire_timestamp if auto_fire_requested else None
            self._vision_received_at = state.received_at
            self._vision_submitted_at = submitted_at

    def _clear_target_state_locked(self):
        self.target_dx = 0.0
        self.target_dy = 0.0
        self.target_info = None
        self.target_revision += 1
        self.target_timestamp = time.perf_counter()
        self._local_motion_dx_since_target = 0.0
        self._local_motion_dy_since_target = 0.0
        self._local_motion_updated_at = None

    def clear_target(self):
        with self.lock:
            self._clear_target_state_locked()

    def reset(self):
        synthetic_left_held = False
        with self.lock:
            self._clear_target_state_locked()
            self._inject_remainder_dx = 0.0
            self._inject_remainder_dy = 0.0
            self._pending_injected_dx = 0.0
            self._pending_injected_dy = 0.0
            self._pending_injected_until = None
            self._manual_override_until = None
            self._manual_fire_suppress_until = None
            self._auto_fire_requested = False
            self._auto_fire_timestamp = None
            self._manual_left_pressed = False
            synthetic_left_held = self._left_click_held
            self._left_click_held = False
            self._input_session_id += 1
        reset_plugins(self.plugins)
        if synthetic_left_held:
            self._send_mouse(MOUSEEVENTF_LEFTUP)

    def is_aiming(self):
        return self._is_aiming

    def _sync_physical_aim_state(self):
        right_pressed = self._read_physical_right_pressed()

        should_reset = False
        with self.lock:
            self._physical_right_pressed = right_pressed
            if right_pressed or self._manual_left_pressed:
                if not self._is_aiming:
                    self._is_aiming = True
                    if right_pressed:
                        self._physical_aim_sync_presses += 1
                return
            if self._is_aiming:
                self._is_aiming = False
                self._physical_aim_sync_resets += 1
                should_reset = True

        if should_reset:
            self.reset()

    def _read_physical_right_pressed(self):
        try:
            return bool(win32api.GetAsyncKeyState(VK_RBUTTON) & 0x8000)
        except Exception:
            return False

    def set_auto_fire(self, pressed: bool, observed_at: float | None = None):
        if pressed and observed_at is None:
            observed_at = time.perf_counter()
        with self.lock:
            self._auto_fire_requested = bool(pressed)
            self._auto_fire_timestamp = observed_at if pressed else None

    def set_auto_rb(self, pressed: bool):
        self.set_auto_fire(pressed)

    def stop(self):
        self.running = False
        self._mouse_listener.stop()
        self._release_synthetic_left_if_held()
        telemetry = getattr(self, "_telemetry", None)
        if telemetry is not None:
            telemetry.close()
            self._telemetry = None

    def _build_frame(self, *, timestamp):
        with self.lock:
            manual_dx = self._acc_dx
            manual_dy = self._acc_dy
            self._acc_dx = 0.0
            self._acc_dy = 0.0
            local_motion_px_per_input = self._local_motion_px_per_input_locked()
            target_dx = self._offset_error_after_motion(
                self.target_dx,
                self._local_motion_dx_since_target * local_motion_px_per_input,
            )
            target_dy = self._offset_error_after_motion(
                self.target_dy,
                self._local_motion_dy_since_target * local_motion_px_per_input,
            )
            auto_fire_requested = self._auto_fire_requested
            if self._manual_fire_suppress_until is not None:
                if timestamp <= self._manual_fire_suppress_until:
                    auto_fire_requested = False
                else:
                    self._manual_fire_suppress_until = None
            target_revision = self.target_revision
            target_timestamp = self.target_timestamp
            target_info = self.target_info
            auto_fire_timestamp = getattr(self, "_auto_fire_timestamp", None)
            vision_received_at = getattr(self, "_vision_received_at", None)
            vision_submitted_at = getattr(self, "_vision_submitted_at", None)
            manual_left_pressed = self._manual_left_pressed
            input_session_id = self._input_session_id
            is_aiming = self._is_aiming
            response_input_scale = self._mouse_response_input_scale_locked()
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
            auto_fire_timestamp=auto_fire_timestamp,
            vision_received_at=vision_received_at,
            vision_submitted_at=vision_submitted_at,
            target=target_info,
            response_input_scale=response_input_scale,
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

    def _update_mouse_response_locked(self, next_dx, next_dy, next_target, *, observed_at=None):
        if not getattr(self, "_mouse_response_enabled", True):
            return
        previous_target = self.target_info
        if previous_target is None or next_target is None:
            return
        if not self._same_target_family_for_response(next_target, previous_target):
            return

        injected_dx = self._local_motion_dx_since_target
        injected_dy = self._local_motion_dy_since_target
        motion_sq = injected_dx ** 2 + injected_dy ** 2
        min_motion = getattr(self, "_mouse_response_min_motion", 4.0)
        if motion_sq < min_motion ** 2:
            return
        if self._is_response_observation_stale_locked(next_target, observed_at=observed_at):
            return

        error_delta_x = self.target_dx - next_dx
        error_delta_y = self.target_dy - next_dy
        projected_response = (
            error_delta_x * injected_dx + error_delta_y * injected_dy
        ) / motion_sq
        if projected_response > 0.0:
            self._blend_mouse_response_px_per_input_locked(projected_response)

    def _is_response_observation_stale_locked(self, next_target, *, observed_at=None):
        last_motion_at = getattr(self, "_local_motion_updated_at", None)
        if last_motion_at is None:
            return False
        if observed_at is None:
            observed_at = getattr(next_target, "observed_at", None)
        if observed_at is None:
            return False
        return observed_at <= last_motion_at + RESPONSE_OBSERVATION_SETTLE_SECONDS

    def _blend_mouse_response_px_per_input_locked(self, sample):
        sample = self._clamp_mouse_response_px_per_input(sample)
        alpha = getattr(self, "_mouse_response_sample_alpha", 0.35)
        current = getattr(self, "_mouse_response_px_per_input", 1.0)
        self._mouse_response_px_per_input = self._clamp_mouse_response_px_per_input(
            current * (1.0 - alpha) + sample * alpha
        )

    def _mouse_response_input_scale_locked(self):
        if not getattr(self, "_mouse_response_enabled", True):
            return 1.0
        px_per_input = self._clamp_mouse_response_px_per_input(
            getattr(self, "_mouse_response_px_per_input", 1.0)
        )
        if px_per_input <= 0.0:
            return 1.0
        return 1.0 / px_per_input

    def _local_motion_px_per_input_locked(self):
        if not getattr(self, "_mouse_response_enabled", True):
            return 1.0
        current = self._clamp_mouse_response_px_per_input(
            getattr(self, "_mouse_response_px_per_input", 1.0)
        )
        floor = self._clamp_mouse_response_px_per_input(
            getattr(self, "_mouse_response_local_min_px_per_input", current)
        )
        return max(current, floor)

    def _clamp_mouse_response_px_per_input(self, value):
        min_value = getattr(self, "_mouse_response_min_px_per_input", 0.08)
        max_value = getattr(self, "_mouse_response_max_px_per_input", 3.0)
        if value <= 0.0:
            value = min_value
        return max(min_value, min(max_value, value))

    @staticmethod
    def _same_target_family_for_response(current, previous):
        current_box = getattr(current, "body_box", None)
        previous_box = getattr(previous, "body_box", None)
        if current_box is not None and previous_box is not None:
            current_center_x = (current_box[0] + current_box[2]) * 0.5
            current_center_y = (current_box[1] + current_box[3]) * 0.5
            previous_center_x = (previous_box[0] + previous_box[2]) * 0.5
            previous_center_y = (previous_box[1] + previous_box[3]) * 0.5
            average_width = (
                max(0.0, current_box[2] - current_box[0])
                + max(0.0, previous_box[2] - previous_box[0])
            ) * 0.5
            average_height = (
                max(0.0, current_box[3] - current_box[1])
                + max(0.0, previous_box[3] - previous_box[1])
            ) * 0.5
            return (
                abs(current_center_x - previous_center_x)
                <= max(
                    RESPONSE_MIN_BODY_BOX_CENTER_DELTA_PX,
                    average_width * RESPONSE_BODY_BOX_CENTER_DELTA_RATIO,
                )
                and abs(current_center_y - previous_center_y)
                <= max(
                    RESPONSE_MIN_BODY_BOX_CENTER_DELTA_PX,
                    average_height * RESPONSE_BODY_BOX_CENTER_DELTA_RATIO,
                )
            )

        dx = current.aim_point_x - previous.aim_point_x
        dy = current.aim_point_y - previous.aim_point_y
        return (dx ** 2 + dy ** 2) ** 0.5 <= 36.0

    def _apply_output(self, output: MouseOutput, *, input_session_id=None):
        stale_session = False
        release_for_stale_session = False
        with self.lock:
            if (
                input_session_id is not None
                and input_session_id != self._input_session_id
            ):
                stale_session = True
                if self._left_click_held:
                    self._left_click_held = False
                    release_for_stale_session = True
        if stale_session:
            if release_for_stale_session:
                self._send_mouse(MOUSEEVENTF_LEFTUP)
                return 0, 0
            return 0, 0
        with self.lock:
            self._inject_remainder_dx += output.move_dx
            self._inject_remainder_dy += output.move_dy
            move_x = int(self._inject_remainder_dx)
            move_y = int(self._inject_remainder_dy)
            self._inject_remainder_dx -= move_x
            self._inject_remainder_dy -= move_y
        if move_x != 0 or move_y != 0:
            injected_at = time.perf_counter()
            with self.lock:
                self._remember_pending_injected_motion_locked(
                    move_x,
                    move_y,
                    timestamp=injected_at,
                )
            if self._send_mouse(MOUSEEVENTF_MOVE, move_x, move_y):
                with self.lock:
                    if self.target_info is not None:
                        self._local_motion_dx_since_target += move_x
                        self._local_motion_dy_since_target += move_y
                        self._local_motion_updated_at = injected_at
            else:
                with self.lock:
                    self._remove_pending_injected_motion_locked(move_x, move_y)
                move_x = 0
                move_y = 0

        if output.left_click and not self._left_click_held:
            # Always send UP before DOWN to create a clean press edge.
            # This ensures the game registers a fresh click even if the
            # button was already held (e.g. by user or previous cycle).
            self._send_mouse(MOUSEEVENTF_LEFTUP)
            if self._send_mouse(MOUSEEVENTF_LEFTDOWN):
                self._left_click_held = True
        elif not output.left_click and self._left_click_held:
            if self._send_mouse(MOUSEEVENTF_LEFTUP):
                self._left_click_held = False
        return move_x, move_y

    def _send_mouse(self, flags, dx=0, dy=0):
        injector = getattr(self, "_injector", None)
        if injector is None:
            injector = _Win32MouseEventInjector()
        try:
            injector.send(flags, dx, dy)
            return True
        except Exception as exc:
            with self.lock:
                error_count = getattr(
                    self,
                    "_injection_error_count",
                    0,
                ) + 1
                self._injection_error_count = error_count
            if error_count <= 3 or error_count % 30 == 0:
                print(f"[MouseInput] injection failed backend={injector.name}: {exc}")
            return False

    def _write_telemetry(self, frame: MouseFrame, output: MouseOutput, move_x, move_y):
        telemetry = getattr(self, "_telemetry", None)
        if telemetry is None:
            return

        ai_plugin = next(
            (plugin for plugin in self.plugins if isinstance(plugin, AIAimPlugin)),
            None,
        )
        target_source = getattr(frame.target, "target_source", None)
        with self.lock:
            inject_remainder_dx = self._inject_remainder_dx
            inject_remainder_dy = self._inject_remainder_dy
            pending_injected_dx = self._pending_injected_dx
            pending_injected_dy = self._pending_injected_dy
            local_motion_dx = self._local_motion_dx_since_target
            local_motion_dy = self._local_motion_dy_since_target
            physical_right_pressed = self._physical_right_pressed
            physical_aim_sync_resets = self._physical_aim_sync_resets
            physical_aim_sync_presses = self._physical_aim_sync_presses
            injection_errors = getattr(self, "_injection_error_count", 0)
            response_px_per_input = getattr(self, "_mouse_response_px_per_input", 1.0)
            response_input_scale = self._mouse_response_input_scale_locked()
        telemetry.write_row(
            (
                f"{frame.timestamp:.6f}",
                int(frame.is_aiming),
                f"{frame.manual_dx:.3f}",
                f"{frame.manual_dy:.3f}",
                int(frame.manual_override_active),
                int(frame.manual_left_pressed),
                f"{frame.target_dx:.3f}",
                f"{frame.target_dy:.3f}",
                frame.target_revision,
                target_source or "",
                self._format_elapsed_ms(frame.target_timestamp, frame.timestamp),
                self._format_elapsed_ms(frame.vision_received_at, frame.vision_submitted_at),
                self._format_elapsed_ms(frame.vision_submitted_at, frame.timestamp),
                getattr(getattr(self, "_injector", None), "name", "mouse_event"),
                getattr(ai_plugin, "_mode", ""),
                getattr(ai_plugin, "_control_phase", ""),
                f"{output.move_dx:.3f}",
                f"{output.move_dy:.3f}",
                move_x,
                move_y,
                int(output.left_click),
                int(output.auto_fire_active),
                f"{inject_remainder_dx:.3f}",
                f"{inject_remainder_dy:.3f}",
                f"{pending_injected_dx:.3f}",
                f"{pending_injected_dy:.3f}",
                f"{local_motion_dx:.3f}",
                f"{local_motion_dy:.3f}",
                int(physical_right_pressed),
                physical_aim_sync_resets,
                physical_aim_sync_presses,
                injection_errors,
                f"{response_px_per_input:.4f}",
                f"{response_input_scale:.3f}",
            )
        )

    def _release_synthetic_left_if_held(self):
        release = False
        with self.lock:
            if self._left_click_held:
                self._left_click_held = False
                release = True
        if release:
            self._send_mouse(MOUSEEVENTF_LEFTUP)

    @staticmethod
    def _offset_error_after_motion(error, motion):
        if motion == 0:
            return error
        adjusted = error - motion
        if error > 0.0 and adjusted < 0.0:
            return 0.0
        if error < 0.0 and adjusted > 0.0:
            return 0.0
        return adjusted

    @staticmethod
    def _format_elapsed_ms(start, end):
        if start is None or end is None:
            return ""
        return f"{max(0.0, (end - start) * 1000.0):.3f}"

    def _remember_pending_injected_motion_locked(self, dx, dy, *, timestamp):
        self._expire_pending_injected_motion_locked(timestamp)
        self._pending_injected_dx += dx * INJECTED_MOTION_ECHO_SCALE
        self._pending_injected_dy += dy * INJECTED_MOTION_ECHO_SCALE
        self._pending_injected_until = timestamp + INJECTED_MOTION_SUPPRESS_SECONDS

    def _remove_pending_injected_motion_locked(self, dx, dy):
        self._pending_injected_dx = self._remove_pending_axis_motion(
            pending=self._pending_injected_dx,
            amount=dx * INJECTED_MOTION_ECHO_SCALE,
        )
        self._pending_injected_dy = self._remove_pending_axis_motion(
            pending=self._pending_injected_dy,
            amount=dy * INJECTED_MOTION_ECHO_SCALE,
        )
        if self._pending_injected_dx == 0.0 and self._pending_injected_dy == 0.0:
            self._pending_injected_until = None

    def _expire_pending_injected_motion_locked(self, timestamp):
        if (
            self._pending_injected_until is not None
            and timestamp > self._pending_injected_until
        ):
            self._pending_injected_dx = 0.0
            self._pending_injected_dy = 0.0
            self._pending_injected_until = None

    def _suppress_observed_injected_motion_locked(self, dx, dy, *, timestamp):
        self._expire_pending_injected_motion_locked(timestamp)
        if self._pending_injected_until is None:
            return dx, dy

        dx, self._pending_injected_dx = self._consume_pending_axis_motion(
            observed=dx,
            pending=self._pending_injected_dx,
        )
        dy, self._pending_injected_dy = self._consume_pending_axis_motion(
            observed=dy,
            pending=self._pending_injected_dy,
        )
        if self._pending_injected_dx == 0.0 and self._pending_injected_dy == 0.0:
            self._pending_injected_until = None
        return dx, dy

    @staticmethod
    def _consume_pending_axis_motion(*, observed, pending):
        if observed == 0.0 or pending == 0.0:
            return observed, pending

        amount = min(abs(observed), abs(pending))
        observed_sign = 1.0 if observed > 0.0 else -1.0
        pending_sign = 1.0 if pending > 0.0 else -1.0
        observed -= observed_sign * amount
        pending -= pending_sign * amount
        if abs(observed) < 1e-9:
            observed = 0.0
        if abs(pending) < 1e-9:
            pending = 0.0
        return observed, pending

    @staticmethod
    def _remove_pending_axis_motion(*, pending, amount):
        if pending == 0.0 or amount == 0.0 or pending * amount <= 0.0:
            return pending
        remove = min(abs(pending), abs(amount))
        sign = 1.0 if amount > 0.0 else -1.0
        pending -= sign * remove
        if abs(pending) < 1e-9:
            pending = 0.0
        return pending

    def run(self):
        while self.running:
            self._sync_physical_aim_state()
            frame = self._build_frame(timestamp=time.perf_counter())
            output = MouseOutput()
            apply_plugins(self.plugins, frame, output)
            move_x, move_y = self._apply_output(
                output,
                input_session_id=frame.input_session_id,
            )
            self._write_telemetry(frame, output, move_x, move_y)
            time.sleep(0.001)
