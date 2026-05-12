import os
import json
import threading
import time
from dataclasses import replace

os.environ["PYGAME_HIDE_SUPPORT_PROMPT"] = "hide"

import pygame
import vgamepad as vg

from recoil_app import GamepadRecoilBridge
from runtime.recoil_sidecar.models import ActiveProfilePayload
from runtime.recoil_sidecar.models import RecognizerState

from .base_controller import BaseController
from .gamepad import (
    AIAimPlugin,
    AutoFireConfig,
    AutoFirePlugin,
    DEFAULT_BUTTON_NAME_MAP,
    DownwardPullDiagnostics,
    GamepadFrame,
    GamepadOutput,
    PygamePhysicalGamepadReader,
    apply_plugins,
    apply_plugins_with_trace,
    reset_plugins,
    RecoilCompensationConfig,
    RecoilCompensationPlugin,
    YButtonTextWeaponRecognizer,
)


class GamepadController(BaseController, threading.Thread):
    """
    Reads a physical gamepad and mirrors it to a virtual Xbox 360 controller.
    Enhancement behavior is applied by controller-level plugins.
    """

    BUTTON_NAME_MAP = DEFAULT_BUTTON_NAME_MAP

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
        self._recoil_app_bridge = self._build_recoil_app_bridge_from_env()
        self._switch_weapon_recognizer = self._build_weapon_switch_recognizer_from_env()
        self._recoil_profile_cache_key = None
        self._recoil_profile_cache_value = None
        self._last_buttons = {}

        self.plugins = list(plugins) if plugins is not None else [
            AIAimPlugin(ai_aim_config),
            AutoFirePlugin(AutoFireConfig(fire_output=auto_fire_output)),
            RecoilCompensationPlugin(
                RecoilCompensationConfig(amount=0.20),
                profile_provider=self._get_active_recoil_profile
                if self._recoil_sidecar_service is not None or self._recoil_app_bridge is not None
                else None,
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
            self._physical_input = PygamePhysicalGamepadReader(pygame_module=pygame, joystick=self.joystick)
            print(f"[Gamepad] Successfully connected to: {self.joystick.get_name()}")
        except Exception as exc:
            print(f"[Error] Gamepad initialization failed: {exc}")
            self.running = False
            return

        self.ready = True
        self.start()

    def update(self, dx, dy, target=None):
        target_timestamp = getattr(target, "observed_at", None)
        if target_timestamp is None:
            target_timestamp = time.perf_counter()
        with self.lock:
            self.target_dx = dx
            self.target_dy = dy
            self.target_info = target
            self.target_revision = getattr(self, "target_revision", 0) + 1
            self.target_timestamp = target_timestamp

    def reset(self):
        # NOTE: do NOT touch self._auto_fire_requested here. Auto-fire lifecycle
        # is still owned by the vision-layer detector, and resetting it here
        # would erase its grace window on frames where there is no valid target.
        with self.lock:
            self._clear_target_state_locked()
        reset_plugins(self.plugins)

    def clear_target(self):
        with self.lock:
            self._clear_target_state_locked()

    def _clear_target_state_locked(self):
        self.target_dx = 0.0
        self.target_dy = 0.0
        self.target_info = None
        self.target_revision = getattr(self, "target_revision", 0) + 1
        self.target_timestamp = time.perf_counter()

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
        return PygamePhysicalGamepadReader.trigger_to_xbox(val)

    def _read_buttons(self):
        return self._physical_input.read_buttons()

    def _read_dpad(self):
        return self._physical_input.read_dpad()

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
        bridge = getattr(self, "_recoil_app_bridge", None)
        if bridge is not None:
            aiming = bool(is_aiming) if is_aiming is not None else bool(getattr(frame, "is_aiming", False))
            return bridge.get_active_profile(is_aiming=aiming)

        service = getattr(self, "_recoil_sidecar_service", None)
        if service is None:
            return None

        aiming = bool(is_aiming) if is_aiming is not None else bool(getattr(frame, "is_aiming", False))
        context = {"stance": "standing", "aim_mode": "ads" if aiming else "hipfire"}
        try:
            recognizer_state = _coerce_recognizer_state(service.read_recognizer_state())
            if recognizer_state is None:
                self._recoil_profile_cache_key = None
                self._recoil_profile_cache_value = None
                return None

            payload = _coerce_active_profile_payload(service.publish_active_profile(recognizer_state, context=context))
            if payload.status != "ready" or not payload.profile_id:
                self._recoil_profile_cache_key = None
                self._recoil_profile_cache_value = None
                return None

            cache_key = (payload.profile_id, payload.updated_at, payload.aim_mode)
            if getattr(self, "_recoil_profile_cache_key", None) == cache_key:
                return getattr(self, "_recoil_profile_cache_value", None)

            matches = service.load_matching_profiles(recognizer_state, context=context)
        except (OSError, UnicodeDecodeError, ValueError, json.JSONDecodeError):
            self._recoil_profile_cache_key = None
            self._recoil_profile_cache_value = None
            return None

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

    def _build_recoil_app_bridge_from_env(self):
        try:
            return GamepadRecoilBridge.from_env()
        except (OSError, UnicodeDecodeError, ValueError, json.JSONDecodeError) as exc:
            print(f"[Recoil] In-process recoil app disabled: {exc}")
            return None

    def _build_weapon_switch_recognizer_from_env(self):
        mode = os.environ.get("RECOIL_SWITCH_RECOGNITION_MODE", "").strip().casefold()
        game = os.environ.get("RECOIL_GAME", "").strip()
        identity_dir = os.environ.get("RECOIL_SIGNATURE_DIR", "").strip()
        recognizer_state_path = os.environ.get("RECOIL_RECOGNIZER_STATE_PATH", "").strip()
        if mode != "y_button_text" or not game or not identity_dir or not recognizer_state_path:
            return None

        try:
            return YButtonTextWeaponRecognizer.from_directory(
                game=game,
                identity_dir=identity_dir,
                state_path=recognizer_state_path,
            )
        except (OSError, UnicodeDecodeError, ValueError, json.JSONDecodeError) as exc:
            print(f"[Recoil] Weapon switch recognizer disabled: {exc}")
            return None

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

    def _handle_weapon_switch_button(self, buttons: dict[str, bool]) -> None:
        bridge = getattr(self, "_recoil_app_bridge", None)
        if bridge is not None:
            bridge.handle_buttons(buttons)
            self._last_buttons = dict(buttons)
            return

        current_pressed = bool(buttons.get("y", False))
        previous_pressed = bool(getattr(self, "_last_buttons", {}).get("y", False))
        if current_pressed and not previous_pressed:
            recognizer = getattr(self, "_switch_weapon_recognizer", None)
            if recognizer is not None:
                recognizer.handle_switch_pressed()
        self._last_buttons = dict(buttons)

    def _handle_recoil_runtime_fire_state(self, *, is_firing: bool, is_aiming: bool) -> None:
        bridge = getattr(self, "_recoil_app_bridge", None)
        if bridge is None:
            return
        bridge.handle_fire_state(is_firing=is_firing, is_aiming=is_aiming)

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
        while self.running:
            self._physical_input.pump()

            left_x = self._axis_to_xbox(self.joystick.get_axis(0))
            left_y = self._axis_to_xbox(-self.joystick.get_axis(1))

            left_trigger, right_trigger = self._physical_input.read_trigger_values()
            self._is_aiming = left_trigger > 10

            manual_right_x = self._apply_stick_deadzone(
                self._axis_to_xbox(self.joystick.get_axis(2))
            )
            manual_right_y = self._apply_stick_deadzone(
                self._axis_to_xbox(-self.joystick.get_axis(3))
            )
            buttons = self._read_buttons()
            dpad = self._read_dpad()
            self._handle_weapon_switch_button(buttons)
            self._handle_recoil_runtime_fire_state(
                is_firing=self._physical_input.read_right_fire_pressed(),
                is_aiming=self._is_aiming,
            )

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
