import argparse
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from ultralytics import YOLO


def _parse_args():
    parser = argparse.ArgumentParser(description="Export a trained YOLO26 person detector to TensorRT.")
    parser.add_argument("--weights", type=Path, required=True, help="Path to trained best.pt or last.pt weights.")
    parser.add_argument("--width", type=int, default=640, help="Export width.")
    parser.add_argument("--height", type=int, default=512, help="Export height.")
    parser.add_argument("--device", default="0", help="CUDA device id.")
    parser.add_argument("--workspace", type=int, default=8, help="TensorRT workspace size in GB.")
    return parser.parse_args()


def main():
    args = _parse_args()
    model = YOLO(str(args.weights))
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
        exported_path = model.export(**export_kwargs)
    except Exception as exc:
        print(f"[Export] Built-in NMS export failed: {exc}")
        print("[Export] Falling back to export without nms=True...")
        fallback_kwargs = dict(export_kwargs)
        fallback_kwargs.pop("nms", None)
        exported_path = model.export(**fallback_kwargs)

    print(f"[Export] complete | artifact={exported_path}")


if __name__ == "__main__":
    main()
