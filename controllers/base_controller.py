from abc import ABC, abstractmethod

class BaseController(ABC):
    """
    Abstract base class for all controller types.
    Defines the common interface that the vision processing module will use.
    """

    @abstractmethod
    def update(self, dx: float, dy: float):
        """
        Receives the delta (dx, dy) from the vision module to adjust aim.
        This is the primary method for the AI to send commands.
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

    def stop(self):
        """
        Optional method to clean up resources, like stopping threads.
        """
        pass
