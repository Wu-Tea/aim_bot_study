from __future__ import annotations

from dataclasses import dataclass
from hashlib import sha256
import math
import random
from typing import Any


@dataclass(frozen=True, slots=True)
class AdsVisibilityGap:
    start_frame: int
    duration_frames: int

    def to_dict(self) -> dict[str, Any]:
        return {
            "start_frame": self.start_frame,
            "duration_frames": self.duration_frames,
        }

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "AdsVisibilityGap":
        _require_exact_keys(data, "AdsVisibilityGap", {"start_frame", "duration_frames"})
        start_frame = _require_int(data["start_frame"], "AdsVisibilityGap.start_frame")
        duration_frames = _require_int(data["duration_frames"], "AdsVisibilityGap.duration_frames")
        return cls(start_frame=start_frame, duration_frames=duration_frames)


@dataclass(frozen=True, slots=True)
class AdsTargetSpec:
    target_id: str
    initial_dx: float
    initial_dy: float
    velocity_x: float
    velocity_y: float
    decel_start_frame: int | None = None
    decel_duration_frames: int = 0
    decel_target_speed_scale: float = 1.0
    visible_start_frame: int = 0
    visible_end_frame: int | None = None
    gap_windows: tuple[AdsVisibilityGap, ...] = ()

    def to_dict(self) -> dict[str, Any]:
        return {
            "target_id": self.target_id,
            "initial_dx": self.initial_dx,
            "initial_dy": self.initial_dy,
            "velocity_x": self.velocity_x,
            "velocity_y": self.velocity_y,
            "decel_start_frame": self.decel_start_frame,
            "decel_duration_frames": self.decel_duration_frames,
            "decel_target_speed_scale": self.decel_target_speed_scale,
            "visible_start_frame": self.visible_start_frame,
            "visible_end_frame": self.visible_end_frame,
            "gap_windows": [gap.to_dict() for gap in self.gap_windows],
        }

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "AdsTargetSpec":
        _require_exact_keys(
            data,
            "AdsTargetSpec",
            {
                "target_id",
                "initial_dx",
                "initial_dy",
                "velocity_x",
                "velocity_y",
                "decel_start_frame",
                "decel_duration_frames",
                "decel_target_speed_scale",
                "visible_start_frame",
                "visible_end_frame",
                "gap_windows",
            },
        )
        decel_start_frame = data["decel_start_frame"]
        visible_end_frame = data["visible_end_frame"]
        if decel_start_frame is not None:
            decel_start_frame = _require_int(decel_start_frame, "AdsTargetSpec.decel_start_frame")
        if visible_end_frame is not None:
            visible_end_frame = _require_int(visible_end_frame, "AdsTargetSpec.visible_end_frame")
        return cls(
            target_id=_require_str(data["target_id"], "AdsTargetSpec.target_id"),
            initial_dx=_require_number(data["initial_dx"], "AdsTargetSpec.initial_dx"),
            initial_dy=_require_number(data["initial_dy"], "AdsTargetSpec.initial_dy"),
            velocity_x=_require_number(data["velocity_x"], "AdsTargetSpec.velocity_x"),
            velocity_y=_require_number(data["velocity_y"], "AdsTargetSpec.velocity_y"),
            decel_start_frame=decel_start_frame,
            decel_duration_frames=_require_int(data["decel_duration_frames"], "AdsTargetSpec.decel_duration_frames"),
            decel_target_speed_scale=_require_number(
                data["decel_target_speed_scale"],
                "AdsTargetSpec.decel_target_speed_scale",
            ),
            visible_start_frame=_require_int(data["visible_start_frame"], "AdsTargetSpec.visible_start_frame"),
            visible_end_frame=visible_end_frame,
            gap_windows=tuple(AdsVisibilityGap.from_dict(item) for item in _require_list(data["gap_windows"], "AdsTargetSpec.gap_windows")),
        )


@dataclass(frozen=True, slots=True)
class AdsLocalizationEvent:
    frame: int
    target_id: str | None

    def to_dict(self) -> dict[str, Any]:
        return {
            "frame": self.frame,
            "target_id": self.target_id,
        }

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "AdsLocalizationEvent":
        _require_exact_keys(data, "AdsLocalizationEvent", {"frame", "target_id"})
        target_id = data["target_id"]
        if target_id is not None:
            target_id = _require_str(target_id, "AdsLocalizationEvent.target_id")
        return cls(
            frame=_require_int(data["frame"], "AdsLocalizationEvent.frame"),
            target_id=target_id,
        )


@dataclass(frozen=True, slots=True)
class AdsScenarioManifest:
    scenario_key: str
    family: str
    engagement_target_id: str
    targets: tuple[AdsTargetSpec, ...]
    localization_schedule: tuple[AdsLocalizationEvent, ...] = ()

    def __post_init__(self) -> None:
        _validate_ads_manifest(self)

    def to_dict(self) -> dict[str, Any]:
        return {
            "scenario_key": self.scenario_key,
            "family": self.family,
            "engagement_target_id": self.engagement_target_id,
            "targets": [target.to_dict() for target in self.targets],
            "localization_schedule": [event.to_dict() for event in self.localization_schedule],
        }

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "AdsScenarioManifest":
        _require_exact_keys(
            data,
            "AdsScenarioManifest",
            {"scenario_key", "family", "engagement_target_id", "targets", "localization_schedule"},
        )
        return cls(
            scenario_key=_require_str(data["scenario_key"], "AdsScenarioManifest.scenario_key"),
            family=_require_str(data["family"], "AdsScenarioManifest.family"),
            engagement_target_id=_require_str(
                data["engagement_target_id"],
                "AdsScenarioManifest.engagement_target_id",
            ),
            targets=tuple(AdsTargetSpec.from_dict(item) for item in _require_list(data["targets"], "AdsScenarioManifest.targets")),
            localization_schedule=tuple(
                AdsLocalizationEvent.from_dict(item)
                for item in _require_list(data["localization_schedule"], "AdsScenarioManifest.localization_schedule")
            ),
        )


@dataclass(frozen=True, slots=True)
class ExpandedAdsTargetState:
    target_id: str
    target_x: float
    target_y: float
    velocity_x: float
    velocity_y: float
    visible: bool


@dataclass(frozen=True, slots=True)
class ExpandedAdsFrameState:
    frame: int
    engagement_target_id: str
    localized_target_id: str | None
    targets: tuple[ExpandedAdsTargetState, ...]


ALLOWED_ADS_FAMILIES = {
    "single_static_offset",
    "single_strafe_then_decel",
    "single_diagonal_then_decel",
    "reacquire_after_gap",
    "dual_target_disambiguation",
}

ADS_FAMILY_COUNTS = (
    ("single_static_offset", 8),
    ("single_strafe_then_decel", 8),
    ("single_diagonal_then_decel", 8),
    ("reacquire_after_gap", 6),
    ("dual_target_disambiguation", 6),
)


def expand_ads_manifest(
    manifest: AdsScenarioManifest,
    frame_dt: float,
    sim_frames: int,
) -> tuple[ExpandedAdsFrameState, ...]:
    target_states = {target.target_id: _expand_target_spec(target, frame_dt, sim_frames) for target in manifest.targets}
    frames: list[ExpandedAdsFrameState] = []
    for frame in range(sim_frames):
        expanded_targets = tuple(states[frame] for states in target_states.values())
        localized_target_id = _localized_target_id_for_frame(manifest, frame, expanded_targets)
        frames.append(
            ExpandedAdsFrameState(
                frame=frame,
                engagement_target_id=manifest.engagement_target_id,
                localized_target_id=localized_target_id,
                targets=expanded_targets,
            )
        )
    return tuple(frames)


def generate_ads_manifests(run_key: str, run_seed: int) -> tuple[AdsScenarioManifest, ...]:
    manifests: list[AdsScenarioManifest] = []
    scenario_index = 0
    for family, count in ADS_FAMILY_COUNTS:
        for family_index in range(count):
            rng = random.Random(_stable_seed("ads", run_seed, family, family_index))
            scenario_key = f"{run_key}-ads-s{scenario_index:02d}"
            manifests.append(_manifest_for_family(scenario_key, family, family_index, count, rng))
            scenario_index += 1
    return tuple(manifests)


def _manifest_for_family(
    scenario_key: str,
    family: str,
    family_index: int,
    family_count: int,
    rng: random.Random,
) -> AdsScenarioManifest:
    if family == "single_static_offset":
        return AdsScenarioManifest(
            scenario_key=scenario_key,
            family=family,
            engagement_target_id="engagement",
            targets=(
                AdsTargetSpec(
                    target_id="engagement",
                    initial_dx=rng.choice((-160.0, -120.0, -80.0, -40.0, 40.0, 80.0, 120.0, 160.0)),
                    initial_dy=rng.choice((-64.0, -48.0, -32.0, -16.0, 16.0, 32.0, 48.0, 64.0)),
                    velocity_x=0.0,
                    velocity_y=0.0,
                ),
            ),
        )

    if family == "single_strafe_then_decel":
        speed = rng.choice((240.0, 320.0, 400.0))
        return AdsScenarioManifest(
            scenario_key=scenario_key,
            family=family,
            engagement_target_id="engagement",
            targets=(
                AdsTargetSpec(
                    target_id="engagement",
                    initial_dx=rng.choice((-140.0, -110.0, -80.0, 80.0, 110.0, 140.0)),
                    initial_dy=rng.choice((-40.0, -24.0, -12.0, 12.0, 24.0, 40.0)),
                    velocity_x=speed * rng.choice((-1.0, 1.0)),
                    velocity_y=0.0,
                    decel_start_frame=rng.choice((18, 24, 30)),
                    decel_duration_frames=rng.choice((12, 18)),
                    decel_target_speed_scale=rng.choice((0.55, 0.35, 0.15)),
                ),
            ),
        )

    if family == "single_diagonal_then_decel":
        speed = rng.choice((240.0, 320.0, 400.0))
        heading_deg = rng.choice((30.0, 45.0, 135.0, 150.0, 210.0, 225.0, 315.0, 330.0))
        heading_rad = math.radians(heading_deg)
        return AdsScenarioManifest(
            scenario_key=scenario_key,
            family=family,
            engagement_target_id="engagement",
            targets=(
                AdsTargetSpec(
                    target_id="engagement",
                    initial_dx=rng.choice((-140.0, -100.0, -60.0, 60.0, 100.0, 140.0)),
                    initial_dy=rng.choice((-70.0, -50.0, -30.0, 30.0, 50.0, 70.0)),
                    velocity_x=math.cos(heading_rad) * speed,
                    velocity_y=math.sin(heading_rad) * speed,
                    decel_start_frame=rng.choice((18, 24, 30)),
                    decel_duration_frames=rng.choice((12, 18)),
                    decel_target_speed_scale=rng.choice((0.55, 0.35, 0.15)),
                ),
            ),
        )

    if family == "reacquire_after_gap":
        speed = rng.choice((220.0, 300.0, 360.0))
        return AdsScenarioManifest(
            scenario_key=scenario_key,
            family=family,
            engagement_target_id="engagement",
            targets=(
                AdsTargetSpec(
                    target_id="engagement",
                    initial_dx=rng.choice((-120.0, -90.0, -60.0, 60.0, 90.0, 120.0)),
                    initial_dy=rng.choice((-40.0, -24.0, -12.0, 12.0, 24.0, 40.0)),
                    velocity_x=speed * rng.choice((-1.0, 1.0)),
                    velocity_y=0.0,
                    gap_windows=(
                        AdsVisibilityGap(
                            start_frame=rng.choice((14, 18, 22)),
                            duration_frames=rng.choice((6, 8, 10)),
                        ),
                    ),
                ),
            ),
        )

    if family == "dual_target_disambiguation":
        engagement_x = rng.choice((-100.0, -70.0, 70.0, 100.0))
        engagement_y = rng.choice((-36.0, -20.0, 20.0, 36.0))
        distractor_x = engagement_x + rng.choice((28.0, 40.0, 56.0))
        distractor_y = engagement_y + rng.choice((-18.0, 0.0, 18.0))
        start_on_engagement = family_index < (family_count // 2)
        has_switch = family_index not in {2, 5}
        initial_target_id = "engagement" if start_on_engagement else "distractor"
        switch_target_id = "distractor" if start_on_engagement else "engagement"
        schedule = [AdsLocalizationEvent(frame=0, target_id=initial_target_id)]
        if has_switch:
            schedule.append(
                AdsLocalizationEvent(
                    frame=rng.choice((2, 4, 6)),
                    target_id=switch_target_id,
                )
            )
        return AdsScenarioManifest(
            scenario_key=scenario_key,
            family=family,
            engagement_target_id="engagement",
            targets=(
                AdsTargetSpec(
                    target_id="engagement",
                    initial_dx=engagement_x,
                    initial_dy=engagement_y,
                    velocity_x=0.0,
                    velocity_y=0.0,
                ),
                AdsTargetSpec(
                    target_id="distractor",
                    initial_dx=distractor_x,
                    initial_dy=distractor_y,
                    velocity_x=0.0,
                    velocity_y=0.0,
                ),
            ),
            localization_schedule=tuple(schedule),
        )

    raise ValueError(f"Unhandled ADS family: {family}")


def _expand_target_spec(
    spec: AdsTargetSpec,
    frame_dt: float,
    sim_frames: int,
) -> tuple[ExpandedAdsTargetState, ...]:
    x = spec.initial_dx
    y = spec.initial_dy
    base_velocity_x = spec.velocity_x
    base_velocity_y = spec.velocity_y
    states: list[ExpandedAdsTargetState] = []

    for frame in range(sim_frames):
        scale = _velocity_scale_for_frame(spec, frame)
        current_velocity_x = base_velocity_x * scale
        current_velocity_y = base_velocity_y * scale
        x += current_velocity_x * frame_dt
        y += current_velocity_y * frame_dt
        states.append(
            ExpandedAdsTargetState(
                target_id=spec.target_id,
                target_x=x,
                target_y=y,
                velocity_x=current_velocity_x,
                velocity_y=current_velocity_y,
                visible=_target_visible_for_frame(spec, frame),
            )
        )
    return tuple(states)


def _velocity_scale_for_frame(spec: AdsTargetSpec, frame: int) -> float:
    if spec.decel_start_frame is None:
        return 1.0
    if frame < spec.decel_start_frame:
        return 1.0
    if spec.decel_duration_frames <= 1:
        return spec.decel_target_speed_scale
    step_index = min(spec.decel_duration_frames - 1, frame - spec.decel_start_frame)
    progress = max(0.0, min(1.0, step_index / (spec.decel_duration_frames - 1)))
    return 1.0 + ((spec.decel_target_speed_scale - 1.0) * progress)


def _target_visible_for_frame(spec: AdsTargetSpec, frame: int) -> bool:
    if frame < spec.visible_start_frame:
        return False
    if spec.visible_end_frame is not None and frame > spec.visible_end_frame:
        return False
    for gap in spec.gap_windows:
        if gap.start_frame <= frame < (gap.start_frame + gap.duration_frames):
            return False
    return True


def _localized_target_id_for_frame(
    manifest: AdsScenarioManifest,
    frame: int,
    expanded_targets: tuple[ExpandedAdsTargetState, ...],
) -> str | None:
    visible_by_id = {target.target_id: target.visible for target in expanded_targets}
    selected_target_id = None
    if manifest.localization_schedule:
        for event in manifest.localization_schedule:
            if event.frame <= frame:
                selected_target_id = event.target_id
            else:
                break
    else:
        selected_target_id = manifest.engagement_target_id
    if selected_target_id is None:
        return None
    if not visible_by_id.get(selected_target_id, False):
        return None
    return selected_target_id


def _validate_ads_manifest(manifest: AdsScenarioManifest) -> None:
    if manifest.family not in ALLOWED_ADS_FAMILIES:
        raise ValueError(f"AdsScenarioManifest.family must be one of {sorted(ALLOWED_ADS_FAMILIES)}")
    if not manifest.targets:
        raise ValueError("AdsScenarioManifest.targets must contain at least one target")
    if len(manifest.targets) > 2:
        raise ValueError("AdsScenarioManifest.targets may contain at most two targets")

    target_ids = [target.target_id for target in manifest.targets]
    if len(set(target_ids)) != len(target_ids):
        raise ValueError("AdsScenarioManifest target ids must be unique")
    if manifest.engagement_target_id not in set(target_ids):
        raise ValueError("AdsScenarioManifest.engagement_target_id must reference an existing target")

    for target in manifest.targets:
        if target.visible_start_frame < 0:
            raise ValueError("AdsTargetSpec.visible_start_frame must be non-negative")
        if target.visible_end_frame is not None and target.visible_end_frame < target.visible_start_frame:
            raise ValueError("AdsTargetSpec.visible_end_frame must be >= visible_start_frame")
        if target.decel_start_frame is not None and target.decel_start_frame < 0:
            raise ValueError("AdsTargetSpec.decel_start_frame must be non-negative")
        if target.decel_duration_frames < 0:
            raise ValueError("AdsTargetSpec.decel_duration_frames must be non-negative")
        if not 0.0 <= target.decel_target_speed_scale <= 1.0:
            raise ValueError("AdsTargetSpec.decel_target_speed_scale must be between 0 and 1")
        if target.decel_start_frame is None and target.decel_duration_frames != 0:
            raise ValueError("AdsTargetSpec.decel_duration_frames requires decel_start_frame")
        if target.decel_start_frame is not None and target.decel_duration_frames <= 0:
            raise ValueError("AdsTargetSpec.decel_duration_frames must be positive when decel_start_frame is set")
        for gap in target.gap_windows:
            if gap.start_frame < 0:
                raise ValueError("AdsVisibilityGap.start_frame must be non-negative")
            if gap.duration_frames <= 0:
                raise ValueError("AdsVisibilityGap.duration_frames must be positive")

    last_frame = -1
    valid_schedule_target_ids = set(target_ids)
    for event in manifest.localization_schedule:
        if event.frame < 0:
            raise ValueError("AdsLocalizationEvent.frame must be non-negative")
        if event.frame < last_frame:
            raise ValueError("AdsLocalizationEvent.frame values must be sorted")
        if event.target_id is not None and event.target_id not in valid_schedule_target_ids:
            raise ValueError("AdsLocalizationEvent.target_id must reference an existing target or None")
        last_frame = event.frame

    if manifest.family == "dual_target_disambiguation":
        if len(manifest.targets) != 2:
            raise ValueError("dual_target_disambiguation requires exactly two targets")
        if not manifest.localization_schedule:
            raise ValueError("dual_target_disambiguation requires a localization schedule")
        return

    if len(manifest.targets) != 1:
        raise ValueError(f"{manifest.family} requires exactly one target")

    target = manifest.targets[0]
    if manifest.family == "single_static_offset":
        if target.velocity_x != 0.0 or target.velocity_y != 0.0:
            raise ValueError("single_static_offset requires zero target velocity")
        return

    if manifest.family == "reacquire_after_gap":
        if not target.gap_windows:
            raise ValueError("reacquire_after_gap requires at least one visibility gap")
        return


def _require_str(value: Any, label: str) -> str:
    if type(value) is not str:
        raise ValueError(f"{label} must be a string")
    return value


def _require_int(value: Any, label: str) -> int:
    if type(value) is not int:
        raise ValueError(f"{label} must be an integer")
    return value


def _require_number(value: Any, label: str) -> float:
    if type(value) not in {int, float}:
        raise ValueError(f"{label} must be a number")
    return float(value)


def _require_list(value: Any, label: str) -> list[Any]:
    if not isinstance(value, list):
        raise ValueError(f"{label} must be a list")
    return value


def _require_exact_keys(data: Any, label: str, expected_keys: set[str]) -> None:
    if not isinstance(data, dict):
        raise ValueError(f"{label} must be a dict")
    actual_keys = set(data)
    missing = expected_keys - actual_keys
    extra = actual_keys - expected_keys
    if missing or extra:
        details = []
        if missing:
            details.append(f"missing={sorted(missing)}")
        if extra:
            details.append(f"extra={sorted(extra)}")
        raise ValueError(f"{label} schema mismatch ({', '.join(details)})")


def _stable_seed(*parts: object) -> int:
    payload = "|".join(str(part) for part in parts).encode("utf-8")
    return int.from_bytes(sha256(payload).digest()[:8], "big", signed=False)
