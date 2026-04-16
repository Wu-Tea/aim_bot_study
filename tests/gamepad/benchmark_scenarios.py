from __future__ import annotations

import math
from dataclasses import asdict, dataclass, field
from hashlib import sha256
import random
from typing import Any


@dataclass(frozen=True, slots=True)
class InitialState:
    initial_dx: float
    initial_dy: float
    initial_speed_px_per_sec: float
    initial_heading_deg: float

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "InitialState":
        _require_exact_keys(
            data,
            "InitialState",
            {"initial_dx", "initial_dy", "initial_speed_px_per_sec", "initial_heading_deg"},
        )
        initial_dx = _require_number(data["initial_dx"], "InitialState.initial_dx")
        initial_dy = _require_number(data["initial_dy"], "InitialState.initial_dy")
        initial_speed_px_per_sec = _require_number(
            data["initial_speed_px_per_sec"],
            "InitialState.initial_speed_px_per_sec",
        )
        initial_heading_deg = _require_number(
            data["initial_heading_deg"],
            "InitialState.initial_heading_deg",
        )
        return cls(
            initial_dx=initial_dx,
            initial_dy=initial_dy,
            initial_speed_px_per_sec=initial_speed_px_per_sec,
            initial_heading_deg=initial_heading_deg,
        )


@dataclass(frozen=True, slots=True)
class TurnEvent:
    frame: int
    delta_heading_deg: float
    speed_scale: float = 1.0

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "TurnEvent":
        _require_exact_keys(data, "TurnEvent", {"frame", "delta_heading_deg", "speed_scale"})
        frame = _require_int(data["frame"], "TurnEvent.frame")
        delta_heading_deg = _require_number(data["delta_heading_deg"], "TurnEvent.delta_heading_deg")
        speed_scale = _require_number(data["speed_scale"], "TurnEvent.speed_scale")
        return cls(
            frame=frame,
            delta_heading_deg=delta_heading_deg,
            speed_scale=speed_scale,
        )


@dataclass(frozen=True, slots=True)
class DecelEvent:
    frame: int
    duration_frames: int
    target_speed_scale: float
    hard_stop: bool

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "DecelEvent":
        _require_exact_keys(
            data,
            "DecelEvent",
            {"frame", "duration_frames", "target_speed_scale", "hard_stop"},
        )
        frame = _require_int(data["frame"], "DecelEvent.frame")
        duration_frames = _require_int(data["duration_frames"], "DecelEvent.duration_frames")
        target_speed_scale = _require_number(data["target_speed_scale"], "DecelEvent.target_speed_scale")
        hard_stop = _require_bool(data["hard_stop"], "DecelEvent.hard_stop")
        return cls(
            frame=frame,
            duration_frames=duration_frames,
            target_speed_scale=target_speed_scale,
            hard_stop=hard_stop,
        )


@dataclass(frozen=True, slots=True)
class ResumeEvent:
    frame: int
    duration_frames: int
    target_speed_scale: float

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "ResumeEvent":
        _require_exact_keys(data, "ResumeEvent", {"frame", "duration_frames", "target_speed_scale"})
        frame = _require_int(data["frame"], "ResumeEvent.frame")
        duration_frames = _require_int(data["duration_frames"], "ResumeEvent.duration_frames")
        target_speed_scale = _require_number(data["target_speed_scale"], "ResumeEvent.target_speed_scale")
        return cls(
            frame=frame,
            duration_frames=duration_frames,
            target_speed_scale=target_speed_scale,
        )


@dataclass(frozen=True, slots=True)
class ScenarioManifest:
    scenario_key: str
    kind: str
    initial_state: InitialState
    turn_events: tuple[TurnEvent, ...] = field(default_factory=tuple)
    decel_events: tuple[DecelEvent, ...] = field(default_factory=tuple)
    resume_events: tuple[ResumeEvent, ...] = field(default_factory=tuple)

    def to_dict(self) -> dict[str, Any]:
        return {
            "scenario_key": self.scenario_key,
            "kind": self.kind,
            "initial_state": self.initial_state.to_dict(),
            "turn_events": [event.to_dict() for event in self.turn_events],
            "decel_events": [event.to_dict() for event in self.decel_events],
            "resume_events": [event.to_dict() for event in self.resume_events],
        }

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "ScenarioManifest":
        _require_exact_keys(
            data,
            "ScenarioManifest",
            {"scenario_key", "kind", "initial_state", "turn_events", "decel_events", "resume_events"},
        )
        scenario_key = _require_str(data["scenario_key"], "ScenarioManifest.scenario_key")
        kind = _require_str(data["kind"], "ScenarioManifest.kind")
        initial_state = data["initial_state"]
        turn_events = _load_event_list(data["turn_events"], "turn_events", TurnEvent)
        decel_events = _load_event_list(data["decel_events"], "decel_events", DecelEvent)
        resume_events = _load_event_list(data["resume_events"], "resume_events", ResumeEvent)
        manifest = cls(
            scenario_key=scenario_key,
            kind=kind,
            initial_state=InitialState.from_dict(initial_state),
            turn_events=turn_events,
            decel_events=decel_events,
            resume_events=resume_events,
        )
        _validate_phase1_manifest(manifest)
        return manifest


@dataclass(frozen=True, slots=True)
class ExpandedTargetState:
    frame: int
    target_x: float
    target_y: float
    speed_px_per_sec: float
    heading_deg: float


def _stable_seed(*parts: object) -> int:
    payload = "|".join(str(part) for part in parts).encode("utf-8")
    return int.from_bytes(sha256(payload).digest()[:8], "big", signed=False)


def _require_str(value: Any, label: str) -> str:
    if type(value) is not str:
        raise ValueError(f"{label} must be a string")
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


def _load_event_list(raw_items: Any, label: str, event_type: Any) -> tuple[Any, ...]:
    if not isinstance(raw_items, list):
        raise ValueError(f"{label} must be a list")
    return tuple(event_type.from_dict(item) for item in raw_items)


def _require_int(value: Any, label: str) -> int:
    if type(value) is not int:
        raise ValueError(f"{label} must be an integer")
    return value


def _require_number(value: Any, label: str) -> float:
    if type(value) not in {int, float}:
        raise ValueError(f"{label} must be a number")
    return float(value)


def _require_bool(value: Any, label: str) -> bool:
    if type(value) is not bool:
        raise ValueError(f"{label} must be a boolean")
    return value


_ALLOWED_PHASE1_KINDS = {"steady_turns", "turn_then_decel", "decel_resume"}


def _validate_phase1_manifest(manifest: ScenarioManifest) -> None:
    if manifest.kind not in _ALLOWED_PHASE1_KINDS:
        raise ValueError(f"ScenarioManifest.kind must be one of {sorted(_ALLOWED_PHASE1_KINDS)}")

    for event in manifest.turn_events:
        if event.frame < 0:
            raise ValueError("TurnEvent.frame must be non-negative")
    for event in manifest.decel_events:
        if event.frame < 0:
            raise ValueError("DecelEvent.frame must be non-negative")
        if event.duration_frames <= 0:
            raise ValueError("DecelEvent.duration_frames must be positive")
    for event in manifest.resume_events:
        if event.frame < 0:
            raise ValueError("ResumeEvent.frame must be non-negative")
        if event.duration_frames <= 0:
            raise ValueError("ResumeEvent.duration_frames must be positive")

    if manifest.kind == "steady_turns":
        if len(manifest.turn_events) < 1 or manifest.decel_events or manifest.resume_events:
            raise ValueError("steady_turns requires at least one turn and no decel/resume events")
        return

    if manifest.kind == "turn_then_decel":
        if len(manifest.turn_events) != 1 or len(manifest.decel_events) != 1 or manifest.resume_events:
            raise ValueError("turn_then_decel requires exactly one turn, exactly one decel, and no resume events")
        turn_event = manifest.turn_events[0]
        decel_event = manifest.decel_events[0]
        if decel_event.frame <= turn_event.frame:
            raise ValueError("turn_then_decel decel_event must start after the turn_event")
        return

    if manifest.kind == "decel_resume":
        if manifest.turn_events or len(manifest.decel_events) != 1 or len(manifest.resume_events) > 1:
            raise ValueError("decel_resume requires no turns, exactly one decel, and at most one resume")
        if manifest.resume_events:
            decel_event = manifest.decel_events[0]
            resume_event = manifest.resume_events[0]
            decel_window_end = decel_event.frame + decel_event.duration_frames
            if resume_event.frame < decel_window_end:
                raise ValueError("resume_event must occur after the decel window ends")
        return

    raise ValueError(f"Unhandled ScenarioManifest.kind: {manifest.kind}")


def _interp_scale(start_scale: float, end_scale: float, step_index: int, duration_frames: int) -> float:
    if duration_frames <= 1:
        return end_scale
    progress = min(1.0, max(0.0, step_index / (duration_frames - 1)))
    return start_scale + ((end_scale - start_scale) * progress)


def _events_by_frame(events: tuple[Any, ...]) -> dict[int, list[Any]]:
    by_frame: dict[int, list[Any]] = {}
    for event in events:
        by_frame.setdefault(event.frame, []).append(event)
    return by_frame


def expand_manifest(manifest: ScenarioManifest, frame_dt: float, sim_frames: int) -> list[ExpandedTargetState]:
    x = manifest.initial_state.initial_dx
    y = manifest.initial_state.initial_dy
    base_speed = manifest.initial_state.initial_speed_px_per_sec
    current_speed = base_speed
    current_heading = manifest.initial_state.initial_heading_deg
    states: list[ExpandedTargetState] = []

    turn_by_frame = _events_by_frame(manifest.turn_events)
    decel_by_frame = _events_by_frame(manifest.decel_events)
    resume_by_frame = _events_by_frame(manifest.resume_events)
    active_start_frame: int | None = None
    active_duration = 0
    active_start_scale = 1.0
    active_end_scale = 1.0

    for frame in range(sim_frames):
        if frame in turn_by_frame:
            for turn in turn_by_frame[frame]:
                current_heading = (current_heading + turn.delta_heading_deg) % 360.0
                current_speed = base_speed * turn.speed_scale
                active_start_scale = current_speed / base_speed if base_speed else 0.0
                active_end_scale = active_start_scale
        if frame in decel_by_frame:
            for decel in decel_by_frame[frame]:
                active_start_frame = frame
                active_duration = decel.duration_frames
                active_start_scale = current_speed / base_speed if base_speed else 0.0
                active_end_scale = 0.0 if decel.hard_stop else decel.target_speed_scale
        if frame in resume_by_frame:
            for resume in resume_by_frame[frame]:
                active_start_frame = frame
                active_duration = resume.duration_frames
                active_start_scale = current_speed / base_speed if base_speed else 0.0
                active_end_scale = resume.target_speed_scale
        if active_start_frame is not None and active_duration > 0:
            scale = _interp_scale(active_start_scale, active_end_scale, frame - active_start_frame, active_duration)
            current_speed = base_speed * scale
        heading_rad = math.radians(current_heading)
        x += math.cos(heading_rad) * current_speed * frame_dt
        y += math.sin(heading_rad) * current_speed * frame_dt
        states.append(
            ExpandedTargetState(
                frame=frame,
                target_x=x,
                target_y=y,
                speed_px_per_sec=current_speed,
                heading_deg=current_heading,
            )
        )
    return states


def _rng_for(run_seed: int, family: str, family_index: int) -> random.Random:
    return random.Random(_stable_seed("phase1", run_seed, family, family_index))


def _initial_state_from_rng(rng: random.Random) -> InitialState:
    speed = rng.uniform(220.0, 420.0)
    heading_deg = rng.uniform(-170.0, 170.0)
    return InitialState(
        initial_dx=rng.uniform(-60.0, 60.0),
        initial_dy=rng.uniform(-45.0, 45.0),
        initial_speed_px_per_sec=speed,
        initial_heading_deg=heading_deg,
    )


def _turn_event(rng: random.Random, frame_min: int, frame_max: int, *, speed_scale_min: float = 0.9, speed_scale_max: float = 1.05) -> TurnEvent:
    return TurnEvent(
        frame=rng.randint(frame_min, frame_max),
        delta_heading_deg=rng.uniform(-120.0, 120.0),
        speed_scale=rng.uniform(speed_scale_min, speed_scale_max),
    )


def _decel_event(rng: random.Random, frame_min: int, frame_max: int) -> DecelEvent:
    hard_stop = rng.random() < 0.35
    target_speed_scale = 0.0 if hard_stop else rng.uniform(0.12, 0.65)
    return DecelEvent(
        frame=rng.randint(frame_min, frame_max),
        duration_frames=rng.randint(10, 30),
        target_speed_scale=target_speed_scale,
        hard_stop=hard_stop,
    )


def _resume_event(rng: random.Random, frame_min: int, frame_max: int) -> ResumeEvent:
    return ResumeEvent(
        frame=rng.randint(frame_min, frame_max),
        duration_frames=rng.randint(12, 36),
        target_speed_scale=rng.uniform(0.7, 1.05),
    )


_PHASE1_LAYOUT: tuple[tuple[str, int, int], ...] = (
    ("steady_turns", 0, 0),
    ("steady_turns", 1, 1),
    ("steady_turns", 2, 2),
    ("steady_turns", 3, 3),
    ("steady_turns", 4, 4),
    ("steady_turns", 5, 5),
    ("steady_turns", 6, 6),
    ("steady_turns", 7, 7),
    ("turn_then_decel", 0, 8),
    ("turn_then_decel", 1, 9),
    ("turn_then_decel", 2, 10),
    ("turn_then_decel", 3, 11),
    ("turn_then_decel", 4, 12),
    ("turn_then_decel", 5, 13),
    ("turn_then_decel", 6, 14),
    ("turn_then_decel", 7, 15),
    ("decel_resume", 0, 16),
    ("decel_resume", 1, 17),
    ("decel_resume", 2, 18),
    ("decel_resume", 3, 19),
    ("decel_resume", 4, 20),
    ("decel_resume", 5, 21),
    ("decel_resume", 6, 22),
    ("decel_resume", 7, 23),
)


def _steady_turns_manifest(run_key: str, run_seed: int, family_index: int, scenario_slot: int) -> ScenarioManifest:
    rng = _rng_for(run_seed, "steady_turns", family_index)
    turn_events = [_turn_event(rng, 18, 72, speed_scale_min=0.92, speed_scale_max=1.04)]
    if rng.random() < 0.5:
        second_frame = rng.randint(turn_events[0].frame + 18, min(150, turn_events[0].frame + 70))
        turn_events.append(
            TurnEvent(
                frame=second_frame,
                delta_heading_deg=rng.uniform(-100.0, 100.0),
                speed_scale=rng.uniform(0.92, 1.04),
            )
        )
    turn_events.sort(key=lambda event: event.frame)
    return ScenarioManifest(
        scenario_key=f"{run_key}-s{scenario_slot:02d}",
        kind="steady_turns",
        initial_state=_initial_state_from_rng(rng),
        turn_events=tuple(turn_events),
    )


def _turn_then_decel_manifest(run_key: str, run_seed: int, family_index: int, scenario_slot: int) -> ScenarioManifest:
    rng = _rng_for(run_seed, "turn_then_decel", family_index)
    turn_event = _turn_event(rng, 16, 60, speed_scale_min=0.88, speed_scale_max=1.02)
    decel_frame_min = min(150, turn_event.frame + 18)
    decel_frame_max = min(170, turn_event.frame + 80)
    decel_event = _decel_event(rng, decel_frame_min, max(decel_frame_min, decel_frame_max))
    return ScenarioManifest(
        scenario_key=f"{run_key}-s{scenario_slot:02d}",
        kind="turn_then_decel",
        initial_state=_initial_state_from_rng(rng),
        turn_events=(turn_event,),
        decel_events=(decel_event,),
    )


def _decel_resume_manifest(run_key: str, run_seed: int, family_index: int, scenario_slot: int) -> ScenarioManifest:
    rng = _rng_for(run_seed, "decel_resume", family_index)
    decel_event = _decel_event(rng, 18, 72)
    resume_events = ()
    if rng.random() < 0.875:
        resume_frame_min = min(160, decel_event.frame + decel_event.duration_frames + 12)
        resume_frame_max = min(176, decel_event.frame + decel_event.duration_frames + 70)
        resume_events = (_resume_event(rng, resume_frame_min, max(resume_frame_min, resume_frame_max)),)
    return ScenarioManifest(
        scenario_key=f"{run_key}-s{scenario_slot:02d}",
        kind="decel_resume",
        initial_state=_initial_state_from_rng(rng),
        decel_events=(decel_event,),
        resume_events=resume_events,
    )


def generate_phase1_manifests(run_key: str, run_seed: int) -> list[ScenarioManifest]:
    manifests: list[ScenarioManifest] = []
    for kind, family_index, scenario_slot in _PHASE1_LAYOUT:
        if kind == "steady_turns":
            manifests.append(_steady_turns_manifest(run_key, run_seed, family_index, scenario_slot))
        elif kind == "turn_then_decel":
            manifests.append(_turn_then_decel_manifest(run_key, run_seed, family_index, scenario_slot))
        elif kind == "decel_resume":
            manifests.append(_decel_resume_manifest(run_key, run_seed, family_index, scenario_slot))
        else:
            raise ValueError(f"Unknown scenario kind: {kind}")
    return manifests
