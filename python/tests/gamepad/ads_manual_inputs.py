from __future__ import annotations

from dataclasses import dataclass
from hashlib import sha256
import random

from tests.gamepad.ads_benchmark_scenarios import AdsScenarioManifest


STICK_MAX = 32767


@dataclass(frozen=True, slots=True)
class AdsManualInputConfig:
    max_manual_ratio: float = 0.72
    full_scale_x: float = 90.0
    full_scale_y: float = 80.0
    aligned_scale: float = 0.62
    opposing_scale: float = 0.55
    recover_scale: float = 0.48
    vertical_tail_scale: float = 0.16
    early_window_start_frame: int = 2
    early_window_end_frame: int = 12
    opposing_burst_min_frames: int = 2
    opposing_burst_max_frames: int = 4
    overshoot_aligned_frames: int = 3
    overshoot_recover_frames: int = 3


@dataclass(frozen=True, slots=True)
class AdsManualInputFrame:
    frame: int
    manual_right_x: int
    manual_right_y: int
    profile: str
    in_opposing_burst: bool
    in_recovery_window: bool


def generate_ads_manual_input_frames(
    manifest: AdsScenarioManifest,
    *,
    input_profile: str,
    sim_frames: int,
    config: AdsManualInputConfig | None = None,
) -> tuple[AdsManualInputFrame, ...]:
    generator = AdsManualInputGenerator(
        manifest,
        input_profile=input_profile,
        config=config,
    )
    return tuple(
        generator.generate_frame(
            frame=frame,
            error_x=manifest.targets[0].initial_dx,
            error_y=manifest.targets[0].initial_dy,
        )
        for frame in range(sim_frames)
    )


class AdsManualInputGenerator:
    def __init__(
        self,
        manifest: AdsScenarioManifest,
        *,
        input_profile: str,
        config: AdsManualInputConfig | None = None,
    ):
        if input_profile not in {"none", "aligned_follow", "opposing_burst", "overshoot_recover"}:
            raise ValueError("input_profile must be one of none, aligned_follow, opposing_burst, overshoot_recover")
        self.manifest = manifest
        self.input_profile = input_profile
        self.config = config or AdsManualInputConfig()
        self.rng = random.Random(_stable_seed("ads-input", manifest.scenario_key, input_profile))
        self._burst_start, self._burst_end = self._plan_opposing_burst()

    def generate_frame(
        self,
        *,
        frame: int,
        error_x: float,
        error_y: float,
    ) -> AdsManualInputFrame:
        if self.input_profile == "none":
            return AdsManualInputFrame(
                frame=frame,
                manual_right_x=0,
                manual_right_y=0,
                profile="none",
                in_opposing_burst=False,
                in_recovery_window=False,
            )

        if self.input_profile == "aligned_follow":
            return AdsManualInputFrame(
                frame=frame,
                manual_right_x=self._map_x(error_x, self.config.aligned_scale),
                manual_right_y=self._map_y(error_y, self.config.aligned_scale),
                profile="aligned_follow",
                in_opposing_burst=False,
                in_recovery_window=False,
            )

        if self.input_profile == "opposing_burst":
            in_burst = self._burst_start <= frame <= self._burst_end
            scale = -self.config.opposing_scale if in_burst else self.config.aligned_scale
            return AdsManualInputFrame(
                frame=frame,
                manual_right_x=self._map_x(error_x, scale),
                manual_right_y=self._map_y(error_y, scale),
                profile="opposing_burst",
                in_opposing_burst=in_burst,
                in_recovery_window=False,
            )

        if frame < self.config.overshoot_aligned_frames:
            scale_x = self.config.aligned_scale
            scale_y = self.config.aligned_scale
            in_recovery_window = False
        elif frame < (self.config.overshoot_aligned_frames + self.config.overshoot_recover_frames):
            scale_x = -self.config.recover_scale
            scale_y = -self.config.recover_scale
            in_recovery_window = True
        else:
            scale_x = self.config.recover_scale * 0.35
            scale_y = self.config.vertical_tail_scale
            in_recovery_window = False
        return AdsManualInputFrame(
            frame=frame,
            manual_right_x=self._map_x(error_x, scale_x),
            manual_right_y=self._map_y(error_y, scale_y),
            profile="overshoot_recover",
            in_opposing_burst=False,
            in_recovery_window=in_recovery_window,
        )

    def _plan_opposing_burst(self) -> tuple[int, int]:
        start = self.rng.randint(
            self.config.early_window_start_frame,
            self.config.early_window_end_frame,
        )
        duration = self.rng.randint(
            self.config.opposing_burst_min_frames,
            self.config.opposing_burst_max_frames,
        )
        return start, start + duration - 1

    def _map_x(self, error_x: float, scale: float) -> int:
        value = (error_x / self.config.full_scale_x) * STICK_MAX * self.config.max_manual_ratio
        value *= scale
        return _clamp_int(value)

    def _map_y(self, error_y: float, scale: float) -> int:
        value = (-error_y / self.config.full_scale_y) * STICK_MAX * self.config.max_manual_ratio
        value *= scale
        return _clamp_int(value)


def _stable_seed(*parts: object) -> int:
    payload = "|".join(str(part) for part in parts).encode("utf-8")
    return int.from_bytes(sha256(payload).digest()[:8], "big", signed=False)


def _clamp_int(value: float) -> int:
    return int(max(-STICK_MAX, min(STICK_MAX, round(value))))
