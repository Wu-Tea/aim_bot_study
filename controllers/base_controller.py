from abc import ABC, abstractmethod
from dataclasses import dataclass


@dataclass(slots=True, frozen=True)
class ControllerTarget:
    aim_point_x: float
    aim_point_y: float
    screen_center_x: float
    screen_center_y: float
    body_box: tuple[float, float, float, float] | None = None
    target_source: str | None = None
    # `time.perf_counter()` domain timestamp for when the source frame was observed.
    observed_at: float | None = None


@dataclass(slots=True, frozen=True)
class ControllerTimingSnapshot:
    source_age_ms: float | None = None
    python_handoff_ms: float | None = None
    controller_consume_age_ms: float | None = None
    output_age_ms: float | None = None


@dataclass(slots=True, frozen=True)
class ControllerVisionState:
    dx: float = 0.0
    dy: float = 0.0
    target: ControllerTarget | None = None
    auto_fire_requested: bool = False
    # `time.perf_counter()` domain timestamp for the source state.
    observed_at: float | None = None
    # Optional override when fire intent has a different source timestamp.
    auto_fire_observed_at: float | None = None
    # `time.perf_counter()` domain timestamp when Python received the result.
    received_at: float | None = None
    # `time.perf_counter()` domain timestamp when Python submitted it.
    submitted_at: float | None = None


def _state_observed_at(state: ControllerVisionState) -> float | None:
    if state.observed_at is not None:
        return state.observed_at
    return getattr(state.target, "observed_at", None)


def _state_auto_fire_observed_at(state: ControllerVisionState) -> float | None:
    if state.auto_fire_observed_at is not None:
        return state.auto_fire_observed_at
    return _state_observed_at(state)


def _set_auto_fire_compat(controller, pressed: bool, observed_at: float | None = None) -> None:
    set_auto_fire = getattr(controller, "set_auto_fire", None)
    if not callable(set_auto_fire):
        return
    try:
        set_auto_fire(pressed, observed_at=observed_at)
    except TypeError:
        set_auto_fire(pressed)


def submit_controller_vision_state(controller, state: ControllerVisionState) -> None:
    """
    Submits a coherent vision state to modern controllers while preserving
    compatibility with reset/update/set_auto_fire-only test doubles.
    """
    update_vision_state = None
    if getattr(type(controller), "update_vision_state", None) is not None:
        update_vision_state = getattr(controller, "update_vision_state", None)

    if callable(update_vision_state):
        update_vision_state(state)
        return

    auto_fire_requested = bool(state.auto_fire_requested and state.target is not None)
    _set_auto_fire_compat(
        controller,
        auto_fire_requested,
        observed_at=_state_auto_fire_observed_at(state),
    )
    if state.target is not None:
        controller.update(state.dx, state.dy, target=state.target)
        return

    clear_target = getattr(controller, "clear_target", None)
    if callable(clear_target):
        clear_target()
        return
    controller.reset()


def controller_timing_perf_kwargs(controller) -> dict[str, float | None]:
    get_timing_snapshot = None
    if getattr(type(controller), "get_timing_snapshot", None) is not None:
        get_timing_snapshot = getattr(controller, "get_timing_snapshot", None)

    if not callable(get_timing_snapshot):
        return {}

    snapshot = get_timing_snapshot()
    if snapshot is None:
        return {}
    if isinstance(snapshot, dict):
        return {
            "source_age_ms": snapshot.get("source_age_ms"),
            "python_handoff_ms": snapshot.get("python_handoff_ms"),
            "controller_consume_age_ms": snapshot.get("controller_consume_age_ms"),
            "output_age_ms": snapshot.get("output_age_ms"),
        }
    return {
        "source_age_ms": getattr(snapshot, "source_age_ms", None),
        "python_handoff_ms": getattr(snapshot, "python_handoff_ms", None),
        "controller_consume_age_ms": getattr(snapshot, "controller_consume_age_ms", None),
        "output_age_ms": getattr(snapshot, "output_age_ms", None),
    }


class BaseController(ABC):
    """
    Abstract base class for all controller types.
    Defines the common interface that the vision processing module will use.
    """

    @abstractmethod
    def update(self, dx: float, dy: float, target: ControllerTarget | None = None):
        """
        Receives the delta (dx, dy) from the vision module to adjust aim.
        Optional target metadata lets controller-side execution logic reason
        about body regions without redesigning vision selection behavior.
        """
        pass

    @abstractmethod
    def reset(self):
        """
        Resets any aim adjustments, typically called when no target is found
        or when the user stops aiming.
        """
        pass

    def update_vision_state(self, state: ControllerVisionState):
        """
        Receives one coherent vision state. Concrete controllers can override
        this to update target and fire state under one lock.
        """
        auto_fire_requested = bool(state.auto_fire_requested and state.target is not None)
        _set_auto_fire_compat(
            self,
            auto_fire_requested,
            observed_at=_state_auto_fire_observed_at(state),
        )
        if state.target is not None:
            self.update(state.dx, state.dy, target=state.target)
        else:
            self.clear_target()

    def clear_target(self):
        """
        Clears transient target state without requiring a full controller reset.
        Controllers that do not distinguish the two can keep using `reset()`.
        """
        self.reset()

    @abstractmethod
    def is_aiming(self) -> bool:
        """
        Returns True if the user is currently aiming (e.g., holding right mouse
        button or left trigger), False otherwise.
        """
        pass

    def set_auto_fire(self, pressed: bool, observed_at: float | None = None):
        """
        Optional hook to control automatic fire on the controller's chosen
        output (for example RB or RT).
        """
        pass

    def set_auto_rb(self, pressed: bool):
        """
        Compatibility alias for older vision code paths that still think in
        terms of RB instead of generic automatic fire.
        """
        self.set_auto_fire(pressed)

    def get_external_cue(self):
        """
        Optional hook for integrations that already compute a lightweight
        target cue, such as an enemy-only yellow head marker.
        """
        return None

    def get_timing_snapshot(self):
        """
        Optional hook for controllers that expose the latest consume/output
        timing sample.
        """
        return None

    def stop(self):
        """
        Optional method to clean up resources, like stopping threads.
        """
        pass
