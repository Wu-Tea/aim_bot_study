from __future__ import annotations

from dataclasses import dataclass
from typing import Any


@dataclass(slots=True, frozen=True)
class ResolverRuntimeState:
    confirmed_weapon_id: str | None = None
    switch_suspected: bool = False
    text_window_remaining: int = 0

    def __post_init__(self) -> None:
        object.__setattr__(
            self,
            "confirmed_weapon_id",
            _require_optional_non_empty_str(
                self.confirmed_weapon_id,
                "ResolverRuntimeState.confirmed_weapon_id",
            ),
        )
        object.__setattr__(
            self,
            "switch_suspected",
            _require_bool(self.switch_suspected, "ResolverRuntimeState.switch_suspected"),
        )
        object.__setattr__(
            self,
            "text_window_remaining",
            _require_non_negative_int(
                self.text_window_remaining,
                "ResolverRuntimeState.text_window_remaining",
            ),
        )

    @property
    def text_window_active(self) -> bool:
        return self.switch_suspected or self.text_window_remaining > 0

    def begin_frame(self, *, switch_suspected: bool, text_window_frames: int) -> "ResolverRuntimeState":
        remaining = text_window_frames if switch_suspected else max(self.text_window_remaining - 1, 0)
        return ResolverRuntimeState(
            confirmed_weapon_id=self.confirmed_weapon_id,
            switch_suspected=switch_suspected,
            text_window_remaining=remaining,
        )

    def with_confirmed_weapon(self, canonical_weapon_id: str) -> "ResolverRuntimeState":
        return ResolverRuntimeState(
            confirmed_weapon_id=canonical_weapon_id,
            switch_suspected=self.switch_suspected,
            text_window_remaining=self.text_window_remaining,
        )


def _require_optional_non_empty_str(value: Any, label: str) -> str | None:
    if value is None:
        return None
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


__all__ = ["ResolverRuntimeState"]
