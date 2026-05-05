from __future__ import annotations

from dataclasses import dataclass
from dataclasses import field
from typing import Any


@dataclass(slots=True, frozen=True)
class NormalizedROI:
    left: float
    top: float
    width: float
    height: float

    def __post_init__(self) -> None:
        object.__setattr__(self, "left", _require_fraction(self.left, "NormalizedROI.left"))
        object.__setattr__(self, "top", _require_fraction(self.top, "NormalizedROI.top"))
        object.__setattr__(self, "width", _require_positive_fraction(self.width, "NormalizedROI.width"))
        object.__setattr__(self, "height", _require_positive_fraction(self.height, "NormalizedROI.height"))
        if self.left + self.width > 1.0:
            raise ValueError("NormalizedROI horizontal bounds must remain inside the normalized frame")
        if self.top + self.height > 1.0:
            raise ValueError("NormalizedROI vertical bounds must remain inside the normalized frame")

    def to_dict(self) -> dict[str, float]:
        return {
            "left": self.left,
            "top": self.top,
            "width": self.width,
            "height": self.height,
        }


@dataclass(slots=True, frozen=True)
class SwitchSuspicionHints:
    slot_rois: tuple[NormalizedROI, ...] = ()
    switch_signal_names: tuple[str, ...] = ()
    cache_weapon_until_switch: bool = False
    text_window_frames: int = 0

    def __post_init__(self) -> None:
        object.__setattr__(
            self,
            "slot_rois",
            _require_roi_tuple(self.slot_rois, "SwitchSuspicionHints.slot_rois"),
        )
        object.__setattr__(
            self,
            "switch_signal_names",
            _require_string_tuple(
                self.switch_signal_names,
                "SwitchSuspicionHints.switch_signal_names",
            ),
        )
        object.__setattr__(
            self,
            "cache_weapon_until_switch",
            _require_bool(
                self.cache_weapon_until_switch,
                "SwitchSuspicionHints.cache_weapon_until_switch",
            ),
        )
        object.__setattr__(
            self,
            "text_window_frames",
            _require_non_negative_int(
                self.text_window_frames,
                "SwitchSuspicionHints.text_window_frames",
            ),
        )

    def to_dict(self) -> dict[str, Any]:
        return {
            "slot_rois": [roi.to_dict() for roi in self.slot_rois],
            "switch_signal_names": list(self.switch_signal_names),
            "cache_weapon_until_switch": self.cache_weapon_until_switch,
            "text_window_frames": self.text_window_frames,
        }


@dataclass(slots=True, frozen=True)
class WeaponIdentityAdapter:
    game_id: str
    adapter_name: str
    expected_title_behavior: str
    weapon_icon_roi: NormalizedROI
    weapon_name_text_roi: NormalizedROI
    switch_hints: SwitchSuspicionHints = field(default_factory=SwitchSuspicionHints)

    def __post_init__(self) -> None:
        object.__setattr__(self, "game_id", _require_non_empty_str(self.game_id, "WeaponIdentityAdapter.game_id"))
        object.__setattr__(
            self,
            "adapter_name",
            _require_non_empty_str(
                self.adapter_name,
                "WeaponIdentityAdapter.adapter_name",
            ),
        )
        object.__setattr__(
            self,
            "expected_title_behavior",
            _require_non_empty_str(
                self.expected_title_behavior,
                "WeaponIdentityAdapter.expected_title_behavior",
            ),
        )
        object.__setattr__(
            self,
            "weapon_icon_roi",
            _require_roi(self.weapon_icon_roi, "WeaponIdentityAdapter.weapon_icon_roi"),
        )
        object.__setattr__(
            self,
            "weapon_name_text_roi",
            _require_roi(
                self.weapon_name_text_roi,
                "WeaponIdentityAdapter.weapon_name_text_roi",
            ),
        )
        object.__setattr__(
            self,
            "switch_hints",
            _require_switch_hints(self.switch_hints, "WeaponIdentityAdapter.switch_hints"),
        )


class COD22WeaponIdentityAdapter(WeaponIdentityAdapter):
    __slots__ = ()

    def __init__(self) -> None:
        super().__init__(
            game_id="cod22",
            adapter_name="Call of Duty: Modern Warfare II (COD22) HUD Adapter",
            expected_title_behavior=(
                "Persistent weapon silhouette near the ammo HUD is primary; transient text is secondary "
                "because blueprint names can differ from base weapon names."
            ),
            weapon_icon_roi=NormalizedROI(left=0.827, top=0.848, width=0.118, height=0.104),
            weapon_name_text_roi=NormalizedROI(left=0.661, top=0.784, width=0.224, height=0.055),
            switch_hints=SwitchSuspicionHints(
                slot_rois=(
                    NormalizedROI(left=0.744, top=0.862, width=0.034, height=0.031),
                    NormalizedROI(left=0.744, top=0.900, width=0.034, height=0.031),
                ),
                switch_signal_names=(
                    "icon_signature_jump",
                    "slot_indicator_change",
                    "weapon_name_banner",
                ),
                cache_weapon_until_switch=False,
                text_window_frames=24,
            ),
        )


class COD21WeaponIdentityAdapter(WeaponIdentityAdapter):
    __slots__ = ()

    def __init__(self) -> None:
        super().__init__(
            game_id="cod21",
            adapter_name="Call of Duty: Black Ops 6 / Warzone (COD21) HUD Adapter",
            expected_title_behavior=(
                "Switch-time weapon name is the primary cue; limited icon support means the recognizer "
                "should cache the last confirmed weapon until the next strong switch signal."
            ),
            weapon_icon_roi=NormalizedROI(left=0.819, top=0.852, width=0.112, height=0.099),
            weapon_name_text_roi=NormalizedROI(left=0.592, top=0.746, width=0.278, height=0.062),
            switch_hints=SwitchSuspicionHints(
                slot_rois=(),
                switch_signal_names=(
                    "weapon_name_banner",
                    "icon_signature_jump",
                ),
                cache_weapon_until_switch=True,
                text_window_frames=90,
            ),
        )


class COD20WeaponIdentityAdapter(WeaponIdentityAdapter):
    __slots__ = ()

    def __init__(self) -> None:
        super().__init__(
            game_id="cod20",
            adapter_name="Call of Duty: Modern Warfare III (COD20) HUD Adapter",
            expected_title_behavior=(
                "Persistent weapon icon is primary while switch-time text acts as confirmation, so icon "
                "and text should cross-check each other."
            ),
            weapon_icon_roi=NormalizedROI(left=0.812, top=0.846, width=0.123, height=0.107),
            weapon_name_text_roi=NormalizedROI(left=0.638, top=0.775, width=0.236, height=0.058),
            switch_hints=SwitchSuspicionHints(
                slot_rois=(
                    NormalizedROI(left=0.738, top=0.859, width=0.037, height=0.033),
                    NormalizedROI(left=0.738, top=0.898, width=0.037, height=0.033),
                ),
                switch_signal_names=(
                    "slot_indicator_change",
                    "icon_signature_jump",
                    "weapon_name_banner",
                ),
                cache_weapon_until_switch=False,
                text_window_frames=36,
            ),
        )


def _require_fraction(value: Any, label: str) -> float:
    if type(value) not in {int, float}:
        raise ValueError(f"{label} must be a number")
    result = float(value)
    if result < 0.0 or result > 1.0:
        raise ValueError(f"{label} must be between 0.0 and 1.0")
    return result


def _require_positive_fraction(value: Any, label: str) -> float:
    result = _require_fraction(value, label)
    if result <= 0.0:
        raise ValueError(f"{label} must be greater than 0.0")
    return result


def _require_non_empty_str(value: Any, label: str) -> str:
    if type(value) is not str:
        raise ValueError(f"{label} must be a string")
    result = value.strip()
    if not result:
        raise ValueError(f"{label} must be a non-empty string")
    return result


def _require_bool(value: Any, label: str) -> bool:
    if type(value) is not bool:
        raise ValueError(f"{label} must be a boolean")
    return value


def _require_non_negative_int(value: Any, label: str) -> int:
    if type(value) is not int:
        raise ValueError(f"{label} must be an integer")
    if value < 0:
        raise ValueError(f"{label} must be greater than or equal to zero")
    return value


def _require_roi(value: Any, label: str) -> NormalizedROI:
    if not isinstance(value, NormalizedROI):
        raise ValueError(f"{label} must be a NormalizedROI")
    return value


def _require_roi_tuple(value: Any, label: str) -> tuple[NormalizedROI, ...]:
    if not isinstance(value, (list, tuple)):
        raise ValueError(f"{label} must be a list or tuple of NormalizedROI values")
    return tuple(_require_roi(item, f"{label}[{index}]") for index, item in enumerate(value))


def _require_string_tuple(value: Any, label: str) -> tuple[str, ...]:
    if not isinstance(value, (list, tuple)):
        raise ValueError(f"{label} must be a list or tuple of strings")
    return tuple(_require_non_empty_str(item, f"{label}[{index}]") for index, item in enumerate(value))


def _require_switch_hints(value: Any, label: str) -> SwitchSuspicionHints:
    if not isinstance(value, SwitchSuspicionHints):
        raise ValueError(f"{label} must be a SwitchSuspicionHints")
    return value


ADAPTER_REGISTRY: dict[str, WeaponIdentityAdapter] = {
    "cod20": COD20WeaponIdentityAdapter(),
    "cod21": COD21WeaponIdentityAdapter(),
    "cod22": COD22WeaponIdentityAdapter(),
}


def get_adapter(game_id: str) -> WeaponIdentityAdapter:
    normalized_game_id = _require_non_empty_str(game_id, "game_id").casefold()
    try:
        return ADAPTER_REGISTRY[normalized_game_id]
    except KeyError as exc:
        raise KeyError(f"Unknown weapon identity adapter: {game_id!r}") from exc
