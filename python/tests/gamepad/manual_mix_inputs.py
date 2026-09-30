from __future__ import annotations

from dataclasses import dataclass
from hashlib import sha256
import random

from tests.gamepad.benchmark_scenarios import ScenarioManifest, expand_manifest


STICK_MAX = 32767


@dataclass(frozen=True, slots=True)
class ManualMixInputConfig:
    max_manual_ratio: float = 0.72
    full_scale_x: float = 90.0
    full_scale_y: float = 80.0
    aligned_scale: float = 0.62
    wobble_scale: float = 0.18
    opposing_scale: float = 0.55
    recover_scale: float = 0.48
    vertical_jitter_scale: float = 0.12
    near_target_radius_px: float = 18.0
    wobble_period_frames: int = 6
    event_window_frames: int = 16
    opposing_burst_min_frames: int = 2
    opposing_burst_max_frames: int = 5
    overshoot_recover_frames: int = 3
    reference_frame_dt: float = 1.0 / 60.0


HIGH_INTENSITY_MANUAL_MIX_SEEDS = (1, 2, 3, 4, 5)


def high_intensity_manual_mix_config() -> ManualMixInputConfig:
    return ManualMixInputConfig(
        max_manual_ratio=0.92,
        aligned_scale=0.82,
        wobble_scale=0.28,
        opposing_scale=0.82,
        recover_scale=0.68,
        vertical_jitter_scale=0.22,
        event_window_frames=24,
        opposing_burst_min_frames=4,
        opposing_burst_max_frames=8,
        overshoot_recover_frames=5,
    )


@dataclass(frozen=True, slots=True)
class ManualInputFrame:
    frame: int
    manual_right_x: int
    manual_right_y: int
    mode: str
    in_opposing_burst: bool
    in_overshoot_recover: bool = False


def generate_manual_mix_frames(
    manifest: ScenarioManifest,
    *,
    manual_seed: int,
    config: ManualMixInputConfig | None = None,
    sim_frames: int,
) -> tuple[ManualInputFrame, ...]:
    config = config or ManualMixInputConfig()
    states = expand_manifest(manifest, config.reference_frame_dt, sim_frames)
    generator = ManualMixInputGenerator(manifest, manual_seed=manual_seed, config=config)
    return tuple(
        generator.generate_frame(
            frame=state.frame,
            error_x=state.target_x,
            error_y=state.target_y,
        )
        for state in states
    )


class ManualMixInputGenerator:
    def __init__(
        self,
        manifest: ScenarioManifest,
        *,
        manual_seed: int,
        config: ManualMixInputConfig | None = None,
    ):
        self.manifest = manifest
        self.config = config or ManualMixInputConfig()
        self.rng = random.Random(_stable_seed("manual-mix", manifest.scenario_key, manual_seed))
        self._burst_windows = self._plan_burst_windows()

    def generate_frame(self, *, frame: int, error_x: float, error_y: float) -> ManualInputFrame:
        mode = self._mode_for_frame(frame, error_x, error_y)
        manual_x, manual_y = self._manual_values_for_mode(mode, frame, error_x, error_y)
        return ManualInputFrame(
            frame=frame,
            manual_right_x=manual_x,
            manual_right_y=manual_y,
            mode=mode,
            in_opposing_burst=mode == "opposing_burst",
            in_overshoot_recover=mode == "overshoot_recover",
        )

    def _mode_for_frame(self, frame: int, error_x: float, error_y: float) -> str:
        for start, end in self._burst_windows:
            if start <= frame <= end:
                return "opposing_burst"
            recover_end = end + self.config.overshoot_recover_frames
            if end < frame <= recover_end:
                return "overshoot_recover"

        radial_error = (error_x * error_x + error_y * error_y) ** 0.5
        if radial_error <= self.config.near_target_radius_px:
            if frame % 2 == 0:
                return "corrective_wobble"
            return "vertical_jitter"
        return "aligned_follow"

    def _manual_values_for_mode(
        self,
        mode: str,
        frame: int,
        error_x: float,
        error_y: float,
    ) -> tuple[int, int]:
        if mode == "opposing_burst":
            return (
                self._map_x(error_x, -self.config.opposing_scale),
                self._map_y(error_y, -self.config.opposing_scale * 0.65),
            )

        if mode == "overshoot_recover":
            return (
                self._map_x(error_x, self.config.recover_scale),
                self._map_y(error_y, self.config.recover_scale),
            )

        if mode == "vertical_jitter":
            jitter_sign = -1.0 if (frame // max(1, self.config.wobble_period_frames)) % 2 else 1.0
            return (
                self._map_x(error_x, self.config.wobble_scale * 0.35),
                self._map_y(
                    error_y + (jitter_sign * self.config.near_target_radius_px * 0.45),
                    self.config.vertical_jitter_scale,
                ),
            )

        if mode == "corrective_wobble":
            wobble_sign = -1.0 if (frame // max(1, self.config.wobble_period_frames)) % 2 else 1.0
            return (
                self._map_x(
                    error_x + (wobble_sign * self.config.near_target_radius_px * 0.4),
                    self.config.wobble_scale,
                ),
                self._map_y(error_y, self.config.wobble_scale * 0.75),
            )

        return (
            self._map_x(error_x, self.config.aligned_scale),
            self._map_y(error_y, self.config.aligned_scale),
        )

    def _plan_burst_windows(self) -> tuple[tuple[int, int], ...]:
        candidate_frames = [event.frame for event in self.manifest.turn_events]
        candidate_frames.extend(event.frame for event in self.manifest.decel_events)
        candidate_frames.extend(event.frame for event in self.manifest.resume_events)
        if not candidate_frames:
            candidate_frames.append(self.rng.randint(18, 72))

        windows: list[tuple[int, int]] = []
        for base_frame in candidate_frames:
            offset = self.rng.randint(-self.config.event_window_frames // 2, self.config.event_window_frames // 2)
            start = max(0, base_frame + offset)
            duration = self.rng.randint(
                self.config.opposing_burst_min_frames,
                self.config.opposing_burst_max_frames,
            )
            windows.append((start, start + duration - 1))
        windows.sort()
        return tuple(windows)

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
