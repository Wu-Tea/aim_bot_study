from controllers.mouse_controller import MouseController
from controllers.gamepad_controller import GamepadController
from controllers.kbm_controller import KBMController

class ControllerFactory:
    """
    Factory class to create and return the appropriate controller instance
    based on the specified mode.
    """
    @staticmethod
    def get_controller(controller_mode: str = "mouse", **controller_kwargs):
        """
        Initializes and returns a controller instance.

        Args:
            controller_mode (str): The desired controller mode.
                - "mouse": Native mouse control.
                - "gamepad": Physical gamepad with AI assist.
                - "kbm_to_gamepad": Mouse & keyboard emulating a gamepad with AI assist.

        Returns:
            An instance of a BaseController subclass.
        """
        controller_map = {
            "gamepad": (GamepadController, "[Factory] Creating GamepadController..."),
            "kbm_to_gamepad": (KBMController, "[Factory] Creating KBMController (Mouse-to-Gamepad)..."),
            "mouse": (MouseController, "[Factory] Creating native MouseController..."),
        }

        controller_cls, message = controller_map.get(
            controller_mode,
            (MouseController, f"[Factory] Warning: Unknown mode '{controller_mode}'. Falling back to native mouse."),
        )
        print(message)

        init_kwargs = {}
        if controller_mode == "gamepad":
            init_kwargs["auto_fire_output"] = controller_kwargs.get("auto_fire_output", "RB")

        try:
            controller = controller_cls(**init_kwargs)
        except Exception as exc:
            raise RuntimeError(f"Failed to initialize controller mode '{controller_mode}': {exc}") from exc

        if not getattr(controller, "ready", True):
            raise RuntimeError(f"Controller mode '{controller_mode}' is unavailable on this machine.")

        return controller
