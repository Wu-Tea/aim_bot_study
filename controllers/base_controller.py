from abc import ABC, abstractmethod
from dataclasses import dataclass


@dataclass(slots=True, frozen=True)
class ControllerTarget:
    aim_point_x: float
    aim_point_y: float
    screen_center_x: float
    screen_center_y: float
    body_box: tuple[float, float, float, float] | None = None


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

    @abstractmethod
    def is_aiming(self) -> bool:
        """
        Returns True if the user is currently aiming (e.g., holding right mouse
        button or left trigger), False otherwise.
        """
        pass

    def set_auto_fire(self, pressed: bool):
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

    def stop(self):
        """
        Optional method to clean up resources, like stopping threads.
        """
        pass
