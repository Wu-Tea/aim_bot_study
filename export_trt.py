from ultralytics import YOLO


def export_trt():
    print("[Export] Rebuilding TensorRT engine...")
    model = YOLO("yolo26n-pose.pt")

    export_kwargs = {
        "format": "engine",
        "half": True,
        "imgsz": 640,
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
