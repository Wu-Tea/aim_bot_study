import argparse
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

DEFAULT_MODEL_PATH = PROJECT_ROOT / "models" / "train" / "best.pt"
DEFAULT_DATASET_YAML = PROJECT_ROOT / "models" / "train" / "cod_combined_single_cls.yaml"
DEFAULT_PROJECT_DIR = PROJECT_ROOT / "runs" / "person_val"


def _parse_args(argv=None):
    parser = argparse.ArgumentParser(description="Validate a one-class person detector with stable Windows defaults.")
    parser.add_argument("--model", type=Path, default=DEFAULT_MODEL_PATH, help="Model checkpoint to validate.")
    parser.add_argument("--data", type=Path, default=DEFAULT_DATASET_YAML, help="YOLO dataset YAML.")
    parser.add_argument("--imgsz", type=int, default=640, help="Validation image size.")
    parser.add_argument("--batch", type=int, default=16, help="Validation batch size.")
    parser.add_argument("--device", default="0", help="CUDA device id, e.g. 0.")
    parser.add_argument(
        "--workers",
        type=int,
        default=0,
        help="Data loader workers. Keep 0 on Windows when launched from agent sessions.",
    )
    parser.add_argument("--project", type=Path, default=DEFAULT_PROJECT_DIR, help="Ultralytics validation output dir.")
    parser.add_argument("--name", default="baseline", help="Validation run name.")
    parser.add_argument("--split", default="val", choices=("train", "val", "test"), help="Dataset split to validate.")
    parser.add_argument("--plots", action="store_true", help="Write Ultralytics validation plots.")
    parser.add_argument("--save-json", action="store_true", help="Write COCO-style JSON when supported by the dataset.")
    parser.add_argument("--conf", type=float, default=None, help="Optional confidence threshold override.")
    parser.add_argument("--iou", type=float, default=None, help="Optional IoU threshold override.")
    parser.add_argument("--exist-ok", action="store_true", help="Allow reusing an existing Ultralytics run directory.")
    return parser.parse_args(argv)


def main(argv=None):
    args = _parse_args(argv)
    from ultralytics import YOLO

    kwargs = {
        "data": str(args.data),
        "imgsz": args.imgsz,
        "batch": args.batch,
        "device": args.device,
        "workers": args.workers,
        "project": str(args.project),
        "name": args.name,
        "split": args.split,
        "plots": args.plots,
        "save_json": args.save_json,
        "single_cls": True,
        "exist_ok": args.exist_ok,
    }
    if args.conf is not None:
        kwargs["conf"] = args.conf
    if args.iou is not None:
        kwargs["iou"] = args.iou

    model = YOLO(str(args.model))
    metrics = model.val(**kwargs)
    save_dir = getattr(metrics, "save_dir", None)
    print(f"[Val] complete | save_dir={save_dir}")


if __name__ == "__main__":
    main()
