import argparse
import os

from config import RuntimeConfig, load_tuning_config
from controller import ControllerFactory
from vision import process_native_vision, process_vision


def _env_default(name: str, value: object) -> str:
    return os.getenv(name, str(value))


def _setdefault_env(name: str, value: object) -> None:
    if name not in os.environ:
        os.environ[name] = str(value)


def _bool_env(value: bool) -> str:
    return "1" if value else "0"


def _parse_args(runtime_config: RuntimeConfig | None = None):
    runtime_config = runtime_config or load_tuning_config().runtime
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
        default=_env_default("AUTO_FIRE_OUTPUT", runtime_config.gamepad.auto_fire_output),
        help="Gamepad auto-fire output target.",
    )
    parser.add_argument(
        "--vision-backend",
        choices=("python", "native"),
        default=_env_default("VISION_BACKEND", runtime_config.vision.backend),
        help="Vision runtime backend to use.",
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
    parser.add_argument(
        "--vision-debug",
        action="store_true",
        help="Show a live debug window for the captured vision crop.",
    )
    parser.add_argument(
        "--vision-debug-save",
        action="store_true",
        help="Save annotated debug frames when detections are present.",
    )
    return parser.parse_args()


def _apply_runtime_overrides(args, runtime_config: RuntimeConfig | None = None):
    runtime_config = runtime_config or load_tuning_config().runtime
    vision_config = runtime_config.vision

    if args.perf_log:
        os.environ["VISION_PERF_LOG"] = "1"
    else:
        _setdefault_env("VISION_PERF_LOG", _bool_env(vision_config.perf_log))
    os.environ["VISION_BACKEND"] = args.vision_backend
    if args.crop_size is not None:
        os.environ["VISION_CROP_SIZE"] = str(args.crop_size)
        os.environ["VISION_CROP_WIDTH"] = str(args.crop_size)
        os.environ["VISION_CROP_HEIGHT"] = str(args.crop_size)
    if args.crop_width is not None:
        os.environ["VISION_CROP_WIDTH"] = str(args.crop_width)
    elif args.crop_size is None and "VISION_CROP_SIZE" not in os.environ:
        _setdefault_env("VISION_CROP_WIDTH", vision_config.crop_width)
    if args.crop_height is not None:
        os.environ["VISION_CROP_HEIGHT"] = str(args.crop_height)
    elif args.crop_size is None and "VISION_CROP_SIZE" not in os.environ:
        _setdefault_env("VISION_CROP_HEIGHT", vision_config.crop_height)
    capture_fps = args.capture_fps or args.target_fps
    if capture_fps:
        os.environ["VISION_CAPTURE_FPS"] = str(capture_fps)
    elif "VISION_TARGET_FPS" not in os.environ:
        _setdefault_env("VISION_CAPTURE_FPS", vision_config.capture_fps)
    _setdefault_env("VISION_QUIT_KEY", vision_config.quit_key)
    _setdefault_env("VISION_NATIVE_CUE_SIDECAR", _bool_env(vision_config.native_cue_sidecar))
    if args.vision_debug:
        os.environ["VISION_DEBUG_OVERLAY"] = "1"
    if args.vision_debug_save:
        os.environ["VISION_DEBUG_SAVE"] = "1"


def main():
    runtime_config = load_tuning_config().runtime
    args = _parse_args(runtime_config)
    _apply_runtime_overrides(args, runtime_config)

    controller = None
    try:
        controller = ControllerFactory.get_controller(
            controller_mode=args.controller_mode,
            auto_fire_output=args.auto_fire_output,
        )
        print(f"[Ready] Controller={args.controller_mode} | vision={args.vision_backend} | starting vision...")
        if args.vision_backend == "native":
            process_native_vision(controller=controller)
        else:
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
