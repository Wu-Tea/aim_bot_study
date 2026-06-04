import argparse
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from training.roboflow_visible_body import RoboflowSource, prepare_roboflow_visible_body_dataset

DEFAULT_OUTPUT_ROOT = PROJECT_ROOT / "training_data" / "assembled" / "roboflow_visible_body"


def _parse_source(value: str) -> RoboflowSource:
    if "=" in value:
        name, path = value.split("=", 1)
        return RoboflowSource(root=Path(path), name=name)
    path = Path(value)
    return RoboflowSource(root=path, name=path.name)


def _parse_keep_class_id(value: str) -> tuple[str, set[int] | None]:
    if ":" not in value:
        raise argparse.ArgumentTypeError("expected SOURCE:ID[,ID] or SOURCE:*")
    source, raw_ids = value.split(":", 1)
    source = source.strip()
    if not source:
        raise argparse.ArgumentTypeError("source name cannot be empty")
    if raw_ids.strip() == "*":
        return source, None
    try:
        class_ids = {int(item.strip()) for item in raw_ids.split(",") if item.strip()}
    except ValueError as exc:
        raise argparse.ArgumentTypeError("class ids must be integers or *") from exc
    if not class_ids:
        raise argparse.ArgumentTypeError("at least one class id is required")
    return source, class_ids


def _parse_args(argv=None):
    parser = argparse.ArgumentParser(
        description="Prepare a disk-backed one-class visible-body dataset from manually downloaded Roboflow YOLO exports."
    )
    parser.add_argument(
        "--source",
        action="append",
        type=_parse_source,
        required=True,
        help="Roboflow dataset root. Use name=PATH to control output filename prefixes. Repeat for multiple sources.",
    )
    parser.add_argument(
        "--output-root",
        type=Path,
        default=DEFAULT_OUTPUT_ROOT,
        help="Output YOLO dataset root. Keep this on D: for the current project.",
    )
    parser.add_argument(
        "--link-mode",
        choices=("auto", "hardlink", "copy"),
        default="auto",
        help="How images are materialized. auto tries hardlinks first, then copies.",
    )
    parser.add_argument("--force", action="store_true", help="Overwrite the output dataset directory.")
    parser.add_argument(
        "--keep-empty-images",
        action="store_true",
        help="Keep images whose labels become empty after class/geometry filtering. Off by default.",
    )
    parser.add_argument(
        "--min-box-area",
        type=float,
        default=0.0,
        help="Drop boxes with normalized area below this threshold.",
    )
    parser.add_argument(
        "--min-box-height",
        type=float,
        default=0.0,
        help="Drop boxes with normalized height below this threshold.",
    )
    parser.add_argument(
        "--max-box-aspect",
        type=float,
        default=2.0,
        help="Drop unusually wide boxes when width / height exceeds this value. Use 0 to disable.",
    )
    parser.add_argument(
        "--max-box-tall-ratio",
        type=float,
        default=8.0,
        help="Drop unusually tall boxes when height / width exceeds this value. Use 0 to disable.",
    )
    parser.add_argument(
        "--no-dedupe",
        action="store_true",
        help="Disable .rf.-base filename dedupe across sources within each split.",
    )
    parser.add_argument(
        "--keep-class-id",
        action="append",
        type=_parse_keep_class_id,
        default=[],
        help="Override name-based filtering for one source, e.g. warzone:* or warzone:0,2.",
    )
    return parser.parse_args(argv)


def main(argv=None):
    args = _parse_args(argv)
    summary = prepare_roboflow_visible_body_dataset(
        sources=args.source,
        output_root=args.output_root,
        keep_class_ids_by_source=dict(args.keep_class_id),
        min_box_area=args.min_box_area,
        min_box_height=args.min_box_height,
        max_box_aspect=args.max_box_aspect,
        max_box_tall_ratio=args.max_box_tall_ratio,
        dedupe=not args.no_dedupe,
        keep_empty_images=args.keep_empty_images,
        link_mode=args.link_mode,
        overwrite=args.force,
    )

    print("[RoboflowPrep] visible-body dataset prepared.")
    print(f"[RoboflowPrep] output={summary.output_root}")
    print(f"[RoboflowPrep] sources={', '.join(summary.sources)}")
    print(
        "[RoboflowPrep] images "
        f"seen={summary.images_seen} written={summary.images_written} "
        f"duplicate_drop={summary.images_dropped_as_duplicates} "
        f"empty_drop={summary.images_without_kept_boxes}"
    )
    print(
        "[RoboflowPrep] boxes "
        f"seen={summary.boxes_seen} written={summary.boxes_written} "
        f"class_drop={summary.boxes_dropped_by_class} "
        f"geometry_drop={summary.boxes_dropped_by_geometry}"
    )
    print(f"[RoboflowPrep] yaml={summary.output_root / 'dataset.yaml'}")
    print(f"[RoboflowPrep] summary={summary.output_root / 'summary.json'}")


if __name__ == "__main__":
    main()
