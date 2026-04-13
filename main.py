import argparse
import os

from controller import ControllerFactory
from vision import process_vision


def _parse_args():
    parser = argparse.ArgumentParser(description="YOLO aim assist launcher")
    parser.add_argument(
        "--controller-mode",
        choices=("gamepad", "kbm_to_gamepad", "mouse"),
        default=os.getenv("CONTROLLER_MODE", "gamepad"),
        help="Input backend to use.",
    )
    parser.add_argument(
        "--perf-log",
        action="store_true",
        help="Enable periodic performance logs.",
    )
    parser.add_argument(
        "--auto-fire-output",
        choices=("RB", "RT"),
        default=os.getenv("AUTO_FIRE_OUTPUT", "RB"),
        help="Gamepad auto-fire output target.",
    )
    parser.add_argument(
        "--crop-size",
        type=int,
        default=None,
        help="Override capture crop width and height together.",
    )
    parser.add_argument(
        "--crop-width",
        type=int,
        default=None,
        help="Override capture crop width.",
    )
    parser.add_argument(
        "--crop-height",
        type=int,
        default=None,
        help="Override capture crop height.",
    )
    parser.add_argument(
        "--target-fps",
        type=int,
        default=None,
        help="Compatibility alias for capture FPS.",
    )
    parser.add_argument(
        "--capture-fps",
        type=int,
        default=None,
        help="Override capture FPS.",
    )
    return parser.parse_args()


def _apply_runtime_overrides(args):
    if args.perf_log:
        os.environ["VISION_PERF_LOG"] = "1"
    if args.crop_size:
        os.environ["VISION_CROP_SIZE"] = str(args.crop_size)
    if args.crop_width:
        os.environ["VISION_CROP_WIDTH"] = str(args.crop_width)
    if args.crop_height:
        os.environ["VISION_CROP_HEIGHT"] = str(args.crop_height)
    capture_fps = args.capture_fps or args.target_fps
    if capture_fps:
        os.environ["VISION_CAPTURE_FPS"] = str(capture_fps)


def main():
    args = _parse_args()
    _apply_runtime_overrides(args)

    controller = None
    try:
        controller = ControllerFactory.get_controller(
            controller_mode=args.controller_mode,
            auto_fire_output=args.auto_fire_output,
        )
        print(f"[Ready] Controller={args.controller_mode} | starting vision...")
        process_vision(controller=controller)
        return 0
    except KeyboardInterrupt:
        return 0
    except RuntimeError as exc:
        print(f"[Error] {exc}")
        return 1
    finally:
        if controller:
            controller.reset()
            controller.stop()
            join = getattr(controller, "join", None)
            if callable(join):
                join(timeout=1.0)
        print("[Exit] Program terminated.")


if __name__ == "__main__":
    raise SystemExit(main())
