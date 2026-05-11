from __future__ import annotations

from typing import Any


DEFAULT_BUTTON_NAME_MAP = {
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


class PygamePhysicalGamepadReader:
    RIGHT_TRIGGER_AXIS_INDEX = 5
    LEFT_TRIGGER_AXIS_INDEX = 4
    RIGHT_BUMPER_BUTTON_NAME = "rb"

    def __init__(self, *, pygame_module: Any, joystick: Any, button_name_map: dict[int, str] | None = None) -> None:
        self._pygame = pygame_module
        self._joystick = joystick
        self._button_name_map = dict(button_name_map or DEFAULT_BUTTON_NAME_MAP)
        self._trigger_initialized = False

    def pump(self) -> None:
        self._pygame.event.pump()

    def read_buttons(self) -> dict[str, bool]:
        button_count = self._joystick.get_numbuttons()
        return {
            name: bool(self._joystick.get_button(index)) if index < button_count else False
            for index, name in self._button_name_map.items()
        }

    def read_dpad(self) -> int:
        import vgamepad as vg

        if self._joystick.get_numhats() > 0:
            hat_x, hat_y = self._joystick.get_hat(0)
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

        button_count = self._joystick.get_numbuttons()
        if button_count > 11 and self._joystick.get_button(11):
            return vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_UP
        if button_count > 12 and self._joystick.get_button(12):
            return vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_DOWN
        if button_count > 13 and self._joystick.get_button(13):
            return vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_LEFT
        if button_count > 14 and self._joystick.get_button(14):
            return vg.XUSB_BUTTON.XUSB_GAMEPAD_DPAD_RIGHT
        return 0

    def read_trigger_values(self) -> tuple[int, int]:
        raw_l2 = self._read_axis(self.LEFT_TRIGGER_AXIS_INDEX, fallback=-1.0)
        raw_r2 = self._read_axis(self.RIGHT_TRIGGER_AXIS_INDEX, fallback=-1.0)
        if not self._trigger_initialized and (raw_l2 != 0.0 or raw_r2 != 0.0):
            self._trigger_initialized = True
        if not self._trigger_initialized:
            raw_l2, raw_r2 = -1.0, -1.0
        return self.trigger_to_xbox(raw_l2), self.trigger_to_xbox(raw_r2)

    def read_right_fire_pressed(self) -> bool:
        buttons = self.read_buttons()
        _, right_trigger = self.read_trigger_values()
        return bool(buttons.get(self.RIGHT_BUMPER_BUTTON_NAME, False) or right_trigger > 10)

    def _read_axis(self, index: int, *, fallback: float) -> float:
        try:
            return float(self._joystick.get_axis(index))
        except Exception:
            return float(fallback)

    @staticmethod
    def trigger_to_xbox(val: float) -> int:
        return int(((float(val) + 1.0) / 2.0) * 255)


__all__ = ["DEFAULT_BUTTON_NAME_MAP", "PygamePhysicalGamepadReader"]
