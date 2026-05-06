import os
import threading
import time
from dataclasses import replace

os.environ["PYGAME_HIDE_SUPPORT_PROMPT"] = "hide"

import pygame
import vgamepad as vg

from runtime.recoil_sidecar.models import ActiveProfilePayload
from runtime.recoil_sidecar.models import RecognizerState

from .base_controller import BaseController
from .gamepad import (
    AIAimPlugin,
    AutoFireConfig,
    AutoFirePlugin,
    DownwardPullDiagnostics,
    GamepadFrame,
    GamepadOutput,
    apply_plugins,
    apply_plugins_with_trace,
    reset_plugins,
    RecoilCompensationConfig,
    RecoilCompensationPlugin,
)


class GamepadController(BaseController, threading.Thread):
    """
    Reads a physical gamepad and mirrors it to a virtual Xbox 360 controller.
    Enhancement behavior is applied by controller-level plugins.
    """

    BUTTON_NAME_MAP = {
        0: "a",
        1: "b",
        2: "x",
        3: "y",
        4: "back",
        5: "guide",
        6: "start",
        7: "left_thumb",
        8: "right_thumb",
        9: "lb",
        10: "rb",
    }

    XUSB_BUTTON_MAP = {
        "a": vg.XUSB_BUTTON.XUSB_GAMEPAD_A,
        "b": vg.XUSB_BUTTON.XUSB_GAMEPAD_B,
        "x": vg.XUSB_BUTTON.XUSB_GAMEPAD_X,
        "y": vg.XUSB_BUTTON.XUSB_GAMEPAD_Y,
        "back": vg.XUSB_BUTTON.XUSB_GAMEPAD_BACK,
        "guide": vg.XUSB_BUTTON.XUSB_GAMEPAD_GUIDE,
        "start": vg.XUSB_BUTTON.XUSB_GAMEPAD_START,
        "left_thumb": vg.XUSB_BUTTON.XUSB_GAMEPAD_LEFT_THUMB,
        "right_thumb": vg.XUSB_BUTTON.XUSB_GAMEPAD_RIGHT_THUMB,
        "lb": vg.XUSB_BUTTON.XUSB_GAMEPAD_LEFT_SHOULDER,
        "rb": vg.XUSB_BUTTON.XUSB_GAMEPAD_RIGHT_SHOULDER,
    }
    DPAD_BUTTONS = (
        vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_UP,
        vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_DOWN,
        vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_LEFT,
        vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_RIGHT,
    )

    def __init__(
        self,
        smoothing=None,
        max_pixels=None,
        auto_fire_output="RB",
        plugins=None,
        recoil_sidecar_service=None,
    ):
        super().__init__()
        self.daemon = True
        self.running = True
        self.ready = False
        self.lock = threading.Lock()
        self._is_aiming = False
        self._auto_fire_requested = False
        self.target_dx = 0.0
        self.target_dy = 0.0
        self.target_revision = 0
        self.target_timestamp = None
        self.target_info = None
        from config import load_tuning_config

        tuning = load_tuning_config()
        ai_aim_config = tuning.gamepad_ai_aim
        if smoothing is not None or max_pixels is not None:
            overrides: dict = {}
            if smoothing is not None:
                overrides["smoothing"] = smoothing
            if max_pixels is not None:
                overrides["max_pixels"] = max_pixels
            ai_aim_config = replace(ai_aim_config, **overrides)

        self._recoil_sidecar_service = recoil_sidecar_service or self._build_recoil_sidecar_service_from_env()
        self._recoil_profile_cache_key = None
        self._recoil_profile_cache_value = None

        self.plugins = list(plugins) if plugins is not None else [
            AIAimPlugin(ai_aim_config),
            AutoFirePlugin(AutoFireConfig(fire_output=auto_fire_output)),
            RecoilCompensationPlugin(
                RecoilCompensationConfig(amount=0.20),
                profile_provider=self._get_active_recoil_profile if self._recoil_sidecar_service is not None else None,
            ),
        ]
        self._downward_pull_diagnostics = DownwardPullDiagnostics.from_env()

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
        except Exception as exc:
            print(f"[Error] Gamepad initialization failed: {exc}")
            self.running = False
            return

        self.ready = True
        self.start()

    def update(self, dx, dy, target=None):
        with self.lock:
            self.target_dx = dx
            self.target_dy = dy
            self.target_info = target
            self.target_revision = getattr(self, "target_revision", 0) + 1
            self.target_timestamp = time.perf_counter()

    def reset(self):
        # NOTE: do NOT touch self._auto_fire_requested here. Auto-fire lifecycle
        # is still owned by the vision-layer detector, and resetting it here
        # would erase its grace window on frames where there is no valid target.
        with self.lock:
            self.target_dx = 0.0
            self.target_dy = 0.0
            self.target_info = None
            self.target_revision = getattr(self, "target_revision", 0) + 1
            self.target_timestamp = time.perf_counter()
        reset_plugins(self.plugins)

    def is_aiming(self):
        return self._is_aiming

    def set_auto_fire(self, pressed: bool):
        with self.lock:
            self._auto_fire_requested = bool(pressed)

    def set_auto_rb(self, pressed: bool):
        self.set_auto_fire(pressed)

    def stop(self):
        self.running = False
        pygame.quit()

    def _axis_to_xbox(self, val):
        clamped = max(-1.0, min(1.0, float(val)))
        if clamped >= 0.0:
            return int((clamped * 32767) + 0.5)
        return -int(((-clamped) * 32768) + 0.5)

    def _apply_stick_deadzone(self, val):
        # Preserve the physical stick signal as-is at the host layer.
        # Any filtering or arbitration should happen in the aiming logic,
        # not by silently discarding small manual inputs before plugins run.
        return int(val)

    def _trigger_to_xbox(self, val):
        return int(((val + 1.0) / 2.0) * 255)

    def _read_buttons(self):
        button_count = self.joystick.get_numbuttons()
        return {
            name: bool(self.joystick.get_button(index)) if index < button_count else False
            for index, name in self.BUTTON_NAME_MAP.items()
        }

    def _read_dpad(self):
        if self.joystick.get_numhats() > 0:
            hat_x, hat_y = self.joystick.get_hat(0)
            return (
                vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_UP
                if hat_y == 1
                else vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_DOWN
                if hat_y == -1
                else vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_LEFT
                if hat_x == -1
                else vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_RIGHT
                if hat_x == 1
                else 0
            )

        button_count = self.joystick.get_numbuttons()
        if button_count > 11 and self.joystick.get_button(11):
            return vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_UP
        if button_count > 12 and self.joystick.get_button(12):
            return vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_DOWN
        if button_count > 13 and self.joystick.get_button(13):
            return vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_LEFT
        if button_count > 14 and self.joystick.get_button(14):
            return vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_RIGHT
        return 0

    def _build_frame(
        self,
        *,
        timestamp: float,
        left_x: int,
        left_y: int,
        manual_right_x: int,
        manual_right_y: int,
        left_trigger: int,
        right_trigger: int,
        buttons: dict[str, bool],
        dpad: int,
    ):
        with self.lock:
            target_dx = self.target_dx
            target_dy = self.target_dy
            target_revision = self.target_revision
            target_timestamp = self.target_timestamp
            target_info = self.target_info
            auto_fire_requested = self._auto_fire_requested

        return GamepadFrame(
            timestamp=timestamp,
            left_x=left_x,
            left_y=left_y,
            manual_right_x=manual_right_x,
            manual_right_y=manual_right_y,
            left_trigger=left_trigger,
            right_trigger=right_trigger,
            buttons=buttons,
            is_aiming=self._is_aiming,
            target_dx=target_dx,
            target_dy=target_dy,
            auto_fire_requested=auto_fire_requested,
            dpad=dpad,
            target_revision=target_revision,
            target_timestamp=target_timestamp,
            target=target_info,
        )

    def _build_output(self, frame: GamepadFrame):
        return GamepadOutput(
            left_x=frame.left_x,
            left_y=frame.left_y,
            right_x=frame.manual_right_x,
            right_y=frame.manual_right_y,
            left_trigger=frame.left_trigger,
            right_trigger=frame.right_trigger,
            buttons=dict(frame.buttons),
            dpad=frame.dpad,
        )

    def _get_active_recoil_profile(self, frame: GamepadFrame | None = None, *, is_aiming: bool | None = None):
        service = getattr(self, "_recoil_sidecar_service", None)
        if service is None:
            return None

        aiming = bool(is_aiming) if is_aiming is not None else bool(getattr(frame, "is_aiming", False))
        context = {"stance": "standing", "aim_mode": "ads" if aiming else "hipfire"}
        payload = _coerce_active_profile_payload(service.publish_active_profile(context=context))
        if payload.status != "ready" or not payload.profile_id:
            self._recoil_profile_cache_key = None
            self._recoil_profile_cache_value = None
            return None

        cache_key = (payload.profile_id, payload.updated_at, payload.aim_mode)
        if getattr(self, "_recoil_profile_cache_key", None) == cache_key:
            return getattr(self, "_recoil_profile_cache_value", None)

        recognizer_state = _coerce_recognizer_state(service.read_recognizer_state())
        if recognizer_state is None:
            self._recoil_profile_cache_key = None
            self._recoil_profile_cache_value = None
            return None

        matches = service.load_matching_profiles(recognizer_state, context=context)
        for profile in matches:
            if profile.profile_id == payload.profile_id:
                self._recoil_profile_cache_key = cache_key
                self._recoil_profile_cache_value = profile
                return profile

        self._recoil_profile_cache_key = None
        self._recoil_profile_cache_value = None
        return None

    def _build_recoil_sidecar_service_from_env(self):
        profile_dir = os.environ.get("RECOIL_PROFILE_DIR")
        recognizer_state_path = os.environ.get("RECOIL_RECOGNIZER_STATE_PATH")
        if not profile_dir or not recognizer_state_path:
            return None

        from runtime.recoil_sidecar.service import RecoilSidecarService

        return RecoilSidecarService(
            profile_dir=profile_dir,
            recognizer_state_path=recognizer_state_path,
        )

    def _apply_plugin_pipeline(self, frame: GamepadFrame, output: GamepadOutput):
        diagnostics = getattr(self, "_downward_pull_diagnostics", None)
        if diagnostics is None or not diagnostics.config.enabled:
            apply_plugins(self.plugins, frame, output)
            return []

        traces = apply_plugins_with_trace(self.plugins, frame, output)
        diagnostics.record_if_triggered(
            frame=frame,
            output=output,
            plugin_traces=traces,
            plugins=self.plugins,
        )
        return traces

    def _apply_dpad(self, dpad_button):
        for button in self.DPAD_BUTTONS:
            if button == dpad_button:
                self.virtual_gamepad.press_button(button=button)
            else:
                self.virtual_gamepad.release_button(button=button)

    def _apply_output(self, output: GamepadOutput):
        self.virtual_gamepad.left_joystick(x_value=output.left_x, y_value=output.left_y)
        self.virtual_gamepad.right_joystick(
            x_value=max(-32768, min(32767, output.right_x)),
            y_value=max(-32768, min(32767, output.right_y)),
        )
        self.virtual_gamepad.left_trigger(value=output.left_trigger)
        self.virtual_gamepad.right_trigger(value=output.right_trigger)
        self._apply_dpad(output.dpad)
        for name, button in self.XUSB_BUTTON_MAP.items():
            if output.buttons.get(name, False):
                self.virtual_gamepad.press_button(button=button)
            else:
                self.virtual_gamepad.release_button(button=button)

    def run(self):
        trigger_initialized = False

        while self.running:
            pygame.event.pump()

            left_x = self._axis_to_xbox(self.joystick.get_axis(0))
            left_y = self._axis_to_xbox(-self.joystick.get_axis(1))

            raw_l2 = self.joystick.get_axis(4)
            raw_r2 = self.joystick.get_axis(5)
            if not trigger_initialized and (raw_l2 != 0.0 or raw_r2 != 0.0):
                trigger_initialized = True
            if not trigger_initialized:
                raw_l2, raw_r2 = -1.0, -1.0

            left_trigger = self._trigger_to_xbox(raw_l2)
            right_trigger = self._trigger_to_xbox(raw_r2)
            self._is_aiming = left_trigger > 10

            manual_right_x = self._apply_stick_deadzone(
                self._axis_to_xbox(self.joystick.get_axis(2))
            )
            manual_right_y = self._apply_stick_deadzone(
                self._axis_to_xbox(-self.joystick.get_axis(3))
            )
            buttons = self._read_buttons()
            dpad = self._read_dpad()

            frame = self._build_frame(
                timestamp=time.perf_counter(),
                left_x=left_x,
                left_y=left_y,
                manual_right_x=manual_right_x,
                manual_right_y=manual_right_y,
                left_trigger=left_trigger,
                right_trigger=right_trigger,
                buttons=buttons,
                dpad=dpad,
            )
            output = self._build_output(frame)
            self._apply_plugin_pipeline(frame, output)
            self._apply_output(output)
            self.virtual_gamepad.update()
            time.sleep(0.001)


def _coerce_active_profile_payload(value):
    if isinstance(value, ActiveProfilePayload):
        return value
    if isinstance(value, dict):
        return ActiveProfilePayload.from_dict(value)
    raise ValueError("active profile payload must be an ActiveProfilePayload or dict")


def _coerce_recognizer_state(value):
    if value is None:
        return None
    if isinstance(value, RecognizerState):
        return value
    if isinstance(value, dict):
        return RecognizerState.from_dict(value)
    raise ValueError("recognizer state must be a RecognizerState, dict, or None")
