from pathlib import Path

from ultralytics import YOLO
from vision.runner import VisionConfig

PROJECT_ROOT = Path(__file__).resolve().parent.parent
MODEL_PATH = PROJECT_ROOT / "models" / "yolo26n.pt"


def export_trt():
    print("[Export] Rebuilding TensorRT engine...")
    config = VisionConfig.from_env()
    model = YOLO(str(MODEL_PATH))

    export_kwargs = {
        "format": "engine",
        "half": True,
        "imgsz": config.image_size,
        "batch": 1,
        "workspace": 8,
        "simplify": True,
        "device": 0,
        "nms": True,
        "opset": 17,
    }

    try:
        model.export(**export_kwargs)
    except Exception as exc:
        print(f"[Export] Built-in NMS export failed: {exc}")
        print("[Export] Falling back to engine export without nms=True...")
        fallback_kwargs = dict(export_kwargs)
        fallback_kwargs.pop("nms", None)
        model.export(**fallback_kwargs)

    print("[Export] Engine export complete.")


if __name__ == "__main__":
    export_trt()
