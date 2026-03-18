import win32api
import threading
import time
from .base_controller import BaseController

class MouseController(BaseController, threading.Thread):
    """
    Native mouse controller. The AI directly moves the system cursor.
    """
    def __init__(self, smooth=0.5, sensitivity_multiplier=0.7):
        super().__init__()
        self.daemon = True
        self.smooth = smooth
        self.multiplier = sensitivity_multiplier
        
        self.target_dx = 0.0
        self.target_dy = 0.0
        self.lock = threading.Lock()
        self.running = True
        
        self.MOUSEEVENTF_MOVE = 0x0001
        self.DEADZONE = 8 # Pixels

        self.start()

    def update(self, dx, dy):
        with self.lock:
            self.target_dx = dx * self.multiplier
            self.target_dy = dy * self.multiplier

    def reset(self):
        with self.lock:
            self.target_dx = 0.0
            self.target_dy = 0.0

    def is_aiming(self):
        # Right mouse button state
        return win32api.GetAsyncKeyState(0x02) < 0

    def stop(self):
        self.running = False

    def run(self):
        """
        High-frequency smoothing loop to move the mouse cursor.
        """
        while self.running:
            with self.lock:
                # Deadzone check
                if abs(self.target_dx) < self.DEADZONE and abs(self.target_dy) < self.DEADZONE:
                    self.target_dx = 0.0
                    self.target_dy = 0.0
                
                elif abs(self.target_dx) > 1 or abs(self.target_dy) > 1:
                    # Calculate smoothed movement
                    move_x = self.target_dx * self.smooth
                    move_y = self.target_dy * self.smooth

                    # Ensure minimum movement of 1 pixel to avoid getting stuck
                    if 0 < move_x < 1: move_x = 1
                    elif -1 < move_x < 0: move_x = -1
                    if 0 < move_y < 1: move_y = 1
                    elif -1 < move_y < 0: move_y = -1
                    
                    move_x = int(move_x)
                    move_y = int(move_y)

                    # Move the mouse
                    win32api.mouse_event(self.MOUSEEVENTF_MOVE, move_x, move_y, 0, 0)

                    # Consume the moved distance
                    self.target_dx -= move_x
                    self.target_dy -= move_y

            # Sleep for a short duration to achieve high refresh rate
            time.sleep(0.001)
