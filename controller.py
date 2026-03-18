from controllers.mouse_controller import MouseController
from controllers.gamepad_controller import GamepadController
from controllers.kbm_controller import KBMController

class ControllerFactory:
    """
    Factory class to create and return the appropriate controller instance
    based on the specified mode.
    """
    @staticmethod
    def get_controller(controller_mode: str = "mouse"):
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
        if controller_mode == "gamepad":
            print("[Factory] Creating GamepadController...")
            return GamepadController()
        
        elif controller_mode == "kbm_to_gamepad":
            print("[Factory] Creating KBMController (Mouse-to-Gamepad)...")
            return KBMController()
        
        elif controller_mode == "mouse":
            print("[Factory] Creating native MouseController...")
            return MouseController()
            
        else:
            print(f"[Factory] Warning: Unknown mode '{controller_mode}'. Falling back to native mouse.")
            return MouseController()
