from __future__ import annotations

import ast
import json
import os
import re
import shutil
from dataclasses import asdict, dataclass
from pathlib import Path

from .person_dataset import IMAGE_SUFFIXES, write_dataset_yaml


DEFAULT_KEEP_NAME_TOKENS = (
    "body",
    "enemy",
    "target",
    "person",
    "player",
    "soldier",
    "operator",
)
DEFAULT_DROP_NAME_TOKENS = (
    "head",
    "weapon",
    "gun",
    "ui",
    "label",
    "icon",
    "minimap",
    "objective",
    "barricade",
    "reinforcement",
    "rotation",
    "friendly",
    "teammate",
    "team",
)
SPLIT_MAP = {
    "train": "train",
    "valid": "val",
    "val": "val",
    "test": "test",
}


@dataclass(slots=True, frozen=True)
class RoboflowSource:
    root: Path
    name: str | None = None


@dataclass(slots=True, frozen=True)
class RoboflowVisibleBodySummary:
    output_root: Path
    sources: list[str]
    images_seen: int
    images_written: int
    images_without_kept_boxes: int
    images_dropped_as_duplicates: int
    boxes_seen: int
    boxes_written: int
    boxes_dropped_by_class: int
    boxes_dropped_by_geometry: int


def load_roboflow_class_names(root: Path) -> dict[int, str]:
    data_yaml = Path(root) / "data.yaml"
    if not data_yaml.is_file():
        raise FileNotFoundError(f"Missing Roboflow data.yaml: {data_yaml}")
    text = data_yaml.read_text(encoding="utf-8")
    lines = text.splitlines()
    for index, line in enumerate(lines):
        stripped = line.strip()
        if not stripped.startswith("names:"):
            continue
        value = stripped.split(":", 1)[1].strip()
        if value:
            parsed = ast.literal_eval(value)
            if isinstance(parsed, dict):
                return {int(key): str(name) for key, name in parsed.items()}
            return {idx: str(name) for idx, name in enumerate(parsed)}
        return _parse_block_names(lines[index + 1 :])
    raise ValueError(f"Could not find names: in {data_yaml}")


def roboflow_base_key(path: Path) -> str:
    stem = Path(path).stem
    return stem.split(".rf.", 1)[0]


def prepare_roboflow_visible_body_dataset(
    *,
    sources: list[RoboflowSource],
    output_root: Path,
    keep_name_tokens: tuple[str, ...] = DEFAULT_KEEP_NAME_TOKENS,
    drop_name_tokens: tuple[str, ...] = DEFAULT_DROP_NAME_TOKENS,
    keep_class_ids_by_source: dict[str, set[int] | None] | None = None,
    min_box_area: float = 0.0,
    min_box_height: float = 0.0,
    max_box_aspect: float = 2.0,
    max_box_tall_ratio: float = 8.0,
    dedupe: bool = True,
    keep_empty_images: bool = False,
    link_mode: str = "auto",
    overwrite: bool = False,
) -> RoboflowVisibleBodySummary:
    if not sources:
        raise ValueError("At least one Roboflow source must be provided.")
    output_root = Path(output_root)
    if output_root.exists():
        if not overwrite:
            raise FileExistsError(f"Output dataset already exists: {output_root}")
        shutil.rmtree(output_root)

    for split in ("train", "val", "test"):
        (output_root / "images" / split).mkdir(parents=True, exist_ok=True)
        (output_root / "labels" / split).mkdir(parents=True, exist_ok=True)

    seen_keys: set[str] = set()
    images_seen = 0
    images_written = 0
    images_without_kept_boxes = 0
    images_dropped_as_duplicates = 0
    boxes_seen = 0
    boxes_written = 0
    boxes_dropped_by_class = 0
    boxes_dropped_by_geometry = 0

    normalized_keep = tuple(token.lower() for token in keep_name_tokens)
    normalized_drop = tuple(token.lower() for token in drop_name_tokens)
    source_names: list[str] = []

    for source in sources:
        root = Path(source.root)
        source_slug = _slug(source.name or root.name)
        source_names.append(source_slug)
        class_names = load_roboflow_class_names(root)
        source_overrides = keep_class_ids_by_source or {}
        if source_slug in source_overrides:
            override_ids = source_overrides[source_slug]
            keep_class_ids = set(class_names) if override_ids is None else set(override_ids)
        else:
            keep_class_ids = {
                class_id
                for class_id, class_name in class_names.items()
                if _should_keep_class(class_name, keep_tokens=normalized_keep, drop_tokens=normalized_drop)
            }

        for input_split, output_split in SPLIT_MAP.items():
            images_dir = root / input_split / "images"
            labels_dir = root / input_split / "labels"
            if not images_dir.is_dir():
                continue
            for image_path in sorted(images_dir.rglob("*")):
                if not image_path.is_file() or image_path.suffix.lower() not in IMAGE_SUFFIXES:
                    continue
                images_seen += 1
                base_key = roboflow_base_key(image_path)
                dedupe_key = f"{output_split}:{base_key}"
                if dedupe and dedupe_key in seen_keys:
                    images_dropped_as_duplicates += 1
                    continue

                label_path = labels_dir / image_path.relative_to(images_dir).with_suffix(".txt")
                kept_lines: list[str] = []
                for line in _read_label_lines(label_path):
                    boxes_seen += 1
                    parsed = _parse_yolo_line(line)
                    if parsed is None:
                        boxes_dropped_by_geometry += 1
                        continue
                    class_id, cx, cy, width, height = parsed
                    if class_id not in keep_class_ids:
                        boxes_dropped_by_class += 1
                        continue
                    if not _passes_geometry(
                        width,
                        height,
                        min_box_area=min_box_area,
                        min_box_height=min_box_height,
                        max_box_aspect=max_box_aspect,
                        max_box_tall_ratio=max_box_tall_ratio,
                    ):
                        boxes_dropped_by_geometry += 1
                        continue
                    kept_lines.append(f"0 {cx:.6f} {cy:.6f} {width:.6f} {height:.6f}")

                if not kept_lines and not keep_empty_images:
                    images_without_kept_boxes += 1
                    continue

                seen_keys.add(dedupe_key)
                output_name = f"{source_slug}__{image_path.name}"
                output_image = output_root / "images" / output_split / output_name
                output_label = output_root / "labels" / output_split / Path(output_name).with_suffix(".txt").name
                _link_or_copy_file(image_path, output_image, link_mode=link_mode)
                output_label.write_text(_format_label_lines(kept_lines), encoding="utf-8")
                images_written += 1
                boxes_written += len(kept_lines)

    yaml_path = write_dataset_yaml(output_root, class_name="person")
    _append_test_split(yaml_path)
    summary = RoboflowVisibleBodySummary(
        output_root=output_root,
        sources=source_names,
        images_seen=images_seen,
        images_written=images_written,
        images_without_kept_boxes=images_without_kept_boxes,
        images_dropped_as_duplicates=images_dropped_as_duplicates,
        boxes_seen=boxes_seen,
        boxes_written=boxes_written,
        boxes_dropped_by_class=boxes_dropped_by_class,
        boxes_dropped_by_geometry=boxes_dropped_by_geometry,
    )
    (output_root / "summary.json").write_text(json.dumps(asdict(summary), indent=2, default=str), encoding="utf-8")
    return summary


def _parse_block_names(lines: list[str]) -> dict[int, str]:
    names: dict[int, str] = {}
    for line in lines:
        if not line.startswith((" ", "\t")):
            break
        match = re.match(r"\s*(\d+)\s*:\s*(.+?)\s*$", line)
        if not match:
            continue
        names[int(match.group(1))] = match.group(2).strip().strip("\"'")
    if not names:
        raise ValueError("names: block is empty or unsupported")
    return names


def _should_keep_class(class_name: str, *, keep_tokens: tuple[str, ...], drop_tokens: tuple[str, ...]) -> bool:
    normalized = class_name.strip().lower().replace("-", "_")
    if any(token in normalized for token in drop_tokens):
        return False
    return any(token in normalized for token in keep_tokens)


def _read_label_lines(label_path: Path) -> list[str]:
    if not label_path.is_file():
        return []
    return [
        line.strip()
        for line in label_path.read_text(encoding="utf-8", errors="ignore").splitlines()
        if line.strip()
    ]


def _parse_yolo_line(line: str) -> tuple[int, float, float, float, float] | None:
    parts = line.split()
    if len(parts) < 5:
        return None
    try:
        class_id = int(float(parts[0]))
        cx = _clamp01(float(parts[1]))
        cy = _clamp01(float(parts[2]))
        width = _clamp01(float(parts[3]))
        height = _clamp01(float(parts[4]))
    except ValueError:
        return None
    if width <= 0.0 or height <= 0.0:
        return None
    return class_id, cx, cy, width, height


def _passes_geometry(
    width: float,
    height: float,
    *,
    min_box_area: float,
    min_box_height: float,
    max_box_aspect: float,
    max_box_tall_ratio: float,
) -> bool:
    if width * height < min_box_area:
        return False
    if height < min_box_height:
        return False
    if max_box_aspect > 0.0 and width / max(height, 1e-9) > max_box_aspect:
        return False
    if max_box_tall_ratio > 0.0 and height / max(width, 1e-9) > max_box_tall_ratio:
        return False
    return True


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


def _append_test_split(yaml_path: Path) -> None:
    content = yaml_path.read_text(encoding="utf-8")
    if "\ntest:" in content:
        return
    yaml_path.write_text(content.replace("val: images/val\n", "val: images/val\ntest: images/test\n"), encoding="utf-8")


def _slug(value: str) -> str:
    slug = re.sub(r"[^a-zA-Z0-9_.-]+", "_", value.strip())
    return slug.strip("._-") or "source"


def _clamp01(value: float) -> float:
    return max(0.0, min(1.0, value))
