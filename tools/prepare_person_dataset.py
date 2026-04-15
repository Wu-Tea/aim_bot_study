import argparse
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from training.person_dataset import prepare_person_dataset

DEFAULT_CROWDHUMAN_ROOT = PROJECT_ROOT / "training_data" / "raw" / "crowdhuman"
DEFAULT_GAME_YOLO_ROOT = PROJECT_ROOT / "training_data" / "raw" / "game_yolo"
DEFAULT_OUTPUT_ROOT = PROJECT_ROOT / "training_data" / "assembled" / "person_detect_visible"


def _parse_args():
    parser = argparse.ArgumentParser(description="Prepare a one-class person dataset for YOLO26 detect fine-tuning.")
    parser.add_argument(
        "--crowdhuman-root",
        type=Path,
        default=DEFAULT_CROWDHUMAN_ROOT,
        help="Root directory containing CrowdHuman Images/ and annotation_*.odgt files.",
    )
    parser.add_argument(
        "--game-yolo-root",
        type=Path,
        default=DEFAULT_GAME_YOLO_ROOT,
        help="Optional YOLO-format game screenshot dataset with images/{train,val} and labels/{train,val}.",
    )
    parser.add_argument(
        "--output-root",
        type=Path,
        default=DEFAULT_OUTPUT_ROOT,
        help="Output directory for the assembled YOLO dataset.",
    )
    parser.add_argument(
        "--box-kind",
        choices=("visible", "full"),
        default="visible",
        help="CrowdHuman box type to use. 'visible' is recommended for the current half-body goal.",
    )
    parser.add_argument(
        "--link-mode",
        choices=("auto", "hardlink", "copy"),
        default="auto",
        help="How images are materialized into the assembled dataset.",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Overwrite the output dataset directory if it already exists.",
    )
    return parser.parse_args()


def main():
    args = _parse_args()
    crowdhuman_root = args.crowdhuman_root if args.crowdhuman_root.exists() else None
    game_yolo_root = args.game_yolo_root if args.game_yolo_root.exists() else None

    summary = prepare_person_dataset(
        output_root=args.output_root,
        crowdhuman_root=crowdhuman_root,
        game_yolo_root=game_yolo_root,
        box_kind=args.box_kind,
        link_mode=args.link_mode,
        overwrite=args.force,
    )

    print("[Dataset] Person dataset prepared.")
    print(f"[Dataset] output={summary.output_root}")
    print(f"[Dataset] train_images={summary.train_images} | val_images={summary.val_images}")
    print(f"[Dataset] crowdhuman_images={summary.crowdhuman_images} | game_images={summary.game_images}")
    print(f"[Dataset] yaml={summary.output_root / 'dataset.yaml'}")


if __name__ == "__main__":
    main()
