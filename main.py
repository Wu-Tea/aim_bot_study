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
        "--crop-size",
        type=int,
        default=None,
        help="Override capture crop size.",
    )
    parser.add_argument(
        "--target-fps",
        type=int,
        default=None,
        help="Override capture target FPS.",
    )
    return parser.parse_args()


def _apply_runtime_overrides(args):
    if args.perf_log:
        os.environ["VISION_PERF_LOG"] = "1"
    if args.crop_size:
        os.environ["VISION_CROP_SIZE"] = str(args.crop_size)
    if args.target_fps:
        os.environ["VISION_TARGET_FPS"] = str(args.target_fps)


def main():
    args = _parse_args()
    _apply_runtime_overrides(args)

    controller = None
    try:
        controller = ControllerFactory.get_controller(controller_mode=args.controller_mode)
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
