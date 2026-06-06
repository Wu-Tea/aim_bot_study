from pathlib import Path
import argparse
import shutil
import sys

PROJECT_ROOT = Path(__file__).resolve().parent.parent
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from ultralytics import YOLO
from vision.runner import VisionConfig

MODEL_PATH = PROJECT_ROOT / "models" / "yolo26n.pt"


def _parse_args(argv=None):
    config = VisionConfig.from_env()
    parser = argparse.ArgumentParser(description="Export the default YOLO model to a TensorRT engine.")
    parser.add_argument("--weights", type=Path, default=MODEL_PATH, help="Path to source .pt weights.")
    parser.add_argument("--width", type=int, default=config.capture_width, help="TensorRT engine input width.")
    parser.add_argument("--height", type=int, default=config.capture_height, help="TensorRT engine input height.")
    parser.add_argument("--output", type=Path, default=None, help="Optional output .engine path.")
    parser.add_argument("--device", default="0", help="CUDA device id.")
    parser.add_argument("--workspace", type=int, default=8, help="TensorRT workspace size in GB.")
    return parser.parse_args(argv)


def _project_relative(path: Path) -> Path:
    return path if path.is_absolute() else PROJECT_ROOT / path


def export_trt(argv=None):
    args = _parse_args(argv)
    print("[Export] Rebuilding TensorRT engine...")
    model = YOLO(str(_project_relative(args.weights)))

    export_kwargs = {
        "format": "engine",
        "half": True,
        "imgsz": (args.height, args.width),
        "batch": 1,
        "workspace": args.workspace,
        "simplify": True,
        "device": args.device,
        "nms": True,
        "opset": 17,
    }

    try:
        exported_path = Path(model.export(**export_kwargs))
    except Exception as exc:
        print(f"[Export] Built-in NMS export failed: {exc}")
        print("[Export] Falling back to engine export without nms=True...")
        fallback_kwargs = dict(export_kwargs)
        fallback_kwargs.pop("nms", None)
        exported_path = Path(model.export(**fallback_kwargs))

    if args.output is not None:
        output_path = _project_relative(args.output)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        if exported_path.resolve(strict=False) != output_path.resolve(strict=False):
            shutil.copy2(exported_path, output_path)
        exported_path = output_path

    print(f"[Export] Engine export complete. artifact={exported_path}")


if __name__ == "__main__":
    export_trt()
