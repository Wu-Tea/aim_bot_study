from __future__ import annotations

import json
import os
import shutil
from dataclasses import dataclass
from pathlib import Path

import cv2
import numpy as np


IMAGE_SUFFIXES = (".jpg", ".jpeg", ".png", ".bmp", ".webp")


@dataclass(slots=True, frozen=True)
class CrowdHumanLayout:
    train_images_dir: Path
    val_images_dir: Path
    train_annotations: Path
    val_annotations: Path


@dataclass(slots=True, frozen=True)
class DatasetPrepSummary:
    output_root: Path
    train_images: int
    val_images: int
    crowdhuman_images: int
    game_images: int


def convert_crowdhuman_record_to_yolo_lines(
    record: dict,
    *,
    image_width: int,
    image_height: int,
    box_kind: str = "visible",
) -> list[str]:
    lines: list[str] = []
    for gtbox in record.get("gtboxes", []):
        if _should_skip_gtbox(gtbox):
            continue

        raw_box = _pick_box(gtbox, box_kind=box_kind)
        if raw_box is None:
            continue

        line = _xywh_box_to_yolo_line(raw_box, image_width=image_width, image_height=image_height)
        if line is not None:
            lines.append(line)
    return lines


def discover_crowdhuman_layout(root: Path) -> CrowdHumanLayout:
    resolved_root = Path(root)
    train_annotations = _first_existing(
        resolved_root / "annotation_train.odgt",
        resolved_root / "annotations" / "annotation_train.odgt",
        resolved_root / "Annotations" / "annotation_train.odgt",
    )
    val_annotations = _first_existing(
        resolved_root / "annotation_val.odgt",
        resolved_root / "annotations" / "annotation_val.odgt",
        resolved_root / "Annotations" / "annotation_val.odgt",
    )
    shared_images_dir = _first_existing(
        resolved_root / "Images",
        resolved_root / "images",
    )
    train_images_dir = _first_existing(
        resolved_root / "train" / "Images",
        resolved_root / "train" / "images",
    ) or shared_images_dir
    val_images_dir = _first_existing(
        resolved_root / "val" / "Images",
        resolved_root / "val" / "images",
    ) or shared_images_dir

    missing = []
    if train_annotations is None:
        missing.append("annotation_train.odgt")
    if val_annotations is None:
        missing.append("annotation_val.odgt")
    if train_images_dir is None:
        missing.append("train Images/")
    if val_images_dir is None:
        missing.append("val Images/")
    if missing:
        raise FileNotFoundError(
            f"CrowdHuman layout incomplete under {resolved_root}: missing {', '.join(missing)}"
        )

    return CrowdHumanLayout(
        train_images_dir=train_images_dir,
        val_images_dir=val_images_dir,
        train_annotations=train_annotations,
        val_annotations=val_annotations,
    )


def write_dataset_yaml(dataset_root: Path, *, class_name: str = "person") -> Path:
    dataset_root = Path(dataset_root)
    yaml_path = dataset_root / "dataset.yaml"
    yaml_path.write_text(
        "\n".join(
            [
                f"path: {dataset_root.resolve().as_posix()}",
                "train: images/train",
                "val: images/val",
                "names:",
                f"  0: {class_name}",
                "",
            ]
        ),
        encoding="utf-8",
    )
    return yaml_path


def prepare_person_dataset(
    *,
    output_root: Path,
    crowdhuman_root: Path | None = None,
    game_yolo_root: Path | None = None,
    box_kind: str = "visible",
    link_mode: str = "auto",
    overwrite: bool = False,
) -> DatasetPrepSummary:
    output_root = Path(output_root)
    if crowdhuman_root is None and game_yolo_root is None:
        raise ValueError("At least one dataset source must be provided.")

    if output_root.exists():
        if not overwrite:
            raise FileExistsError(f"Output dataset already exists: {output_root}")
        shutil.rmtree(output_root)

    train_images_dir = output_root / "images" / "train"
    val_images_dir = output_root / "images" / "val"
    train_labels_dir = output_root / "labels" / "train"
    val_labels_dir = output_root / "labels" / "val"
    for path in (train_images_dir, val_images_dir, train_labels_dir, val_labels_dir):
        path.mkdir(parents=True, exist_ok=True)

    crowdhuman_images = 0
    if crowdhuman_root is not None:
        layout = discover_crowdhuman_layout(crowdhuman_root)
        crowdhuman_images += _prepare_crowdhuman_split(
            annotations_path=layout.train_annotations,
            images_dir=layout.train_images_dir,
            output_images_dir=train_images_dir / "crowdhuman",
            output_labels_dir=train_labels_dir / "crowdhuman",
            split_name="train",
            box_kind=box_kind,
            link_mode=link_mode,
        )
        crowdhuman_images += _prepare_crowdhuman_split(
            annotations_path=layout.val_annotations,
            images_dir=layout.val_images_dir,
            output_images_dir=val_images_dir / "crowdhuman",
            output_labels_dir=val_labels_dir / "crowdhuman",
            split_name="val",
            box_kind=box_kind,
            link_mode=link_mode,
        )

    game_images = 0
    if game_yolo_root is not None:
        game_root = Path(game_yolo_root)
        game_images += _prepare_yolo_split(
            images_dir=game_root / "images" / "train",
            labels_dir=game_root / "labels" / "train",
            output_images_dir=train_images_dir / "game",
            output_labels_dir=train_labels_dir / "game",
            link_mode=link_mode,
        )
        game_images += _prepare_yolo_split(
            images_dir=game_root / "images" / "val",
            labels_dir=game_root / "labels" / "val",
            output_images_dir=val_images_dir / "game",
            output_labels_dir=val_labels_dir / "game",
            link_mode=link_mode,
        )

    write_dataset_yaml(output_root)
    return DatasetPrepSummary(
        output_root=output_root,
        train_images=_count_files(train_images_dir, IMAGE_SUFFIXES),
        val_images=_count_files(val_images_dir, IMAGE_SUFFIXES),
        crowdhuman_images=crowdhuman_images,
        game_images=game_images,
    )


def _prepare_crowdhuman_split(
    *,
    annotations_path: Path,
    images_dir: Path,
    output_images_dir: Path,
    output_labels_dir: Path,
    split_name: str,
    box_kind: str,
    link_mode: str,
) -> int:
    output_images_dir.mkdir(parents=True, exist_ok=True)
    output_labels_dir.mkdir(parents=True, exist_ok=True)

    count = 0
    for record in _read_odgt(annotations_path):
        image_id = record.get("ID")
        if not image_id:
            continue

        image_src = _resolve_image_path(images_dir, str(image_id))
        image_width, image_height = _read_image_size(image_src)
        label_lines = convert_crowdhuman_record_to_yolo_lines(
            record,
            image_width=image_width,
            image_height=image_height,
            box_kind=box_kind,
        )

        output_image_path = output_images_dir / f"{split_name}_{image_src.name}"
        output_label_path = output_labels_dir / f"{output_image_path.stem}.txt"
        _link_or_copy_file(image_src, output_image_path, link_mode=link_mode)
        output_label_path.write_text(_format_label_lines(label_lines), encoding="utf-8")
        count += 1
    return count


def _prepare_yolo_split(
    *,
    images_dir: Path,
    labels_dir: Path,
    output_images_dir: Path,
    output_labels_dir: Path,
    link_mode: str,
) -> int:
    if not images_dir.is_dir():
        return 0
    if not labels_dir.is_dir():
        raise FileNotFoundError(f"Missing YOLO labels directory: {labels_dir}")

    count = 0
    for image_path in sorted(images_dir.rglob("*")):
        if not image_path.is_file() or image_path.suffix.lower() not in IMAGE_SUFFIXES:
            continue
        relative_path = image_path.relative_to(images_dir)
        label_path = labels_dir / relative_path.with_suffix(".txt")
        if not label_path.is_file():
            raise FileNotFoundError(f"Missing label for image {image_path}: expected {label_path}")

        output_image_path = output_images_dir / relative_path
        output_label_path = output_labels_dir / relative_path.with_suffix(".txt")
        _link_or_copy_file(image_path, output_image_path, link_mode=link_mode)
        output_label_path.parent.mkdir(parents=True, exist_ok=True)
        output_label_path.write_text(label_path.read_text(encoding="utf-8"), encoding="utf-8")
        count += 1
    return count


def _should_skip_gtbox(gtbox: dict) -> bool:
    if str(gtbox.get("tag", "")).lower() != "person":
        return True
    extra = gtbox.get("extra") or {}
    head_attr = gtbox.get("head_attr") or {}
    return bool(extra.get("ignore") or head_attr.get("ignore"))


def _pick_box(gtbox: dict, *, box_kind: str) -> list[float] | None:
    preferred_key = "vbox" if box_kind == "visible" else "fbox"
    fallback_key = "fbox" if preferred_key == "vbox" else "vbox"
    raw_box = gtbox.get(preferred_key) or gtbox.get(fallback_key)
    if raw_box is None or len(raw_box) < 4:
        return None
    return [float(value) for value in raw_box[:4]]


def _xywh_box_to_yolo_line(raw_box: list[float], *, image_width: int, image_height: int) -> str | None:
    x, y, w, h = raw_box
    left = max(0.0, min(float(image_width), x))
    top = max(0.0, min(float(image_height), y))
    right = max(0.0, min(float(image_width), x + w))
    bottom = max(0.0, min(float(image_height), y + h))
    clamped_width = right - left
    clamped_height = bottom - top
    if clamped_width <= 0.0 or clamped_height <= 0.0:
        return None

    cx = ((left + right) * 0.5) / float(image_width)
    cy = ((top + bottom) * 0.5) / float(image_height)
    norm_w = clamped_width / float(image_width)
    norm_h = clamped_height / float(image_height)
    return f"0 {cx:.6f} {cy:.6f} {norm_w:.6f} {norm_h:.6f}"


def _read_odgt(path: Path) -> list[dict]:
    records = []
    with open(path, "r", encoding="utf-8") as handle:
        for line in handle:
            stripped = line.strip()
            if stripped:
                records.append(json.loads(stripped))
    return records


def _resolve_image_path(images_dir: Path, image_id: str) -> Path:
    base = images_dir / image_id
    candidates = [base] + [base.with_suffix(suffix) for suffix in IMAGE_SUFFIXES]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise FileNotFoundError(f"Could not find image for CrowdHuman ID={image_id} under {images_dir}")


def _read_image_size(path: Path) -> tuple[int, int]:
    encoded = np.fromfile(str(path), dtype=np.uint8)
    image = cv2.imdecode(encoded, cv2.IMREAD_COLOR)
    if image is None:
        raise ValueError(f"Failed to decode image: {path}")
    height, width = image.shape[:2]
    return width, height


def _link_or_copy_file(source: Path, destination: Path, *, link_mode: str) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists():
        destination.unlink()

    if link_mode not in {"auto", "hardlink", "copy"}:
        raise ValueError(f"Unsupported link_mode={link_mode}")

    if link_mode in {"auto", "hardlink"}:
        try:
            os.link(source, destination)
            return
        except OSError:
            if link_mode == "hardlink":
                raise
    shutil.copy2(source, destination)


def _format_label_lines(label_lines: list[str]) -> str:
    if not label_lines:
        return ""
    return "\n".join(label_lines) + "\n"


def _count_files(root: Path, suffixes: tuple[str, ...]) -> int:
    return sum(1 for path in root.rglob("*") if path.is_file() and path.suffix.lower() in suffixes)


def _first_existing(*paths: Path) -> Path | None:
    for path in paths:
        if path.exists():
            return path
    return None
